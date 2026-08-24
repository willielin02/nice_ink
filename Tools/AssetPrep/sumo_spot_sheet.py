"""斑普查的看圖版（2026-08-24，唯讀）：把 sumo_spot_census 的結果畫成一張可以直接看的圖。

四視角熱點圖（強對比色階：灰→黃→橘→紅）＋把前 N 塊斑的編號投影到畫面上，
再輸出各視角的標號座標給後製（cv2）合成一張總表。
先跑 sumo_spot_census.py（會寫 spots.npz），再跑本檔。
產出：Saved/SpotCensus/sheet_<view>.png ＋ sheet_labels.json
"""
import bpy
import os
import json
import numpy as np
from mathutils import Vector
from bpy_extras.object_utils import world_to_camera_view

ROOT = r"C:\games\Unreal Engine\nice_ink"
OUT = os.path.join(ROOT, "Saved", "SpotCensus")
BLEND = os.environ.get("BLEND", os.path.join(ROOT, "SourceAssets", "sumo_character_master.blend"))
TOPN = int(os.environ.get("TOPN", "24"))
RES = 1400

bpy.ops.wm.open_mainfile(filepath=BLEND)
ob = bpy.data.objects["SumoRetopo"]
me = ob.data
n = len(me.vertices)
co = np.empty(n * 3)
me.vertices.foreach_get("co", co)
co = co.reshape(-1, 3)

D = np.load(os.path.join(OUT, "spots.npz"))
rough = D["rough"]
hot = D["hot"]
lab = D["lab"]
varea = D["varea"]
THR = float(D["thr"])

tri = []
for p in me.polygons:
    ls = list(p.vertices)
    for k in range(1, len(ls) - 1):
        tri.append((ls[0], ls[k], ls[k + 1]))
tri = np.array(tri, np.int64)
fn = np.cross(co[tri[:, 1]] - co[tri[:, 0]], co[tri[:, 2]] - co[tri[:, 0]])
acc = np.zeros((n, 3))
for k in range(3):
    np.add.at(acc, tri[:, k], fn)
gn = acc / np.maximum(np.linalg.norm(acc, axis=1), 1e-12)[:, None]

# ---- 重建斑（用 census 存下來的 lab）並排序，排序鍵與 census 一致 ----
blobs = []
for c in range(int(lab.max()) + 1 if lab.max() >= 0 else 0):
    vs = np.where(lab == c)[0]
    if len(vs) == 0:
        continue
    area = varea[vs].sum() * 1e4
    if area < 0.30:
        continue
    pk = int(vs[np.argmax(rough[vs])])
    blobs.append(dict(area=area, peak=float(rough[pk]), pos=co[vs].mean(0),
                      nrm=gn[vs].mean(0), pct=float(rough[pk]) * 0.98))
blobs.sort(key=lambda b: -(b["pct"] * np.sqrt(b["area"])))
blobs = blobs[:TOPN]
print("labelling %d blobs" % len(blobs), flush=True)

# ---- 上色：強對比色階 ----
t = np.clip((rough - THR) / max(THR * 2.5, 1e-6), 0, 1)
cols = np.zeros((n, 4))
cols[:, 3] = 1
base = np.array([0.72, 0.71, 0.69])
ramp = np.array([[1.00, 0.92, 0.35],    # 黃
                 [1.00, 0.55, 0.10],    # 橘
                 [0.88, 0.06, 0.06]])   # 紅
for i in range(n):
    if not hot[i]:
        cols[i, :3] = base
    else:
        u = t[i] * 2.0
        k = min(int(u), 1)
        f = u - k
        cols[i, :3] = ramp[k] * (1 - f) + ramp[k + 1] * f
ca = me.color_attributes.get("Sheet") or me.color_attributes.new("Sheet", 'FLOAT_COLOR', 'POINT')
ca.data.foreach_set("color", cols.ravel())
me.attributes.active_color_name = "Sheet"

for o in bpy.data.objects:
    if o.name != "SumoRetopo":
        o.hide_render = True
ob.hide_render = False
sc = bpy.context.scene
sc.render.engine = 'BLENDER_WORKBENCH'
sh = sc.display.shading
sh.light = 'FLAT'
sh.color_type = 'VERTEX'
sh.show_specular_highlight = False
sc.render.resolution_x = RES
sc.render.resolution_y = RES
sc.render.film_transparent = False
if sc.world is None:
    sc.world = bpy.data.worlds.new("SheetWorld")
sc.world.color = (0.10, 0.10, 0.11)
sc.display.shading.background_type = 'VIEWPORT'
sc.display.shading.background_color = (0.10, 0.10, 0.11)
cd = bpy.data.cameras.new("SheetCam")
cam = bpy.data.objects.new("SheetCam", cd)
bpy.context.collection.objects.link(cam)
sc.camera = cam
cd.lens = 42

# user 用 NiMark 圈選的兩點（2026-08-24）：UV 分支用成對距離指紋選出，
# M1 的筆劃跨 UV 接縫＝定位存疑，M2 可靠。畫在圖上是為了讓 user 自己核對位置。
MARKS = [("M1", np.array([-0.488, -0.164, 0.699])), ("M2", np.array([-0.403, 0.403, 0.862]))]
VIEWS = [("front", (0, -1, 0.10)), ("back", (0, 1, 0.10)),
         ("left", (1, -0.10, 0.08)), ("right", (-1, -0.10, 0.08))]
labels = {}
for tag, dv in VIEWS:
    f = np.array([0.0, 0.0, 0.86])
    v = np.array(dv, float)
    v /= np.linalg.norm(v)
    eye = f + v * 3.1
    cam.location = Vector(eye.tolist())
    cam.rotation_euler = Vector((f - eye).tolist()).to_track_quat('-Z', 'Y').to_euler()
    bpy.context.view_layer.update()
    sc.render.filepath = os.path.join(OUT, "sheet_%s.png" % tag)
    bpy.ops.render.render(write_still=True)
    L = []
    for i, b in enumerate(blobs, 1):
        nrm = b["nrm"] / max(np.linalg.norm(b["nrm"]), 1e-9)
        if float(nrm @ v) < 0.25:               # 背對鏡頭的不標（v＝注視點→鏡頭）
            continue
        p = world_to_camera_view(sc, cam, Vector(b["pos"].tolist()))
        if not (0.02 < p.x < 0.98 and 0.02 < p.y < 0.98):
            continue
        L.append(dict(i=i, x=float(p.x) * RES, y=(1.0 - float(p.y)) * RES,
                      pct=b["pct"], area=b["area"]))
    MK = []
    for nm, pos in MARKS:
        p2 = world_to_camera_view(sc, cam, Vector(pos.tolist()))
        vn = pos - f
        vn /= max(np.linalg.norm(vn), 1e-9)
        if float(vn @ v) < -0.15:
            continue
        if not (0.02 < p2.x < 0.98 and 0.02 < p2.y < 0.98):
            continue
        MK.append(dict(name=nm, x=float(p2.x) * RES, y=(1.0 - float(p2.y)) * RES))
    labels[tag] = L
    labels[tag + "_marks"] = MK
    print("WROTE sheet_%s.png (%d labels, %d marks)" % (tag, len(L), len(MK)), flush=True)

json.dump(dict(thr=THR, views=labels,
               blobs=[dict(i=i + 1, pct=b["pct"], area=b["area"], peak=b["peak"],
                           pos=[float(x) for x in b["pos"]]) for i, b in enumerate(blobs)]),
          open(os.path.join(OUT, "sheet_labels.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
print("WROTE sheet_labels.json", flush=True)
