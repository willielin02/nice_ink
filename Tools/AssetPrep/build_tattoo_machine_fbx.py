# 刺青機資產處理（直接畫制・伸縮針制 2026-07-21）：Meshy 生成 FBX →
# 拆針（握管底部細尖）→ 分區上材質槽（Frame 黑鐵/Coils 銅/Grip 鋼/Brass 銅金小件）→
# 出針口置原點、+Z=出針口→機頂（離皮膚向）、機身實尺寸 20cm →
# sumo_tattoo_machine.fbx（機身）＋ sumo_tattoo_needle.fbx（針，基座在原點、
# 沿 -Z 長 1cm＝UE 端 Z scale=針長 cm）。幾何+材質槽 only；顏色引擎端 MID 上。
# Run: blender --background --python build_tattoo_machine_fbx.py
import bpy
import numpy as np
from mathutils import Matrix, Vector

SRC_FBX = r"C:\Users\willi\Downloads\Meshy_AI_Coil_Tattoo_Machine_0721125609_generate.fbx"
OUT_MACHINE = r"C:\games\Unreal Engine\nice_ink\SourceAssets\sumo_tattoo_machine.fbx"
OUT_NEEDLE = r"C:\games\Unreal Engine\nice_ink\SourceAssets\sumo_tattoo_needle.fbx"
PREVIEW_DIR = r"C:\Users\willi\AppData\Local\Temp\claude\c--games-Unreal-Engine-nice-ink\e71707dc-7c0e-4058-9640-c456ee06b377\scratchpad"
MACHINE_LEN_M = 0.60   # 出針口→機頂 60cm（07-21 user 三調：20→28→60，派對巨械尺度）
NEEDLE_UNIT_M = 0.01   # 針資產長度歸一 1cm（UE 端 scale.Z = 針長 cm）
RENDER_PREVIEW = True

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=SRC_FBX)

meshes = [o for o in bpy.data.objects if o.type == 'MESH']
assert len(meshes) == 1, f"expected 1 mesh, got {len(meshes)}"
obj = meshes[0]

# 攤平世界變換進頂點（Meshy 層級/單位全吃掉；此後全在攤平座標工作）
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
print(f"[probe] flattened bbox: x[{V[:,0].min():.3f},{V[:,0].max():.3f}] "
      f"y[{V[:,1].min():.3f},{V[:,1].max():.3f}] z[{V[:,2].min():.3f},{V[:,2].max():.3f}]")
# 慣例校驗：長軸必須已是 Z、針尖在 -Z 端（渲染確認過）；不是就直接炸
spans = V.max(0) - V.min(0)
assert spans[2] == spans.max(), f"long axis is not Z: spans={spans}"

# --- 分離鬆散件：大件=骨架+線圈（上）、中件=握管+針（下）、小件=接管 ---
bpy.context.view_layer.objects.active = obj
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.separate(type='LOOSE')
bpy.ops.object.mode_set(mode='OBJECT')
parts = sorted([o for o in bpy.data.objects if o.type == 'MESH'],
               key=lambda p: -len(p.data.vertices))
assert len(parts) == 3, f"expected 3 loose parts, got {len(parts)}"
upper, gripneedle, connector = parts
print(f"[probe] upper={len(upper.data.vertices)}v grip={len(gripneedle.data.vertices)}v "
      f"conn={len(connector.data.vertices)}v")

# --- 針的偵測與分離：握管件由下而上掃 z 切片，半徑突增處＝出針口 ---
Vg = varr(gripneedle)
gz0, gz1 = Vg[:, 2].min(), Vg[:, 2].max()
# 握管軸心＝握管上半的 xy 中值（針可能歪，避開下端）
axis_xy = np.median(Vg[Vg[:, 2] > (gz0 + gz1) / 2][:, :2], axis=0)
r_all = np.linalg.norm(Vg[:, :2] - axis_xy, axis=1)
nslice = 40
z_edges = np.linspace(gz0, gz1, nslice + 1)
slice_rmax = []
for i in range(nslice):
    m = (Vg[:, 2] >= z_edges[i]) & (Vg[:, 2] <= z_edges[i + 1])
    slice_rmax.append(r_all[m].max() if m.any() else 0.0)
    print(f"[probe] grip z-slice {z_edges[i]:+.3f}..{z_edges[i+1]:+.3f} rmax={slice_rmax[-1]:.4f} n={m.sum()}")
# 出針口＝-0.86（探測定值：針尖錐段 z<-0.86、基座半徑~0.045u；其上為握管針管錐）
z_exit = -0.86
needle_len = z_exit - gz0
print(f"[probe] z_exit={z_exit:.3f} needle_len={needle_len:.3f}")

# 面級分離：整面頂點都在 z_exit 之下＝針
gripneedle.data.update()
needle_faces = [p.index for p in gripneedle.data.polygons
                if all(gripneedle.data.vertices[vi].co.z < z_exit + 1e-4 for vi in p.vertices)]
