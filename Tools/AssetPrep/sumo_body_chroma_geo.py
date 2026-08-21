# 全身色度轉印——幾何端（改編自 sumo_hair_transfer_scan.py，髮區→全身）
# 每個 UV0 紋素（512 格，色度=低頻場不需 2048）：3D 位置 → BVH 最近掃描表面點
# → 重心插值掃描 UV → body_scanuv.npy + body_cover.npy 給 venv 端 cv2.remap
# 不存 master——只出 npy。
import bpy
import os
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

MASTER = r"C:\games\Unreal Engine\nice_ink\SourceAssets\sumo_character_master.blend"
BASE = r"C:\games\Unreal Engine\nice_ink\SourceAssets\sumo_retopo_base.blend"
S = os.environ.get("CHROMA_S", "")
SIZE = 512

bpy.ops.wm.open_mainfile(filepath=MASTER)
body = bpy.data.objects["SumoRetopo"]
me = body.data
mw = body.matrix_world

uv0 = me.uv_layers["UVMap"]
me.calc_loop_triangles()
body_tris = []
for tri in me.loop_triangles:
    uvr, pr = [], []
    for li in tri.loops:
        u, v = uv0.data[li].uv
        uvr += [u, v]
        p = mw @ me.vertices[me.loops[li].vertex_index].co
        pr += [p.x, p.y, p.z]
    body_tris.append((uvr, pr))
print(f"BODY tris={len(body_tris)}")

# ---- 附加 SumoScan（置中座標系，待對齊）----
before = set(bpy.data.objects.keys())
bpy.ops.wm.append(directory=BASE + r"\Object" + "\\", filename="SumoScan")
scan = bpy.data.objects[(set(bpy.data.objects.keys()) - before).pop()]
sme = scan.data
smw = scan.matrix_world
print(f"SCAN appended: verts={len(sme.vertices)} polys={len(sme.polygons)}")

sverts = np.empty((len(sme.vertices), 3), np.float64)
sme.vertices.foreach_get("co", sverts.ravel())
M = np.array(smw)
sverts = sverts @ M[:3, :3].T + M[:3, 3]

sme.calc_loop_triangles()
ntri = len(sme.loop_triangles)
tri_vidx = np.empty((ntri, 3), np.int64)
sme.loop_triangles.foreach_get("vertices", tri_vidx.ravel())
tri_loops = np.empty((ntri, 3), np.int64)
sme.loop_triangles.foreach_get("loops", tri_loops.ravel())
suvl = sme.uv_layers.active
suv = np.empty((len(sme.loops), 2), np.float64)
suvl.data.foreach_get("uv", suv.ravel())

# ---- 平移 ICP：master 點 → scan 表面（同髮區版）----
bvh = BVHTree.FromPolygons([Vector(v) for v in sverts],
                           [tuple(t) for t in tri_vidx], all_triangles=True)
head_pts = [mw @ v.co for v in me.vertices if (mw @ v.co).z > 1.30]
body_pts = [mw @ me.vertices[i].co for i in range(0, len(me.vertices), 8)]
pts = head_pts + body_pts
t = Vector((0.0, 0.0, 0.0))
t.z = 0.0 - float(sverts[:, 2].min())
res = 0.0
for it in range(12):
    deltas = []
    for p in pts:
        co, nrm, idx, dist = bvh.find_nearest(p - t)
        if co is not None:
            deltas.append(p - (co + t))
    d = sum(deltas, Vector()) / len(deltas)
    t += d
    res = sum(dv.length for dv in deltas) / len(deltas)
    if d.length < 1e-5:
        break
print(f"ICP t=({t.x:.4f},{t.y:.4f},{t.z:.4f}) mean_res={res*1000:.2f}mm iters={it+1}")

# ---- 全身紋素 → 掃描 UV ----
uv_map = np.zeros((SIZE, SIZE, 2), np.float32)
cover = np.zeros((SIZE, SIZE), bool)
dists = []
for uvr, pr in body_tris:
    uvt = np.array(uvr, np.float64).reshape(3, 2)
    pos = np.array(pr, np.float64).reshape(3, 3)
    dst = np.stack([uvt[:, 0] * SIZE, (1.0 - uvt[:, 1]) * SIZE], axis=1)
    x0 = max(int(np.floor(dst[:, 0].min())) - 1, 0)
    y0 = max(int(np.floor(dst[:, 1].min())) - 1, 0)
    x1 = min(int(np.ceil(dst[:, 0].max())) + 1, SIZE)
    y1 = min(int(np.ceil(dst[:, 1].max())) + 1, SIZE)
    if x1 <= x0 or y1 <= y0:
        continue
    xs, ys = np.meshgrid(np.arange(x0, x1) + 0.5, np.arange(y0, y1) + 0.5)
    d = dst
    det = (d[1,1]-d[2,1])*(d[0,0]-d[2,0]) + (d[2,0]-d[1,0])*(d[0,1]-d[2,1])
    if abs(det) < 1e-9:
        continue
    w0 = ((d[1,1]-d[2,1])*(xs-d[2,0]) + (d[2,0]-d[1,0])*(ys-d[2,1])) / det
    w1 = ((d[2,1]-d[0,1])*(xs-d[2,0]) + (d[0,0]-d[2,0])*(ys-d[2,1])) / det
    w2 = 1.0 - w0 - w1
    inside = (w0 >= -0.001) & (w1 >= -0.001) & (w2 >= -0.001)
    if not inside.any():
        continue
    P = (w0[..., None] * pos[0] + w1[..., None] * pos[1] + w2[..., None] * pos[2])
    ii, jj = np.nonzero(inside)
    for k in range(len(ii)):
        py, px = y0 + ii[k], x0 + jj[k]
        if cover[py, px]:
            continue
        q = Vector(P[ii[k], jj[k]]) - t
        co, nrm, idx, dist = bvh.find_nearest(q)
        if co is None:
            continue
        va, vb, vc = sverts[tri_vidx[idx]]
        e0 = vb - va; e1 = vc - va; e2 = np.array(co) - va
        d00 = e0 @ e0; d01 = e0 @ e1; d11 = e1 @ e1
        d20 = e2 @ e0; d21 = e2 @ e1
        den = d00 * d11 - d01 * d01
        if abs(den) < 1e-14:
            wb = wc = 0.0
        else:
            wb = (d11 * d20 - d01 * d21) / den
            wc = (d00 * d21 - d01 * d20) / den
        wa = 1.0 - wb - wc
        la, lb, lc = tri_loops[idx]
        uv_hit = wa * suv[la] + wb * suv[lb] + wc * suv[lc]
        uv_map[py, px] = uv_hit
        cover[py, px] = True
        dists.append(dist)
print(f"TEXELS covered={cover.sum()} sample_dist mean={np.mean(dists)*1000:.2f}mm "
      f"p95={np.percentile(dists,95)*1000:.2f}mm")
np.save(S + r"\body_scanuv.npy", uv_map)
np.save(S + r"\body_cover.npy", cover)
print("CHROMA_GEO_DONE")
