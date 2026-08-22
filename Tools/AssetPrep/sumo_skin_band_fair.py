"""皮膚帶路徑勒痕整形（2026-08-23，user 定案「不是推平，是大腿該有的圓滑飽滿」）

定罪（lump 圖 R=3cm）：大腿面本身乾淨，唯獨沿褌帶路徑整圈有「藍溝＋紅脊」（寬 ~3cm、
1.5~3mm）＝掃描本人的內褲勒痕；所有 σ6/σ15 整平都碰不到這個尺度，接觸線把它原樣描出來。

修法＝區域雙調和整形（thin-plate fairing）：帶路徑兩側 BAND_HALF 內為自由區、區外固定
（兩圈＝C1），求 ||L x||² 最小＝外圍形狀自然延伸進來的單調凸面。位移只取法線分量（UV/
拓樸零變動）、cap；解剖保護區（會陰/臀縫）排除。之後對動過的頂點重算 custom normals。

**出貨參數（2026-08-23，v37 皮膚＝現行 SK_Sumo 的底）**：
  BAND_HALF=0.12 FAIR_CAP=0.035 FAIR_MU=1e9 FAIR_R0=0.03 FAIR_R1=0.10（ARC_WIDEN_MM 未設＝0）
  ——出貨版**沒有**後來加的 ①單點去尖刺 ②位移場空間平滑 σ12（那兩刀把大腿上段的
  解震盪 5.1mm→1.2mm，重跑會比出貨版乾淨；逐位重現出貨版＝用 masters/v37_softfair）。
**鐵則**：布若加寬（fundoshi_arc 的 ARC_WIDEN_MM），本腳本必須用同一個值重跑——
腳印與布不同源＝接觸線落在只填了一半的衰減帶上＝「凹凸又回來了」（08-23 血價）。

三段：blender dump → venv(scipy) solve → blender apply。
Run: blender --background --python sumo_skin_band_fair.py -- dump
     venv python sumo_skin_band_fair.py solve
     blender --background --python sumo_skin_band_fair.py -- apply
"""
import sys, os, numpy as np
ROOT = r"C:\games\Unreal Engine\nice_ink"
MASTER = os.path.join(ROOT, "SourceAssets", "sumo_character_master.blend")
WORK = os.path.join(ROOT, "Saved", "FundoshiPlate", "skin_fair.npz")
SOLVED = os.path.join(ROOT, "Saved", "FundoshiPlate", "skin_fair_solved.npz")
LINES = os.path.join(ROOT, "Saved", "FundoshiPlate", "edge_lines.npz")
BAND_HALF = float(os.environ.get("BAND_HALF", "0.06"))   # 自由區半寬（m）
CAP = float(os.environ.get("FAIR_CAP", "0.006"))          # 位移上限
ZMAX = 1.02

mode = sys.argv[-1]

