# 肥肉引擎：把 implicit skinning 套到現行 master 身體上，並用**同一把尺**驗收。
# 2026-08-27。
#
# Run: blender --background <master.blend> --python pose_domain_implicit.py -- <tag> [--shots]
#   tag = pose_domain_ladder.py 裡的姿勢代號（pick_a / pick_b / ceremony_pickup / ...）
#   --shots 另外渲 LBS vs Implicit 的同機位對照圖
#
# 驗收語義（不可改）：
#   * 指標＝pose_domain_metrics.Metrics，與掃描器逐位同源（backcheck 已證 36 姿勢全等）
#   * 空對照＝把修正器套在靜止姿勢，位移必須 ≈ 0（否則修正器在自己亂動）
#   * 陽性對照＝LBS 基線與修正後同時量、同一列輸出，差多少一目了然
#
# 部位剛體變換不用 Blender 的骨矩陣推導，而是**用 Kabsch 從實際頂點量出來**——
# 這樣自動吸收 ground_and_balance 的整體平移，也繞開 armature/mesh 空間換算的慣例陷阱。
import bpy
import sys
import os
import time
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pose_domain_metrics import Metrics, eval_arrays, verdict_of, body_part  # noqa: E402
from pose_domain_rig import ground_and_balance, apply_ops  # noqa: E402
from pose_domain_ladder import LADDER  # noqa: E402
import implicit_skin as isk  # noqa: E402

ARGS = sys.argv[sys.argv.index("--") + 1:]
TAG = ARGS[0] if ARGS else "pick_a"
WANT_SHOTS = "--shots" in ARGS
PROBE = "c:/games/Unreal Engine/nice_ink/Saved/PoseProbe"
os.makedirs(PROBE, exist_ok=True)

arm = bpy.data.objects["Skeleton_Plus-size"]
arm.data.pose_position = "POSE"
if arm.animation_data:
    arm.animation_data.action = None
BODY = bpy.data.objects["SumoRetopo"]
CLOTH = bpy.data.objects.get("Fundoshi")
BASE_LOC = tuple(arm.location)

# 部位＝近似剛體的一塊。Jiggle_* 併入解剖父部位：它們在掃描期恆為單位旋轉，
# 與父骨共用同一個剛體變換（pose_domain_jigglecheck 已實測全 0.0000°）。
PART_DEF = [
    ("head",      ["Head"]),
    ("neck",      ["Neck"]),
    ("chest",     ["Spine1", "Jiggle_Chest_L", "Jiggle_Chest_R"]),
    # 肚與骨盆**分開**（2026-08-27 二次定案）。曾經合併過，理由是垂下來的肚子
    # 把骨盆表面含進自己的 blob 裡（骨盆頂點量到 belly 場 = 0.68）。改用「接觸解算」
    # 之後那個理由消失了：靜止重疊被 g₀ 基線整個吸收掉。
    # 而合併會造成一個**致命**的副作用：torsoLow 含 Hips ⇒ 它與大腿變成骨架相鄰
    # ⇒ 大腿被歸類成「關節」而排除在碰撞之外 ⇒ 肚子壓大腿這件正主完全不參與解算
    # （實測：只有 4484 個頂點被推、平均 0.71mm，破圖原封不動）。
    # 分開之後：肚↔骨盆相鄰（關節，排除）、腿↔骨盆相鄰（關節，排除）、
    # **肚↔腿不相鄰（碰撞，解算）**——正好是我們要的那一組。
    ("belly",     ["Spine", "Jiggle_Belly"]),
    ("pelvis",    ["Hips", "Root", "Jiggle_Butt_L", "Jiggle_Butt_R"]),
    ("shoulderL", ["LeftShoulder"]),
    ("armL",      ["LeftArm"]),
    ("forearmL",  ["LeftForeArm"]),
    ("handL",     ["LeftHand", "LeftHandIndex1", "LeftHandIndex2",
                   "LeftHandThumb1", "LeftHandThumb2", "LeftHandProp"]),
    ("shoulderR", ["RightShoulder"]),
    ("armR",      ["RightArm"]),
    ("forearmR",  ["RightForeArm"]),
    ("handR",     ["RightHand", "RightHandIndex1", "RightHandIndex2",
                   "RightHandThumb1", "RightHandThumb2", "RightHandProp"]),
    ("thighL",    ["LeftUpLeg"]),
    ("shinL",     ["LeftLeg"]),
    ("footL",     ["LeftFoot", "LeftToeBase"]),
    ("thighR",    ["RightUpLeg"]),
    ("shinR",     ["RightLeg"]),
    ("footR",     ["RightFoot", "RightToeBase"]),
]
# 軟組織＝要修的範圍。頭／頸／手／腳排除：那裡沒有肥肉，而且五官細節不該讓場碰。
SOFT = {"chest", "belly", "pelvis", "shoulderL", "armL", "forearmL",
        "shoulderR", "armR", "forearmR", "thighL", "shinL", "thighR", "shinR"}

