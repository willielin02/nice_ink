# 皮膚邊帶細分 v2（2026-08-21）——v1 的 CC 極限面投影在凸面天生縮 1~2mm＝沿布邊
# 挖出淺溝（大腿內側凹槽實錘 p50 -0.4/min -2.0mm）。v2＝投影退役、改 **Taubin 零收縮**
# （不縮形＝不挖溝；user 指示低通再調強＝120 對清殘噪）。範圍不變＝只動布邊窄帶。
# 皮膚邊帶細分（2026-08-21）——褌邊全戰役的基底修：
# 「布接觸皮膚處的可見線（牆交線/肉縫皺摺線）繼承皮膚 3cm 面片噪聲」＝布側五刀
# 都治不到的病根（user 點破：凹凸是皮膚本身、布又不可能不貼皮）。
# 修＝只在布邊界 ±20mm 的皮膚窄帶：細分 4:1（邊長 3cm→7.5mm、面片矢高 1/16）
# ＋投影到原網格的 Catmull-Clark 極限面（光滑），帶緣 10mm 平滑混回、其餘一頂點不動。
# 代價（誠實記帳）：body tris 23,674→~35k（tri-cache 線性掃描 1.5×）；帶內皮膚
# 形狀移 ≤~3mm（大半蓋在布下）；UV0/FaceUV/HairUV/FaceMask/權重全由 bmesh 內插保留。
# Run: blender --background --python sumo_edge_band_refine.py（之後必重跑 fundoshi_plate.py）
import bpy, bmesh, os, shutil
import numpy as np
from collections import defaultdict
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from mathutils.kdtree import KDTree

ROOT = r"C:\games\Unreal Engine\nice_ink"
MASTER = os.path.join(ROOT, "SourceAssets", "sumo_character_master.blend")
BK = os.path.join(ROOT, "SourceAssets", "masters", "sumo_character_master_v23_prebandrefine.blend")

BAND_D = 0.020        # 細分帶：離布邊界線 20mm 內的面
CUTS = 3              # 每邊切 3 刀＝4:1
PROJ_FULL = 0.012     # 投影權重=1 的距離
PROJ_ZERO = 0.022     # 投影權重=0 的距離（10mm 過渡）

def P(*a): print(*a, flush=True)

bpy.ops.wm.open_mainfile(filepath=MASTER)
if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')
if not os.path.exists(BK):
    shutil.copy2(MASTER, BK); P("backup ->", BK)

body = bpy.data.objects["SumoRetopo"]
fund = bpy.data.objects["Fundoshi"]
# v2：先把身體退回 v23 乾淨基底（退掉 v1 的帶狀細分＋溝）
if len(body.data.vertices) != 11931:
    with bpy.data.libraries.load(BK, link=False) as (_df, _dt):
        _dt.objects = ["SumoRetopo"]
    _donor = _dt.objects[0]
    _old = body.data
    body.data = _donor.data.copy()
    bpy.data.objects.remove(_donor, do_unlink=True)
    bpy.data.meshes.remove(_old)
    print(f"reset to v23 base: verts={len(body.data.vertices)}", flush=True)
    assert len(body.data.vertices) == 11931
me = body.data
n_tris0 = sum(len(p.vertices) - 2 for p in me.polygons)
P(f"body faces={len(me.polygons)} tris={n_tris0}")

# ---- 邊界導引線＝現行 Fundoshi 的頂緣環（牆腳/皺摺線都在其 ±2cm 內）----
fco = np.array([v.co[:] for v in fund.data.vertices])
ftris = [p for p in fund.data.polygons if len(p.vertices) == 3]
ntop = len(ftris) // 2
topverts = set()
for p in ftris[:ntop]: topverts.update(p.vertices)
fquads = [p for p in fund.data.polygons if len(p.vertices) == 4]
ring_verts = set()
for q in fquads:
    for v in q.vertices:
        if v in topverts: ring_verts.add(v)
guide = fco[sorted(ring_verts)]
P(f"guide pts={len(guide)}")
kd = KDTree(len(guide))
for i, p in enumerate(guide): kd.insert(Vector(p), i)
kd.balance()

