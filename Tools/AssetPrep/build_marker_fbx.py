# 麥克筆資產處理（直接畫制 S5）：SourceAssets/Marker/marker.glb →
# 去蓋（Cap_Pencil_0）→ PCA 主軸對齊 +Z → 筆尖（半徑較小端）置原點 →
# 縮到實尺寸（長 13cm）→ sumo_marker.fbx（幾何 only；材質引擎端上）
# Run: blender --background --python build_marker_fbx.py
import bpy
import numpy as np
from mathutils import Matrix, Quaternion, Vector

GLB = r"C:\games\Unreal Engine\nice_ink\SourceAssets\Marker\marker.glb"
OUT_FBX = r"C:\games\Unreal Engine\nice_ink\SourceAssets\sumo_marker.fbx"
TARGET_LEN_M = 0.13  # 13cm 麥克筆

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=GLB)

pen = bpy.data.objects.get("Pen_Pencil_0")
assert pen is not None, "Pen_Pencil_0 not found"

# 攤平世界變換進頂點（glb 的層級都在 empty 上）
world = pen.matrix_world.copy()
pen.parent = None
pen.matrix_world = Matrix.Identity(4)
pen.data.transform(world)

# 清場：只留筆身
for o in list(bpy.data.objects):
    if o is not pen:
        bpy.data.objects.remove(o, do_unlink=True)


def vert_array():
    return np.array([v.co[:] for v in pen.data.vertices])


# PCA 主軸 → +Z
V = vert_array()
C = V.mean(0)
Cov = (V - C).T @ (V - C)
_, Vec = np.linalg.eigh(Cov)
axis = Vector(Vec[:, 2].tolist())
pen.data.transform(Matrix.Translation((-Vector(C.tolist()))))
pen.data.transform(axis.rotation_difference(Vector((0, 0, 1))).to_matrix().to_4x4())

# 筆尖判定：半徑較小的端＝尖端 → 轉到 -Z 端後置原點
V = vert_array()
zmin, zmax = V[:, 2].min(), V[:, 2].max()
band = (zmax - zmin) * 0.1
r_low = np.sqrt(V[V[:, 2] < zmin + band, 0] ** 2 + V[V[:, 2] < zmin + band, 1] ** 2).mean()
r_high = np.sqrt(V[V[:, 2] > zmax - band, 0] ** 2 + V[V[:, 2] > zmax - band, 1] ** 2).mean()
if r_high < r_low:
    pen.data.transform(Quaternion((0.0, 1.0, 0.0, 0.0)).to_matrix().to_4x4())  # 繞 X 轉 180
    V = vert_array()
    zmin, zmax = V[:, 2].min(), V[:, 2].max()

# 尖端置原點（xy 置中、z=0=尖端、筆身沿 +Z）＋實尺寸
cx = (V[:, 0].min() + V[:, 0].max()) * 0.5
cy = (V[:, 1].min() + V[:, 1].max()) * 0.5
pen.data.transform(Matrix.Translation((-cx, -cy, -zmin)))
scale = TARGET_LEN_M / max(zmax - zmin, 1e-6)
pen.data.transform(Matrix.Scale(scale, 4))

V = vert_array()
print(f"MARKER dims: len={V[:,2].max()*100:.1f}cm dia~={(V[:,0].max()-V[:,0].min())*100:.1f}cm tip_z={V[:,2].min()*100:.2f}cm")

pen.select_set(True)
bpy.context.view_layer.objects.active = pen
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
