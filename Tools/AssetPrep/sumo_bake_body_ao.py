# 身體 AO 烘焙：數學檢測凹陷（乳下溝、肚腩摺、頸摺）→ body_ao.png（UV0 空間）
# 用 AO 節點（可控距離）→ Emission → bake EMIT（比 bake AO 的世界距離設定可控）
# 不存 master——只產出貼圖。
import bpy

MASTER = r"C:\games\Unreal Engine\nice_ink\SourceAssets\sumo_character_master.blend"
OUT = r"C:\Users\willi\AppData\Local\Temp\claude\c--games-Unreal-Engine-nice-ink\00d214a0-ef56-4e54-9786-cb494e647707\scratchpad\body_ao.png"
SIZE = 2048
AO_DIST = 0.15   # 採樣距離：抓 5~15cm 尺度的肥肉溝

bpy.ops.wm.open_mainfile(filepath=MASTER)
body = bpy.data.objects["SumoRetopo"]
me = body.data
me.uv_layers.active = me.uv_layers["UVMap"]

img = bpy.data.images.new("body_ao", SIZE, SIZE, alpha=False, float_buffer=False)

mat = bpy.data.materials.new("M_AOBake")
mat.use_nodes = True
nt = mat.node_tree
for n in list(nt.nodes):
    if n.type != 'OUTPUT_MATERIAL':
        nt.nodes.remove(n)
ao = nt.nodes.new("ShaderNodeAmbientOcclusion")
ao.inputs["Distance"].default_value = AO_DIST
ao.samples = 16
em = nt.nodes.new("ShaderNodeEmission")
out = next(n for n in nt.nodes if n.type == 'OUTPUT_MATERIAL')
nt.links.new(ao.outputs["AO"], em.inputs["Color"])
nt.links.new(em.outputs[0], out.inputs["Surface"])
tex = nt.nodes.new("ShaderNodeTexImage")
tex.image = img
nt.nodes.active = tex

me.materials.clear()
me.materials.append(mat)

for o in bpy.context.view_layer.objects:
    o.select_set(False)
body.select_set(True)
bpy.context.view_layer.objects.active = body

scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.cycles.samples = 64
scene.render.bake.margin = 16
bpy.ops.object.bake(type='EMIT')
img.filepath_raw = OUT
img.file_format = 'PNG'
img.save()
print("AO_BAKED", OUT)
