# 凹陷曲率圖（cavity）：每頂點 c=dot(鄰居均值-自身, 法線)/平均邊長 → 正=內凹
# 網格上做 2 輪標量平滑（島安全），再光柵化進 UV0
import bpy
import numpy as np

MASTER = r"C:\games\Unreal Engine\nice_ink\SourceAssets\sumo_character_master.blend"
S = r"C:\Users\willi\AppData\Local\Temp\claude\c--games-Unreal-Engine-nice-ink\00d214a0-ef56-4e54-9786-cb494e647707\scratchpad"
SIZE = 2048
bpy.ops.wm.open_mainfile(filepath=MASTER)
body = bpy.data.objects["SumoRetopo"]
me = body.data
mw = body.matrix_world
n_v = len(me.vertices)
P = np.empty((n_v, 3), np.float64)
me.vertices.foreach_get("co", P.ravel())
M = np.array(mw)
Pw = P @ M[:3, :3].T + M[:3, 3]
N = np.empty((n_v, 3), np.float64)
me.vertices.foreach_get("normal", N.ravel())
N = N @ M[:3, :3].T   # 均勻縮放下方向正確
N /= (np.linalg.norm(N, axis=1, keepdims=True) + 1e-12)

adj = [[] for _ in range(n_v)]
for e in me.edges:
    a, b = e.vertices
    adj[a].append(b)
    adj[b].append(a)
cav = np.zeros(n_v, np.float64)
for i in range(n_v):
    nb = adj[i]
    if not nb:
        continue
    d = Pw[nb].mean(axis=0) - Pw[i]
    el = np.mean(np.linalg.norm(Pw[nb] - Pw[i], axis=1))
    cav[i] = np.dot(d, N[i]) / (el + 1e-9)
for it in range(2):   # 網格上平滑（非 UV——島安全）
    new = cav.copy()
    for i in range(n_v):
        if adj[i]:
            new[i] = 0.5 * cav[i] + 0.5 * np.mean(cav[[*adj[i]]])
    cav = new
print(f"CAV pct: p50={np.percentile(cav,50):.4f} p90={np.percentile(cav,90):.4f} "
      f"p99={np.percentile(cav,99):.4f} max={cav.max():.4f}")

uv0 = me.uv_layers["UVMap"]
me.calc_loop_triangles()
img = np.zeros((SIZE, SIZE), np.float32)
cover = np.zeros((SIZE, SIZE), bool)
for tri in me.loop_triangles:
    vi = [me.loops[l].vertex_index for l in tri.loops]
    uv = np.array([uv0.data[l].uv[:] for l in tri.loops], np.float64)
    cv = np.array([cav[v] for v in vi], np.float64)
    dst = np.stack([uv[:, 0] * SIZE, (1.0 - uv[:, 1]) * SIZE], axis=1)
    x0 = max(int(dst[:, 0].min()) - 1, 0); y0 = max(int(dst[:, 1].min()) - 1, 0)
    x1 = min(int(dst[:, 0].max()) + 2, SIZE); y1 = min(int(dst[:, 1].max()) + 2, SIZE)
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
    inside = (w0 >= -0.02) & (w1 >= -0.02) & (w2 >= -0.02)
    if not inside.any():
        continue
    val = w0 * cv[0] + w1 * cv[1] + w2 * cv[2]
    sub = cover[y0:y1, x0:x1]
    sel = inside & ~sub
    img[y0:y1, x0:x1][sel] = val[sel].astype(np.float32)
    sub[inside] = True
np.save(S + r"\body_cavity.npy", img)
np.save(S + r"\body_cavity_cover.npy", cover)
print("CAVITY_DONE")