if mode == "dump":
    import bpy
    from mathutils import Vector
    from mathutils.kdtree import KDTree
    bpy.ops.wm.open_mainfile(filepath=MASTER)
    if bpy.context.object and bpy.context.object.mode != 'OBJECT': bpy.ops.object.mode_set(mode='OBJECT')
    me = bpy.data.objects["SumoRetopo"].data
    co = np.empty(len(me.vertices) * 3); me.vertices.foreach_get("co", co); co = co.reshape(-1, 3)
    nrm = np.empty(len(me.vertices) * 3); me.vertices.foreach_get("normal", nrm); nrm = nrm.reshape(-1, 3)
    edges = np.empty(len(me.edges) * 2, np.int64); me.edges.foreach_get("vertices", edges); edges = edges.reshape(-1, 2)
    D = np.load(LINES); path = np.vstack([D[k] for k in D if k.startswith("foot")])
    kd = KDTree(len(path))
    for i, p in enumerate(path): kd.insert(Vector(p), i)
    kd.balance()
    dband = np.array([kd.find(Vector(p))[2] for p in co])
    free = (dband < BAND_HALF) & (co[:, 2] < ZMAX)
    # 解剖保護：會陰帶（兩腿之間）＋臀縫
    protect = ((np.abs(co[:, 0]) < 0.10) & (co[:, 2] < 0.70)) | ((np.abs(co[:, 0]) < 0.05) & (co[:, 1] > 0.05))
    free &= ~protect
    print(f"free verts {free.sum()} (band<{BAND_HALF*100:.0f}cm, protect removed {int((protect & (dband < BAND_HALF)).sum())})")
    # 布腳印：手繪遮罩（UV0）σ15mm 模糊場 >0.5（與 fundoshi_plate 的裁切場同源）→ 每頂點
    MASK = os.environ.get("FUNDOSHI_MASK", os.path.join(ROOT, "SourceAssets", "fundoshi_mask_sharp.png"))
    img = bpy.data.images.load(MASK, check_existing=True); W, H = img.size
    px = np.empty(W * H * 4, np.float32); img.pixels.foreach_get(px); mask = px.reshape(H, W, 4)[:, :, 0]
    # 腳印跟布同寬（08-23 血價：布加寬後接觸線跑進衰減帶＝殘餘起伏被描出來）：同 fundoshi_arc 的 ARC_WIDEN_MM 膨脹
    WIDEN_MM = float(os.environ.get("ARC_WIDEN_MM", "0"))
    if WIDEN_MM > 0:
        _r = int(round(WIDEN_MM * 0.617 * W / 4096.0)); _d = mask.copy()
        for dy in range(-_r, _r + 1):
            for dx in range(-_r, _r + 1):
                if dx * dx + dy * dy <= _r * _r: _d = np.maximum(_d, np.roll(np.roll(mask, dy, 0), dx, 1))
        mask = _d; print(f"footprint widened {WIDEN_MM}mm/side ({_r}px)")
    PXMM = 0.617 * W / 4096.0; sig_px = 15.0 * PXMM
    fm = np.fft.rfft2(mask); fy = np.fft.fftfreq(H)[:, None]; fx = np.fft.rfftfreq(W)[None, :]
    mask_b = np.fft.irfft2(fm * np.exp(-2 * np.pi ** 2 * sig_px ** 2 * (fx ** 2 + fy ** 2)), s=(H, W))
    uvl = me.uv_layers["UVMap"].data; luv = np.empty(len(uvl) * 2); uvl.foreach_get("uv", luv); luv = luv.reshape(-1, 2)
    lvi = np.empty(len(me.loops), np.int64); me.loops.foreach_get("vertex_index", lvi)
    u = np.clip((np.mod(luv[:, 0], 1.0) * (W - 1)).astype(int), 0, W - 1); v_ = np.clip((np.mod(luv[:, 1], 1.0) * (H - 1)).astype(int), 0, H - 1)
    ms = np.zeros(len(co)); mc = np.zeros(len(co)); np.add.at(ms, lvi, mask_b[v_, u]); np.add.at(mc, lvi, 1)
    vmask = ms / np.maximum(mc, 1)
    if os.environ.get("ARC_SYM", "1") == "1":      # 與 fundoshi_arc 同源：鏡射平均＝腳印左右對稱
        _kdb = KDTree(len(co))
        for _i, _p in enumerate(co): _kdb.insert(Vector(_p), _i)
        _kdb.balance()
        _mir = np.array([_kdb.find(Vector((-p[0], p[1], p[2])))[1] for p in co])
        vmask = 0.5 * (vmask + vmask[_mir]); print("footprint symmetrized")
    tris = []
    for pg in me.polygons:
        vs = list(pg.vertices)
        for i in range(1, len(vs) - 1): tris.append((vs[0], vs[i], vs[i + 1]))
    # 腳印小島剔除（與 fundoshi_arc MIN_ISLAND 同規則）：手繪雜點/鏡射平均殘渣不得成為整形腳印
    foot0 = vmask > 0.5
    import collections
    adjl = collections.defaultdict(list)
    for a, b in edges: adjl[a].append(b); adjl[b].append(a)
    seen = np.zeros(len(co), bool); removed = 0
    for s0 in np.where(foot0)[0]:
        if seen[s0]: continue
        comp = [s0]; seen[s0] = True; q = collections.deque([s0])
        while q:
            x = q.popleft()
            for y in adjl[x]:
                if foot0[y] and not seen[y]: seen[y] = True; q.append(y); comp.append(y)
        if len(comp) < 800: vmask[comp] = 0.0; removed += 1
    print(f"footprint small islands removed: {removed}")
    np.savez(WORK, co=co, nrm=nrm, edges=edges, free=free, dband=dband, tris=np.array(tris, np.int64), vmask=vmask)
    print("DUMPED")

