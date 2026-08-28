# Implicit Skinning（Vaillant / Barthe / Guennebaud / Cani / Wyvill, SIGGRAPH 2013）
# ——本作的「肥肉引擎」離線實作。2026-08-27。
#
# 為什麼是這條路（授權已查，2026-08-27）：
#   論文本身免費（HAL 開放典藏），演算法不受著作權保護；作者的原型碼是 GPL3（病毒式、
#   不能連進閉源 UE 遊戲）、Implicit Blending Library 是 academic-only（商用禁止）、
#   production 版走 Toulouse Tech Transfer 商業授權（要付費、而且是 Maya 外掛）。
#   ⇒ **本檔案從論文重寫，一行都沒有抄他們的碼**，零授權風險、零費用。
#
# 機理（為什麼它能解「肥肉互穿」）：
#   LBS 是逐頂點的線性混合，兩塊肉靠近時沒有任何東西阻止它們互相穿過去。
#   Implicit skinning 把每個身體部位近似成一個**隱式純量場**，每一幀把這些場**合成**，
#   再把 LBS 算出來的頂點沿場的梯度**投影回它自己在靜止時的等值**。
#   不互穿於是變成**構造保證**——合成場的等值面是聯集的邊界，肚子的頂點投影到那個面上
#   就停在大腿表面，而不是穿過去。接觸處的鼓起則來自合成算子本身。
#
# 五個步驟（對應論文）：
#   ① 分部     ：依最大蒙皮權重把網格切成每骨一塊（Jiggle_* 併入解剖父部位——
#                 它們在掃描期恆為單位旋轉，與父骨同一個剛體變換）
#   ② HRBF     ：每部位擬一個 Hermite RBF 隱式場（f(p_i)=0、∇f(p_i)=n_i），
#                 φ(r)=r³；開放邊界用 cap disc 封起來（不封＝場在切口發散）
#   ③ 重參數化 ：把近似有號距離壓成 [0,1] 的緊支撐場（五次多項式，iso=0.5，支撐半徑 R）
#                 ——「遠距不混合」由緊支撐構造保證
#   ④ 合成     ：**梯度驅動的 Ricci 算子** f=(Σfᵢⁿ)^(1/n)
#                 n 由兩場梯度夾角決定：夾角小（同一肢體彎關節）→ n 大 ≈ max ＝乾淨聯集
#                 不長假鼓包；夾角大（正面接觸）→ n 小 ＝接觸處鼓起。
#                 這一族算子同時滿足論文列的四個目標（合併不長鼓包／遠距不混合／
#                 保持聯集拓樸／尖銳細節不被吹大）。
#                 **誠實但書**：論文用的是 Gourmel 2013 的 profile-curve 算子族（IBL，
#                 academic-only），本檔用 Ricci 族取代——行為同構、參數少一個維度。
#   ⑤ 投影     ：兩種形式都在檔案裡，**出貨走 project_contact**：
#                 - project()          ＝論文原形：把頂點沿 ∇f 拉回它靜止時的等值 f₀
#                 - project_contact()  ＝本作實際採用：只看「別人的場」，以靜止值 g₀ 當基線
#                 為什麼換：力士**站著的時候肚子就已經壓在大腿上**，那一帶的 f₀ 本來就
#                 遠離 0.5（實測 29.6% 的軀幹頂點落在信賴帶外，而破圖正好都在那裡）
#                 ⇒「復原靜止等值」在最需要它的地方最不可信。以 g₀ 當基線就繞開了：
#                 基線多少不重要，**不要比靜止時更陷進去**就好。細節見 project_contact。
#
# 已知界限（實測，不是猜的）：pick_a 這個姿勢下 LBS 新增的 396 對自穿透裡，
#   42%（168 對）是**同一塊自己穿自己**（thighR×thighR 82、thighL×thighL 61、armL×armL 25）。
#   一塊部位＝一顆 blob，隱式場**在原理上無法**表達它自己摺到自己 ⇒ 這 42% 這套方法治不了。
#   18%（head×chest 73）是下巴陷進胸口——骨架相鄰，被「關節不算碰撞」的規則排除掉了；
#   要救它得用論文的梯度判準去區分「關節合併」與「正面相撞」，目前未做。
#   32%（125 對）才是這套方法的主場（armL×thighL 62、armR×thighR 26、head×shoulder 37）。
#
# 兩個構造性紅利（本專案的鐵律）：
#   * **只動頂點位置、不動 UV0** ⇒ 舊刺青座標完全安全；刺青跟著肉被擠壓變形＝加分。
#   * **純函式**（姿勢 → 頂點），零狀態、零累積、零亂數 ⇒ 六台機器逐位一致。
#
# 自檢閘門（`null_control` ）：把修正器套在**靜止姿勢**上，每個頂點的位移必須 ≈ 0。
#   不驗這條，等於重蹈 fundoshi_pose_check「七個姿勢數字全等其實是量了同一份資料」。
import numpy as np

