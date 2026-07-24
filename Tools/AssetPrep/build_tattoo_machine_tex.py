# 刺青機貼圖版資產處理（2026-07-22；取代 build_tattoo_machine_fbx.py 的分區平塗制）：
# Meshy Text-to-Texture GLB（PBR 四張內嵌、UV 已展）→ 抽貼圖 PNG →
# 拆針（沿用幾何常數 z_exit=-0.86；UV 縫拆點網格不可 weld、面級 z 判定即可）→
# 拆握管（伸縮分帳制 07-22：伸長量一半給針、一半給握管——握管必須是獨立可縮放件；
#   夾環下緣低於握管頂＝z 平面切不開 → 用 generate 版原網格的 loose 零件歸屬分類，
#   兩版頂點座標完全相同＝最近鄰距離≈0、零誤判）→
# 出針口置原點、+Z=離皮膚向、骨架側轉 +Y、尺度基準 60cm/1.813u →
# sumo_tattoo_machine.fbx（機械體、pivot=握管頂接點）＋ sumo_tattoo_grip.fbx（握管、
# 頂在原點、沿 -Z、單位長 1cm ⇒ scale.Z=握管長 cm）＋ sumo_tattoo_needle.fbx（同慣例）。
# Run: blender --background --python build_tattoo_machine_tex.py
import bpy
import numpy as np
from mathutils import Matrix, Vector

SRC_GLB = r"C:\Users\willi\Downloads\Meshy_AI_Coil_Tattoo_Machine_0721155551_texture.glb"
SRC_FBX_GEN = r"C:\Users\willi\Downloads\Meshy_AI_Coil_Tattoo_Machine_0721125609_generate.fbx"
OUT_MACHINE = r"C:\games\Unreal Engine\nice_ink\SourceAssets\sumo_tattoo_machine.fbx"
OUT_GRIP = r"C:\games\Unreal Engine\nice_ink\SourceAssets\sumo_tattoo_grip.fbx"
OUT_NEEDLE = r"C:\games\Unreal Engine\nice_ink\SourceAssets\sumo_tattoo_needle.fbx"
TEX_DIR = r"C:\games\Unreal Engine\nice_ink\SourceAssets\TattooMachineTex"
MACHINE_LEN_M = 0.60   # 出針口→機頂 60cm（user 三調定值）
NEEDLE_UNIT_M = 0.01
Z_EXIT = -0.86         # 出針口平面（generate 版探測定值；框架相同直接沿用）

import os
os.makedirs(TEX_DIR, exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=SRC_GLB)

meshes = [o for o in bpy.data.objects if o.type == 'MESH']
assert len(meshes) == 1, f"expected 1 mesh, got {len(meshes)}"
obj = meshes[0]
world = obj.matrix_world.copy()
obj.parent = None
obj.matrix_world = Matrix.Identity(4)
obj.data.transform(world)
for o in list(bpy.data.objects):
    if o is not obj and o.type != 'MESH':
        bpy.data.objects.remove(o, do_unlink=True)


def varr(o):
    return np.array([v.co[:] for v in o.data.vertices])


V = varr(obj)
spans = V.max(0) - V.min(0)
print(f"[build] bbox spans={spans} (expect long axis Z ~1.906)")
assert spans[2] == spans.max() and abs(spans[2] - 1.906) < 0.05, f"frame mismatch: {spans}"
assert obj.data.uv_layers, "no UV layers!"

# --- 抽貼圖（packed→PNG；non-color 圖 save() 存原始像素）---
tex_out = {}
for img in bpy.data.images:
    if not img.packed_file:
        continue
    name = img.name.split(".")[0]
    path = os.path.join(TEX_DIR, f"T_TattooMachine_{name}.png")
    img.filepath_raw = path
    img.file_format = 'PNG'
    img.save()
    tex_out[name] = path
    print(f"[build] texture saved: {name} -> {path}")
assert "base_color" in tex_out and "normal" in tex_out and "metallic_roughness" in tex_out, tex_out

# --- 拆針：面級判定（UV 縫拆點網格勿 weld；z<Z_EXIT 之下只有針錐）---
needle_faces = [p.index for p in obj.data.polygons
                if all(obj.data.vertices[vi].co.z < Z_EXIT + 1e-4 for vi in p.vertices)]
