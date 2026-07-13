# 相撲體型改造 step2c：徑向剖面場轉移（對參考模型的姿勢免疫；shrinkwrap 碎裂法已棄）
# R_sumo(z,θ) 沿他自己的彎曲軸取樣（歸一化傾斜）；char17 頂點按 R_s/R_c 比例徑向外推。
# 朝向定案（2026-07-08 臉部渲染驗證）：char17 與相撲 glb 的世界正面都是 −Y。
# θ 座標系：atan2(y,x)，θ=90° 帶指向 +Y（背面）、θ=270° 帶指向 −Y（正面）。
import bpy, bmesh, math
from mathutils import Vector

OUT = r"C:\Users\willi\AppData\Local\Temp\claude\c--games-Unreal-Engine-nice-ink\935f71c0-54fe-4782-8f05-ff3a52565e53\scratchpad\sumo_radial_result.txt"
REN = r"C:\Users\willi\AppData\Local\Temp\claude\c--games-Unreal-Engine-nice-ink\935f71c0-54fe-4782-8f05-ff3a52565e53\scratchpad\ren5_"
WORK = r"C:\games\Unreal Engine\nice_ink\SourceAssets\char20_work.blend"
GLB = r"C:\games\Unreal Engine\nice_ink\SourceAssets\Sumo wrestler\sumo_wrestler_2k.glb"
LINES = []

def log(s):
    LINES.append(str(s))
    print(s)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(LINES))

