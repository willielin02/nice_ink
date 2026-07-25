# T_UI_MarkerPen 渲染（07-25 打稿制 FP 2D 筆貼圖版）：
# SourceAssets/sumo_marker.fbx（尖端=原點、筆身 +Z、13cm）→ 染紫三分區
#（筆頭深/筆身龍膽紫/尾帽淺）→ 徑向 2.2×（對齊遊戲內加粗）→ 正交前視、
# 透明背景 1024² → PNG + 印出筆尖正規化 UV（HUD RotPivot 用）。
# Run: blender --background --python render_marker_ui.py
import bpy
from mathutils import Matrix, Vector

FBX = r"C:/games/Unreal Engine/nice_ink/SourceAssets/sumo_marker.fbx"
OUT = r"C:/games/Unreal Engine/nice_ink/SourceAssets/T_UI_MarkerPen.png"

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=FBX)

pen = None
for o in bpy.data.objects:
    if o.type == "MESH":
        pen = o
        break
assert pen is not None, "no mesh imported"

# 攤平變換（FBX 匯入可能帶 scale/rot）
world = pen.matrix_world.copy()
pen.parent = None
pen.matrix_world = Matrix.Identity(4)
pen.data.transform(world)

# 量實際邊界（不猜單位）
zs = [v.co.z for v in pen.data.vertices]
zmin, zmax = min(zs), max(zs)
L = zmax - zmin
print(f"MARKER_BOUNDS zmin={zmin:.4f} zmax={zmax:.4f} L={L:.4f}")

# 徑向加粗 1.6×（2.2×=罐頭讀感；UI 比例 3.7:1=粗壯麥克筆、螢幕粗細由 HUD 尺寸控）
pen.data.transform(Matrix.Diagonal((1.6, 1.6, 1.0, 1.0)))

# 三分區材質：筆頭（下 16%）深紫黑、筆身龍膽紫、尾帽（上 12%）淺灰紫
def make_mat(name, rgb, rough):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (*rgb, 1.0)
    bsdf.inputs["Roughness"].default_value = rough
    return m

m_nib = make_mat("Nib", (0.035, 0.012, 0.055), 0.35)
m_body = make_mat("Body", (0.24, 0.06, 0.38), 0.45)
m_cap = make_mat("Cap", (0.72, 0.69, 0.78), 0.5)
pen.data.materials.clear()
pen.data.materials.append(m_body)  # 0
pen.data.materials.append(m_nib)   # 1
pen.data.materials.append(m_cap)   # 2
nib_z = zmin + L * 0.16
cap_z = zmin + L * 0.88
for p in pen.data.polygons:
    cz = sum(pen.data.vertices[i].co.z for i in p.vertices) / len(p.vertices)
    p.material_index = 1 if cz < nib_z else (2 if cz > cap_z else 0)

# 燈光：主光（camera 左上）＋補光
sun = bpy.data.objects.new("Sun", bpy.data.lights.new("Sun", "SUN"))
sun.data.energy = 4.0
sun.rotation_euler = (0.9, 0.0, -0.6)
bpy.context.collection.objects.link(sun)
fill = bpy.data.objects.new("Fill", bpy.data.lights.new("Fill", "SUN"))
fill.data.energy = 1.5
fill.rotation_euler = (1.2, 0.0, 2.4)
bpy.context.collection.objects.link(fill)

# 正交相機：看 -Y、置中筆身、留 8% 邊
cam_data = bpy.data.cameras.new("Cam")
cam_data.type = "ORTHO"
ortho = L * 1.16
cam_data.ortho_scale = ortho
cam = bpy.data.objects.new("Cam", cam_data)
cam.location = (0.0, -1.0, (zmin + zmax) * 0.5)
cam.rotation_euler = (1.5707963, 0.0, 0.0)
bpy.context.collection.objects.link(cam)
bpy.context.scene.camera = cam

sc = bpy.context.scene
sc.render.engine = "BLENDER_EEVEE"
sc.render.film_transparent = True
sc.render.resolution_x = 1024
sc.render.resolution_y = 1024
sc.render.image_settings.file_format = "PNG"
sc.render.image_settings.color_mode = "RGBA"
sc.render.filepath = OUT
bpy.ops.render.render(write_still=True)

# 筆尖（world 0,0,zmin... 尖端=原點 z=0；量測後用 zmin 保險）正規化 UV：
# 像素 y 從頂算：py = (cam_z - z)/ortho*1024 + 512
cam_z = (zmin + zmax) * 0.5
tip_v = ((cam_z - zmin) / ortho) + 0.5
print(f"MARKER_TIP_UV u=0.5000 v={tip_v:.4f}")
print("RENDER_DONE " + OUT)
