# 清除 SumoRetopo/Fundoshi 的 custom split normals（2026-08-05 頭燈斷層終案）
#
# 定罪鏈（全程儀器實錘）：user 抓「左胸陰影斷層/乳暈硬環」→ A/B 探針（關頭燈
# 消失=著色層病；雕像無環=資產分歧）→ Interchange 匯入資料實錘 FbxImportUI 選項
# 被忽略 → master 審計=has_custom_normals=True、零銳邊、全 smooth face＝
# **掃描時代殘留的自訂 split normals**。SK（Interchange 骨骼）忠實沿用→頭燈
# 假光把法線摺痕放大成硬環；SM（靜態管線）匯入重算平滑→user 驗收慣的乾淨版。
# 07-14 去蠟拆的是材質假細節，網格層的這批一直沒被看到（雕像代為重算掉了）。
#
# 手術：兩網格 customdata_custom_splitnormals_clear（shade smooth 已全開＝
# 回歸標準平滑頂點法線＝與雕像同款讀感）。之後照舊 build_sumo_skeletal_fbx →
# SK 重匯入。
# Run: blender --background --python sumo_clear_custom_normals.py
import bpy

MASTER = r"C:\games\Unreal Engine\nice_ink\SourceAssets\sumo_character_master.blend"

bpy.ops.wm.open_mainfile(filepath=MASTER)
if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')

for name in ("SumoRetopo", "Fundoshi"):
    ob = bpy.data.objects[name]
    assert ob.data.has_custom_normals, f"{name}: no custom normals to clear (already done?)"
    bpy.context.view_layer.objects.active = ob
    with bpy.context.temp_override(object=ob, active_object=ob):
        bpy.ops.mesh.customdata_custom_splitnormals_clear()
    print(f"{name}: cleared -> has_custom_normals={ob.data.has_custom_normals}")
    assert not ob.data.has_custom_normals, f"{name}: clear failed"
    # smooth face 保持全開（審計=11802/11802；不動）
    assert all(p.use_smooth for p in ob.data.polygons), f"{name}: smooth faces broken"

bpy.ops.wm.save_mainfile()
print("CLEAR NORMALS DONE (master saved)")
