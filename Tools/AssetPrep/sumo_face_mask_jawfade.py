# 臉罩下顎底淡出（2026-08-16）：M_InkBodyChar 的臉貼圖閘門＝T_FaceMask（UV0 貼圖，不是頂點色）。
# 縫合版＋軟法線後，seam 上方 0~6cm 的下顎底（臉照的頸部陰影區）不再被硬邊摺疊遮住＝
# 「喉部髒一坨」。這裡把 face_mask.png 在該帶內沿頭側距離淡出（seam 上 2cm 內 0 → 6cm 原值），
# 其餘紋素一位元不動。原檔備份 face_mask_orig.png。之後：ue_import_sumo 的 T_FaceMask 重匯入。
# Run: blender --background --python sumo_face_mask_jawfade.py
import bpy, collections, numpy as np
from mathutils import Vector
from mathutils.kdtree import KDTree
import os
S = r"C:\games\Unreal Engine\nice_ink\SourceAssets"
FADE_LO, FADE_HI = 0.02, 0.06
bpy.ops.wm.open_mainfile(filepath=S + r"\sumo_character_master.blend")
body = bpy.data.objects["SumoRetopo"]; me = body.data; mw = body.matrix_world
fc = collections.Counter()
for p in me.polygons:
    for e in p.edge_keys: fc[e] += 1
bverts = set()
for e in me.edges:
    if fc[tuple(sorted((e.vertices[0], e.vertices[1])))] == 1: bverts.update(e.vertices)
assert len(bverts) == 168
seam = [mw @ me.vertices[i].co for i in bverts]
c = sum(seam, Vector()) / len(seam)
P = np.array([[p.x, p.y, p.z] for p in seam]); P -= P.mean(0)
_, _, vt = np.linalg.svd(P, full_matrices=False); nrm = Vector(vt[-1])
if nrm.z < 0: nrm = -nrm
kd = KDTree(len(seam))
for i, p in enumerate(seam): kd.insert(p, i)
kd.balance()
def smooth(t):
    t = max(0.0, min(1.0, t)); return t * t * (3 - 2 * t)
k = np.ones(len(me.vertices))
for v in me.vertices:
    w = mw @ v.co
    if kd.find(w)[2] > 0.16: continue
    s = (w - c).dot(nrm)
    if s <= 0.0: continue
    k[v.index] = smooth((s - FADE_LO) / (FADE_HI - FADE_LO))
print("verts faded:", int((k < 0.999).sum()))
# 讀 face_mask.png（Blender 內建影像 IO）
img = bpy.data.images.load(S + r"\face_mask.png"); W, H = img.size
px = np.array(img.pixels[:], dtype=np.float32).reshape(H, W, 4)   # bottom-up
kmap = np.ones((H, W), np.float32)
uv = me.uv_layers["UVMap"].data
me.calc_loop_triangles()
n_tri = 0
for tri in me.loop_triangles:
    vi = [me.loops[l].vertex_index for l in tri.loops]
    kv = [k[i] for i in vi]
    if min(kv) > 0.999: continue
    n_tri += 1
    uvs = np.array([[uv[l].uv.x * W, uv[l].uv.y * H] for l in tri.loops])
    x0, y0 = np.floor(uvs.min(0)).astype(int) - 1; x1, y1 = np.ceil(uvs.max(0)).astype(int) + 1
    x0 = max(x0, 0); y0 = max(y0, 0); x1 = min(x1, W - 1); y1 = min(y1, H - 1)
    if x1 <= x0 or y1 <= y0: continue
    xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
    (ax, ay), (bx, by), (cx, cy) = uvs
    det = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay)
    if abs(det) < 1e-9: continue
    l1 = ((bx - xs) * (cy - ys) - (cx - xs) * (by - ys)) / det
    l2 = ((cx - xs) * (ay - ys) - (ax - xs) * (cy - ys)) / det
    l3 = 1 - l1 - l2
    inside = (l1 >= -0.02) & (l2 >= -0.02) & (l3 >= -0.02)
    val = l1 * kv[0] + l2 * kv[1] + l3 * kv[2]
    sub = kmap[y0:y1 + 1, x0:x1 + 1]
    sub[inside] = np.minimum(sub[inside], np.clip(val[inside], 0, 1))
print("triangles touched:", n_tri, "texels faded:", int((kmap < 0.999).sum()))
if not os.path.exists(S + r"\face_mask_orig.png"):
    import shutil; shutil.copy(S + r"\face_mask.png", S + r"\face_mask_orig.png")
px[..., 0] *= kmap; px[..., 1] *= kmap; px[..., 2] *= kmap
out = bpy.data.images.new("face_mask_out", W, H, alpha=True)
out.pixels = px.ravel().tolist()
out.filepath_raw = S + r"\face_mask.png"; out.file_format = 'PNG'; out.save()
print("FACE_MASK_JAWFADE_DONE")