bpy.ops.object.select_all(action='DESELECT')
gripneedle.select_set(True)
bpy.context.view_layer.objects.active = gripneedle
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.select_all(action='DESELECT')
bpy.ops.object.mode_set(mode='OBJECT')
for fi in needle_faces:
    gripneedle.data.polygons[fi].select = True
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.select_mode(type='FACE')
bpy.ops.mesh.separate(type='SELECTED')
bpy.ops.object.mode_set(mode='OBJECT')
new_objs = [o for o in bpy.data.objects if o.type == 'MESH' and o not in (upper, gripneedle, connector)]
assert len(new_objs) == 1, f"needle separate failed: {len(new_objs)}"
needle = new_objs[0]
grip = gripneedle
print(f"[probe] needle={len(needle.data.vertices)}v grip(after)={len(grip.data.vertices)}v")

# --- 線圈偵測（上件）：線圈 z 帶內 xy 2-means → 兩軸心；命中半徑內＝Coils ---
Vu = varr(upper)
uz0, uz1 = Vu[:, 2].min(), Vu[:, 2].max()
# 線圈=大直徑圓柱體：以「xy 距上件中心的半徑」直方圖無從辨識——改掃 z 帶密度：
# 對每個 z 帶做 xy 2-means，分群緊致（群內 rms 小、兩心距穩定）＝線圈帶。先印給人看。
def two_means(P, iters=20):
    c0, c1 = P[P[:, 0].argmin()], P[P[:, 0].argmax()]
    for _ in range(iters):
        d0 = np.linalg.norm(P - c0, axis=1)
        d1 = np.linalg.norm(P - c1, axis=1)
        m = d0 < d1
        if m.sum() == 0 or (~m).sum() == 0:
            break
        c0, c1 = P[m].mean(0), P[~m].mean(0)
    return c0, c1, m

# 線圈軸心：乾淨罐壁帶 z∈[0.034,0.136]（避開骨架後壁/前臂夾具污染）
mband = (Vu[:, 2] >= 0.034) & (Vu[:, 2] <= 0.136)
cA, cB, _ = two_means(Vu[mband][:, :2])
print(f"[probe] coil centers: A=({cA[0]:+.3f},{cA[1]:+.3f}) B=({cB[0]:+.3f},{cB[1]:+.3f})")

COIL_R = 0.23          # 線圈命中半徑（罐壁 r_med~0.20；0.26 會吃到骨架後壁）
COIL_Z = (0.10, 0.40)  # 線圈 z 帶（下界 0.10 避開前臂夾具；罐底/頂環落黑＝墊圈讀感）
FRAME_X = -0.45        # 此線之外（-x）＝骨架板專區，線圈不得越界
BRASS_Z = 0.60         # 其上＝銜鐵/接線柱（銅金件）

# --- 材質槽指派（在原始攤平座標系分類；之後才做座標定準）---
mat_frame = bpy.data.materials.new("TattooFrame")
mat_coils = bpy.data.materials.new("TattooCoils")
mat_grip = bpy.data.materials.new("TattooGrip")
mat_brass = bpy.data.materials.new("TattooBrass")
mat_needle = bpy.data.materials.new("TattooNeedle")
# Blender 預覽色（≈引擎端 MID 目標色；引擎端另上）
for m, col in ((mat_frame, (0.035, 0.035, 0.045)), (mat_coils, (0.42, 0.16, 0.06)),
               (mat_grip, (0.34, 0.36, 0.39)), (mat_brass, (0.50, 0.34, 0.10)),
               (mat_needle, (0.62, 0.65, 0.70))):
    m.use_nodes = True
    m.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (*col, 1.0)
    m.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.55

def classify_upper(fc):
    # fc = 面中心 (Vector)
    if fc.z > BRASS_Z:
        return 3  # brass
    if COIL_Z[0] <= fc.z <= COIL_Z[1] and fc.x > FRAME_X:
        for c in (cA, cB):
            if (Vector((fc.x - c[0], fc.y - c[1], 0.0))).length <= COIL_R:
                return 1  # coils
    return 0  # frame

upper.data.materials.append(mat_frame)   # slot0
upper.data.materials.append(mat_coils)   # slot1
upper.data.materials.append(mat_grip)    # slot2（機身統一槽序）
upper.data.materials.append(mat_brass)   # slot3
for p in upper.data.polygons:
    p.material_index = classify_upper(Vector(p.center))
for o, idx in ((grip, 2), (connector, 3)):
    o.data.materials.append(mat_frame)
    o.data.materials.append(mat_coils)
    o.data.materials.append(mat_grip)
    o.data.materials.append(mat_brass)
    for p in o.data.polygons:
        p.material_index = idx
needle.data.materials.append(mat_needle)

counts = [0, 0, 0, 0]
for p in upper.data.polygons:
    counts[p.material_index] += 1