EPS = 1e-12


# ---------------------------------------------------------------- HRBF

def hrbf_fit(centers, normals, reg=1e-8):
    """解 4N×4N 系統，使 f(cᵢ)=0 且 ∇f(cᵢ)=nᵢ。回傳 (alpha (N,), beta (N,3))。
    φ(r)=r³ ⇒ φ(0)=0, ∇φ(0)=0, Hφ(0)=0，所以對角區塊為零（標準 HRBF）。"""
    n = centers.shape[0]
    D = centers[:, None, :] - centers[None, :, :]        # (N,N,3)
    r = np.linalg.norm(D, axis=2)
    r_safe = np.where(r < EPS, 1.0, r)
    phi = r ** 3
    gphi = (3.0 * r_safe)[:, :, None] * D                # ∇φ  (N,N,3)
    # Hφ(d) = 3(r I + d dᵀ / r)
    A = np.zeros((4 * n, 4 * n), dtype=np.float64)
    A[:n, :n] = phi
    # 列 i、行 (3j+k) = ∇φ(cᵢ-c_j)_k
    A[:n, n:] = gphi.reshape(n, 3 * n)
    # 列 (3i+k)、行 j = ∇φ(cᵢ-c_j)_k ⇒ 要把軸序換成 (i,k,j) 再攤平。
    # 用 transpose(1,0,2) 是錯的（那會攤成 (3j+i,...)）——單元自檢抓到：
    # 擬球後 |∇f| 只有 0.60、梯度甚至指向內側（cos=−0.56）。
    A[n:, :n] = gphi.transpose(0, 2, 1).reshape(3 * n, n)
    H = 3.0 * (r_safe[:, :, None, None] * np.eye(3)[None, None, :, :]
               + (D[:, :, :, None] * D[:, :, None, :]) / r_safe[:, :, None, None])
    H[r < EPS] = 0.0
    A[n:, n:] = H.transpose(0, 2, 1, 3).reshape(3 * n, 3 * n)
    b = np.concatenate([np.zeros(n), normals.reshape(-1)])
    A[np.diag_indices_from(A)] += reg
    sol = np.linalg.solve(A, b)
    return sol[:n], sol[n:].reshape(n, 3)


def hrbf_eval(centers, alpha, beta, X, want_grad=True, chunk=4096):
    """回傳 (f (m,), grad (m,3))。f>0 在外、f<0 在內（≈有號距離）。"""
    m = X.shape[0]
    f = np.empty(m, dtype=np.float64)
    g = np.zeros((m, 3), dtype=np.float64) if want_grad else None
    for s in range(0, m, chunk):
        e = min(s + chunk, m)
        D = X[s:e, None, :] - centers[None, :, :]
        r = np.sqrt(np.einsum('ijk,ijk->ij', D, D))
        r_safe = np.where(r < EPS, 1.0, r)
        db = np.einsum('ijk,jk->ij', D, beta)
        f[s:e] = (r ** 3) @ alpha + 3.0 * np.sum(r_safe * db, axis=1)
        if want_grad:
            t1 = np.einsum('j,ij,ijk->ik', alpha, 3.0 * r_safe, D)
            t2 = 3.0 * (r_safe @ beta
                        + np.einsum('ijk,ij->ik', D, db / r_safe))
            g[s:e] = t1 + t2
    return f, g


