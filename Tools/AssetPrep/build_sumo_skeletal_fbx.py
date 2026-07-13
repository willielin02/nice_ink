# Sumo 骨骼網格 FBX 匯出（一副 SK 撐全場：站立本體/PoseableMesh 彎腰/ragdoll 睡姿共用）
#
# 與 char17 匯出腳本的關鍵差異：**絕不重展 UV0**。
# master 的 UVMap 是凍結版面——所有手繪遮罩（fundoshi_mask 手繪正源、hair/face mask、
# edge shadow、nodraw 合成、眼罩 UV 對應表）全部錨在當前版面上；重展=全部作廢。
# 這裡只做密度量測（供 MarkerUvRadius 重算），輸出到 stdout。
#
# Run: blender --background --python build_sumo_skeletal_fbx.py
import bpy
import math

MASTER = r"C:\games\Unreal Engine\nice_ink\SourceAssets\sumo_character_master.blend"
OUT_FBX = r"C:\games\Unreal Engine\nice_ink\SourceAssets\sumo_skeletal.fbx"

bpy.ops.wm.open_mainfile(filepath=MASTER)
if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')

body = bpy.data.objects["SumoRetopo"]
arm = bpy.data.objects["Skeleton_Plus-size"]
me = body.data

# 全場景匯出防呆：master 只允許身體＋褌兩個 MESH（雜物會被 combine 進 SK/SM）
_meshes = sorted(o.name for o in bpy.data.objects if o.type == 'MESH')
assert _meshes == ["Fundoshi", "SumoRetopo"], f"unexpected meshes in master: {_meshes}"

# --- 褌斷言：權重/綁定/UV/材質槽名（槽名=UE 匯入腳本與 C++ section 判定的契約）---
fundoshi = bpy.data.objects["Fundoshi"]
assert any(m.type == 'ARMATURE' for m in fundoshi.modifiers), "fundoshi armature modifier missing"
assert fundoshi.parent == bpy.data.objects["Skeleton_Plus-size"], "fundoshi not parented to armature"
assert "UVMap" in [l.name for l in fundoshi.data.uv_layers], "fundoshi UVMap missing"
assert [m.name for m in fundoshi.data.materials] == ["M_Fundoshi"], \
    f"fundoshi material slots: {[m.name for m in fundoshi.data.materials]}"
assert all(v.groups for v in fundoshi.data.vertices), "fundoshi has unweighted vertices"

# --- 前置斷言：三 UV 通道、FaceMask 頂點色、綁定 ---
assert set(l.name for l in me.uv_layers) >= {"UVMap", "FaceUV", "HairUV"}, \
    f"expected UVMap/FaceUV/HairUV, got {[l.name for l in me.uv_layers]}"
assert me.color_attributes.get("FaceMask") is not None, "FaceMask color attribute missing"
assert any(m.type == 'ARMATURE' for m in body.modifiers), "armature modifier missing"
assert body.parent == arm, "body not parented to armature"

# --- UV0 密度量測（不改 UV！只量現況）→ MarkerUvRadius ---
me.calc_loop_triangles()
uv0 = me.uv_layers["UVMap"].data
import numpy as np
areas_uv, areas_w = [], []
mw = body.matrix_world
for tri in me.loop_triangles:
    uvs = [uv0[l].uv for l in tri.loops]
    a_uv = abs((uvs[1][0] - uvs[0][0]) * (uvs[2][1] - uvs[0][1])
               - (uvs[2][0] - uvs[0][0]) * (uvs[1][1] - uvs[0][1])) / 2
    vs = [mw @ me.vertices[me.loops[l].vertex_index].co for l in tri.loops]
    a_w = ((vs[1] - vs[0]).cross(vs[2] - vs[0])).length / 2
    if a_w > 1e-12:
        areas_uv.append(a_uv)
        areas_w.append(a_w)
areas_uv = np.array(areas_uv)
areas_w = np.array(areas_w)
dens = np.sqrt(areas_uv / areas_w) * 2048.0 / 1000.0   # px/mm @2048
w = areas_w / areas_w.sum()
mean_d = float((dens * w).sum())
PEN_RADIUS_MM = 1.938   # char17 定案筆半徑（0.00085 × 2048 / 0.898）
uv_radius = mean_d / 2048.0 * PEN_RADIUS_MM
print(f"UV0 density @2048: mean {mean_d:.3f} px/mm  p10 {np.percentile(dens, 10):.3f} "
      f"p90 {np.percentile(dens, 90):.3f}")
print(f"MARKER_UV_RADIUS = {uv_radius:.6f}   (pen radius {PEN_RADIUS_MM}mm)")

# --- 材質槽合併到 slot 0（單一 section → UE 單一 MID）---
if len(body.material_slots) > 1:
    mi = np.zeros(len(me.polygons), np.int32)
    me.polygons.foreach_set("material_index", mi)
    bpy.context.view_layer.objects.active = body
    body.select_set(True)
    with bpy.context.temp_override(object=body, active_object=body):
        while len(body.material_slots) > 1:
            body.active_material_index = len(body.material_slots) - 1
            bpy.ops.object.material_slot_remove()
    print("material slots merged -> 1")

# --- 匯出（整場景：armature + mesh；不烘修改器=保綁定）---
for o in bpy.context.view_layer.objects:
    o.select_set(True)
bpy.ops.export_scene.fbx(
    filepath=OUT_FBX,
    use_selection=False,
    object_types={'ARMATURE', 'MESH'},
    apply_unit_scale=True,
    apply_scale_options='FBX_SCALE_NONE',
    path_mode='STRIP',
    use_mesh_modifiers=False,
    add_leaf_bones=False,
    colors_type='SRGB',
    armature_nodetype='NULL',
    use_armature_deform_only=False,
)
print("EXPORTED", OUT_FBX)
