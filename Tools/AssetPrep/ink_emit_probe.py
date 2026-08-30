# 出墨落點離線探針（唯讀；不開引擎、不開 PIE、一輪數十秒）
#
# 為什麼有這支：2026-08-30 user 質問「為什麼一定需要我？你不能自己抓？」——
# 過去兩輪修出墨路徑都是「取樣三個看起來像真人的手速」然後宣告乾淨，而 bug
# 不是手速的函數，是「兩幀之間筆尖跳多遠」與「筆軸與視線夾角」的函數。
# 那兩個純量我完全控制得了 ⇒ 用掃描取代取樣。
#
# 複製的是 NiceInkCharacter.cpp 的出墨幾何（逐行對照）：
#   P    = TraceAimToTarget(eye, dir)                       :2546
#   Tip  = P（07-27 起墨鏈真相＝準星命中點，非筆軸再 trace）  :5990,6016
#   EmitDotAt(TipP) → TipToSurfaceUV：                       :2879
#       A = TipP + Shaft*2.5cm ；B = TipP - Shaft*3.0cm      （ShaftOut 指向手側）
#       hit = LineTrace(A,B)；miss ⇒ 無聲丟棄（本檔記成 DROP）
#   稿筆補間：EmitDotAt(Lerp(TipFrom,TipNow,t)) 每 SpacingCm :3014
#       SpacingCm = TattooSpacingK(0.5) * TattooNibDiameterCm(0.30) = 0.15cm
#
# 兩個假說：
#   H1 純幾何（零手速、零掉幀）：Tip 已經是準星點 P，但 EmitDotAt 仍沿「筆軸」
#      重打一條 5.5cm 射線把它變成 UV。筆軸≠視線 ⇒ 凸起會攔截 ⇒ 墨落在準星以外。
#      07-27 的註解說這個 bug 修過了——修在筆的視覺與 PenTipWorld，沒修到 emitter。
#   H2 補間：兩幀之間拉世界直線再逐點沿筆軸投影。
#
# 座標：blend 是公尺，UE 是公分。本檔內部用公尺、輸出一律 cm。
# 姿勢：rest pose（非 lean-lock 作畫姿）——凸起形狀同量級，但不是同一份幾何。
#
# 用法：blender --background --python Tools/AssetPrep/ink_emit_probe.py
import bpy
import os
import numpy as np
from collections import deque
from mathutils import Vector
from mathutils.bvhtree import BVHTree

SA = r"C:\games\Unreal Engine\nice_ink\SourceAssets"
MASTER = os.path.join(SA, "sumo_character_master.blend")

SHAFT_BACK_M = 0.025   # A = Tip + Shaft*2.5cm
SHAFT_FWD_M = 0.030    # B = Tip - Shaft*3.0cm
SPACING_M = 0.0015     # 0.15cm
EYE_DIST_M = 0.30      # 眼距 30cm（作畫定律）
ISLAND_MAX = 5000

def P(*a):
    print(*a, flush=True)

bpy.ops.wm.open_mainfile(filepath=MASTER)
ob = bpy.data.objects["SumoRetopo"]
me = ob.data
mw = np.array(ob.matrix_world)
nv = len(me.vertices)
co = np.empty(nv * 3)
me.vertices.foreach_get("co", co)
co = co.reshape(nv, 3) @ mw[:3, :3].T + mw[:3, 3]
me.calc_loop_triangles()
nt = len(me.loop_triangles)
tv = np.empty(nt * 3, np.int64)
me.loop_triangles.foreach_get("vertices", tv)
tv = tv.reshape(nt, 3)
P("MESH verts=%d tris=%d" % (nv, nt))

bvh = BVHTree.FromPolygons([Vector(c) for c in co], [list(t) for t in tv], all_triangles=True)

