# FP 2D 筆貼圖（2026-07-22 user 定案「FP 要 2D 感、像 FPS viewmodel」）：
# 機械體+握管（基準長）組裝 → 去背直式渲染 1024²（muzzle 朝下、頂部機械）→
# SourceAssets/TattooMachineTex/T_UI_TattooPen.png；印出出針口在圖中的
# 正規化座標（u 從左、v 從上）供 NiceInkHUD 針線錨定。
# Run: blender --background --python render_tattoo_pen_sprite.py
import bpy
import math
from mathutils import Vector

FBX_BODY = r"C:\games\Unreal Engine\nice_ink\SourceAssets\sumo_tattoo_machine.fbx"
FBX_GRIP = r"C:\games\Unreal Engine\nice_ink\SourceAssets\sumo_tattoo_grip.fbx"
TEX_DIR = r"C:\games\Unreal Engine\nice_ink\SourceAssets\TattooMachineTex"
OUT_PNG = TEX_DIR + r"\T_UI_TattooPen.png"
GRIP_LEN_M = 0.23166  # PenGripBaseLenCm

bpy.ops.wm.read_factory_settings(use_empty=True)

def import_fbx(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=path)
    return [o for o in bpy.data.objects if o not in before and o.type == 'MESH'][0]

body = import_fbx(FBX_BODY)
grip = import_fbx(FBX_GRIP)
grip.scale = (1.0, 1.0, GRIP_LEN_M / 0.01)  # 單位 1cm → 基準長

# 貼圖接回（FBX path_mode=STRIP 只留槽；渲染要重接 base_color）
img = bpy.data.images.load(TEX_DIR + r"\T_TattooMachine_base_color.png")
mat = bpy.data.materials.new("SpriteTex")
mat.use_nodes = True
bsdf = mat.node_tree.nodes["Principled BSDF"]
tex = mat.node_tree.nodes.new("ShaderNodeTexImage")
tex.image = img
mat.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
bsdf.inputs["Roughness"].default_value = 0.55
for o in (body, grip):
    o.data.materials.clear()
    o.data.materials.append(mat)

# 場景：去背、直式構圖（muzzle 朝下）
scene = bpy.context.scene
try:
    scene.render.engine = 'BLENDER_EEVEE_NEXT'
except Exception:
    pass
scene.render.film_transparent = True
scene.render.resolution_x = 1024
scene.render.resolution_y = 1024
scene.render.image_settings.color_mode = 'RGBA'

sun = bpy.data.objects.new("Sun", bpy.data.lights.new("Sun", 'SUN'))
sun.data.energy = 4.0
sun.rotation_euler = (math.radians(55), 0.0, math.radians(35))
bpy.context.collection.objects.link(sun)
w = bpy.data.worlds.new("W")
w.use_nodes = True
w.node_tree.nodes["Background"].inputs[0].default_value = (1, 1, 1, 1)
w.node_tree.nodes["Background"].inputs[1].default_value = 0.45
scene.world = w

cam_data = bpy.data.cameras.new("Cam")
cam_data.lens = 65.0
cam = bpy.data.objects.new("Cam", cam_data)
bpy.context.collection.objects.link(cam)
scene.camera = cam
# 機器 z ∈ [-0.232, +0.246]；相機 3/4 前右視角、置中略高
center = Vector((0.0, 0.02, 0.01))
cam.location = center + Vector((0.85, -0.55, 0.12)).normalized() * 1.05
cam.rotation_euler = (center - cam.location).to_track_quat('-Z', 'Y').to_euler()

scene.render.filepath = OUT_PNG
bpy.ops.render.render(write_still=True)

# 出針口（muzzle=grip 底 (0,0,-GRIP_LEN)）的影像座標（u 左起、v 上起）
from bpy_extras.object_utils import world_to_camera_view
muzzle = Vector((0.0, 0.0, -GRIP_LEN_M))
uv = world_to_camera_view(scene, cam, muzzle)
print(f"SPRITE_MUZZLE u={uv.x:.4f} v_from_top={1.0 - uv.y:.4f}")
top = world_to_camera_view(scene, cam, Vector((0.0, 0.0, 0.246)))
print(f"SPRITE_TOP    u={top.x:.4f} v_from_top={1.0 - top.y:.4f}")
print("SPRITE_DONE", OUT_PNG)