elif mode == "solve":
    # 軟衰減雙調和（08-23 user 抓「硬邊界壓痕＋肚下淺凹」）：
    # 能量 = x^T K x + Σ area_i·μ(d_i)·|x_i − x0_i|²，K = Lc M^-1 Lc（cotan 雙調和）。
    # μ(d)：d<R0 為 0（自由）、R0→R1 平滑上升到 μmax、d>R1 釘死（不在未知數內）＝沒有任何硬邊界。
    import scipy.sparse as sp, scipy.sparse.linalg as spl
    from scipy.spatial import cKDTree
    Z = np.load(WORK); co = Z["co"]; nrm = Z["nrm"]; tris = Z["tris"]; vmask = Z["vmask"]; free0 = Z["free"]
    R0 = float(os.environ.get("FAIR_R0", "0.015")); R1 = float(os.environ.get("FAIR_R1", "0.09"))
    MU = float(os.environ.get("FAIR_MU", "1e6"))
    n = len(co)
    foot = (vmask > 0.5) & (co[:, 2] < ZMAX)
    dfoot = cKDTree(co[foot]).query(co)[0]; dfoot[foot] = 0.0
    protect = ((np.abs(co[:, 0]) < 0.10) & (co[:, 2] < 0.70)) | ((np.abs(co[:, 0]) < 0.05) & (co[:, 1] > 0.05))
    free = (dfoot < R1) & (co[:, 2] < ZMAX) & ~protect
    I = []; J = []; V = []
    for k in range(3):
        i = tris[:, k]; j = tris[:, (k + 1) % 3]; o = tris[:, (k + 2) % 3]
        e1 = co[i] - co[o]; e2 = co[j] - co[o]
        cot = np.einsum('ij,ij->i', e1, e2) / np.maximum(np.linalg.norm(np.cross(e1, e2), axis=1), 1e-12)
        cot = np.clip(cot, -5, 5) * 0.5
        I += [i, j]; J += [j, i]; V += [cot, cot]
    I = np.concatenate(I); J = np.concatenate(J); V = np.concatenate(V)
    Wm = sp.coo_matrix((V, (I, J)), shape=(n, n)).tocsr()
    Lc = sp.diags(np.asarray(Wm.sum(1)).ravel()) - Wm
    fa = np.linalg.norm(np.cross(co[tris[:, 1]] - co[tris[:, 0]], co[tris[:, 2]] - co[tris[:, 0]]), axis=1) * 0.5
    area = np.zeros(n); np.add.at(area, tris.ravel(), np.repeat(fa / 3.0, 3)); area = np.maximum(area, 1e-10)
    K = (Lc @ sp.diags(1.0 / area) @ Lc).tocsr()
    s_ = np.clip((dfoot - R0) / (R1 - R0), 0, 1); ramp = s_ * s_ * (3 - 2 * s_)          # smoothstep
    mu = MU * ramp ** 2 * area                                                              # 面積加權＝網格密度無關
    F = np.where(free)[0]; X = np.where(~free)[0]
    A = (K[F][:, F] + sp.diags(mu[F])).tocsc()
    rhs = -(K[F][:, X] @ co[X]) + mu[F, None] * co[F]
    sol = spl.spsolve(A, rhs)
    new = co.copy(); new[F] = sol
    d = new - co
    # 單點尖刺去除（退化頂點的雙調和爆衝：|d_v − mean(d_nb)| > 5mm → 以鄰域均值取代，兩輪）
    edges_ = Z["edges"]; nsp = 0
    SPK = float(os.environ.get("FAIR_SPIKE", "0.0025"))
    for _r in range(4):
        acc = np.zeros_like(d); cnt = np.zeros(n)
        np.add.at(acc, edges_[:, 0], d[edges_[:, 1]]); np.add.at(acc, edges_[:, 1], d[edges_[:, 0]])
        np.add.at(cnt, edges_[:, 0], 1); np.add.at(cnt, edges_[:, 1], 1)
        nm = acc / np.maximum(cnt, 1)[:, None]
        spk = np.linalg.norm(d - nm, axis=1) > SPK      # 變數名不可用 sp（scipy.sparse 別名）
        nsp += int(spk.sum()); d[spk] = nm[spk]
    print(f"spikes replaced: {nsp}")
    # 位移場空間平滑（08-23 血價：雙調和在腳印邊界的多點震盪＝大腿上段 5mm 新凹陷；
    # 單點去尖刺抓不到多點簇）。σ8mm 高斯（面積加權）×2＝mm 級震盪消滅、cm 級填充保留。
    SM = float(os.environ.get("FAIR_SMOOTH", "0.008"))
    if SM > 0:
        tree_ = cKDTree(co)
        for _r in range(2):
            d2 = d.copy()
            for v in F:
                nb = tree_.query_ball_point(co[v], 2.5 * SM)
                w = np.exp(-0.5 * (np.linalg.norm(co[nb] - co[v], axis=1) / SM) ** 2) * area[nb]
                if w.sum() > 1e-12: d2[v] = (w[:, None] * d[nb]).sum(0) / w.sum()
            d = d2
        print(f"displacement field smoothed sigma={SM*1000:.0f}mm x2")
    dn = np.einsum('ij,ij->i', d, nrm)
    mag = np.linalg.norm(d, axis=1); scale = np.minimum(1.0, CAP / np.maximum(mag, 1e-12))
    cur = co + d * scale[:, None]
    # 小尺度 Taubin（cotan 權重、只動 free）：多頂點小簇殘渣收掉
    Wn = sp.csr_matrix(Wm.multiply(1.0 / np.maximum(np.asarray(Wm.sum(1)), 1e-12)))
    for _it in range(int(os.environ.get("FAIR_POST", "6"))):
        for lam in (0.4, -0.42):
            dl = Wn @ cur - cur
            cur[F] = cur[F] + lam * dl[F]
    # 位移隨距離的衰減剖面（驗：平滑歸零、無硬邊）
    for lo, hi in ((0, 0.015), (0.015, 0.03), (0.03, 0.045), (0.045, 0.06), (0.06, 0.075), (0.075, 0.09)):
        m_ = free & (dfoot >= lo) & (dfoot < hi)
        if m_.any(): print(f"  d {lo*100:.1f}~{hi*100:.1f}cm: |disp| p50 {np.percentile(mag[m_],50)*1000:.2f} p90 {np.percentile(mag[m_],90)*1000:.2f} max {mag[m_].max()*1000:.2f} mm")
    m = np.abs(dn[F]) * 1000
    print(f"soft-biharmonic: foot {foot.sum()} free {len(F)}; |dn| p50 {np.percentile(m,50):.2f} p90 {np.percentile(m,90):.2f} max {m.max():.2f} mm; outward frac {(dn[F] > 0).mean():.2f}")
    np.savez(SOLVED, co=cur, moved=free)
    print("SOLVED")

