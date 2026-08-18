"""褌邊緣剖面圓角（2026-08-18，user 指示「垂直皮膚方向也要圓滑」）。

現況：加厚是直接沿配對方向擠出 5mm ⇒ 剖面是硬 90° 的方角。
本腳本對「頂面/側壁」與「側壁/底面」兩圈折邊做 bevel，剖面變圓。

邊界定義＝**恰好一側是外層面的邊**（不是「與內層相鄰的頂點」——後者在窄處會
把對面邊界收進來，實測會併成單一連通分量）。
bevel 由 bmesh 執行，UV / 頂點群組 / 頂點色皆由 Blender 內插，不需手工搬運。

Run: blender --background <master.blend> --python fundoshi_bevel.py
"""
import bpy, bmesh, numpy as np, os, shutil

ROOT = r"C:\games\Unreal Engine\nice_ink"
MASTER = os.path.join(ROOT, "SourceAssets", "sumo_character_master.blend")
BACKUP = os.path.join(ROOT, "SourceAssets", "masters", "sumo_character_master_v19_prebevel.blend")
OFFSET_M = 0.0015     # 圓角半徑 1.5mm（厚度 5mm，留平的中段）
SEGMENTS = 3

if not os.path.exists(BACKUP):
    shutil.copy2(MASTER, BACKUP); print(f"BACKUP -> {BACKUP}")
bpy.ops.wm.open_mainfile(filepath=MASTER)
if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')
ob = bpy.data.objects["Fundoshi"]; me = ob.data
n0, f0 = len(me.vertices), len(me.polygons)
half = n0 // 2

bm = bmesh.new(); bm.from_mesh(me)
bm.verts.ensure_lookup_table(); bm.edges.ensure_lookup_table(); bm.faces.ensure_lookup_table()

def is_outer(f):
    return all(v.index < half for v in f.verts)
def is_inner(f):
    return all(v.index >= half for v in f.verts)

crease = []
for e in bm.edges:
    fs = e.link_faces
    if len(fs) != 2:
        continue
    o = sum(1 for f in fs if is_outer(f))
    i = sum(1 for f in fs if is_inner(f))
    # 頂面↔側壁（恰一面是外層）或 底面↔側壁（恰一面是內層）
    if o == 1 or i == 1:
        crease.append(e)
print(f"折邊 {len(crease)} 條（頂/底兩圈）")
if not crease:
    raise RuntimeError("找不到折邊——結構假設不成立")

res = bmesh.ops.bevel(bm, geom=crease, offset=OFFSET_M, offset_type='OFFSET',
                      segments=SEGMENTS, profile=0.5, affect='EDGES',
                      clamp_overlap=True, loop_slide=True)
bm.to_mesh(me); bm.free()
me.update()
print(f"頂點 {n0} -> {len(me.vertices)}   面 {f0} -> {len(me.polygons)}")

# 厚度抽樣（bevel 後配對不再是 i/i+half，改量整體包圍盒厚度不可行；
# 改報「殼的體積/面積」當守恆訊號）
me.calc_loop_triangles()
area = sum(p.area for p in me.polygons)
print(f"總表面積 {area*1e4:.0f} cm^2（bevel 會略增，因為多了圓角面）")
print(f"材質槽 {[m.name for m in me.materials]}")
print(f"UV 層 {[l.name for l in me.uv_layers]}  頂點色 {[a.name for a in me.color_attributes]}")
assert all(v.groups for v in me.vertices), "bevel 後出現無權重頂點"
print("權重檢查：全部頂點都有權重 OK")
bpy.ops.wm.save_mainfile()
print("SAVED master")