# 旋鈕（全部是這個檔案唯一的來源；改參數不用碰演算法）
R_MAX = 0.050          # 緊支撐半徑上限（m）＝「接觸從幾公分外開始被感覺到」
N_SAMPLES = 150        # 每部位 HRBF 取樣數（大部位）
N_SAMPLES_SMALL = 70
RICCI_N_MAX = 24.0     # 梯度同向（關節彎）→ 近似 max ＝乾淨聯集
RICCI_N_MIN = 2.2      # 梯度反向（正面接觸）→ 鼓起
RICCI_COS_LO = -0.35   # 這個餘弦以下算「接觸」
RICCI_COS_HI = 0.55    # 這個餘弦以上算「同一肢體」
REPORT_KABSCH = True   # 每次擺姿勢印剛體擬合殘差（部位真的是剛體嗎）
CONTACT_COS = -1.0     # 投影時偵測接觸的梯度發散門檻（<=-1 ＝關閉，見 implicit_skin）
# 信賴度羽化：|f₀−0.5| 是「合成場在這裡忠不忠實重現靜止網格」的直接量測。
# 全信到 BAND_FULL、線性收到 BAND_ZERO 歸零，**不用硬門檻**——硬門檻會在
# 活動集邊界留下一圈「修過的頂點貼著沒修的鄰居」，實測 fold 從 147 暴衝到 862。
# （同一條教訓＝褌整形的軟衰減：硬圓周會在肚下留可見壓痕。）
BAND_FULL, BAND_ZERO = 0.03, 0.12
WEIGHT_SMOOTH_ITERS = 8
MAX_OUT_MM = 6.0      # 向外（分離造成的）位移上限——真肉不會因為隔壁讓開就自己長出來
# 推出上限 15mm（2026-08-27 掃參數定案：60/15/10 三檔實測，15 最好）。
# 放到 60mm 反而更糟（xsect 416 vs 297）——推得太深會把被推的那一片自己摺起來，
# 解掉一個「兩塊互穿」換來一個「同一塊自摺」。
MAX_IN_MM = 15.0      # 向內（接觸擠壓造成的）位移上限
# 位移場低通：把「修正量」本身抹平，而不是抹平網格。
# 高頻皺紋的來源是每個頂點各自牛頓收斂到自己的等值，鄰居落點差幾分之一公釐就成漣漪
#（渲染圖實測：臀部／大腿後側／膝窩整片起皺）。修正量抹平之後，
# 細節仍然住在 LBS 基底位置裡（乳頭／肚臍原樣），只有低頻的推擠留下來。
DISP_SMOOTH_ITERS = 25


def _knob(name, default):
    """旋鈕可用環境變數覆寫，方便掃參數而不用改檔（改檔會讓每次比較的來源不同）。"""
    v = os.environ.get("NI_" + name)
    return type(default)(v) if v is not None else default


MAX_IN_MM = _knob("MAX_IN_MM", MAX_IN_MM)
DISP_SMOOTH_ITERS = _knob("DISP_SMOOTH_ITERS", DISP_SMOOTH_ITERS)
RELAX_ITERS = _knob("RELAX_ITERS", 4)


def log(*a):
    print(*a, flush=True)


def reset_pose():
    for pb in arm.pose.bones:
        pb.rotation_mode = 'QUATERNION'
        pb.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
        pb.location = (0.0, 0.0, 0.0)
        pb.scale = (1.0, 1.0, 1.0)


# ------------------------------------------------------------------ 靜止資料
reset_pose()
arm.location = BASE_LOC
bpy.context.view_layer.update()
M = Metrics(BODY, CLOTH)
CO_REST, TRIS = M.co0.copy(), M.tris
NV = len(CO_REST)


def vertex_normals(co, tris):
    n = np.zeros_like(co)
    a, b, c = co[tris[:, 0]], co[tris[:, 1]], co[tris[:, 2]]
    fn = np.cross(b - a, c - a)          # 面積加權（不正規化）
    for k in range(3):
        np.add.at(n, tris[:, k], fn)
    return n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)


NRM_REST = vertex_normals(CO_REST, TRIS)

# 權重矩陣：(NV, n_bones)
gnames = [g.name for g in BODY.vertex_groups]
gidx = {n: i for i, n in enumerate(gnames)}
W = np.zeros((NV, len(gnames)), dtype=np.float64)
for v in BODY.data.vertices:
    for ge in v.groups:
        W[v.index, ge.group] = ge.weight
wsum = np.maximum(W.sum(axis=1, keepdims=True), 1e-12)
W /= wsum

part_names = [p[0] for p in PART_DEF]
PW = np.zeros((NV, len(PART_DEF)), dtype=np.float64)
for pi, (pn, bones) in enumerate(PART_DEF):
    for b in bones:
        if b in gidx:
            PW[:, pi] += W[:, gidx[b]]
DOM = np.argmax(PW, axis=1)
log("segmentation:", ", ".join("%s=%d" % (part_names[i], int((DOM == i).sum()))
                               for i in range(len(PART_DEF))))

# 鄰接（CSR）
E = np.concatenate([TRIS[:, [0, 1]], TRIS[:, [1, 2]], TRIS[:, [2, 0]]], axis=0)
E = np.concatenate([E, E[:, ::-1]], axis=0)
order = np.lexsort((E[:, 1], E[:, 0]))
E = E[order]
uniq = np.ones(len(E), dtype=bool)
uniq[1:] = np.any(E[1:] != E[:-1], axis=1)
E = E[uniq]
ADJ_PTR = np.searchsorted(E[:, 0], np.arange(NV + 1))
ADJ_IDX = E[:, 1]