# ------------------------------------------------- 重參數化（緊支撐 [0,1]）

def reparam(d, R):
    """近似有號距離 d（外正）→ [0,1] 場，iso=0.5，|d|≥R 飽和。C¹ 五次。"""
    t = np.clip(d / R, -1.0, 1.0)
    return 0.5 - (15.0 / 16.0) * t + (5.0 / 8.0) * t ** 3 - (3.0 / 16.0) * t ** 5


def reparam_d(d, R):
    t = np.clip(d / R, -1.0, 1.0)
    out = (-15.0 / 16.0 + (15.0 / 8.0) * t ** 2 - (15.0 / 16.0) * t ** 4) / R
    out[np.abs(d) >= R] = 0.0
    return out


# ------------------------------------------------- 梯度驅動 Ricci 合成

def ricci_pair(fa, ga, fb, gb, n_min, n_max, cos_lo, cos_hi, jointed=None):
    """兩場合成。n 由 ∇fa·∇fb 決定：同向 → n 大＝乾淨聯集不長鼓包；
    反向（正面接觸）→ n 小＝接觸處鼓起。回傳 (f, grad)。

    jointed：這兩塊在骨架上**直接相連**（膝、髖、肩…）＝強制乾淨聯集。
    只看梯度夾角是不夠的：膝窩在彎曲時大腿與小腿的梯度本來就相對，
    會被誤判成「兩塊肉撞在一起」而長出鼓包——實測膝後摺團 25.1cm。
    關節不該鼓，撞擊才該鼓；而「是不是關節」骨架自己知道，不用用梯度去猜。"""
    na = np.linalg.norm(ga, axis=1)
    nb = np.linalg.norm(gb, axis=1)
    denom = np.maximum(na * nb, EPS)
    c = np.clip(np.einsum('ij,ij->i', ga, gb) / denom, -1.0, 1.0)
    # c=+1 同向 → n_max；c=-1 反向 → n_min
    w = np.clip((c - cos_lo) / max(cos_hi - cos_lo, EPS), 0.0, 1.0)
    nexp = n_min + (n_max - n_min) * w
    if jointed is not None:
        nexp = np.where(jointed, n_max, nexp)
    a = np.maximum(fa, 0.0)
    b = np.maximum(fb, 0.0)
    both = (a > EPS) & (b > EPS)
    f = np.maximum(a, b).copy()
    g = np.where((a >= b)[:, None], ga, gb).copy()
    if np.any(both):
        i = np.where(both)[0]
        ai, bi, ni = a[i], b[i], nexp[i]
        an = ai ** ni
        bn = bi ** ni
        s = an + bn
        fi = s ** (1.0 / ni)
        f[i] = fi
        w1 = (fi / np.maximum(s, EPS)) * (an / np.maximum(ai, EPS))
        w2 = (fi / np.maximum(s, EPS)) * (bn / np.maximum(bi, EPS))
        g[i] = w1[:, None] * ga[i] + w2[:, None] * gb[i]
    return f, g