assert 20 <= len(needle_faces) <= 400, f"needle face count suspicious: {len(needle_faces)}"
bpy.ops.object.select_all(action='DESELECT')
obj.select_set(True)
bpy.context.view_layer.objects.active = obj
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.select_all(action='DESELECT')
bpy.ops.object.mode_set(mode='OBJECT')
for fi in needle_faces:
    obj.data.polygons[fi].select = True
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.select_mode(type='FACE')
bpy.ops.mesh.separate(type='SELECTED')
bpy.ops.object.mode_set(mode='OBJECT')
new_objs = [o for o in bpy.data.objects if o.type == 'MESH' and o is not obj]
assert len(new_objs) == 1
needle = new_objs[0]
machine = obj
print(f"[build] needle={len(needle.data.vertices)}v machine={len(machine.data.vertices)}v")

# --- 座標定準參數（同 generate 版）：原點=針基座環心@Z_EXIT、骨架 -X→+Y、實尺寸 ---
Vn = varr(needle)
base_ring = Vn[Vn[:, 2] > Vn[:, 2].max() - 0.02]
base_xy = base_ring[:, :2].mean(axis=0)
origin = Vector((base_xy[0], base_xy[1], Z_EXIT))
uz1 = varr(machine)[:, 2].max()  # 手術前的原始 span＝尺度基準（維持既有的公分/單位比）
scale_u = MACHINE_LEN_M / (uz1 - Z_EXIT)

# --- 拆握管（伸縮分帳制；必須在比例手術「前」做——最近鄰比對靠兩版座標逐點相同）---
# generate 版的「握管+針」loose 件（594v）＝握管殼真相；貼圖版頂點座標與其相同 ⇒
# 每個面取面心最近的 generate 頂點、看它屬哪個 loose 件。夾環貼著握管（同半徑、
# z 平面切不開）也切得開——它們是不同殼、各自距離≈0。
GRIP_TOP = -0.16       # 握管頂接點（比例手術/引擎 pivot 共同基準）
gen_before = set(bpy.data.objects)
bpy.ops.import_scene.fbx(filepath=SRC_FBX_GEN)
gen_new = [o for o in bpy.data.objects if o not in gen_before and o.type == 'MESH']
assert len(gen_new) == 1
gen_obj = gen_new[0]
gw = gen_obj.matrix_world.copy()
gen_obj.parent = None
gen_obj.matrix_world = Matrix.Identity(4)
gen_obj.data.transform(gw)
bpy.ops.object.select_all(action='DESELECT')
gen_obj.select_set(True)
bpy.context.view_layer.objects.active = gen_obj
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.separate(type='LOOSE')
bpy.ops.object.mode_set(mode='OBJECT')
gen_parts = [o for o in bpy.data.objects if o.type == 'MESH' and o not in gen_before
             and o not in (machine, needle)]
assert len(gen_parts) == 3, f"generate loose parts != 3: {len(gen_parts)}"
grip_part = min(gen_parts, key=lambda o: min(v.co.z for v in o.data.vertices))  # 含針尖=zmin 最深
VG = np.array([v.co[:] for v in grip_part.data.vertices if v.co.z >= Z_EXIT - 0.01])
VO = np.vstack([np.array([v.co[:] for v in o.data.vertices])
                for o in gen_parts if o is not grip_part])

def min_d2(P, Q, chunk=512):
    out = np.empty(len(P))
    for i in range(0, len(P), chunk):
        d = ((P[i:i + chunk, None, :] - Q[None, :, :]) ** 2).sum(-1)
        out[i:i + chunk] = d.min(1)
    return out

# 頂點級比對（兩版頂點座標逐點相同 ⇒ 正確類的距離≈0；面心比對會被
# 稀疏件（接管 40v 大面）的自距離騙走——實測接管被握管搶）＋面內多數決
MV = varr(machine)
v_is_grip = min_d2(MV, VG) < min_d2(MV, VO)
grip_faces = [p.index for p in machine.data.polygons
              if sum(1 for vi in p.vertices if v_is_grip[vi]) * 2 > len(p.vertices)]