# ---- v3 帶阻濾波（2026-08-21 user 定罪「布邊波浪=貼邊皮膚波浪」實錘：頂緣 2~8cm 帶振幅
# p50 4.18mm ≒ 貼邊皮膚 4.10mm）：對「布蓋住＋邊緣 2.2cm」的粗籠頂點削 2~8cm 波長帶
# （σ10mm 場 − σ40mm 場、原樣扣除）；>8cm 設計形與 <2cm（後續 Taubin 殺）不動。
# 同一顆平滑籠另存 cage_smooth（布的 U 從它長）＝布皮同源同平。----
CAGE_SMOOTH = os.path.join(ROOT, "SourceAssets", "masters", "cage_smooth_v3.blend")
fkd = KDTree(len(fund.data.vertices))
for _v in fund.data.vertices:
    fkd.insert(_v.co, _v.index)
fkd.balance()
cage_co0 = np.array([v.co[:] for v in me.vertices])
cloth_d = np.array([fkd.find(Vector(c))[2] for c in cage_co0])
region = cloth_d < 0.028
w_bs = np.clip((0.028 - cloth_d) / 0.008, 0.0, 1.0)
w_bs = w_bs * w_bs * (3 - 2 * w_bs)
# v3.1：σ10−σ40 帶通在曲面上帶曲率偏置（~2-4mm 假信號＝把表面整體內推）——退役。
# 正解＝粗籠直接強 Taubin（零收縮＝無偏；2~8cm 波在 3cm 籠上近 Nyquist＝天生強衰減、
# >12cm 設計形保留）。同面判準保留（臀縫兩面不互抹）。
def cage_vnormals(mesh):
    vn = np.zeros((len(mesh.vertices), 3))
    for poly in mesh.polygons:
        n_ = np.array(poly.normal)
        for vi_ in poly.vertices:
            vn[vi_] += n_
    l_ = np.linalg.norm(vn, axis=1); l_[l_ == 0] = 1
    return vn / l_[:, None]
cage_n0 = cage_vnormals(me)
adj0 = defaultdict(set)
for e in me.edges:
    a0, b0 = e.vertices
    if np.dot(cage_n0[a0], cage_n0[b0]) > 0.3:   # 同面才相鄰（臀縫兩壁不互抹）
        adj0[a0].add(b0); adj0[b0].add(a0)
reg_idx = np.where(region)[0]
nbr0 = {int(i): np.array(sorted(adj0[int(i)]), dtype=np.int64) for i in reg_idx}
x0 = cage_co0.copy()
for _it in range(60):
    for lam_ in (0.5, -0.53):
        dx = np.zeros_like(x0)
        for i in reg_idx:
            nb = nbr0[int(i)]
            if len(nb): dx[i] = x0[nb].mean(0) - x0[i]
        x0 = x0 + lam_ * (w_bs[:, None] * dx)
# 韁繩：窄條（襠帶籠 2~3 顆寬）上 Taubin 會沿條滑移＝33mm 外溢實錘
# → 同面鄰居 <4 顆的頂點不動；位移上限 5mm（超出等比縮回）
for i in reg_idx:
    if len(nbr0[int(i)]) < 4:
        x0[i] = cage_co0[i]
    else:
        dv = x0[i] - cage_co0[i]
        m_ = np.linalg.norm(dv)
        if m_ > 0.005:
            x0[i] = cage_co0[i] + dv * (0.005 / m_)
d_band = cage_co0[reg_idx] - x0[reg_idx]   # 報告用
for _i in reg_idx:
    me.vertices[int(_i)].co = Vector(x0[int(_i)])
me.update()
me.update()
amp = np.linalg.norm(d_band, axis=1) * 1000
P(f"bandstop 2~8cm: region verts={len(reg_idx)} 削除振幅 p50 {np.percentile(amp,50):.2f} p90 {np.percentile(amp,90):.2f} max {amp.max():.2f} mm")
# 平滑籠另存（粗、未細分＝布的 cage）
import bpy as _bpy
cage_copy = body.copy(); cage_copy.data = me.copy(); cage_copy.name = "CageSmooth"   # 唯一名（同場景撞名會被改成 .001＝下游載入 None 實錘）
_bpy.data.libraries.write(CAGE_SMOOTH, {cage_copy}, fake_user=True)
_bpy.data.objects.remove(cage_copy, do_unlink=True)
P(f"cage_smooth saved -> {CAGE_SMOOTH}")