def compose(F, G, n_min, n_max, cos_lo, cos_hi, topk=5, adj=None):
    """F=(m,P) 各部位場、G=(m,P,3) 梯度 → 合成 (f,(m,), grad (m,3))。
    每個頂點只取值最大的 topk 個部位（其餘 ≈0，緊支撐保證無影響）。
    adj=(P,P) 布林骨架相鄰矩陣；相鄰＝關節＝強制乾淨聯集。"""
    m, P = F.shape
    k = min(topk, P)
    order = np.argsort(-F, axis=1)[:, :k]
    rows = np.arange(m)[:, None]
    Fs = F[rows, order]
    Gs = G[rows, order]
    f = Fs[:, 0].copy()
    g = Gs[:, 0].copy()
    dom = order[:, 0]
    for j in range(1, k):
        jointed = adj[dom, order[:, j]] if adj is not None else None
        f, g = ricci_pair(f, g, Fs[:, j], Gs[:, j], n_min, n_max,
                          cos_lo, cos_hi, jointed)
    return f, g


# ---------------------------------------------------------------- 部位

class Part:
    """一塊近似剛體的身體部位＋它的 HRBF 隱式場（存在部位的靜止局部座標系）。"""

    def __init__(self, name, bones, vidx, centers, alpha, beta, R, rest_inv):
        self.name = name
        self.bones = bones
        self.vidx = vidx
        self.centers = centers
        self.alpha = alpha
        self.beta = beta
        self.R = R
        self.rest_inv = rest_inv      # 世界→部位靜止局部
        lo = centers.min(axis=0) - R * 1.5
        hi = centers.max(axis=0) + R * 1.5
        self.local_lo, self.local_hi = lo, hi

    def eval_world(self, X, M_world):
        """M_world＝該部位這一幀的剛體變換（4×4，作用在靜止世界座標上）。"""
        Minv = np.linalg.inv(M_world)
        Xl = X @ Minv[:3, :3].T + Minv[:3, 3]
        inside = np.all((Xl >= self.local_lo) & (Xl <= self.local_hi), axis=1)
        f = np.zeros(X.shape[0])
        g = np.zeros_like(X)
        if not np.any(inside):
            return f, g
        d, gl = hrbf_eval(self.centers, self.alpha, self.beta, Xl[inside])
        f[inside] = reparam(d, self.R)
        g[inside] = (gl * reparam_d(d, self.R)[:, None]) @ M_world[:3, :3].T
        return f, g


def fit_part(name, bones, vidx, verts, normals, boundary_loops, n_samples, R,
             rest_inv=np.eye(4)):
    """對一塊部位的頂點取樣＋封蓋，擬 HRBF。verts/normals 為**靜止世界座標**。"""
    P = verts[vidx]
    N = normals[vidx]
    sel = farthest_point_sample(P, min(n_samples, len(P)))
    C = [P[sel]]
    NN = [N[sel]]
    for loop, ndir in boundary_loops:
        cap_p, cap_n = cap_disc(verts[loop], ndir)
        C.append(cap_p)
        NN.append(cap_n)
    C = np.concatenate(C, axis=0)
    NN = np.concatenate(NN, axis=0)
    # 去重（HRBF 矩陣不能有重合中心）
    keep = dedup(C, tol=2e-3)
    C, NN = C[keep], NN[keep]
    NN = NN / np.maximum(np.linalg.norm(NN, axis=1, keepdims=True), EPS)
    alpha, beta = hrbf_fit(C, NN)
    return Part(name, bones, vidx, C, alpha, beta, R, rest_inv)


def cap_disc(loop_pts, ndir, rings=(0.0, 0.45, 0.8)):
    """把一個開放邊界環封成圓盤取樣點，法線＝ndir（指向部位外）。"""
    c = loop_pts.mean(axis=0)
    out_p = []
    out_n = []
    for t in rings:
        if t == 0.0:
            out_p.append(c[None, :])
            out_n.append(ndir[None, :])
        else:
            idx = np.linspace(0, len(loop_pts) - 1, 8).astype(int)
            p = c + (loop_pts[idx] - c) * t
            out_p.append(p)
            out_n.append(np.repeat(ndir[None, :], len(p), axis=0))
    return np.concatenate(out_p, 0), np.concatenate(out_n, 0)


