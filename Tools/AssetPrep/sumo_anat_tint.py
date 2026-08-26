# 解剖色調烘焙（乳頭／乳暈／肚臍）——寫進「色度場」通道 T_BodyChroma
#
# 為什麼是這條路：材質 M_InkBodyChar 的 BaseColor 鏈是
#     dec  = chroma.rgb × 2              （貼圖 16-bit，值=factor/2）
#     skin = SkinTone × lerp(1, dec, ChromaStrength)
#   ⇒ chroma 貼圖本來就是「皮膚 albedo 的乘法變化場」，而它自 08-22 起是
#     32×32 全等 1.0 的中和圖（追記74 塊斑症的治法）＝整條通道是空的。
#   在這張圖上畫解剖色調＝零材質改動、零 C++，強度用既有旋鈕 ChromaStrength（=0.6）。
#   （皮膚零烘焙陰影鐵律 #37 仍成立：這是 albedo 色素，不是 AO／陰影。）
#
# **位置與範圍不是手放的，是從檔案讀出來的**（08-27 user 質問「定位有問題」後改制）：
#   SumoRetopo 有 7 個連通元件，其中五顆小島就是 retopo 的「保護島」＝解剖特徵本體：
#     乳頭 ×2＝170 頂點 / 10.71 cm² / 1.9cm 見方
#     乳暈 ×2＝266 頂點 / 94.15 cm² / 6.6×5.2×6.5cm
#     肚臍  ＝2304 頂點 / 35.81 cm² / 3.8×2.0×3.8cm
#   （另兩個是主體 93098 與頭 6103。）島的邊界就是特徵的邊界 ⇒ 塗色只要
#   「島內全量、島外沿 3D 距離淡出」——不需要錨點、半徑、投影軸、法線閘。
#   舊制（sumo_bump_scan 的排除球＋柱面投影）已退役：球心是刻意埋在皮下的排除球、
#   seed 吸附又會跳到 12mm 外的尖點 ⇒ user 實測定位偏掉。
#
# **預設值＝出貨值**（2026-08-27 user 在 viewport 逐級裁決：先看極值 0.95 確認範圍，
#   再要 2/5，最後定在「極值的一半」）：暗度 0.475/0.45/0.475、淡出 1.5mm、無偏紅
#   ⇒ 引擎端（ChromaStrength 0.6）實際變暗 28.5%。改預設就是改出貨，改完要重匯。
#
# 用法：blender --background --python Tools/AssetPrep/sumo_anat_tint.py
# 旋鈕（環境變數）：ANAT_SIZE / ANAT_NIP_A / ANAT_NIP_R / ANAT_NAV_A /
#                   ANAT_FADE_MM / ANAT_CHW / ANAT_BASE
import bpy
import os
import struct
import zlib
import numpy as np
from collections import deque

SA = r"C:\games\Unreal Engine\nice_ink\SourceAssets"
MASTER = os.path.join(SA, "sumo_character_master.blend")
OUT_TINT = os.path.join(SA, "body_anat_tint.png")     # 純色調場（1.0=無變化，唯讀檢查用）
OUT_SHIP = os.path.join(SA, "body_chroma_anat.png")   # T_BodyChroma 出貨內容（值=factor/2）
BASE = os.environ.get("ANAT_BASE", "")                # 空＝中和底(1.0)；給檔名＝乘在血色場上

# 2048 的理由：解剖島在 UV0 上被切成 9~12 個小 chart（全網格 193 charts、中位數 88 tris），
# 1024 下每個 chart 只有幾紋素寬，雙線性會把邊緣紋素跟鄰居(=1.0)混掉。
SIZE = int(os.environ.get("ANAT_SIZE", "2048"))
NIP_A = float(os.environ.get("ANAT_NIP_A", "0.475"))  # 乳頭島暗度（貼圖 factor = 1-A）
NIP_R = float(os.environ.get("ANAT_NIP_R", "0.45"))   # 乳暈島暗度
NAV_A = float(os.environ.get("ANAT_NAV_A", "0.475"))  # 肚臍島暗度
FADE = float(os.environ.get("ANAT_FADE_MM", "1.5")) / 1000.0   # 島邊界往外的淡出寬度
CH_W = np.array([float(v) for v in os.environ.get("ANAT_CHW", "1.0,1.0,1.0").split(",")])
DILATE_PX = 6                                          # 只往沒有三角形覆蓋的 gutter 外擴
ISLAND_MAX = 5000                                      # 大於此＝主體/頭，不是解剖島
GROUP_R = 0.06                                         # 島分組半徑（乳頭島與乳暈島同組）