print(f"[build] grip classification: {len(grip_faces)} faces (of {len(machine.data.polygons)})")
assert 400 <= len(grip_faces) <= 1500, f"grip face count suspicious: {len(grip_faces)}"
for o in gen_parts:
    bpy.data.objects.remove(o, do_unlink=True)
bpy.ops.object.select_all(action='DESELECT')
machine.select_set(True)
bpy.context.view_layer.objects.active = machine
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.select_all(action='DESELECT')
bpy.ops.object.mode_set(mode='OBJECT')
for fi in grip_faces:
    machine.data.polygons[fi].select = True
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.select_mode(type='FACE')
bpy.ops.mesh.separate(type='SELECTED')
bpy.ops.object.mode_set(mode='OBJECT')
new_objs = [o for o in bpy.data.objects if o.type == 'MESH' and o not in (machine, needle)]
assert len(new_objs) == 1, f"grip separate failed: {len(new_objs)}"
grip = new_objs[0]
body = machine
print(f"[build] grip={len(grip.data.vertices)}v body={len(body.data.vertices)}v")

# --- 比例手術（07-22 三輪定版：握管原長、機械×2/3）——分件後各自處理 ---
# body：z>GRIP_TOP 的機械組對接點 ×UPPER_SCALE 收攏＋隨握管增長上移；
#   z≤GRIP_TOP 且貼軸（r<0.09 或 z<-0.25）＝環抱握管的夾圈＝不動（跟住握管頂）。
# grip：沿軸伸縮 GRIP_LEN_SCALE（出針口不動）。針不動。
GRIP_LEN_SCALE = 1.0   # user 三改定版：握管原長
UPPER_SCALE = 2.0 / 3.0
GRIP_SHIFT = (GRIP_LEN_SCALE - 1.0) * (GRIP_TOP - Z_EXIT)
ax, ay = float(base_xy[0]), float(base_xy[1])
n_upper = 0
for v in body.data.vertices:
    dx, dy = v.co.x - ax, v.co.y - ay
    r = (dx * dx + dy * dy) ** 0.5
    if v.co.z > GRIP_TOP:
        v.co.x = ax + dx * UPPER_SCALE
        v.co.y = ay + dy * UPPER_SCALE
        v.co.z = GRIP_TOP + (v.co.z - GRIP_TOP) * UPPER_SCALE + GRIP_SHIFT
        n_upper += 1
    elif not (v.co.z < -0.25 or r < 0.09):
        v.co.z += GRIP_SHIFT  # 非貼軸的下部件（理論上不存在）跟機械走
for v in grip.data.vertices:
    v.co.z = Z_EXIT + (v.co.z - Z_EXIT) * GRIP_LEN_SCALE
new_top = GRIP_TOP + (uz1 - GRIP_TOP) * UPPER_SCALE + GRIP_SHIFT
G0_CM = (GRIP_TOP - Z_EXIT) * GRIP_LEN_SCALE * scale_u * 100.0
print(f"[build] surgery: grip len x{GRIP_LEN_SCALE} = {G0_CM:.2f}cm (C++ PenGripBaseLenCm), "
      f"upper x{UPPER_SCALE:.2f} verts={n_upper}, total {(new_top - Z_EXIT) * scale_u * 100:.1f}cm")

print(f"[build] origin={tuple(round(v,4) for v in origin)} scale={scale_u:.5f}")
xform = Matrix.Scale(scale_u, 4) @ Matrix.Rotation(np.deg2rad(-90.0), 4, 'Z') @ Matrix.Translation(-origin)
for o in (body, grip, needle):
    o.data.transform(xform)

Vn = varr(needle)
natural = -Vn[:, 2].min()
mz = Matrix.Identity(4)
mz[2][2] = NEEDLE_UNIT_M / natural
needle.data.transform(mz)
print(f"[build] needle natural={natural*100:.2f}cm -> 1cm/unit, "
      f"base_r={np.linalg.norm(varr(needle)[varr(needle)[:,2]>-0.001][:,:2],axis=1).max()*100:.2f}cm")