def smoothstep(a, b, x):
    t = max(0.0, min(1.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)

body = bpy.data.objects["PlusSize_Male_Body_01"]
W = body.matrix_world.copy()
Winv = W.inverted()

# --- 匯入對齊 ---
pre = set(bpy.data.objects)
bpy.ops.import_scene.gltf(filepath=GLB)
sumo_objs = [o for o in bpy.data.objects if o not in pre and o.type == 'MESH']
sumo = max(sumo_objs, key=lambda o: len(o.data.vertices))

def wverts(obj):
    m = obj.matrix_world
    return [m @ v.co for v in obj.data.vertices]

cw = wverts(body)
c_zmin, c_zmax = min(v.z for v in cw), max(v.z for v in cw)
sv = []
for o in sumo_objs:
    sv.extend(wverts(o))
s_zmin, s_zmax = min(v.z for v in sv), max(v.z for v in sv)
scale = (c_zmax - c_zmin) / (s_zmax - s_zmin)
for o in sumo_objs:
    o.scale = tuple(s * scale for s in o.scale)
bpy.context.view_layer.update()
sv = []
for o in sumo_objs:
    sv.extend(wverts(o))
dx = (max(v.x for v in cw) + min(v.x for v in cw)) / 2 - (max(v.x for v in sv) + min(v.x for v in sv)) / 2
dy = (max(v.y for v in cw) + min(v.y for v in cw)) / 2 - (max(v.y for v in sv) + min(v.y for v in sv)) / 2
dz = c_zmin - min(v.z for v in sv)
for o in sumo_objs:
    o.location = (o.location[0] + dx, o.location[1] + dy, o.location[2] + dz)
bpy.context.view_layer.update()
log(f"aligned scale={scale:.4f} sumo_body={sumo.name}")

# --- 兩邊的軸線（每 z 帶的軀幹質心；相撲用自己的＝傾斜歸一化） ---
Z0, Z1 = 0.60, 1.50          # 場的涵蓋範圍（略寬於變形窗）
NZ, NTH = 46, 72             # z 2cm 一格、θ 5° 一格
sumo_world = wverts(sumo)
sumo_inv = sumo.matrix_world.inverted()

def axis_of(verts, halfwidth):
    # 帶內包圍盒中點（不是質心）——密度免疫：char17 背面的高密度精雕區會把質心拖歪
    ax = {}
    for iz in range(NZ):
        z = Z0 + (Z1 - Z0) * iz / (NZ - 1)
        band = [v for v in verts if abs(v.z - z) < 0.04 and abs(v.x) < halfwidth]
        if band:
            ax[iz] = Vector(((max(v.x for v in band) + min(v.x for v in band)) / 2,
                             (max(v.y for v in band) + min(v.y for v in band)) / 2, z))
    # 補洞
    for iz in range(NZ):
        if iz not in ax:
            near = min(ax.keys(), key=lambda k: abs(k - iz))
            ax[iz] = Vector((ax[near].x, ax[near].y, Z0 + (Z1 - Z0) * iz / (NZ - 1)))
    return ax

sumo_axis = axis_of(sumo_world, 0.30)   # 窄半寬：排除手臂對質心的拉扯
char_axis = axis_of(cw, 0.30)
log("axes built")

# --- 半徑場：從軸沿水平角 θ 射線、取最遠命中（多層命中時取外殼＝輪廓） ---
def radial_field(obj, axis):
    inv = obj.matrix_world.inverted()
    R = [[None] * NTH for _ in range(NZ)]
    for iz in range(NZ):
        origin_w = axis[iz]
        for it in range(NTH):
            th = 2 * math.pi * it / NTH
            d_w = Vector((math.cos(th), math.sin(th), 0.0))
            o_l = inv @ origin_w
            d_l = (inv.to_3x3() @ d_w).normalized()
            # 第一命中＝軀幹壁（軸心在軀幹內部；手臂在更外層永遠不會先被打到）
            hit, loc, nrm, fi = obj.ray_cast(o_l, d_l, distance=3.0)
            if hit:
                R[iz][it] = ((obj.matrix_world @ loc) - origin_w).length
    return R

R_s = radial_field(sumo, sumo_axis)
R_c = radial_field(body, char_axis)
miss_s = sum(1 for row in R_s for x in row if x is None)
miss_c = sum(1 for row in R_c for x in row if x is None)
log(f"fields sampled miss_s={miss_s} miss_c={miss_c}")

# 補 None：同列鄰近插補
def fill(R):
    for iz in range(NZ):
        row = R[iz]
        known = [i for i in range(NTH) if row[i] is not None]
        if not known:
            R[iz] = R[iz - 1][:] if iz > 0 else [0.2] * NTH
            continue
        for i in range(NTH):
            if row[i] is None:
                near = min(known, key=lambda k: min((k - i) % NTH, (i - k) % NTH))
                row[i] = row[near]
fill(R_s)
fill(R_c)

def sample(R, axis, p):
    zf = (p.z - Z0) / (Z1 - Z0) * (NZ - 1)
    iz = max(0, min(NZ - 2, int(zf)))
    tz = zf - iz
    a = axis[iz].lerp(axis[iz + 1], tz)
    rel = Vector((p.x - a.x, p.y - a.y, 0.0))
    r = rel.length
    th = math.atan2(rel.y, rel.x) % (2 * math.pi)
    tf = th / (2 * math.pi) * NTH
    it = int(tf) % NTH
    tt = tf - int(tf)
    def bil(Rf):
        r00 = Rf[iz][it]; r01 = Rf[iz][(it + 1) % NTH]
        r10 = Rf[iz + 1][it]; r11 = Rf[iz + 1][(it + 1) % NTH]
        return (r00 * (1 - tt) + r01 * tt) * (1 - tz) + (r10 * (1 - tt) + r11 * tt) * tz
    return a, rel, r, bil(R)

# 場消毒：逐列中位數夾制（張腿縫隙/拳頭造成的野值），再 θ 與 z 方向各平滑一次
def sanitize(R):
    for iz in range(NZ):
        row = sorted(R[iz])
        med = row[NTH // 2]
        R[iz] = [max(med * 0.55, min(med * 1.8, x)) for x in R[iz]]
sanitize(R_s)
sanitize(R_c)
# 正面扇區（θ 210°-330°，正面＝−Y）：z<1.0 由上往下單調傳播（微衰減 max）——
# 他的兜襠布前垂片會把低處正面射線切短，肚腩量體必須從上方輾過去
iz_top = int((1.00 - Z0) / (Z1 - Z0) * (NZ - 1))
for it in range(NTH):
    th_deg = 360.0 * it / NTH
    if 210.0 <= th_deg <= 330.0:
        for iz in range(iz_top - 1, -1, -1):
            R_s[iz][it] = max(R_s[iz][it], R_s[iz + 1][it] * 0.985)
for R in (R_s, R_c):
    for _ in range(2):
        for iz in range(NZ):
            row = R[iz][:]
            for it in range(NTH):
                R[iz][it] = (row[(it - 1) % NTH] + 2 * row[it] + row[(it + 1) % NTH]) / 4
    for it in range(NTH):
        col = [R[iz][it] for iz in range(NZ)]
        for iz in range(1, NZ - 1):
            R[iz][it] = (col[iz - 1] + 2 * col[iz] + col[iz + 1]) / 4

# 印兩邊腹帶輪廓（sanity）
for tag, R, axis in (("sumo", R_s, sumo_axis), ("char", R_c, char_axis)):
    iz = int((0.95 - Z0) / (Z1 - Z0) * (NZ - 1))
    row = R[iz]
    log(f"{tag} belly z=0.95: front={row[54]:.3f} back={row[18]:.3f} side={row[0]:.3f}/{row[36]:.3f}")

# --- 保護島：密集區偵測（乳頭/肚臍/背面精雕區）＋固定候選，全部剛體跟隨 ---
bm2 = bmesh.new()
bm2.from_mesh(body.data)
bm2.verts.ensure_lookup_table()
lens2 = []
ae = {}
for v in bm2.verts:
    ls = [e.calc_length() for e in v.link_edges]
    if ls:
        ae[v.index] = sum(ls) / len(ls)
        lens2.append(ae[v.index])
lens2.sort()
med2 = lens2[len(lens2) // 2]
dense = []
for v in bm2.verts:
    p = W @ v.co
    if ae.get(v.index, 9) < med2 * 0.62 and 0.70 < p.z < 1.25:
        dense.append(p.copy())
bm2.free()
dcl = []
for p in dense:
    for c in dcl:
        if (p - c["c"]).length < 0.06:
            c["v"].append(p)
            c["c"] = sum(c["v"], Vector()) / len(c["v"])
            break
    else:
        dcl.append({"v": [p], "c": p.copy()})
dcl = [c for c in dcl if len(c["v"]) >= 8]
dcl.sort(key=lambda c: -len(c["v"]))
islands = []
for c in dcl[:5]:
    # 正面（y<0）的精雕密集區＝肚臍（中線、z<1.05）與左右乳頭（成對、z>1.05）。
    # 舊版把肚臍誤判成「背面肛門」、乳頭硬編碼在 +Y——朝向定案後全部改自動偵測。
    p = c["c"]
    if p.y > 0 or abs(p.x) > 0.30:
        continue  # 背側／手部密集區不騎乘
    is_navel = abs(p.x) < 0.05 and p.z < 1.05
    is_nipple = 0.05 < abs(p.x) < 0.30 and p.z > 1.05
    if is_navel or is_nipple:
        spread = max((q - p).length for q in c["v"])
        islands.append((p, min(0.06, max(0.03, spread * 1.15))))
        log(f"{'navel' if is_navel else 'nipple'} island n={len(c['v'])} "
            f"at ({p.x:.3f},{p.y:.3f},{p.z:.3f}) r={islands[-1][1]:.3f}")
log(f"islands total={len(islands)}")

# --- 權重（世界空間）：軀幹窗＝他單柱狀軀幹的 z 範圍；手臂用軸線距離精確挖除 ---
def dist_to_seg(p, a, b):
    ab = b - a
    t = max(0.0, min(1.0, (p - a).dot(ab) / ab.length_squared))
    return (p - (a + ab * t)).length

ARM_L = (Vector((0.28, 0.03, 1.32)), Vector((0.76, 0.03, 0.93)))   # 肩→手 A-pose 軸
ARM_R = (Vector((-0.28, 0.03, 1.32)), Vector((-0.76, 0.03, 0.93)))

def weight_of(p):
    # 島不再擋變形：改在轉移迴圈內做「島內係數一致化」（均勻縮放＝結構保真、無邊界台階）
    w = 1.0
    w = min(w, smoothstep(1.42, 1.34, p.z))     # 頭頸
    if p.z > 0.88:
        d_arm = min(dist_to_seg(p, *ARM_L), dist_to_seg(p, *ARM_R))
        w = min(w, smoothstep(0.055, 0.12, d_arm))  # 手臂軸挖除（含腋窩過渡）
    return w

# --- Simple 細分 ×2 ---
orig = body.copy()
orig.data = body.data.copy()
orig.name = "Body_Orig_Ref"
bpy.context.collection.objects.link(orig)
bpy.context.view_layer.objects.active = body
for o in bpy.context.view_layer.objects:
    o.select_set(o == body)
mod = body.modifiers.new("Densify", 'SUBSURF')
mod.subdivision_type = 'SIMPLE'
mod.levels = 2
bpy.ops.object.modifier_apply(modifier="Densify")
log(f"subdivided verts={len(body.data.vertices)}")

pre_world = [W @ v.co for v in body.data.vertices]

# --- 徑向轉移 ---
def scale_factor_at(p):
    """該點的總徑向縮放因子（含方向增益與 z 窗；回傳 None＝不動）"""
    if p.z <= Z0 + 0.02 or p.z >= Z1 - 0.02:
        return None
    wgt = weight_of(p)
    if wgt <= 0.001:
        return None
    a, rel, r, rc = sample(R_c, char_axis, p)
    _, _, _, rs = sample(R_s, char_axis, p)
    if r < 1e-5 or rc < 1e-5:
        return None
    fr = max(0.0, -rel.y / r); bk = max(0.0, rel.y / r); sd = abs(rel.x) / r   # 正面＝−Y
    fr *= fr; bk *= bk; sd *= sd
    side_gain = 0.45 * smoothstep(1.22, 1.02, p.z)
    gain = fr * 1.0 + bk * 0.85 + sd * side_gain
    # 寬過渡帶：正面圍裙下探 z0.68-0.82；側/背 z0.82-0.98 漸入
    wlow = max(smoothstep(0.82, 0.98, p.z), fr * smoothstep(0.68, 0.82, p.z))
    f = max(0.85, min(2.0, rs / rc))
    return (a, rel, r, 1.0 + (f - 1.0) * wgt * gain * wlow)

island_f = []
for c, r in islands:
    got = scale_factor_at(c)
    island_f.append(got[3] if got else 1.0)
    log(f"island@{tuple(round(x,3) for x in c)} coherent factor={island_f[-1]:.3f}")

moved = 0
maxd = 0.0
for i, v in enumerate(body.data.vertices):
    p = pre_world[i]
    got = scale_factor_at(p)
    if got is None:
        continue
    a, rel, r, s = got
    # 島內係數一致化：靠近島心 → 縮放因子拉向島心的因子（均勻縮放、結構保真）
    for (c, ir), fc in zip(islands, island_f):
        blend = smoothstep(ir * 1.5, ir * 0.5, (p - c).length)
        s = s * (1 - blend) + fc * blend
    new_r = r * s
    p2 = Vector((a.x + rel.x / r * new_r, a.y + rel.y / r * new_r, p.z))
    d = (p2 - p).length
    if d > 1e-6:
        v.co = Winv @ p2
        moved += 1
        maxd = max(maxd, d)
log(f"radial transfer moved={moved} maxd={maxd:.3f}")

# --- 大腿增粗（沿用，讀起來是對的） ---
legged = 0
for i, v in enumerate(body.data.vertices):
    p = W @ v.co
    if p.z >= 0.86 or p.z <= 0.12:
        continue
    fade = smoothstep(0.86, 0.64, p.z) * smoothstep(0.12, 0.20, p.z)
    if fade <= 0:
        continue
    axis = Vector((0.09 if p.x >= 0 else -0.09, 0.03, p.z))
    radial = p - axis
    radial.z = 0
    p2 = axis + radial * (1.0 + 0.42 * fade)
    p2.z = p.z
    v.co = Winv @ p2
    legged += 1
log(f"legs inflated {legged}")


# --- 輕度拉普拉斯平滑（動過的頂點、島核心除外——島用一致化縮放已自帶平滑邊界） ---
import collections
island_core = set()
for c, r in islands:
    for i in range(len(body.data.vertices)):
        if (pre_world[i] - c).length < r:
            island_core.add(i)
bm3 = bmesh.new()
bm3.from_mesh(body.data)
bm3.verts.ensure_lookup_table()
adj = collections.defaultdict(list)
for e in bm3.edges:
    a, b = e.verts[0].index, e.verts[1].index
    adj[a].append(b)
    adj[b].append(a)
bm3.free()
deformed_idx = {i for i in range(len(body.data.vertices))
                if (W @ body.data.vertices[i].co - pre_world[i]).length > 0.004} - island_core
for _ in range(2):
    cos = [v.co.copy() for v in body.data.vertices]
    for i in deformed_idx:
        ns = adj[i]
        if len(ns) < 3:
            continue
        avg = sum((cos[j] for j in ns), Vector()) / len(ns)
        body.data.vertices[i].co = cos[i].lerp(avg, 0.4)
log(f"smoothed {len(deformed_idx)} verts (island cores excluded: {len(island_core)})")
body.data.update()

# --- 對比渲染：orig / new / sumo × front / side ---
scene = bpy.context.scene
scene.render.engine = 'BLENDER_WORKBENCH'
scene.display.shading.light = 'STUDIO'
scene.render.resolution_x = 720
scene.render.resolution_y = 1080
cam = bpy.data.objects.new("QACam", bpy.data.cameras.new("QACam"))
bpy.context.collection.objects.link(cam)
scene.camera = cam
mosaic = bpy.data.objects.get("PlusSize_Male_Mosaic_01")
if mosaic:
    mosaic.hide_render = True
views = {"front": ((0.0, -3.6, 0.95), (math.radians(90), 0, 0)),
         "side": ((3.6, 0.0, 0.95), (math.radians(90), 0, math.radians(90))),
         "back": ((0.0, 3.6, 0.95), (math.radians(90), 0, math.radians(180)))}
variants = {"orig": ([orig], [body] + sumo_objs),
            "new": ([body], [orig] + sumo_objs),
            "sumo": (sumo_objs, [body, orig])}
for vname, (loc, rot) in views.items():
    cam.location = loc
    cam.rotation_euler = rot
    for tag, (show, hide) in variants.items():
        for o in show:
            o.hide_render = False
        for o in hide:
            o.hide_render = True
        scene.render.filepath = REN + f"{tag}_{vname}.png"
        bpy.ops.render.render(write_still=True)
log("renders done")

orig.hide_set(True)
orig.hide_render = True
bpy.ops.wm.save_as_mainfile(filepath=WORK)
log("RADIAL DONE saved")