def farthest_point_sample(P, k):
    """均勻鋪點：貪婪最遠點取樣（決定性：起點固定取索引 0）。"""
    n = len(P)
    if k >= n:
        return np.arange(n)
    sel = np.empty(k, dtype=np.int64)
    sel[0] = 0
    d = np.linalg.norm(P - P[0], axis=1)
    for i in range(1, k):
        j = int(np.argmax(d))
        sel[i] = j
        d = np.minimum(d, np.linalg.norm(P - P[j], axis=1))
    return sel


def dedup(C, tol):
    keep = []
    used = np.zeros(len(C), dtype=bool)
    for i in range(len(C)):
        if used[i]:
            continue
        keep.append(i)
        used |= np.linalg.norm(C - C[i], axis=1) < tol
        used[i] = True
    return np.array(keep, dtype=np.int64)


# ---------------------------------------------------------------- 投影

def project(X0, f_target, g_rest_world, eval_fn, vrot, max_iter=24,
            tol=2e-4, max_step=0.02, max_out=0.006, max_in=0.060,
            contact_cos=0.30,
            relax_neighbors=None, relax_iters=6, relax_lambda=0.45):
    """把 LBS 頂點 X0 沿合成場梯度投影回各自的靜止等值 f_target。

    vrot        : (m,3,3) 每頂點的主導骨旋轉（把靜止梯度帶到這一幀）
    contact_cos : 當前梯度與「靜止梯度轉過來」的夾角餘弦低於此 ⇒ 判定接觸、停步。
                  這一停就是接觸摺痕的來源（論文的 gradient-divergence 準則）。
    回傳 (X, moved_mask, stats)
    """
    X = X0.copy()
    g_ref = np.einsum('mij,mj->mi', vrot, g_rest_world)
    g_ref /= np.maximum(np.linalg.norm(g_ref, axis=1, keepdims=True), EPS)
    active = np.ones(len(X), dtype=bool)
    contact = np.zeros(len(X), dtype=bool)
    it_used = 0
    for it in range(max_iter):
        idx = np.where(active)[0]
        if len(idx) == 0:
            break
        it_used = it + 1
        f, g = eval_fn(X[idx])
        gn = np.linalg.norm(g, axis=1)
        err = f_target[idx] - f
        done = np.abs(err) < tol
        weak = gn < 1e-6
        cosang = np.einsum('ij,ij->i', g, g_ref[idx]) / np.maximum(gn, EPS)
        # contact_cos <= -1 ⇒ 關閉接觸早停。這條規則是**逐頂點的不連續判定**：
        # 一個頂點停了、隔壁沒停，表面就被撕出鋸齒 ⇒ 反而製造自穿透。
        # 論文靠它生接觸摺痕，但那需要更強的鬆弛把它縫回去；本作先關閉，
        # 由「聯集不互穿」本身承擔防穿透，摺痕交給幾何自然形成。
        hit = (cosang < contact_cos) if contact_cos > -1.0 else np.zeros_like(done)
        contact[idx[hit]] = True
        stop = done | weak | hit
        step = (err / np.maximum(gn ** 2, EPS))[:, None] * g
        sl = np.linalg.norm(step, axis=1)
        too_long = sl > max_step
        step[too_long] *= (max_step / np.maximum(sl[too_long], EPS))[:, None]
        step[stop] = 0.0
        X[idx] += step
        # 總位移上限，**方向不對稱**：向內（被擠）放寬、向外（隔壁讓開）收緊。
        # 理由是物理不是美學——我們模擬的是脂肪被壓扁；「隔壁走開所以我長出來」
        # 沒有物理依據，那是靜止場的疊合誤差在被復原。
        tot = X[idx] - X0[idx]
        tl = np.linalg.norm(tot, axis=1)
        # ∇f 指向**內側**（場在體內＝1、體外＝0），所以「往外」是 tot·g < 0。
        # 這個號差一開始寫反了，等於把 60mm 的額度發給了膨脹、6mm 給了擠壓。
        outward = np.einsum('ij,ij->i', tot, g_ref[idx]) < 0.0
        cap = np.where(outward, max_out, max_in)
        over = tl > cap
        if np.any(over):
            X[idx[over]] = X0[idx[over]] + tot[over] * (cap[over] / tl[over])[:, None]
            stop[over] = True
        active[idx[stop]] = False
    moved = np.linalg.norm(X - X0, axis=1) > 1e-5
    # 切向鬆弛：把擠成一堆的頂點攤開（不做＝假摺痕）。
    # **只鬆弛真的動過的頂點（外擴兩環當羽化）**——對沒動過的頂點做平滑
    # ＝把乳頭／肚臍那些細節磨掉，那是修正器不該碰的東西。
    if relax_neighbors is not None and relax_iters > 0:
        rm = dilate(moved, relax_neighbors, 2)
        X = tangential_relax(X, f_target, eval_fn, relax_neighbors,
                             relax_iters, relax_lambda, max_step, rm)
    # 鬆弛之後**再夾一次總位移**：鬆弛每輪都能走 max_step，六輪就能把
    # 投影階段夾好的上限整個漏掉（實測位移 112mm 遠超 60mm 的內夾上限）。
    tot = X - X0
    tl = np.linalg.norm(tot, axis=1)
    cap = np.where(np.einsum('ij,ij->i', tot, g_ref) < 0.0, max_out, max_in)
    over = tl > cap
    if np.any(over):
        X[over] = X0[over] + tot[over] * (cap[over] / np.maximum(tl[over], EPS))[:, None]
    moved = np.linalg.norm(X - X0, axis=1) > 1e-5
    return X, moved, dict(iters=it_used, contact=int(contact.sum()),
                          unconverged=int(active.sum()), relaxed=int(
                              dilate(moved, relax_neighbors, 0).sum())
                          if relax_neighbors is not None else 0)