elif mode == "apply":
    import bpy
    bpy.ops.wm.open_mainfile(filepath=MASTER)
    if bpy.context.object and bpy.context.object.mode != 'OBJECT': bpy.ops.object.mode_set(mode='OBJECT')
    ob = bpy.data.objects["SumoRetopo"]; me = ob.data
    Z = np.load(SOLVED); co = Z["co"]; moved = Z["moved"]
    assert len(co) == len(me.vertices)
    old = np.empty(len(me.vertices) * 3); me.vertices.foreach_get("co", old)
    me.vertices.foreach_set("co", co.ravel()); me.update()
    # custom normals：動過的頂點（含 2 圈）改用新幾何的平滑頂點法線，其餘保留
    import bmesh
    ring = moved.copy()
    edges = np.empty(len(me.edges) * 2, np.int64); me.edges.foreach_get("vertices", edges); edges = edges.reshape(-1, 2)
    for _ in range(2):
        nxt = ring.copy(); nxt[edges[:, 0]] |= ring[edges[:, 1]]; nxt[edges[:, 1]] |= ring[edges[:, 0]]; ring = nxt
    old_ln = np.empty(len(me.loops) * 3); me.corner_normals.foreach_get("vector", old_ln); old_ln = old_ln.reshape(-1, 3)
    vn = np.empty(len(me.vertices) * 3); me.vertices.foreach_get("normal", vn); vn = vn.reshape(-1, 3)
    lvi = np.empty(len(me.loops), np.int64); me.loops.foreach_get("vertex_index", lvi)
    new_ln = np.where(ring[lvi][:, None], vn[lvi], old_ln)
    me.normals_split_custom_set([tuple(x) for x in new_ln])
    d = np.linalg.norm((co - old.reshape(-1, 3)), axis=1) * 1000
    print(f"applied: moved {(d > 0.01).sum()} verts max {d.max():.2f} mm; normals refreshed on {ring.sum()} verts")
    bpy.ops.wm.save_mainfile(filepath=MASTER); print("SAVED")