# --- 材質槽名統一 TattooTex（C++/UE 匯入辨識鍵；三件共用同一貼圖組）---
assert len(body.data.materials) >= 1
body.data.materials[0].name = "TattooTex"
for o in (body, grip, needle):
    if not o.data.materials:
        o.data.materials.append(body.data.materials[0])
    for i, m in enumerate(o.data.materials):
        o.data.materials[i] = body.data.materials[0]
body.name = "TattooMachine"
grip.name = "TattooGrip"
needle.name = "TattooNeedle"

Vm = varr(body)
print(f"[build] body final bbox(m): x[{Vm[:,0].min():+.3f},{Vm[:,0].max():+.3f}] "
      f"y[{Vm[:,1].min():+.3f},{Vm[:,1].max():+.3f}] z[{Vm[:,2].min():+.3f},{Vm[:,2].max():+.3f}]")
Vg = varr(grip)
print(f"[build] grip final z(m): [{Vg[:,2].min():+.4f},{Vg[:,2].max():+.4f}] (expect ~[0,{G0_CM/100:.4f}])")

# --- 預覽渲染（比例手術自查）---
import math
scene = bpy.context.scene
try:
    scene.render.engine = 'BLENDER_EEVEE_NEXT'
except Exception:
    pass
scene.render.resolution_x = 720
scene.render.resolution_y = 720
_w = bpy.data.worlds.new("W")
_w.use_nodes = True
_w.node_tree.nodes["Background"].inputs[0].default_value = (0.9, 0.9, 0.9, 1)
scene.world = _w
_sun = bpy.data.objects.new("Sun", bpy.data.lights.new("Sun", 'SUN'))
_sun.data.energy = 3.0
_sun.rotation_euler = (math.radians(50), 0, math.radians(30))
bpy.context.collection.objects.link(_sun)
_cam = bpy.data.objects.new("Cam", bpy.data.cameras.new("Cam"))
bpy.context.collection.objects.link(_cam)
scene.camera = _cam
_Vm = varr(body)
_c = Vector((0.0, float(_Vm[:, 1].mean()), float(_Vm[:, 2].max()) * 0.5))
_size = float(_Vm[:, 2].max())
PREVIEW_DIR = r"C:\Users\willi\AppData\Local\Temp\claude\c--games-Unreal-Engine-nice-ink\e71707dc-7c0e-4058-9640-c456ee06b377\scratchpad"
for _name, _d in (("prop_persp", Vector((0.8, -0.6, 0.35))), ("prop_side", Vector((1.0, 0.0, 0.1)))):
    _cam.location = _c + _size * 2.2 * _d.normalized()
    _cam.rotation_euler = (_c - _cam.location).to_track_quat('-Z', 'Y').to_euler()
    scene.render.filepath = PREVIEW_DIR + "\\tatmachine_" + _name + ".png"
    bpy.ops.render.render(write_still=True)
    print("[build] preview", _name)

# --- 各件 pivot 定準（渲染完才動——渲染要看組裝態）---
# body：握管頂接點置原點（引擎擺位錨=握管頂）；grip：頂置原點、沿 -Z、歸一化
# 單位長 1cm（scale.Z=握管長 cm，同針慣例）；needle 已歸一化。
G0_M = G0_CM / 100.0
for v in body.data.vertices:
    v.co.z -= G0_M
mgz = Matrix.Identity(4)
mgz[2][2] = NEEDLE_UNIT_M / G0_M
grip.data.transform(Matrix.Translation((0.0, 0.0, -G0_M)))
grip.data.transform(mgz)
Vg = varr(grip)
print(f"[build] grip normalized: z[{Vg[:,2].min():+.4f},{Vg[:,2].max():+.4f}] (unit 1cm)")

for o, path in ((body, OUT_MACHINE), (grip, OUT_GRIP), (needle, OUT_NEEDLE)):
    bpy.ops.object.select_all(action='DESELECT')
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    bpy.ops.export_scene.fbx(
        filepath=path,
        use_selection=True,
        object_types={'MESH'},
        apply_unit_scale=True,
        apply_scale_options='FBX_SCALE_NONE',
        path_mode='STRIP',
        use_mesh_modifiers=False,
        add_leaf_bones=False,
    )
    print("EXPORTED", path)
print("BUILD_DONE")