print(f"[build] upper faces frame={counts[0]} coils={counts[1]} grip={counts[2]} brass={counts[3]}")

# --- 座標定準：骨架板在 -X 側 → 繞 Z 轉 -90°（骨架朝 +Y）；出針口置原點；實尺寸 ---
Vn = varr(needle)
base_ring = Vn[Vn[:, 2] > Vn[:, 2].max() - 0.02]
base_xy = base_ring[:, :2].mean(axis=0)
origin = Vector((base_xy[0], base_xy[1], z_exit))
print(f"[build] needle base xy=({base_xy[0]:+.4f},{base_xy[1]:+.4f}) origin={tuple(round(v,4) for v in origin)}")

rotz = Matrix.Rotation(np.deg2rad(-90.0), 4, 'Z')
scale_u = MACHINE_LEN_M / (uz1 - z_exit)
print(f"[build] machine span={uz1 - z_exit:.3f}u scale={scale_u:.5f} m/u "
      f"-> machine {MACHINE_LEN_M*100:.0f}cm, coil_r={0.21*scale_u*100:.1f}cm, "
      f"needle natural={needle_len*scale_u*100:.2f}cm")
xform = Matrix.Scale(scale_u, 4) @ rotz @ Matrix.Translation(-origin)
for o in (upper, grip, connector, needle):
    o.data.transform(xform)

# 針長歸一：z 從 [-natural,0] → [-NEEDLE_UNIT_M,0]（xy 保持機身尺度；UE scale.Z=針長cm）
Vn = varr(needle)
natural = -Vn[:, 2].min()
zscale = NEEDLE_UNIT_M / natural
mz = Matrix.Identity(4)
mz[2][2] = zscale
needle.data.transform(mz)
print(f"[build] needle normalized: natural={natural*100:.2f}cm -> {NEEDLE_UNIT_M*100:.0f}cm/unit, "
      f"base_r={np.linalg.norm(varr(needle)[varr(needle)[:,2]>-0.001][:,:2],axis=1).max()*100:.2f}cm")

# --- 合併機身三件 → TattooMachine；針獨立 → TattooNeedle ---
bpy.ops.object.select_all(action='DESELECT')
for o in (upper, grip, connector):
    o.select_set(True)
bpy.context.view_layer.objects.active = upper
bpy.ops.object.join()
machine = upper
machine.name = "TattooMachine"
needle.name = "TattooNeedle"

# Smart UV（引擎 lightmap/未來貼圖升級保險；平色 MID 不讀 UV）
for o in (machine, needle):
    bpy.ops.object.select_all(action='DESELECT')
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=np.deg2rad(66.0), island_margin=0.02)
    bpy.ops.object.mode_set(mode='OBJECT')

Vm = varr(machine)
print(f"[build] machine final bbox(m): x[{Vm[:,0].min():+.3f},{Vm[:,0].max():+.3f}] "
      f"y[{Vm[:,1].min():+.3f},{Vm[:,1].max():+.3f}] z[{Vm[:,2].min():+.3f},{Vm[:,2].max():+.3f}]")

# --- 彩色預覽渲染（分區自查）---
if RENDER_PREVIEW:
    import math
    scene = bpy.context.scene
    try:
        scene.render.engine = 'BLENDER_EEVEE_NEXT'
    except Exception:
        pass
    scene.render.resolution_x = 720
    scene.render.resolution_y = 720
    w = bpy.data.worlds.new("W")
    w.use_nodes = True
    w.node_tree.nodes["Background"].inputs[0].default_value = (0.85, 0.85, 0.85, 1)
    w.node_tree.nodes["Background"].inputs[1].default_value = 1.1
    scene.world = w
    sun = bpy.data.objects.new("Sun", bpy.data.lights.new("Sun", 'SUN'))
    sun.data.energy = 3.5
    sun.rotation_euler = (math.radians(50), 0, math.radians(30))
    bpy.context.collection.objects.link(sun)
    cam = bpy.data.objects.new("Cam", bpy.data.cameras.new("Cam"))
    bpy.context.collection.objects.link(cam)
    scene.camera = cam
    center = Vector((0, 0.02, MACHINE_LEN_M * 0.45))
    for name, off in (("colored_front", Vector((0, -0.5, 0.1))),
                      ("colored_frame", Vector((0.35, 0.35, 0.12))),
                      ("colored_persp", Vector((0.38, -0.28, 0.2)))):
        cam.location = center + off
        cam.rotation_euler = (center - cam.location).to_track_quat('-Z', 'Y').to_euler()
        scene.render.filepath = PREVIEW_DIR + "\\tatmachine_" + name + ".png"
        bpy.ops.render.render(write_still=True)
    print("[build] previews rendered")

# --- 匯出（機身與針各自置原點；比照 build_marker_fbx 慣例）---
for o, path in ((machine, OUT_MACHINE), (needle, OUT_NEEDLE)):
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