# ---- 解剖島（沿用 sumo_anat_tint 的連通元件法：島邊界＝特徵邊界）----
ne = len(me.edges)
ev = np.empty(ne * 2, np.int64)
me.edges.foreach_get("vertices", ev)
ev = ev.reshape(ne, 2)
adj = [[] for _ in range(nv)]
for a, b in ev:
    adj[a].append(b)
    adj[b].append(a)
comp = np.full(nv, -1, np.int64)
nc = 0
for s in range(nv):
    if comp[s] >= 0:
        continue
    q = deque([s]); comp[s] = nc
    while q:
        u = q.popleft()
        for v in adj[u]:
            if comp[v] < 0:
                comp[v] = nc; q.append(v)
    nc += 1
sizes = np.bincount(comp)
islands = [i for i in range(nc) if sizes[i] <= ISLAND_MAX]
assert len(islands) == 5, "預期 5 顆解剖島，實得 %d（網格改過了）" % len(islands)
cores = sorted(islands, key=lambda i: sizes[i])
nip = cores[0]                      # 最小＝乳頭核心島
nip_c = co[comp == nip].mean(0)
P("NIPPLE island verts=%d center=(%.4f,%.4f,%.4f)" % (sizes[nip], *nip_c))

def surf(p):
    loc, nor, idx, d = bvh.find_nearest(Vector(p))
    return np.array(loc), np.array(nor)

nip_s, nip_n = surf(nip_c)
eye = nip_s + nip_n * EYE_DIST_M

# 掃描線方向＝與法線正交、取世界 Z 分量最大的正交方向（沿身體上下掃過乳頭）
up = np.array([0.0, 0.0, 1.0])
tang = up - nip_n * np.dot(up, nip_n)
tang /= np.linalg.norm(tang)

def cast_eye(target_pt):
    d = target_pt - eye
    d = d / np.linalg.norm(d)
    loc, nor, idx, dist = bvh.ray_cast(Vector(eye), Vector(d), 3.0)
    return (None if loc is None else np.array(loc))

def emit(tip, shaft):
    """複製 TipToSurfaceUV：沿筆軸 A->B 打線段；miss=無聲丟棄"""
    a = tip + shaft * SHAFT_BACK_M
    b = tip - shaft * SHAFT_FWD_M
    d = b - a
    L = np.linalg.norm(d)
    loc, nor, idx, dist = bvh.ray_cast(Vector(a), Vector(d / L), L)
    return (None if loc is None else np.array(loc))

def shaft_dir(theta_deg):
    """筆軸＝視線繞 tangent 轉 theta（ShaftOut 指向手側＝背離皮膚）"""
    v = nip_n.copy()
    t = np.radians(theta_deg)
    axis = np.cross(nip_n, tang); axis /= np.linalg.norm(axis)
    return v * np.cos(t) + np.cross(axis, v) * np.sin(t)

# ---------------- H1：零手速、單針、純幾何 ----------------
P("")
P("======== H1  單針落點偏移（零手速／零掉幀）========")
P("掃描線＝沿身體軸過乳頭中心 ±2.0cm，步進 0.5mm；數字＝|墨落點 − 準星P| (cm)")
P("")
offs = np.arange(-0.020, 0.0201, 0.0005)
P("%-8s %10s %10s %10s %8s" % ("筆軸角", "中位偏移", "p90偏移", "最大偏移", "丟棄"))
for th in [0, 10, 20, 30, 40, 50]:
    sh = shaft_dir(th)
    errs, drops = [], 0
    for o in offs:
        p = cast_eye(nip_s + tang * o)
        if p is None:
            continue
        ink = emit(p, sh)
        if ink is None:
            drops += 1
            continue
        errs.append(np.linalg.norm(ink - p) * 100.0)
    e = np.array(errs)
    P("%-8s %10.3f %10.3f %10.3f %8d" % ("%d deg" % th, np.median(e), np.percentile(e, 90), e.max(), drops))