def part_boundary_loops(pi):
    """回傳 [(loop_vertex_indices, outward_dir)]——開放切口，HRBF 要拿它封蓋。

    **一個關節一個蓋**：邊界頂點先依「隔壁是哪一塊」分組，再各自取連通元件。
    只用連通性分是錯的——骨盆的腰環與兩個腿環在鼠蹊處相連成一個元件，
    會被封成一片橫跨整個骨盆的假蓋，把隔壁部位整個吞進去
    （實測病徵：骨盆表面頂點的合成場 f₀=0.889，應該是 0.5）。
    """
    inpart = DOM == pi
    vids = np.where(inpart)[0]
    if len(vids) == 0:
        return []
    groups = {}
    for v in vids:
        nb = ADJ_IDX[ADJ_PTR[v]:ADJ_PTR[v + 1]]
        out_nb = nb[~inpart[nb]]
        if len(out_nb) == 0:
            continue
        others, cnt = np.unique(DOM[out_nb], return_counts=True)
        groups.setdefault(int(others[np.argmax(cnt)]), []).append(v)
    cen = CO_REST[vids].mean(axis=0)
    out = []
    for other, lst in groups.items():
        bids = np.array(lst, dtype=np.int64)
        if len(bids) < 6:
            continue
        isb = np.zeros(NV, dtype=bool)
        isb[bids] = True
        seen = set()
        for s in bids:
            s = int(s)
            if s in seen:
                continue
            comp = [s]
            seen.add(s)
            stack = [s]
            while stack:
                v = stack.pop()
                for nb in ADJ_IDX[ADJ_PTR[v]:ADJ_PTR[v + 1]]:
                    nb = int(nb)
                    if isb[nb] and nb not in seen:
                        seen.add(nb)
                        comp.append(nb)
                        stack.append(nb)
            if len(comp) < 6:
                continue
            lp = np.array(comp, dtype=np.int64)
            d = CO_REST[lp].mean(axis=0) - cen
            n = np.linalg.norm(d)
            if n < 1e-6:
                continue
            out.append((lp, d / n))
    return out


# ------------------------------------------------------------------ 擬場
t0 = time.time()
PARTS = []
for pi, (pn, bones) in enumerate(PART_DEF):
    vidx = np.where(DOM == pi)[0]
    if len(vidx) < 20:
        log("  skip part %s (only %d verts)" % (pn, len(vidx)))
        PARTS.append(None)
        continue
    P = CO_REST[vidx]
    half = (P.max(axis=0) - P.min(axis=0)) * 0.5
    R = float(min(R_MAX, max(0.012, 0.60 * np.min(half))))
    ns = N_SAMPLES if len(vidx) > 4000 else N_SAMPLES_SMALL
    loops = part_boundary_loops(pi)
    part = isk.fit_part(pn, bones, vidx, CO_REST, NRM_REST, loops, ns, R)
    PARTS.append(part)
    log("  fit %-10s verts=%6d samples=%3d loops=%d R=%.3f" %
        (pn, len(vidx), len(part.centers), len(loops), R))
log("HRBF fitted in %.1fs" % (time.time() - t0))

VALID = [i for i, p in enumerate(PARTS) if p is not None]

# 骨架相鄰矩陣（以 VALID 的順序索引）：兩塊只要有一根骨是另一塊某根骨的親／子，
# 就算直接相連＝關節。從骨階層自動導出，不手寫清單（換 rig 不會過期）。
_bone_parent = {b.name: (b.parent.name if b.parent else None)
                for b in arm.data.bones}
_bones_of = {}
for _pi, (_pn, _bs) in enumerate(PART_DEF):
    _bones_of[_pi] = set(_bs)
ADJ_PARTS = np.zeros((len(VALID), len(VALID)), dtype=bool)
for _a, _ia in enumerate(VALID):
    for _b, _ib in enumerate(VALID):
        if _a == _b:
            ADJ_PARTS[_a, _b] = True
            continue
        # 空部位（例如 neck 一個頂點都沒有）要**收縮掉**：不收縮的話頭與胸
        # 不算相鄰 → 頸部被當成兩塊肉相撞 → 實測渲染圖在脖子出現一條裂縫。
        _empty = set()
        for _pj, (_pn2, _bs2) in enumerate(PART_DEF):
            if PARTS[_pj] is None:
                _empty |= set(_bs2)

        def _up(x):
            y = _bone_parent.get(x)
            while y in _empty:
                y = _bone_parent.get(y)
            return y

        link = any(_up(x) in _bones_of[_ib] for x in _bones_of[_ia]) or                any(_up(y) in _bones_of[_ia] for y in _bones_of[_ib])
        ADJ_PARTS[_a, _b] = link
log("skeletal adjacency (關節＝乾淨聯集):")
for _a, _ia in enumerate(VALID):
    _nb = [part_names[VALID[_b]] for _b in range(len(VALID))
           if ADJ_PARTS[_a, _b] and _b != _a]
    log("  %-10s <-> %s" % (part_names[_ia], ", ".join(_nb) if _nb else "(none)"))


def make_eval(mats):
    """mats[i]＝部位 i 這一幀的剛體變換（4×4）。回傳 eval_fn(X)->(f,grad)。"""
    def ev(X):
        m = X.shape[0]
        F = np.zeros((m, len(VALID)))
        G = np.zeros((m, len(VALID), 3))
        for k, i in enumerate(VALID):
            f, g = PARTS[i].eval_world(X, mats[i])
            F[:, k] = f
            G[:, k] = g
        return isk.compose(F, G, RICCI_N_MIN, RICCI_N_MAX,
                           RICCI_COS_LO, RICCI_COS_HI, adj=ADJ_PARTS)
    return ev


