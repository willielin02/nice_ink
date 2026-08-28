# Radiola 電視資產轉換（2026-08-28）：SourceAssets/Television_Sirenko/*.glb → tv_radiola.fbx
# 資產＝"Radiola from Matrix" by Sirenko（CC-BY-4.0、可商用需署名；ATTRIBUTION.txt 同資料夾）。
#
# 轉換內容：
#   1. 全部件 join 成單一網格（3,432 面＝遊戲道具級）
#   2. 轉 +90°Z：glb 的正面朝 −Y → 專案電視慣例「螢幕朝 actor +X」（TvSet 以 yaw90 spawn）
#   3. 縮放 0.5：glb 是 2 單位正規化盒 → 實物尺寸 75W×78D×100H cm（1950s 落地電視）
#   4. 原點＝底面中心（腳貼地）；印出 Screen_LowB 玻璃包圍盒＝C++ RT 面板的貼放座標
#   5. 貼圖 path_mode='COPY'+embed → UE FbxImport 直接抽出建材質
#
# Run: blender --background --python tv_radiola_prep.py
import bpy
import os

SRC = r"C:\games\Unreal Engine\nice_ink\SourceAssets\Television_Sirenko\radiola_from_matrix.glb"
OUT_FBX = r"C:\games\Unreal Engine\nice_ink\SourceAssets\tv_radiola.fbx"

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=SRC)

# 找玻璃群組（join 之前先記名下的頂點，join 後用座標窗仍可對賬）
screen_pts = []
for o in bpy.data.objects:
    if o.type != 'MESH':
        continue
    top = o
    while top.parent and top.parent.type == 'EMPTY' and top.parent.name != 'Collada visual scene group':
        top = top.parent
    if top.name == 'Screen_LowB':
        screen_pts = [o.matrix_world @ v.co for v in o.data.vertices]
assert screen_pts, 'Screen_LowB not found'

# 玻璃島拆槽＋UV 重寫（2026-08-28 user 抓「把框也畫成螢幕」）：
# Screen_LowB 的深度分布實測——**內凹的才是玻璃**（cy>-0.665：106 面、
# x±0.525 z -0.055..0.839），外圈凸出的是暗色邊框。把玻璃面指定給新材質槽
# 'TvGlass'、UV 歸一化到島的包圍盒（U=+X、V=−Z＝觀眾視角不鏡像）＝
# RT 節目直接畫在圓角玻璃上，圓角由網格邊界免費提供。
for o in bpy.data.objects:
    if o.type != 'MESH':
        continue
    top = o
    while top.parent and top.parent.type == 'EMPTY' and top.parent.name != 'Collada visual scene group':
        top = top.parent
    if top.name != 'Screen_LowB':
        continue
    me = o.data
    glass_mat = bpy.data.materials.new('TvGlass')
    me.materials.append(glass_mat)
    glass_slot = len(me.materials) - 1
    glass_polys = []
    for pl in me.polygons:
        ws = [o.matrix_world @ me.vertices[v].co for v in pl.vertices]
        cy = sum(w.y for w in ws) / len(ws)
        if cy > -0.665:
            glass_polys.append(pl.index)
    pts = []
    for pi in glass_polys:
        for v in me.polygons[pi].vertices:
            pts.append(o.matrix_world @ me.vertices[v].co)
    gxlo = min(c.x for c in pts); gxhi = max(c.x for c in pts)
    gzlo = min(c.z for c in pts); gzhi = max(c.z for c in pts)
    uv = me.uv_layers.active.data
    for pi in glass_polys:
        pl = me.polygons[pi]
        pl.material_index = glass_slot
        for li, vi in zip(pl.loop_indices, pl.vertices):
            w = o.matrix_world @ me.vertices[vi].co
            uv[li].uv = ((w.x - gxlo) / (gxhi - gxlo), (w.z - gzlo) / (gzhi - gzlo))
    print("GLASS island: polys=%d  x(%.3f..%.3f) z(%.3f..%.3f)  size=%.1fx%.1fcm"
          % (len(glass_polys), gxlo, gxhi, gzlo, gzhi,
             (gxhi - gxlo) * 50, (gzhi - gzlo) * 50))

# join 全部 mesh
meshes = [o for o in bpy.data.objects if o.type == 'MESH']
bpy.ops.object.select_all(action='DESELECT')
for o in meshes:
    o.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
bpy.ops.object.join()
tv = bpy.context.view_layer.objects.active
tv.name = 'TvRadiola'
tv.parent = None
tv.matrix_world = tv.matrix_world  # keep world

# 烘進世界變換 → 再統一施旋轉/縮放/平移
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

import math
from mathutils import Matrix, Vector
XFORM = Matrix.Rotation(math.radians(90.0), 4, 'Z')  # 正面 −Y → +X
SCALE = 0.5
tv.data.transform(Matrix.Scale(SCALE, 4) @ XFORM)

# 底面貼地、xy 置中
xs = [v.co for v in tv.data.vertices]
lo = Vector((min(c[i] for c in xs) for i in range(3)))
hi = Vector((max(c[i] for c in xs) for i in range(3)))
off = Vector((-(lo.x + hi.x) * 0.5, -(lo.y + hi.y) * 0.5, -lo.z))
tv.data.transform(Matrix.Translation(off))
xs = [v.co for v in tv.data.vertices]
lo = Vector((min(c[i] for c in xs) for i in range(3)))
hi = Vector((max(c[i] for c in xs) for i in range(3)))
print("TV final bbox cm: min=(%.1f,%.1f,%.1f) max=(%.1f,%.1f,%.1f)"
      % (lo.x * 100, lo.y * 100, lo.z * 100, hi.x * 100, hi.y * 100, hi.z * 100))

# 玻璃包圍盒經同一變換 → C++ 貼放座標
sp = [Matrix.Translation(off) @ (Matrix.Scale(SCALE, 4) @ XFORM) @ p for p in screen_pts]
slo = Vector((min(c[i] for c in sp) for i in range(3)))
shi = Vector((max(c[i] for c in sp) for i in range(3)))
print("SCREEN glass bbox cm: min=(%.1f,%.1f,%.1f) max=(%.1f,%.1f,%.1f)"
      % (slo.x * 100, slo.y * 100, slo.z * 100, shi.x * 100, shi.y * 100, shi.z * 100))
print("SCREEN quad → RelLoc=(%.1f, %.1f, %.1f)  size=(%.1f x %.1f)"
      % (shi.x * 100 + 0.6, (slo.y + shi.y) * 0.5 * 100, (slo.z + shi.z) * 0.5 * 100,
         (shi.y - slo.y) * 100, (shi.z - slo.z) * 100))

bpy.ops.object.select_all(action='DESELECT')
tv.select_set(True)
bpy.context.view_layer.objects.active = tv
bpy.ops.export_scene.fbx(
    filepath=OUT_FBX,
    use_selection=True,
    object_types={'MESH'},
    apply_unit_scale=True,
    apply_scale_options='FBX_SCALE_NONE',
    # 5.7 Interchange 讀不了 FBX 內嵌貼圖（Invalid translator couldn't retrieve
    # a payload）——貼圖落地成散檔放 FBX 旁邊才吃得到
    path_mode='COPY',
    embed_textures=False,
    use_mesh_modifiers=False,
    add_leaf_bones=False,
)
print("EXPORTED", OUT_FBX, os.path.getsize(OUT_FBX))