# ---- 原網格 CC 極限面（光滑目標）----
proxy = body.copy(); proxy.data = body.data.copy(); proxy.name = "CCProxy"
bpy.context.collection.objects.link(proxy)
for m in list(proxy.modifiers): proxy.modifiers.remove(m)
proxy.parent = None
mod = proxy.modifiers.new("ss", 'SUBSURF'); mod.levels = 3; mod.render_levels = 3
dg = bpy.context.evaluated_depsgraph_get()
cc_bvh = BVHTree.FromObject(proxy, dg)

# ---- 細分帶 ----
bm = bmesh.new()
bm.from_mesh(me)
bm.verts.ensure_lookup_table(); bm.faces.ensure_lookup_table()
def dist_to_guide(co):
    return kd.find(Vector(co))[2]
band_faces = [f for f in bm.faces if min(dist_to_guide(v.co) for v in f.verts) < BAND_D]
band_edges = set()
for f in band_faces: band_edges.update(f.edges)
P(f"band faces={len(band_faces)} edges={len(band_edges)}")
bmesh.ops.subdivide_edges(bm, edges=list(band_edges), cuts=CUTS, use_grid_fill=True, use_only_quads=False)
bm.verts.ensure_lookup_table()
P(f"after subdivide: verts={len(bm.verts)} faces={len(bm.faces)}")

# ---- Taubin 零收縮平滑（v2；投影退役——CC 極限面在凸面縮形＝挖溝實錘）----
# 帶內 w=1、帶緣 10mm smoothstep 過渡；120 對＝user 指示的更強低通；
# 零收縮＝面片稜線削掉、公分級形狀與體積保留＝大腿內側不再有溝。
TAUBIN_PAIRS = 120; LAM = 0.5; MU = -0.53
import numpy as _np
bm.verts.ensure_lookup_table()
nv = len(bm.verts)
co = _np.array([v.co[:] for v in bm.verts])
wgt = _np.zeros(nv)
for i, v in enumerate(bm.verts):
    d = dist_to_guide(v.co)
    if d >= PROJ_ZERO: continue
    t = 1.0 if d <= PROJ_FULL else 1.0 - (d - PROJ_FULL) / (PROJ_ZERO - PROJ_FULL)
    wgt[i] = t * t * (3 - 2 * t)
from collections import defaultdict as _dd
adj2 = _dd(set)
for e in bm.edges:
    a, b = e.verts[0].index, e.verts[1].index
    adj2[a].add(b); adj2[b].add(a)
mov = _np.where(wgt > 0)[0]
nbrs = {int(i): _np.array(sorted(adj2[int(i)]), dtype=_np.int64) for i in mov}
x = co.copy()
def _lap(xx):
    out = _np.zeros_like(xx)
    for i in mov:
        nb = nbrs[int(i)]
        if len(nb): out[i] = xx[nb].mean(0) - xx[i]
    return out
for _it in range(TAUBIN_PAIRS):
    x = x + LAM * wgt[:, None] * _lap(x)
    x = x + MU * wgt[:, None] * _lap(x)
mv_ = _np.linalg.norm(x - co, axis=1) * 1000
P(f"taubin x{TAUBIN_PAIRS}: movable={len(mov)} move p50 {_np.percentile(mv_[mov],50):.2f} p95 {_np.percentile(mv_[mov],95):.2f} max {mv_.max():.2f} mm")
assert (mv_[wgt == 0] < 1e-9).all(), "pinned verts moved"
for i in mov:
    bm.verts[int(i)].co = x[int(i)]

bm.to_mesh(me); bm.free()
me.update()
n_tris1 = sum(len(p.vertices) - 2 for p in me.polygons)
P(f"body tris {n_tris0} -> {n_tris1}")
# 斷言：三 UV 層/頂點色/權重都還在
assert set(l.name for l in me.uv_layers) >= {"UVMap", "FaceUV", "HairUV"}, "UV layers lost"
assert me.color_attributes.get("FaceMask") is not None, "FaceMask lost"
unweighted = sum(1 for v in me.vertices if not v.groups)
P(f"unweighted verts={unweighted}")
assert unweighted == 0, "weights lost on subdivided verts"
bpy.data.objects.remove(proxy, do_unlink=True)
bpy.ops.wm.save_mainfile(filepath=MASTER)
P("SAVED master")