def make_eval_other(mats, own_part):
    """g(x) = max_{q 非自己且非關節鄰居} f_q(x)，回傳 (g, ∇g)。
    own_part = 每個查詢點所屬的部位索引（與 X 同長）。"""
    vmap = {ip: k for k, ip in enumerate(VALID)}

    def ev(X, sub=None):
        own = own_part if sub is None else own_part[sub]
        m = X.shape[0]
        best = np.zeros(m)
        bestg = np.zeros((m, 3))
        ok_own = np.array([vmap.get(int(o), -1) for o in own])
        for k, i in enumerate(VALID):
            f, g = PARTS[i].eval_world(X, mats[i])
            # 自己 / 關節鄰居 一律不參與：關節不是碰撞
            skip = np.zeros(m, dtype=bool)
            valid_own = ok_own >= 0
            skip[valid_own] = ADJ_PARTS[ok_own[valid_own], k]
            f = np.where(skip, 0.0, f)
            take = f > best
            best = np.where(take, f, best)
            bestg = np.where(take[:, None], g, bestg)
        return best, bestg
    return ev


IDENT = [np.eye(4) for _ in PART_DEF]
EV_REST = make_eval(IDENT)

# 靜止等值與靜止梯度（每頂點的追蹤目標）
t0 = time.time()
F0 = np.zeros(NV)
G0 = np.zeros((NV, 3))
CH = 20000
for s in range(0, NV, CH):
    e = min(s + CH, NV)
    F0[s:e], G0[s:e] = EV_REST(CO_REST[s:e])
log("rest field evaluated in %.1fs   f0: min=%.3f med=%.3f max=%.3f"
    % (time.time() - t0, F0.min(), np.median(F0), F0.max()))

# 分解診斷：擬合誤差（自己的場對自己的頂點）vs 合成誤差（合成場對同一批頂點）。
# 兩者混在一起看，永遠不知道該修 HRBF 還是該修算子。
log("=== FIT vs COMPOSE 分解 ===")
log("  part        own|d|mm(mean/p95)   composed f0 (mean / %%在[.4,.6]外)")
for pi, pn in enumerate(part_names):
    if PARTS[pi] is None:
        continue
    v = PARTS[pi].vidx
    d, _ = isk.hrbf_eval(PARTS[pi].centers, PARTS[pi].alpha, PARTS[pi].beta,
                         CO_REST[v], want_grad=False)
    f = F0[v]
    out = float(np.mean((f < 0.40) | (f > 0.60))) * 100.0
    # 誰把誰吞進去：在本部位的頂點上，別的部位的場平均值最大的前兩名
    off = []
    for qi, qn in enumerate(part_names):
        if qi == pi or PARTS[qi] is None:
            continue
        fq, _ = PARTS[qi].eval_world(CO_REST[v], np.eye(4))
        off.append((float(fq.mean()), qn))
    off.sort(reverse=True)
    log("  %-10s   %6.1f %6.1f          %.3f   %5.1f%%    吞食者: %s"
        % (pn, np.abs(d).mean() * 1000, np.percentile(np.abs(d), 95) * 1000,
           f.mean(), out, ", ".join("%s=%.2f" % (n, s) for s, n in off[:2])))

soft_mask = np.isin(DOM, [i for i, n in enumerate(part_names) if n in SOFT])
# 接觸基線：靜止時每個頂點已經陷進「別人」多少（力士站著肚子就壓在大腿上）
t0 = time.time()
G0_OTHER = np.zeros(NV)
_ev_rest_other = make_eval_other(IDENT, DOM)
for s_ in range(0, NV, CH):
    e_ = min(s_ + CH, NV)
    G0_OTHER[s_:e_], _ = make_eval_other(IDENT, DOM[s_:e_])(CO_REST[s_:e_])
log("rest contact baseline in %.1fs  g0: mean=%.3f  >0.5 (靜止就相陷): %d verts"
    % (time.time() - t0, G0_OTHER.mean(), int((G0_OTHER > 0.5).sum())))

dev = np.abs(F0 - 0.5)
# 接觸解算以 g₀ 當基線，靜止重疊已經被吸收 ⇒ 不再需要信賴度閘門去閃避它。
# CONF 只剩下「這是不是軟組織」這一個作用（羽化邊界，避免硬邊）。
CONF = soft_mask.astype(np.float64)
# 把信賴度在網格上抹平：逐頂點的門檻會有斑點，斑點就是鋸齒的種子
for _ in range(WEIGHT_SMOOTH_ITERS):
    acc = np.add.reduceat(CONF[ADJ_IDX], ADJ_PTR[:-1])
    cnt = np.maximum(np.diff(ADJ_PTR), 1)
    CONF = np.minimum(CONF, acc / cnt)      # 只往下抹＝羽化不外擴
ACT = np.where(CONF > 0.01)[0]
log("active verts = %d / %d  (soft=%d, conf>0.99: %d, mean conf=%.3f)"
    % (len(ACT), NV, int(soft_mask.sum()), int((CONF > 0.99).sum()), CONF[ACT].mean()))

# 活動集內部的鄰接（切向鬆弛用）
pos_in_act = -np.ones(NV, dtype=np.int64)
pos_in_act[ACT] = np.arange(len(ACT))
nb_list = []
nb_ptr = [0]
for v in ACT:
    nb = ADJ_IDX[ADJ_PTR[v]:ADJ_PTR[v + 1]]
    nb = nb[pos_in_act[nb] >= 0]
    if len(nb) == 0:
        nb = np.array([v])
    nb_list.append(pos_in_act[nb])
    nb_ptr.append(nb_ptr[-1] + len(nb))
