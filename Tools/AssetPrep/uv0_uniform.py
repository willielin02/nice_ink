# UV0（墨水圖集）均勻紋素密度重排——SPEC v3.1「全身筆跡嚴格一致」的地基。
# Smart UV Project（角度 66°）→ Average Islands Scale（各島同密度）→ Pack。
# FaceUV（臉貼圖通道）與 FaceMask 頂點色不受影響。
#
# 所有導出腳本（站姿/睡姿/骨骼版）與 export_ink_uv_map.py 都必須先呼叫本函式，
# 確保四個消費者用同一套 UV0（對相同輸入網格，操作皆確定性）。
import bpy, math


def reunwrap_uv0_uniform(body, atlas_px=2048):
    mesh = body.data
    mesh.uv_layers.active = mesh.uv_layers["UVMap"]

    for o in bpy.context.view_layer.objects:
        o.select_set(False)
    body.select_set(True)
    bpy.context.view_layer.objects.active = body
    bpy.context.scene.tool_settings.use_uv_select_sync = True

    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.reveal()
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(66.0), island_margin=0.003,
                             correct_aspect=True, scale_to_bounds=False)
    bpy.ops.uv.average_islands_scale()
    bpy.ops.uv.pack_islands(margin=0.004)
    bpy.ops.object.mode_set(mode='OBJECT')

    # 密度驗證：面積加權平均與 p10/p90 應緊貼（同時作為跨腳本確定性指紋）
    uv_layer = mesh.uv_layers["UVMap"]
    mesh.calc_loop_triangles()
    items = []
    for tri in mesh.loop_triangles:
        a3 = tri.area
        if a3 <= 1e-12:
            continue
        uvs = [uv_layer.data[l].uv for l in tri.loops]
        auv = abs((uvs[1][0]-uvs[0][0])*(uvs[2][1]-uvs[0][1]) -
                  (uvs[2][0]-uvs[0][0])*(uvs[1][1]-uvs[0][1])) * 0.5
        if auv <= 1e-14:
            continue
        items.append((math.sqrt(auv / a3) * atlas_px / 1000.0, a3))
    total = sum(a for _, a in items)
    mean = sum(d*a for d, a in items) / total
    ds = sorted(d for d, _ in items)
    print(f"UV0-UNIFORM density @: {atlas_px}px  mean {mean:.3f} px/mm  "
          f"p10 {ds[len(ds)//10]:.3f}  p90 {ds[9*len(ds)//10]:.3f}  "
          f"min {ds[0]:.3f}  max {ds[-1]:.3f}  tris {len(ds)}")
    return mean