def P(*a):
    print(*a, flush=True)


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / max(e1 - e0, 1e-9), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def write_png16(path, rgb):
    h, w, _ = rgb.shape
    a = (np.clip(rgb, 0.0, 1.0) * 65535.0 + 0.5).astype(">u2").reshape(h, w * 3)
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += a[y].tobytes()

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 16, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 6))
    png += chunk(b"IEND", b"")
    open(path, "wb").write(png)


def read_png(path):
    d = open(path, "rb").read()
    pos, idat, w, h, bd, ct = 8, b"", 0, 0, 0, 0
    while pos < len(d):
        ln = struct.unpack(">I", d[pos:pos + 4])[0]
        typ = d[pos + 4:pos + 8]
        blk = d[pos + 8:pos + 8 + ln]
        if typ == b"IHDR":
            w, h, bd, ct = struct.unpack(">IIBB", blk[:10])
        elif typ == b"IDAT":
            idat += blk
        pos += 12 + ln
    raw = zlib.decompress(idat)
    nch = {0: 1, 2: 3, 4: 2, 6: 4}[ct]
    bpp = nch * (bd // 8)
    stride = w * bpp
    out = np.zeros((h, stride), np.uint8)
    prev = np.zeros(stride, np.int32)
    p = 0
    for y in range(h):
        f = raw[p]
        p += 1
        line = np.frombuffer(raw[p:p + stride], np.uint8).astype(np.int32)
        p += stride
        if f == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 255
        elif f == 2:
            line = (line + prev) & 255
        elif f == 3:
            for i in range(stride):
                a_ = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((a_ + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                a_ = line[i - bpp] if i >= bpp else 0
                c_ = prev[i - bpp] if i >= bpp else 0
                b_ = prev[i]
                pp = a_ + b_ - c_
                pa, pb, pc = abs(pp - a_), abs(pp - b_), abs(pp - c_)
                pr = a_ if (pa <= pb and pa <= pc) else (b_ if pb <= pc else c_)
                line[i] = (line[i] + pr) & 255
        out[y] = line
        prev = line
    if bd == 16:
        arr = out.reshape(h, w, nch, 2)
        arr = ((arr[..., 0].astype(np.uint32) << 8) | arr[..., 1]).astype(np.float32) / 65535.0
    else:
        arr = out.reshape(h, w, nch).astype(np.float32) / 255.0
    return arr[:, :, :3]



def closest_dist_to_tris(P, A, B, C, chunk=200):
    """點到三角形集合的最短距離（Ericson 區域法，對 P 分塊向量化）。P(n,3)、A/B/C(m,3)"""
    out = np.empty(len(P))
    for s0 in range(0, len(P), chunk):
        p = P[s0:s0 + chunk][:, None, :]
        ab = (B - A)[None]
        ac = (C - A)[None]
        ap = p - A[None]
        d1 = (ab * ap).sum(-1)
        d2 = (ac * ap).sum(-1)
        bp = p - B[None]
        d3 = (ab * bp).sum(-1)
        d4 = (ac * bp).sum(-1)
        cp = p - C[None]
        d5 = (ab * cp).sum(-1)
        d6 = (ac * cp).sum(-1)
        va = d3 * d6 - d5 * d4
        vb = d5 * d2 - d1 * d6
        vc = d1 * d4 - d3 * d2
        den = va + vb + vc
        v = np.where(den != 0, vb / np.where(den == 0, 1, den), 0.0)
        w = np.where(den != 0, vc / np.where(den == 0, 1, den), 0.0)
        q = A[None] + v[..., None] * ab + w[..., None] * ac          # 面內投影
        # 各退化區域
        m1 = (d1 <= 0) & (d2 <= 0)
        q = np.where(m1[..., None], A[None] + 0 * ab, q)
        m2 = (d3 >= 0) & (d4 <= d3)
        q = np.where(m2[..., None], B[None] + 0 * ab, q)
        m3 = (d6 >= 0) & (d5 <= d6)
        q = np.where(m3[..., None], C[None] + 0 * ab, q)
        e1 = (vc <= 0) & (d1 >= 0) & (d3 <= 0)
        t1 = np.where(d1 - d3 != 0, d1 / np.where(d1 - d3 == 0, 1, d1 - d3), 0.0)
        q = np.where((e1 & ~m1 & ~m2)[..., None], A[None] + t1[..., None] * ab, q)
        e2 = (vb <= 0) & (d2 >= 0) & (d6 <= 0)
        t2 = np.where(d2 - d6 != 0, d2 / np.where(d2 - d6 == 0, 1, d2 - d6), 0.0)
        q = np.where((e2 & ~m1 & ~m3)[..., None], A[None] + t2[..., None] * ac, q)
        e3 = (va <= 0) & (d4 - d3 >= 0) & (d5 - d6 >= 0)
        dd = (d4 - d3) + (d5 - d6)
        t3 = np.where(dd != 0, (d4 - d3) / np.where(dd == 0, 1, dd), 0.0)
        q = np.where((e3 & ~m2 & ~m3)[..., None], B[None] + t3[..., None] * (C - B)[None], q)
        out[s0:s0 + chunk] = np.linalg.norm(p - q, axis=-1).min(axis=1)
    return out


def raster(ti_list, tuv, op):
    """逐三角形光柵化；每個被覆蓋的紋素把重心座標交給 op(ti, bary, ys, xs)"""
    for ti in ti_list:
        uv = tuv[ti]
        dst = np.stack([uv[:, 0] * SIZE, (1.0 - uv[:, 1]) * SIZE], axis=1)
        x0 = max(int(np.floor(dst[:, 0].min())) - 1, 0)
        y0 = max(int(np.floor(dst[:, 1].min())) - 1, 0)
        x1 = min(int(np.ceil(dst[:, 0].max())) + 2, SIZE)
        y1 = min(int(np.ceil(dst[:, 1].max())) + 2, SIZE)
        if x1 <= x0 or y1 <= y0:
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1) + 0.5, np.arange(y0, y1) + 0.5)
        d0 = dst[1] - dst[0]
        d1 = dst[2] - dst[0]
        den = d0[0] * d1[1] - d1[0] * d0[1]
        if abs(den) < 1e-12:
            continue
        px = gx - dst[0, 0]
        py = gy - dst[0, 1]
        b1 = (px * d1[1] - d1[0] * py) / den
        b2 = (d0[0] * py - px * d0[1]) / den
        b0 = 1.0 - b1 - b2
        m = (b0 > -0.08) & (b1 > -0.08) & (b2 > -0.08)
        if not m.any():
            # 保守補位：解剖島的 chart 只有 2~3 紋素寬，次紋素三角形常常一個紋素中心
            # 都不含 ⇒ 整個 chart 空著＝渲染時取到 1.0。退而求其次寫重心那一格。
            cx = int(np.clip(dst[:, 0].mean(), 0, SIZE - 1))
            cy = int(np.clip(dst[:, 1].mean(), 0, SIZE - 1))
            op(ti, np.full((1, 3), 1.0 / 3.0), np.array([cy]), np.array([cx]))
            continue
        bb = np.clip(np.stack([b0[m], b1[m], b2[m]], axis=1), 0.0, 1.0)
        bb /= bb.sum(axis=1, keepdims=True)
        op(ti, bb, gy[m].astype(int), gx[m].astype(int))


# ---------------- 幾何 ----------------
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
tl = np.empty(nt * 3, np.int64)
me.loop_triangles.foreach_get("loops", tl)
tl = tl.reshape(nt, 3)
uvl = me.uv_layers[0]
assert uvl.name == "UVMap", f"UV0 應為 UVMap，實為 {uvl.name}"
uva = np.empty(len(me.loops) * 2)
uvl.data.foreach_get("uv", uva)
uva = uva.reshape(-1, 2)
tuv = uva[tl]
tri_area = 0.5 * np.linalg.norm(np.cross(co[tv][:, 1] - co[tv][:, 0],
                                         co[tv][:, 2] - co[tv][:, 0]), axis=1)
P(f"MESH verts={nv} tris={nt} SIZE={SIZE}")

# ---------------- 連通元件 → 解剖島 ----------------
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
    q = deque([s])
    comp[s] = nc
    while q:
        u = q.popleft()
        for v in adj[u]:
            if comp[v] < 0:
                comp[v] = nc
                q.append(v)
    nc += 1
sizes = np.bincount(comp)
islands = [i for i in range(nc) if sizes[i] <= ISLAND_MAX]
assert len(islands) == 5, (f"預期 5 顆解剖島，實得 {len(islands)}"
                           f"（sizes={np.sort(sizes)[::-1][:8].tolist()}）——網格改過了")
info = {}
for i in islands:
    m = comp == i
    tm = m[tv].all(axis=1)
    info[i] = dict(n=int(sizes[i]), c=co[m].mean(0), area=float(tri_area[tm].sum()) * 1e4,
                   verts=np.nonzero(m)[0])
    c = info[i]["c"]
    P(f"ISLAND {i}: verts={info[i]['n']:5d} 面積={info[i]['area']:7.2f} cm² "
      f"中心=({c[0]:+.4f},{c[1]:+.4f},{c[2]:+.4f})")

# 分組：距離 <GROUP_R 的島同組；組內最小者＝核心（乳頭），最大者＝外圈（乳暈）
groups = []
used = set()
for i in islands:
    if i in used:
        continue
    g = [i]
    used.add(i)
    for j in islands:
        if j not in used and np.linalg.norm(info[j]["c"] - info[i]["c"]) < GROUP_R:
            g.append(j)
            used.add(j)
    groups.append(sorted(g, key=lambda k: info[k]["n"]))
assert len(groups) == 3, f"預期 3 組（左乳/右乳/肚臍），實得 {len(groups)}"

FEATURES = []
for g in groups:
    core = g[0]
    ring = g[-1] if len(g) > 1 else None
    if ring is None:
        name, Ac, Ar = "navel", NAV_A, NAV_A
    else:
        name = "nipple_L" if info[core]["c"][0] > 0 else "nipple_R"
        Ac, Ar = NIP_A, NIP_R
    FEATURES.append((name, core, ring, Ac, Ar))
    P(f"GROUP {name}: core=島{core}({info[core]['n']}v) "
      f"ring={'島%d(%dv)' % (ring, info[ring]['n']) if ring is not None else '—'} "
      f"暗度 core={Ac:.2f} ring={Ar:.2f}")

# ---------------- 逐紋素上色：暗度先算在頂點上，再重心內插 ----------------
# 為什麼算在頂點上：乳暈島 266 頂點鋪 94 cm²（間距 ~2cm），若逐紋素取「到最近島頂點
# 的距離」，島內部也會被 6mm 淡出帶咬到 ⇒ 邊緣鋸成星芒、核心變方塊（08-27 實拍）。
# 頂點層用**點到島三角形**的真距離（島內＝0），再線性內插＝平滑且島內恆為全量。
dark = np.zeros((SIZE, SIZE), np.float32)
for name, core, ring, Ac, Ar in FEATURES:
    lobes = [(core, Ac)] + ([(ring, Ar)] if ring is not None else [])
    allv = np.concatenate([info[k]["verts"] for k, _ in lobes])
    ctr = co[allv].mean(0)
    rad = float(np.linalg.norm(co[allv] - ctr, axis=1).max())
    near = np.nonzero(np.linalg.norm(co - ctr, axis=1) < rad + FADE + 0.02)[0]
    dv = np.zeros(nv, np.float64)
    for k, amp in lobes:
        vmask = np.zeros(nv, bool)
        vmask[info[k]["verts"]] = True
        itris = tv[vmask[tv].all(axis=1)]
        d = closest_dist_to_tris(co[near], co[itris[:, 0]], co[itris[:, 1]], co[itris[:, 2]])
        dv[near] = np.maximum(dv[near], amp * (1.0 - smoothstep(0.0, FADE, d)))
    sel = np.nonzero((dv[tv] > 0.005).any(axis=1))[0]
    assert sel.size > 50, f"{name} 只選到 {sel.size} 個三角形"
    before = int((dark > 0.01).sum())
    far = [0.0]

    def op(ti, bb, ys, xs, _dv=dv, _ctr=ctr):
        val = bb @ _dv[tv[ti]]
        hit = val > 0.02
        if hit.any():
            wp = (bb @ co[tv[ti]])[hit]
            far[0] = max(far[0], float(np.linalg.norm(wp - _ctr, axis=1).max()))
        np.maximum.at(dark, (ys, xs), val.astype(np.float32))

    raster(sel, tuv, op)
    added = int((dark > 0.01).sum()) - before
    # 上限＝島半徑＋淡出寬＋一個三角形邊長（頂點場線性內插會在大三角形上多帶一格）
    emax = float(np.linalg.norm(co[tv[sel]][:, 1] - co[tv[sel]][:, 0], axis=1).max())
    lim = rad + FADE + emax
    P(f"   {name}: tris={sel.size} 上色紋素+{added} 島半徑={rad*1000:.1f}mm "
      f"最遠上色點 {far[0]*1000:.1f}mm（上限 {lim*1000:.1f}mm＝島{rad*1000:.0f}+淡出{FADE*1000:.0f}+邊長{emax*1000:.0f}）")
    assert far[0] <= lim, f"{name} 有顏色落在 {far[0]*1000:.0f}mm 外——超出島範圍"

# ---------------- 全網格覆蓋圖（外擴只准往 gutter 走）----------------
cover = np.zeros((SIZE, SIZE), bool)


def op_cov(ti, bb, ys, xs):
    cover[ys, xs] = True


raster(range(nt), tuv, op_cov)
P(f"COVER {cover.sum()} / {SIZE*SIZE} texels ({100.0*cover.mean():.1f}%)")

src = dark.copy()
grew = 0
for _ in range(DILATE_PX):
    nb = np.zeros_like(src)
    for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        nb = np.maximum(nb, np.roll(np.roll(src, dy, 0), dx, 1))
    fill = (src < 1e-6) & (nb > 1e-6) & (~cover)
    grew += int(fill.sum())
    src = np.where(fill, nb, src)
dark = src
P(f"DILATE(gutter only) +{grew} texels")
assert dark.max() > 0.9 * max(NIP_A, NAV_A), f"最深只有 {dark.max():.3f}，核心沒被畫到"

# ---------------- 編碼 ----------------
tint = np.clip(1.0 - dark[:, :, None] * CH_W[None, None, :], 0.02, 1.0)
write_png16(OUT_TINT, tint)
if BASE:
    path = BASE if os.path.isabs(BASE) else os.path.join(SA, BASE)
    base = read_png(path) * 2.0
    assert base.shape[0] == SIZE, f"底圖尺寸 {base.shape[0]} != {SIZE}"
    P(f"BASE {path}: mean={base.mean():.3f}")
else:
    base = np.ones((SIZE, SIZE, 3), np.float32)
write_png16(OUT_SHIP, np.clip(base * tint / 2.0, 0.0, 1.0))
P(f"TINT min={tint.reshape(-1, 3).min(0).round(4)} mean={tint.reshape(-1, 3).mean(0).round(5)}")
P(f"WROTE {OUT_TINT}")
P(f"WROTE {OUT_SHIP}")
P("ANAT_TINT_DONE")
