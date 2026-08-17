# 酒瓶資產處理（開場儀式）：SourceAssets/whiskey/whiskey.glb → sumo_bottle.fbx
#
# 源模型內容（實測 6 件）：瓶身 Cube.002_glass_0（沿 +Z 直立、高 1.433）＋瓶內酒
# Cube.003_whiskey_0＋標籤 Cube.000_paper_0＋瓶蓋 Cylinder.002_viko_0（皆 x≈0）；
# **外加一只獨立威士忌杯**（Cylinder_glass_0＋Cylinder.001_whiskey_0，x≈0.783）
# ——杯子必須剔除，否則 PCA 主軸被它拉歪（首版實錘：算出 30×8.3×22.5cm 的怪東西）。
#
# 管線：選瓶件（|center.x| < 0.4）→ 合併 → 繞 Y 轉 +90°（+Z 直立 ⇒ +X 橫躺、瓶口朝 +X）
# → 縮到實尺寸（長 30cm）→ 原點＝旋轉樞軸（長軸質心、左右置中、底面 z=0）。
# 材質槽保留（glass/whiskey/paper/viko 四段）＝日後可分別上材質。
#
# 樞軸語義（引擎端依賴）：actor 放在地板上、繞世界 Z 轉 ⇒ 瓶口指向 = actor +X 的 yaw。
# Run: blender --background --python build_bottle_fbx.py
import bpy
import numpy as np
from mathutils import Matrix, Quaternion, Vector

GLB = r"C:\games\Unreal Engine\nice_ink\SourceAssets\whiskey\whiskey.glb"
OUT_FBX = r"C:\games\Unreal Engine\nice_ink\SourceAssets\sumo_bottle.fbx"
TARGET_LEN_M = 0.30  # 30cm 酒瓶

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=GLB)

# 攤平世界變換進頂點（glb 的層級都在 empty 上）
meshes = [o for o in bpy.data.objects if o.type == 'MESH']
assert meshes, "no mesh in glb"
for o in meshes:
    world = o.matrix_world.copy()
    o.parent = None
    o.matrix_world = Matrix.Identity(4)
    o.data.transform(world)

# 剔除旁邊那只獨立威士忌杯（x≈0.783）：只留瓶件（|center.x| < 0.4）
TUMBLER_X = 0.4
for o in list(bpy.data.objects):
    if o.type != 'MESH':
        bpy.data.objects.remove(o, do_unlink=True)
        continue
    xs = [v.co.x for v in o.data.vertices]
    cx = (max(xs) + min(xs)) * 0.5
    if abs(cx) >= TUMBLER_X:
        print(f"drop tumbler part: {o.name} (center.x={cx:.3f})")
        bpy.data.objects.remove(o, do_unlink=True)

bpy.ops.object.select_all(action='DESELECT')
meshes = [o for o in bpy.data.objects if o.type == 'MESH']
for o in meshes:
    o.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
if len(meshes) > 1:
    bpy.ops.object.join()
bottle = bpy.context.view_layer.objects.active
print(f"joined {len(meshes)} mesh objects -> {bottle.name} verts={len(bottle.data.vertices)}")


def verts():
    return np.array([v.co[:] for v in bottle.data.vertices])


# 橫躺：繞 Y 轉 +90°（直立的 +Z ⇒ +X；瓶蓋在高 Z ⇒ 落在 +X＝瓶口方向）
# （−90° 會把 +Z 映到 −X＝瓶口朝後，首版實錘被自檢斷言擋下）
V = verts()
bottle.data.transform(Matrix.Translation((-Vector(V.mean(0).tolist()))))
bottle.data.transform(Matrix.Rotation(np.radians(90.0), 4, 'Y'))

# 自檢：瓶口端（yz 截面半徑較小）必須在 +X
V = verts()
xmin, xmax = V[:, 0].min(), V[:, 0].max()
band = (xmax - xmin) * 0.12
lo = V[V[:, 0] < xmin + band]
hi = V[V[:, 0] > xmax - band]
r_lo = np.sqrt(lo[:, 1] ** 2 + lo[:, 2] ** 2).mean()
r_hi = np.sqrt(hi[:, 1] ** 2 + hi[:, 2] ** 2).mean()
print(f"end radii: -X={r_lo:.3f} +X={r_hi:.3f} (瓶口應為較小的 +X)")
assert r_hi < r_lo, "瓶口不在 +X——源模型朝向與假設不符，先看 inspect 輸出"

# 實尺寸
V = verts()
scale = TARGET_LEN_M / max(V[:, 0].max() - V[:, 0].min(), 1e-6)
bottle.data.transform(Matrix.Scale(scale, 4))

# 原點＝旋轉樞軸：長軸取質心（轉起來不偏心）、左右置中、底面貼 z=0
V = verts()
cx = V[:, 0].mean()
cy = (V[:, 1].min() + V[:, 1].max()) * 0.5
zmin = V[:, 2].min()
bottle.data.transform(Matrix.Translation((-cx, -cy, -zmin)))

V = verts()
print("BOTTLE dims: len={:.1f}cm dia={:.1f}cm  neck_x={:.1f}cm base_x={:.1f}cm  z=[{:.1f},{:.1f}]cm".format(
    (V[:, 0].max() - V[:, 0].min()) * 100,
    (V[:, 1].max() - V[:, 1].min()) * 100,
    V[:, 0].max() * 100, V[:, 0].min() * 100,
    V[:, 2].min() * 100, V[:, 2].max() * 100))

bpy.ops.object.select_all(action='DESELECT')
bottle.select_set(True)
bpy.context.view_layer.objects.active = bottle
bpy.ops.export_scene.fbx(
    filepath=OUT_FBX,
    use_selection=True,
    object_types={'MESH'},
    apply_unit_scale=True,
    apply_scale_options='FBX_SCALE_NONE',
    path_mode='STRIP',
    use_mesh_modifiers=False,
    add_leaf_bones=False,
)
print("EXPORTED", OUT_FBX)