# ---------------- H1b：被攔截的針落到哪裡（爆墨＝聚成一坨？）----------------
P("")
P("======== H1b  攔截針的聚集度（爆墨的直接證據）========")
P("同一條掃描線上，準星散開 4.0cm；若攔截針全擠進一小塊 ⇒ 那就是那一坨")
P("")
P("%-8s %8s %12s %12s %12s" % ("筆軸角", "攔截數", "準星散布cm", "落墨散布cm", "壓縮倍率"))
for th in [10, 20, 30, 40, 50]:
    sh = shaft_dir(th)
    hitP, hitI = [], []
    for o in offs:
        p_ = cast_eye(nip_s + tang * o)
        if p_ is None:
            continue
        ink = emit(p_, sh)
        if ink is None:
            continue
        if np.linalg.norm(ink - p_) > 0.002:      # >2mm＝被攔截（非同點）
            hitP.append(p_); hitI.append(ink)
    if len(hitI) < 2:
        P("%-8s %8d %12s %12s %12s" % ("%d deg" % th, len(hitI), "-", "-", "-"))
        continue
    A = np.array(hitP) * 100.0
    B = np.array(hitI) * 100.0
    sprA = np.linalg.norm(A - A.mean(0), axis=1).max() * 2.0
    sprB = np.linalg.norm(B - B.mean(0), axis=1).max() * 2.0
    P("%-8s %8d %12.3f %12.3f %12.1fx" % ("%d deg" % th, len(hitI), sprA, sprB,
                                          sprA / max(sprB, 1e-6)))

# ---------------- H1c：沿掃描線的攔截剖面 ----------------
P("")
P("======== H1c  攔截發生在哪（筆軸 30 deg）========")
sh = shaft_dir(30)
P("offset_cm  |ink-P|_cm   狀態")
for o in np.arange(-0.020, 0.0201, 0.002):
    p_ = cast_eye(nip_s + tang * o)
    if p_ is None:
        P("%+8.2f %11s   %s" % (o * 100, "-", "準星打空"))
        continue
    ink = emit(p_, sh)
    if ink is None:
        P("%+8.2f %11s   %s" % (o * 100, "-", "DROP 無聲丟棄"))
        continue
    e = np.linalg.norm(ink - p_) * 100.0
    P("%+8.2f %11.3f   %s" % (o * 100, e, "攔截" if e > 0.2 else "ok"))

# ---------------- H2：補間（兩幀之間拉世界直線）----------------
def stroke(p0, p1, sh):
    """複製稿筆 freehand 補間：Lerp(TipFrom,TipNow) 每 SpacingCm 一針"""
    L = np.linalg.norm(p1 - p0)
    req, land = 0, []
    walked = SPACING_M
    while walked <= L:
        req += 1
        tip = p0 + (p1 - p0) * (walked / L)
        ink = emit(tip, sh)
        if ink is not None:
            land.append(ink)
        walked += SPACING_M
    return req, np.array(land) if land else np.zeros((0, 3))

def gaps(land):
    if len(land) < 2:
        return 0.0, 0.0
    d = np.linalg.norm(np.diff(land, axis=0), axis=1) * 100.0
    return d.max(), d.min()

P("")
P("======== H2  補間：掃過乳頭 vs 平滑對照（筆軸 30 deg）========")
P("Delta = 兩幀之間準星跳幅；實線門檻＝墨寬 0.28cm（超過就是看得見的斷）")
P("")
sh = shaft_dir(30)
flat_s, flat_n = surf(nip_c + tang * 0.10)   # 同高度旁邊 10cm＝平滑對照
for label, base, tg in (("乳頭", nip_s, tang), ("平滑對照", flat_s, tang)):
    P("--- %s ---" % label)
    P("%8s %6s %6s %6s %9s %9s %10s" % ("Delta_cm", "請求", "落地", "丟棄", "最大間距", "最小間距", "落地率"))
    for dcm in [0.15, 0.3, 0.6, 1.0, 1.5, 2.0, 3.0, 4.5, 6.0]:
        d = dcm / 100.0
        p0 = cast_eye(base - tg * (d / 2))
        p1 = cast_eye(base + tg * (d / 2))
        if p0 is None or p1 is None:
            continue
        req, land = stroke(p0, p1, sh)
        mx, mn = gaps(land)
        P("%8.2f %6d %6d %6d %9.3f %9.3f %9.0f%%" %
          (dcm, req, len(land), req - len(land), mx, mn,
           100.0 * len(land) / max(req, 1)))
    P("")