NB = (np.concatenate(nb_list), np.array(nb_ptr, dtype=np.int64))


def kabsch(A, B):
    """求 R,t 使 R·A+t ≈ B（都是 (n,3)）。"""
    ca, cb = A.mean(axis=0), B.mean(axis=0)
    H = (A - ca).T @ (B - cb)
    U, S, Vt = np.linalg.svd(H)
    d = np.sign(np.linalg.det(Vt.T @ U.T))
    D = np.diag([1.0, 1.0, d])
    R = Vt.T @ D @ U.T
    Mx = np.eye(4)
    Mx[:3, :3] = R
    Mx[:3, 3] = cb - R @ ca
    return Mx


def part_transforms(co_posed):
    """量出來的、不是推導出來的：每部位取權重≥0.98 的核心頂點做 Kabsch。"""
    mats = [np.eye(4) for _ in PART_DEF]
    for pi in range(len(PART_DEF)):
        if PARTS[pi] is None:
            continue
        for thr in (0.98, 0.90, 0.70):
            core = np.where((PW[:, pi] >= thr) & (DOM == pi))[0]
            if len(core) >= 30:
                break
        if len(core) < 30:
            core = PARTS[pi].vidx
        if len(core) > 4000:
            core = core[:: max(1, len(core) // 4000)]
        mats[pi] = kabsch(CO_REST[core], co_posed[core])
        if REPORT_KABSCH:
            pred = CO_REST[core] @ mats[pi][:3, :3].T + mats[pi][:3, 3]
            res = np.linalg.norm(pred - co_posed[core], axis=1)
            ang = np.degrees(np.arccos(np.clip(
                (np.trace(mats[pi][:3, :3]) - 1.0) * 0.5, -1.0, 1.0)))
            log("    kabsch %-10s core=%5d  rot=%5.1f°  residual mean=%.2fmm "
                "max=%.2fmm  -> %s"
                % (part_names[pi], len(core), ang, res.mean() * 1000,
                   res.max() * 1000, "rigid" if res.max() < 0.005 else "NOT RIGID"))
    return mats




def diag_by_part(vals, idx, title, scale=1.0, fmt="%7.2f"):
    """逐部位分解——「先量分解再動手」。整體平均會把問題藏起來。"""
    log("    %s (mean / p99 / max):" % title)
    for pi, pn in enumerate(part_names):
        sel = DOM[idx] == pi
        if not np.any(sel):
            continue
        v = vals[sel] * scale
        log(("      %-10s n=%6d  " + fmt + " " + fmt + " " + fmt)
            % (pn, int(sel.sum()), v.mean(), np.percentile(v, 99), v.max()))


def correct(co_lbs, label, diag=False):
    mats = part_transforms(co_lbs)
    ev = make_eval(mats)
    vrot = np.stack([mats[d][:3, :3] for d in DOM[ACT]], axis=0)
    if diag:
        f_lbs, _ = ev(co_lbs[ACT])
        ferr = np.abs(f_lbs - F0[ACT])
        log("    field error |f(LBS)-f0|: mean=%.4f p99=%.4f max=%.4f  (>0.02: %d verts)"
            % (ferr.mean(), np.percentile(ferr, 99), ferr.max(), int((ferr > 0.02).sum())))
        diag_by_part(ferr, ACT, "field error", 1.0, "%7.4f")
    t = time.time()
    ev_other = make_eval_other(mats, DOM[ACT])
    X, st = isk.project_contact(co_lbs[ACT], G0_OTHER[ACT], ev_other,
                                max_push=MAX_IN_MM / 1000.0,
                                relax_neighbors=NB, relax_iters=RELAX_ITERS)
    st['contact'] = st['touched']
    log("    推出上限 %.0fmm：撞到上限（＝陷太深、沒推乾淨）的頂點 = %d"
        % (MAX_IN_MM, st['capped']))
    st['unconverged'] = st['unresolved']
    D = X - co_lbs[ACT]
    if DISP_SMOOTH_ITERS > 0:
        nb_idx, nb_ptr = NB
        cnt = np.maximum(np.diff(nb_ptr), 1)[:, None]
        for _ in range(DISP_SMOOTH_ITERS):
            D = 0.5 * D + 0.5 * (np.add.reduceat(D[nb_idx], nb_ptr[:-1], axis=0) / cnt)
    out = co_lbs.copy()
    # 信賴度加權：修正量乘上「這裡的場可不可信」。羽化到 0 的地方＝原樣 LBS。
    out[ACT] = co_lbs[ACT] + CONF[ACT][:, None] * D
    disp = np.linalg.norm(out - co_lbs, axis=1)
    log("  [%s] contact %.1fs iters=%d pushed=%d unresolved=%d  "
        "disp: mean=%.2fmm p99=%.2fmm max=%.2fmm"
        % (label, time.time() - t, st['iters'], st['contact'], st['unconverged'],
           disp.mean() * 1000, np.percentile(disp, 99) * 1000, disp.max() * 1000))
    if diag:
        diag_by_part(disp[ACT], ACT, "displacement (mm)", 1000.0)
    return out, disp


# ------------------------------------------------------------------ 空對照
log("=== NULL CONTROL (靜止姿勢：位移必須 ≈ 0) ===")
reset_pose()
arm.location = BASE_LOC
bpy.context.view_layer.update()
co_rest_lbs, _ = eval_arrays(BODY)
_, disp0 = correct(co_rest_lbs, "rest")
NULL_MAX_MM = disp0.max() * 1000.0
log("NULL CONTROL max displacement = %.3f mm  -> %s"
    % (NULL_MAX_MM, "PASS" if NULL_MAX_MM < 1.0 else "FAIL"))

# ------------------------------------------------------------------ 陽性對照
log("=== POSITIVE CONTROL (場真的會動嗎) ===")
probe = CO_REST[ACT[:200]]
f_a, _ = EV_REST(probe)
shifted = [np.eye(4) for _ in PART_DEF]
bi = part_names.index("belly")
shifted[bi] = np.eye(4)
shifted[bi][2, 3] = 0.03
f_b, _ = make_eval(shifted)(probe)
log("belly field shifted 3cm -> field delta: mean=%.4f max=%.4f  -> %s"
    % (np.abs(f_b - f_a).mean(), np.abs(f_b - f_a).max(),
       "PASS" if np.abs(f_b - f_a).max() > 0.01 else "FAIL"))

# ------------------------------------------------------------------ 目標姿勢
OPS = dict((t, o) for _g, t, _l, o in LADDER)[TAG]
log("=== POSE %s ===" % TAG)
reset_pose()
arm.location = BASE_LOC
apply_ops(arm, OPS)
bpy.context.view_layer.update()
ground_and_balance(arm, lambda: eval_arrays(BODY), M.foot_mask,
                   M.foot_zmin, M.foot_cy, BASE_LOC)
bpy.context.view_layer.update()

CO_LBS, _ = eval_arrays(BODY)
CO_IMP, DISP = correct(CO_LBS, TAG, diag=True)

CCO_LBS = CCO_IMP = None
if CLOTH is not None:
    CCO_LBS, CTRIS = eval_arrays(CLOTH)
    # 褌是照皮膚裁出來的（弓形實體＝皮膚等值線），所以它跟著皮膚的位移場走：
    # 每個布頂點取最近的皮膚頂點的位移。一階處理，記帳在報告裡。
    # （Blender 沒有 scipy ⇒ 用 mathutils.kdtree）
    from mathutils.kdtree import KDTree
    kd = KDTree(len(CO_LBS))
    for i, p in enumerate(CO_LBS):
        kd.insert(p.tolist(), i)
    kd.balance()
    SKIN_D = CO_IMP - CO_LBS
    CCO_IMP = CCO_LBS.copy()
    for i, p in enumerate(CCO_LBS):
        _, j, _ = kd.find(p.tolist())
        CCO_IMP[i] += SKIN_D[j]

def xsect_pairs(co):
    from mathutils.bvhtree import BVHTree
    tree = BVHTree.FromPolygons(co.tolist(), TRIS.tolist(), all_triangles=True,
                                epsilon=0.0)
    out = set()
    for a, b in tree.overlap(tree):
        if a >= b:
            continue
        ta, tb = TRIS[a], TRIS[b]
        if ta[0] in tb or ta[1] in tb or ta[2] in tb:
            continue
        out.add((a, b))
    return out


REST_PAIRS = xsect_pairs(CO_REST)
log("rest self-intersecting pairs (基線，站姿肚子本來就壓著自己) = %d" % len(REST_PAIRS))


def classify_xsect(co, label):
    """自穿透的三角形對，按「哪兩塊互穿」分類。
    這決定了碰撞規則能不能碰到真兇：同一塊自己穿自己、或關節相鄰的兩塊互穿，
    都被排除在解算之外——不先分類就調參數，等於閉著眼睛修。"""
    tp = DOM[TRIS[:, 0]]
    tally = {}
    # **只看新增的**：靜止網格本身就有 837 對互穿（力士站著肚子就疊在自己身上），
    # 拿絕對值分類會把基線當成姿勢造成的破圖。指標本身也是減完基線的。
    for a, b in (xsect_pairs(co) - REST_PAIRS):
        pa, pb = int(tp[a]), int(tp[b])
        key = (min(pa, pb), max(pa, pb))
        tally[key] = tally.get(key, 0) + 1
    rows = sorted(tally.items(), key=lambda kv: -kv[1])[:8]
    vmap = {ip: k for k, ip in enumerate(VALID)}
    log("  **新增**自穿透分類 [%s]（前 8 組，總計 %d 對）:"
        % (label, sum(tally.values())))
    for (pa, pb), n in rows:
        ka, kb = vmap.get(pa, -1), vmap.get(pb, -1)
        if pa == pb:
            kind = "同一塊（自己穿自己）→ 碰撞規則排除"
        elif ka >= 0 and kb >= 0 and ADJ_PARTS[ka, kb]:
            kind = "骨架相鄰（關節）→ 碰撞規則排除"
        else:
            kind = "非相鄰 → **解算中**"
        log("    %-10s x %-10s  %6d  %s" % (part_names[pa], part_names[pb], n, kind))


classify_xsect(CO_LBS, "LBS")
m_lbs = M.measure(co_override=CO_LBS, cco_override=CCO_LBS)
def why_new_xsect(co_lbs, co_imp):
    """**修正器自己造出來的**互穿，是怎麼造出來的？

    假說：接觸推出是沿 −∇g（別人的場的梯度）。在**凹面**（腋下、胯下）推出時，
    各點的推出方向互相收斂 ⇒ 表面被橫向擠壓 ⇒ 三角形面積縮小、彼此疊過去。
    驗法＝量這些三角形的面積比（imp/lbs）。顯著 <1 就是橫向壓縮實錘。
    """
    new_lbs = xsect_pairs(co_lbs) - REST_PAIRS
    new_imp = xsect_pairs(co_imp) - REST_PAIRS
    created = new_imp - new_lbs
    healed = new_lbs - new_imp
    log("=== 為什麼互穿變多 ===")
    log("  LBS 新增 %d 對；implicit 新增 %d 對" % (len(new_lbs), len(new_imp)))
    log("  其中 修正器治好的 = %d 對，修正器自己造的 = %d 對"
        % (len(healed), len(created)))
    if not created:
        return

    def area(co, idx):
        a, b, c = co[TRIS[idx, 0]], co[TRIS[idx, 1]], co[TRIS[idx, 2]]
        return np.linalg.norm(np.cross(b - a, c - a), axis=1) * 0.5

    tset = np.array(sorted({t for p in created for t in p}), dtype=np.int64)
    allt = np.arange(len(TRIS))
    ar_bad = area(co_imp, tset) / np.maximum(area(co_lbs, tset), 1e-12)
    ar_all = area(co_imp, allt) / np.maximum(area(co_lbs, allt), 1e-12)
    dv = np.linalg.norm(co_imp - co_lbs, axis=1) * 1000.0
    d_bad = dv[TRIS[tset]].mean(axis=1)
    log("  肇事三角形 %d 個：面積比 mean=%.3f  p10=%.3f   （全網格 mean=%.3f）"
        % (len(tset), ar_bad.mean(), np.percentile(ar_bad, 10), ar_all.mean()))
    log("  肇事三角形的位移 mean=%.2fmm p90=%.2fmm （全網格 mean=%.2fmm）"
        % (d_bad.mean(), np.percentile(d_bad, 90), dv.mean()))
    tp = DOM[TRIS[:, 0]]
    tally = {}
    for a_, b_ in created:
        k = (min(int(tp[a_]), int(tp[b_])), max(int(tp[a_]), int(tp[b_])))
        tally[k] = tally.get(k, 0) + 1
    for (pa, pb), n in sorted(tally.items(), key=lambda kv: -kv[1])[:6]:
        log("    造出來的： %-10s x %-10s %5d 對" % (part_names[pa], part_names[pb], n))


why_new_xsect(CO_LBS, CO_IMP)
classify_xsect(CO_IMP, "implicit")
m_imp = M.measure(co_override=CO_IMP, cco_override=CCO_IMP)

HDR = ("case\tmode\tverdict\tfold\tfold_groups\tfold_max_cm\tfold_at\t"
       "xsect\txs_groups\txs_max_cm\txs_at\tcross\tvol_loss_pct\tbalance_cm")


def row(mode, m):
    return "%s\t%s\t%s\t%d\t%d\t%.1f\t%s\t%d\t%d\t%.1f\t%s\t%d\t%.2f\t%.1f" % (
        TAG, mode, verdict_of(m), m['fold'], m['fold_groups'], m['fold_max_cm'],
        m['fold_at'], m['xsect'], m['xs_groups'], m['xs_max_cm'], m['xs_at'],
        m['cross'], m['vol_loss'], m['balance'] * 100.0)


lines = [HDR, row("lbs", m_lbs), row("implicit", m_imp)]
log("\n".join(lines))
out_tsv = os.path.join(PROBE, "implicit_%s.tsv" % TAG)
with open(out_tsv, "w", encoding="utf-8") as f:
    f.write("\n".join(lines) + "\n")
np.save(os.path.join(PROBE, "implicit_%s_co.npy" % TAG), CO_IMP)
log("WROTE %s" % out_tsv)

# ---------------------------------------------------- 修正量落在哪裡（分區表）
# 「看不出差別」不是拿眼睛去找 2mm，而是把位移量印出來／畫出來。
REGION_ROWS = []
_reg = np.array([body_part(p) for p in CO_REST[::7]])   # 抽樣算分區名太慢，逐點分桶
_regions = {}
for i in range(NV):
    if DISP[i] <= 1e-5:
        continue
    k = body_part(CO_REST[i])
    _regions.setdefault(k, []).append(DISP[i] * 1000.0)
log("=== 修正量落在哪裡（>0.01mm 的頂點，依部位分桶）===")
log("  %-16s %7s %9s %9s %9s" % ("部位", "頂點數", "平均mm", "p99mm", "最大mm"))
for k, v in sorted(_regions.items(), key=lambda kv: -np.mean(kv[1]))[:14]:
    v = np.array(v)
    log("  %-16s %7d %9.2f %9.2f %9.2f"
        % (k, len(v), v.mean(), np.percentile(v, 99), v.max()))
    REGION_ROWS.append("%s\t%d\t%.2f\t%.2f\t%.2f"
                       % (k, len(v), v.mean(), np.percentile(v, 99), v.max()))
with open(os.path.join(PROBE, "implicit_%s_where.tsv" % TAG), "w",
          encoding="utf-8") as f:
    f.write("region\tverts\tmean_mm\tp99_mm\tmax_mm\n" + "\n".join(REGION_ROWS) + "\n")

log("SUMMARY %s: xsect %d -> %d | fold_max %.1fcm -> %.1fcm | cross %d -> %d | "
    "vol_loss %.2f%% -> %.2f%% | null_ctrl %.3fmm"
    % (TAG, m_lbs['xsect'], m_imp['xsect'], m_lbs['fold_max_cm'], m_imp['fold_max_cm'],
       m_lbs['cross'], m_imp['cross'], m_lbs['vol_loss'], m_imp['vol_loss'], NULL_MAX_MM))

# ------------------------------------------------------------------ 對照圖
if WANT_SHOTS:
    import math
    from mathutils import Vector
    OUT = os.path.join(PROBE, "implicit_%s" % TAG)
    os.makedirs(OUT, exist_ok=True)
    scene = bpy.context.scene
    scene.render.engine = 'BLENDER_WORKBENCH'
    scene.render.resolution_x = 640
    scene.render.resolution_y = 780
    scene.display.shading.light = 'STUDIO'
    scene.display.shading.color_type = 'SINGLE'
    scene.display.shading.single_color = (0.72, 0.58, 0.50)
    scene.display.shading.show_cavity = True
    cd = bpy.data.cameras.new("ICam")
    cd.type = 'ORTHO'
    cam = bpy.data.objects.new("ICam", cd)
    scene.collection.objects.link(cam)
    scene.camera = cam
    arm.hide_render = True

    lo, hi = CO_LBS.min(axis=0), CO_LBS.max(axis=0)
    CTR = Vector(((lo + hi) * 0.5).tolist())
    SIZE = float(max(hi - lo)) * 1.10
    HIP = arm.matrix_world @ arm.pose.bones["LeftUpLeg"].head

    def bake(co, name, src=None):
        """把一組世界座標烘成可渲染的網格。

        **不要用 from_pydata 重建**（2026-08-27 血價）：新網格的 polygon 預設
        use_smooth=False ⇒ 每個三角形各自一條法線 ⇒ 整身魚鱗狀刻面。
        SumoRetopo 本體是 100% smooth、而且帶 custom split normals（08-05 柔化法線轉印）。
        正解＝複製原網格資料塊、只覆寫座標，平滑與銳邊標記原樣帶過來。

        custom_normal 則**要清掉**：那份法線是為靜止姿勢烘的，直接搬到擺過姿勢的
        網格上是錯的（它不會跟著骨頭轉）。清掉之後由幾何自己算平滑法線——
        LBS 與 implicit 兩邊**同樣處理**，所以看到的著色差異才真的來自幾何差異。
        代價：這批探針圖的著色與遊戲內（用柔化法線）不完全相同，記帳在此。
        """
        me = (src or BODY).data.copy()
        me.name = name
        me.vertices.foreach_set("co", np.ascontiguousarray(co, dtype=np.float32).ravel())
        cn = me.attributes.get("custom_normal")
        if cn is not None:
            me.attributes.remove(cn)
        me.update()
        ob = bpy.data.objects.new(name, me)
        scene.collection.objects.link(ob)
        return ob

    ob_imp = bake(CO_IMP, "SumoImplicit")
    ob_lbs = bake(CO_LBS, "SumoLBS")

    # 位移熱圖：灰＝沒動、黃＝中、紅＝到上限。**這才是回答「作用在哪裡」的圖**，
    # 用肉眼在兩張幾乎一樣的圖之間找 2mm 差異是不合理的要求。
    HEAT_MAX_MM = MAX_IN_MM
    ob_heat = bake(CO_IMP, "SumoHeat")
    ca = ob_heat.data.color_attributes.new("disp", 'FLOAT_COLOR', 'POINT')
    t = np.clip(DISP * 1000.0 / HEAT_MAX_MM, 0.0, 1.0)
    grey = np.array([0.72, 0.68, 0.64])
    yellow = np.array([0.98, 0.80, 0.15])
    red = np.array([0.90, 0.12, 0.10])
    a = np.clip(t * 2.0, 0, 1)[:, None]
    b = np.clip(t * 2.0 - 1.0, 0, 1)[:, None]
    col = (grey[None, :] * (1 - a) + yellow[None, :] * a) * (1 - b) + red[None, :] * b
    rgba = np.concatenate([col, np.ones((NV, 1))], axis=1)
    ca.data.foreach_set("color", rgba.astype(np.float32).ravel())
    ob_heat.data.color_attributes.active_color_index =         ob_heat.data.color_attributes.find("disp")
    ob_heat.hide_render = True
    ob_cloth = None
    if CCO_IMP is not None:
        ob_cloth = bake(CCO_IMP, "FundoshiImplicit", CLOTH)
        ob_cloth.hide_render = True

    def look(center, az_deg, el_deg, scale, path):
        cd.ortho_scale = scale
        az, el = math.radians(az_deg), math.radians(el_deg)
        d = Vector((math.sin(az) * math.cos(el), -math.cos(az) * math.cos(el),
                    math.sin(el)))
        cam.location = center + d * 6.0
        cam.rotation_euler = (-d).to_track_quat('-Z', 'Y').to_euler()
        scene.render.filepath = path
        bpy.ops.render.render(write_still=True)

    BODY.hide_render = True
    if CLOTH is not None:
        CLOTH.hide_render = True
    views = ([("az%03d" % a, CTR, a, 8.0, SIZE) for a in range(0, 360, 45)]
             + [("hip%03d" % a, Vector(HIP), a, 5.0, 0.55) for a in (0, 45, 90, 135)])
    for mode, ob in (("imp", ob_imp), ("lbs", ob_lbs), ("heat", ob_heat)):
        for o in (ob_imp, ob_lbs, ob_heat):
            o.hide_render = (o is not ob)
        scene.display.shading.color_type = 'VERTEX' if mode == "heat" else 'SINGLE'
        for nm, c, a, e, s in views:
            look(c, a, e, s, os.path.join(OUT, "%s_%s_%s.png" % (TAG, mode, nm)))
        log("  shots done: %s" % mode)
    log("WROTE %s" % OUT)

log("DONE")
