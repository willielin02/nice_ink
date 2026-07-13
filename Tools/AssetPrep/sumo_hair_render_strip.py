# 髮簾渲染：真 hair curves ＋ Principled Hair BSDF（Cycles）→ 水平可平鋪髮絲貼圖
# 結構：150 束（圖集 Nyquist 內）× 每束 6 根細髮（束=結構、細髮=質感）
# 光沿相機軸 → 每絲隨波動各自閃碎 glint（極細極密銀白反光）
import bpy
import numpy as np
import math

S = r"C:\Users\willi\AppData\Local\Temp\claude\c--games-Unreal-Engine-nice-ink\00d214a0-ef56-4e54-9786-cb494e647707\scratchpad"
W_M = 0.628      # 簾寬＝頭圍（φ 一圈）
L_M = 0.314      # 可見長（相機視野 y）
N_COL = 150      # 束數
HAIRS = 10        # 每束細髮
NPTS = 24        # 每絲取樣點
R_HAIR = 0.00042  # 髮絲半徑 0.35mm
MARGIN = 0.02    # 平鋪複製邊界

bpy.ops.wm.read_factory_settings(use_empty=True)
rng = np.random.RandomState(11)

ts = np.linspace(0.0, 1.0, NPTS)
y0, y1 = -0.02, L_M + 0.02
dx = W_M / N_COL

strands = []   # 每條: (NPTS,3)
for ci in range(N_COL):
    xc = (ci + 0.5) * dx
    # 束級波動（整束一起擺）
    A_c = rng.uniform(0.0010, 0.0030)
    f_c = rng.uniform(0.7, 1.4)
    ph_c = rng.uniform(0, 2 * math.pi)
    col_wave = A_c * np.sin(2 * math.pi * (f_c * ts) + ph_c)
    for hi in range(HAIRS):
        x_off = rng.normal(0, dx * 0.22)
        A_h = rng.uniform(0.0003, 0.0007)
        f_h = rng.uniform(2.0, 4.5)
        ph_h = rng.uniform(0, 2 * math.pi)
        hair_wave = A_h * np.sin(2 * math.pi * (f_h * ts) + ph_h)
        z_base = rng.uniform(0.0, 0.003)
        A_z = rng.uniform(0.0002, 0.0006)
        ph_z = rng.uniform(0, 2 * math.pi)
        z_wave = z_base + A_z * np.sin(2 * math.pi * (rng.uniform(1.0, 2.5) * ts) + ph_z)
        xs = xc + x_off + col_wave + hair_wave
        ys = y0 + (y1 - y0) * ts
        pts = np.stack([xs, ys, z_wave], axis=1)
        strands.append(pts)
        # 平鋪複製：靠邊的絲補到另一側
        if xs.min() < MARGIN:
            strands.append(pts + np.array([W_M, 0, 0]))
        if xs.max() > W_M - MARGIN:
            strands.append(pts - np.array([W_M, 0, 0]))

sizes = [NPTS] * len(strands)
cur = bpy.data.hair_curves.new("HairSheet")
cur.add_curves(sizes)
allpts = np.concatenate(strands, axis=0).astype(np.float32)
cur.points.foreach_set("position", allpts.ravel())
rad = cur.attributes.new("radius", 'FLOAT', 'POINT')
rad.data.foreach_set("value", np.full(len(allpts), R_HAIR, np.float32))

mat = bpy.data.materials.new("M_Hair")
mat.use_nodes = True
nt = mat.node_tree
for n in list(nt.nodes):
    if n.type == 'BSDF_PRINCIPLED':
        nt.nodes.remove(n)
hair = nt.nodes.new("ShaderNodeBsdfHairPrincipled")
out = next(n for n in nt.nodes if n.type == 'OUTPUT_MATERIAL')
nt.links.new(hair.outputs[0], out.inputs["Surface"])
try:
    hair.parametrization = 'MELANIN'
except Exception as e:
    print("PARAM_SET_FAIL", e)
print("HAIR_INPUTS", [i.name for i in hair.inputs])


def hset(name, val):
    if name in hair.inputs:
        hair.inputs[name].default_value = val
    else:
        print("MISS_INPUT", name)


hset("Melanin", 1.0)
hset("Melanin Redness", 0.3)
hset("Roughness", 0.18)
hset("Radial Roughness", 0.2)
hset("Coat", 0.05)
hset("Random Roughness", 0.35)
hset("Random Color", 0.1)
cur.materials.append(mat)

ob = bpy.data.objects.new("HairSheet", cur)
bpy.context.scene.collection.objects.link(ob)

# 底板（絲隙＝深髮陰影，不是背景）
pm = bpy.data.materials.new("M_Under")
pm.use_nodes = True
_pnt = pm.node_tree
for _n in list(_pnt.nodes):
    if _n.type != 'OUTPUT_MATERIAL':
        _pnt.nodes.remove(_n)
_em = _pnt.nodes.new("ShaderNodeEmission")   # 純黑發光：繞過鏡面項（Principled 底板會把斜陽漏成灰底）
_em.inputs["Color"].default_value = (0.002, 0.002, 0.003, 1.0)
_pout = next(_n for _n in _pnt.nodes if _n.type == 'OUTPUT_MATERIAL')
_pnt.links.new(_em.outputs[0], _pout.inputs["Surface"])
import bmesh
pme = bpy.data.meshes.new("plane")
bm = bmesh.new()
bmesh.ops.create_grid(bm, x_segments=1, y_segments=1, size=1.0)
bm.to_mesh(pme); bm.free()
pl = bpy.data.objects.new("plane", pme)
bpy.context.scene.collection.objects.link(pl)
pl.scale = (2.0, 2.0, 1.0)
pl.location = (W_M / 2, L_M / 2, -0.004)
pme.materials.append(pm)

# 光：主光沿相機軸（每絲自閃 glint）＋斜補光
sd = bpy.data.lights.new("key", 'SUN')
sd.energy = 7.0
sd.angle = math.radians(2.0)
so = bpy.data.objects.new("key", sd)
bpy.context.scene.collection.objects.link(so)
so.rotation_euler = (math.radians(12), 0.0, 0.0)  # 斜 12 度：絲波斜率 ±8 度，鏡面回反需 ~6 度
fd = bpy.data.lights.new("fill", 'SUN')
fd.energy = 0.25
fd.angle = math.radians(5.0)
fo = bpy.data.objects.new("fill", fd)
bpy.context.scene.collection.objects.link(fo)
fo.rotation_euler = (math.radians(-40), 0.0, 0.0)
world = bpy.data.worlds.new("w")
bpy.context.scene.world = world
world.use_nodes = True
world.node_tree.nodes["Background"].inputs[0].default_value = (0.008, 0.008, 0.01, 1.0)

cd = bpy.data.cameras.new("cam")
cd.type = 'ORTHO'
cd.ortho_scale = W_M
cam = bpy.data.objects.new("cam", cd)
bpy.context.scene.collection.objects.link(cam)
cam.location = (W_M / 2, L_M / 2, 1.0)
cam.rotation_euler = (0.0, 0.0, 0.0)
scene = bpy.context.scene
scene.camera = cam
scene.render.engine = 'CYCLES'
scene.cycles.samples = 128
scene.cycles.use_denoising = True
scene.render.resolution_x = 4096
scene.render.resolution_y = 2048
scene.view_settings.view_transform = 'Standard'
scene.render.image_settings.file_format = 'PNG'
scene.render.filepath = S + r"\hair_strip.png"
print(f"CURVES={len(strands)} rendering...")
bpy.ops.render.render(write_still=True)
print("STRIP_DONE")