# ---------------- H3：候選修法 A/B（筆軸射線 → 視線射線）----------------
# 現制 shaft = PenShaftDirWorld（筆軸，從手側斜入）
# 候選 shaft = 視線方向（eye→tip）：端點的首命中依定義就是準星點 P 本身
def emit_eye(tip):
    back = eye - tip
    back /= np.linalg.norm(back)
    return emit(tip, back)

def stroke_eye(p0, p1):
    L = np.linalg.norm(p1 - p0)
    req, land = 0, []
    walked = SPACING_M
    while walked <= L:
        req += 1
        ink = emit_eye(p0 + (p1 - p0) * (walked / L))
        if ink is not None:
            land.append(ink)
        walked += SPACING_M
    return req, np.array(land) if land else np.zeros((0, 3))

P("")
P("======== H3  候選修法 A/B：視線射線取代筆軸射線 ========")
P("")
P("-- 單針落點偏移（掃描線過乳頭）--")
P("%-14s %10s %10s %8s" % ("射線來源", "p90偏移cm", "最大偏移cm", "丟棄"))
for name, fn in (("筆軸 30 deg", lambda t: emit(t, shaft_dir(30))),
                 ("筆軸 50 deg", lambda t: emit(t, shaft_dir(50))),
                 ("視線（候選）", emit_eye)):
    errs, drops = [], 0
    for o in offs:
        p_ = cast_eye(nip_s + tang * o)
        if p_ is None:
            continue
        ink = fn(p_)
        if ink is None:
            drops += 1
            continue
        errs.append(np.linalg.norm(ink - p_) * 100.0)
    e = np.array(errs)
    P("%-14s %10.4f %10.4f %8d" % (name, np.percentile(e, 90), e.max(), drops))

P("")
P("-- 補間筆劃（掃過乳頭）--")
P("%8s %14s %14s %14s %14s" % ("Delta_cm", "筆軸最大間距", "視線最大間距", "筆軸落地率", "視線落地率"))
for dcm in [1.0, 2.0, 3.0, 4.5, 6.0]:
    d = dcm / 100.0
    p0 = cast_eye(nip_s - tang * (d / 2))
    p1 = cast_eye(nip_s + tang * (d / 2))
    r1, l1 = stroke(p0, p1, shaft_dir(30))
    r2, l2 = stroke_eye(p0, p1)
    m1, _ = gaps(l1)
    m2, _ = gaps(l2)
    P("%8.2f %14.3f %14.3f %13.0f%% %13.0f%%" %
      (dcm, m1, m2, 100.0 * len(l1) / max(r1, 1), 100.0 * len(l2) / max(r2, 1)))

P("")
P("-- 平滑對照（候選不可以動到這裡）--")
for dcm in [1.0, 3.0, 6.0]:
    d = dcm / 100.0
    p0 = cast_eye(flat_s - tang * (d / 2))
    p1 = cast_eye(flat_s + tang * (d / 2))
    r1, l1 = stroke(p0, p1, shaft_dir(30))
    r2, l2 = stroke_eye(p0, p1)
    m1, n1 = gaps(l1)
    m2, n2 = gaps(l2)
    P("Delta=%.1fcm  筆軸 n=%d gap[%.3f,%.3f]  視線 n=%d gap[%.3f,%.3f]" %
      (dcm, len(l1), n1, m1, len(l2), n2, m2))