def project_contact(X0, g_target, eval_other, max_push=0.060, max_iter=30,
                    tol=1e-4, max_step=0.008,
                    relax_neighbors=None, relax_iters=0, relax_lambda=0.4):
    """**接觸解算**（本作實際採用的形式）。

    與「復原靜止等值」不同：這裡只看**別人的場**，而且以靜止時的值當基線。

        g(x) = max over q（q 不是我自己、也不是我的關節鄰居）of f_q(x)

    靜止時記下 g₀；擺姿勢後若 g > g₀，代表這個頂點比靜止時**更深地陷進別人身體裡**，
    就沿 −∇g 推出去直到回到 g₀。g ≤ g₀ ⇒ 一步都不動。

    為什麼要這樣改（2026-08-27 實測定案）：力士的肚子在**靜止站姿就已經壓在大腿上**，
    所以那一帶的合成場 f₀ 本來就遠離 0.5 ⇒「復原靜止等值」在最需要它的地方最不可信
    （量到 torsoLow 有 29.6% 的頂點落在信賴帶外，而破圖正好都在那裡）。
    以 g₀ 當基線就把這件事整個繞開：**基線是多少不重要，不要比靜止時更陷進去就好。**

    三個構造性好處：
      * 單向：只推出、不長出來 ⇒ 不可能有「隔壁走開所以我膨脹」那種假變形
      * 關節免疫：骨架相鄰的兩塊直接不參與 ⇒ 膝窩／髖不會被當成兩塊肉相撞
      * 與靜止重疊無關 ⇒ 不需要信賴度門檻去閃避它
    """
    X = X0.copy()
    active = np.ones(len(X), dtype=bool)
    capped = np.zeros(len(X), dtype=bool)   # 撞到位移上限＝**沒解完**，不能靜默當作解完
    it_used = 0
    for it in range(max_iter):
        idx = np.where(active)[0]
        if len(idx) == 0:
            break
        it_used = it + 1
        g, gg = eval_other(X[idx], idx)
        err = g_target[idx] - g              # <0 ⇒ 陷太深，要往外
        gn = np.linalg.norm(gg, axis=1)
        stop = (err > -tol) | (gn < 1e-6)
        step = (err / np.maximum(gn ** 2, EPS))[:, None] * gg
        sl = np.linalg.norm(step, axis=1)
        too_long = sl > max_step
        step[too_long] *= (max_step / np.maximum(sl[too_long], EPS))[:, None]
        step[stop] = 0.0
        X[idx] += step
        tot = X[idx] - X0[idx]
        tl = np.linalg.norm(tot, axis=1)
        over = tl > max_push
        if np.any(over):
            X[idx[over]] = X0[idx[over]] + tot[over] * (max_push / tl[over])[:, None]
            stop[over] = True
            capped[idx[over]] = True
        active[idx[stop]] = False
    # 推完之後把被推的那一片攤平：只推不攤，被推的區域會自己摺起來
    # （實測 thighR×thighR 從 82 對變 186 對＝解一個碰撞、造一個自摺）。
    if relax_neighbors is not None and relax_iters > 0:
        pushed = np.linalg.norm(X - X0, axis=1) > 1e-5
        rm = dilate(pushed, relax_neighbors, 2)
        nb_idx, nb_ptr = relax_neighbors
        cnt = np.maximum(np.diff(nb_ptr), 1)[:, None]
        sel = np.where(rm)[0]
        for _ in range(relax_iters):
            avg = np.add.reduceat(X[nb_idx], nb_ptr[:-1], axis=0) / cnt
            X[sel] = X[sel] + relax_lambda * (avg[sel] - X[sel])
        tot = X - X0
        tl = np.linalg.norm(tot, axis=1)
        over = tl > max_push
        if np.any(over):
            X[over] = X0[over] + tot[over] * (max_push / np.maximum(tl[over], EPS))[:, None]
    depth = np.linalg.norm(X - X0, axis=1)
    return X, dict(iters=it_used, unresolved=int(active.sum()),
                   capped=int(capped.sum()),
                   touched=int((depth > 1e-5).sum()))


