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


def _seam_pairs(me, tol=1e-4):
    """切縫兩側同位頂點對（頭殼/身殼各一）——依座標配對"""
    from collections import defaultdict
    buckets = defaultdict(list)
    for v in me.vertices:
        key = (round(v.co.x / tol), round(v.co.y / tol), round(v.co.z / tol))
        buckets[key].append(v.index)
    return [tuple(b) for b in buckets.values() if len(b) == 2]


def _vertex_normal_from_loops(me, vi, loops_of_vert):
    import mathutils
    n = mathutils.Vector((0, 0, 0))
    for li in loops_of_vert[vi]:
        n += me.corner_normals[li].vector
    return n.normalized() if n.length > 0 else n


def _seam_normal_gap_deg(me):
    import math
    loops_of_vert = {}
    for poly in me.polygons:
        for li in poly.loop_indices:
            loops_of_vert.setdefault(me.loops[li].vertex_index, []).append(li)
    pairs = _seam_pairs(me)
    if not pairs:
        return 0.0, 0.0, 0
    worst = 0.0
    total = 0.0
    for a, b in pairs:
        na = _vertex_normal_from_loops(me, a, loops_of_vert)
        nb = _vertex_normal_from_loops(me, b, loops_of_vert)
        d = max(-1.0, min(1.0, na.dot(nb)))
        deg = math.degrees(math.acos(d))
        worst = max(worst, deg)
        total += deg
    return worst, total / len(pairs), len(pairs)


def bake_soft_normals(name, weld_seam):
    ob = bpy.data.objects[name]
    # 平滑副本（只當法線來源，用完即刪）
    src = ob.copy()
    src.data = ob.data.copy()
    src.name = name + "_NormalSrc"
    bpy.context.collection.objects.link(src)
    bpy.context.view_layer.objects.active = src
    # 副本先清舊 custom normals（否則 SMOOTH 動了頂點、儲存的 split normals 卻不重算＝轉印陳舊值）
    if src.data.has_custom_normals:
        with bpy.context.temp_override(object=src, active_object=src):
            bpy.ops.mesh.customdata_custom_splitnormals_clear()
    if weld_seam:
        # 08-16 站立/作畫縫隙真兇修：08-05 首版對「已切開」的網格平滑——頭殼/身殼各自是
        # 開放邊界，Laplacian 把兩側邊環各自往內捲＝轉印回去的縫兩側法線分家（引擎渲染
        # 緩衝實測 mean 55.8° / max 100°；rest 姿即現鋸齒亮暗跳階；甦醒者兩端相隔 46cm
        # 才沒被看見）。修＝平滑來源先把縫兩側同位頂點焊回一體（只影響來源副本；
        # 正本拓樸/UV/權重零改動），縫上法線由此連續。
        import bmesh
        bm = bmesh.new()
        bm.from_mesh(src.data)
        nv0 = len(bm.verts)
        bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-4)
        nv1 = len(bm.verts)
        bm.to_mesh(src.data)
        bm.free()
        src.data.update()
        print(f"{name}: weld for smoothing source {nv0} -> {nv1} verts (merged {nv0 - nv1})")
        assert len(src.data.loops) == len(ob.data.loops), "weld changed loop count (TOPOLOGY mapping invalid)"
        assert len(src.data.polygons) == len(ob.data.polygons), "weld changed polygon count"
    sm = src.modifiers.new("Soften", 'SMOOTH')
    sm.iterations = SMOOTH_ITER
    sm.factor = SMOOTH_FACTOR
    # 平滑副本要先烘掉修飾器（DataTransfer 吃 evaluated mesh——保險起見直接 apply）
    with bpy.context.temp_override(object=src, active_object=src):
        bpy.ops.object.modifier_apply(modifier=sm.name)
    # 法線轉印：src 的 loop normals → ob 的 custom split normals
    dt = ob.modifiers.new("NormalXfer", 'DATA_TRANSFER')
    dt.object = src
    dt.use_loop_data = True
    dt.data_types_loops = {'CUSTOM_NORMAL'}
    dt.loop_mapping = 'TOPOLOGY'  # 焊接不改面/loop 序＝逐 loop 精確對應
    bpy.context.view_layer.objects.active = ob
    with bpy.context.temp_override(object=ob, active_object=ob):
        bpy.ops.object.modifier_apply(modifier=dt.name)
    bpy.data.objects.remove(src, do_unlink=True)
    assert ob.data.has_custom_normals, f"{name}: normal transfer failed"
    print(f"{name}: soft normals baked (iter={SMOOTH_ITER} factor={SMOOTH_FACTOR} weld={weld_seam})")
    if weld_seam:
        worst, mean, n = _seam_normal_gap_deg(ob.data)
        print(f"{name}: seam pairs={n} normal gap max={worst:.2f}deg mean={mean:.2f}deg")
        assert n >= 80, f"{name}: seam pair count unexpected {n}"
        assert worst < 3.0, f"{name}: seam normals still discontinuous ({worst:.1f}deg)"


bake_soft_normals("SumoRetopo", weld_seam=True)
bake_soft_normals("Fundoshi", weld_seam=False)

# 防呆：armature 綁定/頂點數不得變
body = bpy.data.objects["SumoRetopo"]
assert any(m.type == 'ARMATURE' for m in body.modifiers), "armature modifier lost"
print("verts:", len(body.data.vertices))

bpy.ops.wm.save_mainfile()
print("SOFT NORMALS DONE (master saved)")
