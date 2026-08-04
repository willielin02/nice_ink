# 柔化法線轉印（2026-08-05 頭燈斷層終案二段）
#
# 定罪鏈終局：乳暈環/胸斷層＝**真幾何**（幾何探針：乳尖 +13mm、乳暈溝 -8mm＝
# 1.5cm 內 2cm 落差）；SK（骨骼）忠實渲染、SM（靜態管線）匯入時把法線抹軟＝
# user 數週驗收慣的讀感。頭燈假光的 pow 曲線會把陡峭法線擺動放大成「陰影斷層」。
# 修法＝把「平滑副本的法線」轉印回原網格當 custom split normals：幾何輪廓一頂點
# 不動、只軟化著色——與雕像同款讀感、在骨骼身體上確定性重現。
#
# 強度旋鈕：SMOOTH_ITER / SMOOTH_FACTOR（大=更軟；視覺對齊雕像後定案）。
# Run: blender --background --python sumo_soft_normals_bake.py
# 之後照舊：build_sumo_skeletal_fbx.py → SK 重匯入 → robo_headlight_probe A/B。
import bpy

MASTER = r"C:\games\Unreal Engine\nice_ink\SourceAssets\sumo_character_master.blend"
SMOOTH_ITER = 12
SMOOTH_FACTOR = 0.5

bpy.ops.wm.open_mainfile(filepath=MASTER)
if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')


def bake_soft_normals(name):
    ob = bpy.data.objects[name]
    # 平滑副本（只當法線來源，用完即刪）
    src = ob.copy()
    src.data = ob.data.copy()
    src.name = name + "_NormalSrc"
    bpy.context.collection.objects.link(src)
    sm = src.modifiers.new("Soften", 'SMOOTH')
    sm.iterations = SMOOTH_ITER
    sm.factor = SMOOTH_FACTOR
    # 平滑副本要先烘掉修飾器（DataTransfer 吃 evaluated mesh——保險起見直接 apply）
    bpy.context.view_layer.objects.active = src
    with bpy.context.temp_override(object=src, active_object=src):
        bpy.ops.object.modifier_apply(modifier=sm.name)
    # 法線轉印：src 的 loop normals → ob 的 custom split normals
    dt = ob.modifiers.new("NormalXfer", 'DATA_TRANSFER')
    dt.object = src
    dt.use_loop_data = True
    dt.data_types_loops = {'CUSTOM_NORMAL'}
    dt.loop_mapping = 'TOPOLOGY'  # 平滑副本=同拓樸＝逐 loop 精確對應
    bpy.context.view_layer.objects.active = ob
    with bpy.context.temp_override(object=ob, active_object=ob):
        bpy.ops.object.modifier_apply(modifier=dt.name)
    bpy.data.objects.remove(src, do_unlink=True)
    assert ob.data.has_custom_normals, f"{name}: normal transfer failed"
    print(f"{name}: soft normals baked (iter={SMOOTH_ITER} factor={SMOOTH_FACTOR})")


for n in ("SumoRetopo", "Fundoshi"):
    bake_soft_normals(n)

# 防呆：armature 綁定/頂點數不得變
body = bpy.data.objects["SumoRetopo"]
assert any(m.type == 'ARMATURE' for m in body.modifiers), "armature modifier lost"
print("verts:", len(body.data.vertices))

bpy.ops.wm.save_mainfile()
print("SOFT NORMALS DONE (master saved)")