def dilate(mask, nb, rings):
    nb_idx, nb_ptr = nb
    m = mask.copy()
    for _ in range(rings):
        hit = np.zeros(len(m), dtype=bool)
        src = m[nb_idx]
        np.logical_or.at(hit, np.repeat(np.arange(len(m)), np.diff(nb_ptr)), src)
        m = m | hit
    return m


def tangential_relax(X, f_target, eval_fn, nb, iters, lam, max_step, mask):
    """鄰居平均 → 只取切向分量 → 一步牛頓拉回等值面。保形不掉體積。
    只更新 mask 為真的頂點；鄰居位置照樣讀全體（否則邊界會被自己拉歪）。"""
    nb_idx, nb_ptr = nb
    sel = np.where(mask)[0]
    if len(sel) == 0:
        return X
    cnt = np.maximum(np.diff(nb_ptr), 1)[:, None]
    for _ in range(iters):
        avg = np.add.reduceat(X[nb_idx], nb_ptr[:-1], axis=0) / cnt
        f, g = eval_fn(X[sel])
        gn = np.maximum(np.linalg.norm(g, axis=1), EPS)
        u = g / gn[:, None]
        d = avg[sel] - X[sel]
        d -= u * np.einsum('ij,ij->i', d, u)[:, None]     # 去掉法向分量
        dl = np.linalg.norm(d, axis=1)
        over = dl > max_step
        d[over] *= (max_step / np.maximum(dl[over], EPS))[:, None]
        X[sel] = X[sel] + lam * d
        f, g = eval_fn(X[sel])
        gn2 = np.maximum(np.linalg.norm(g, axis=1) ** 2, EPS)
        corr = ((f_target[sel] - f) / gn2)[:, None] * g
        cl = np.linalg.norm(corr, axis=1)
        over = cl > max_step
        corr[over] *= (max_step / np.maximum(cl[over], EPS))[:, None]
        X[sel] = X[sel] + corr
    return X
