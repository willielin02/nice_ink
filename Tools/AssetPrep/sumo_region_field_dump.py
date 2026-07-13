# 區域「指示場」導出：取代邊界曲線判側法（窄帶區判側神諭不可靠——
# 兩迴圈相距~帶寬時符號亂跳：帶內膚縫/帶外碎片/鬼影，夾具治標不治本）。
# 法：頂點指示值(區內=1/外=0) → 網格圖上值擴散 N 輪 → 場的等值線=平滑邊界，
#     全域一致無符號、跨 UV 島天然連續。
# argv（-- 之後）: <MARK_群組名> <輸出json> [擴散輪數=8]
import bpy, json, sys
import numpy as np

MASTER = r"C:\games\Unreal Engine\nice_ink\SourceAssets\sumo_character_master.blend"
argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
GROUP = argv[0]
OUT = argv[1]
ROUNDS = int(argv[2]) if len(argv) > 2 else 8

bpy.ops.wm.open_mainfile(filepath=MASTER)
body = bpy.data.objects["SumoRetopo"]
me = body.data
mw = body.matrix_world
g = body.vertex_groups[GROUP]
n_v = len(me.vertices)

marked = set()
for v in me.vertices:
    for ge in v.groups:
        if ge.group == g.index and ge.weight > 0.5:
            marked.add(v.index)

# --- 頂點級針孔（沿用 face dump 的機器）---
adj = {}
for e in me.edges:
    a, b = e.vertices
    adj.setdefault(a, []).append(b)
    adj.setdefault(b, []).append(a)
unmarked = set(range(n_v)) - marked
seen = set(); comps = []
for v0 in unmarked:
    if v0 in seen:
        continue
    stack = [v0]; comp = [v0]; seen.add(v0)
    while stack:
        v = stack.pop()
        for nb in adj.get(v, ()):
            if nb in unmarked and nb not in seen:
                seen.add(nb); stack.append(nb); comp.append(nb)
    comps.append(comp)
comps.sort(key=len, reverse=True)
for comp in comps[1:]:
    touches = any(nb in marked for v in comp for nb in adj.get(v, ()))
    if len(comp) < 400 and touches:
        marked.update(comp)

# --- 面級清理（游離雜點丟棄＋橋接合體＋包圍洞吸收，沿用 region dump 的機器）---
face_set = {p.index for p in me.polygons
            if all(me.loops[li].vertex_index in marked for li in p.loop_indices)}
fadj = {}
_e2f = {}
for p in me.polygons:
    for ek in p.edge_keys:
        _e2f.setdefault(tuple(sorted(ek)), []).append(p.index)
for fids in _e2f.values():
    for a in fids:
        for b in fids:
            if a != b:
                fadj.setdefault(a, set()).add(b)

def face_comps(fset):
    seen2, out = set(), []
    for f0 in fset:
        if f0 in seen2:
            continue
        stack, comp = [f0], {f0}
        seen2.add(f0)
        while stack:
            f = stack.pop()
            for nb in fadj.get(f, ()):
                if nb in fset and nb not in seen2:
                    seen2.add(nb); stack.append(nb); comp.add(nb)
        out.append(comp)
    return sorted(out, key=len, reverse=True)

for c in face_comps(face_set):
    if len(c) < 20:
        face_set -= c
while True:
    comps_now = face_comps(face_set)
    if len(comps_now) <= 1:
        break
    comp_of = {}
    for ci, c in enumerate(comps_now):
        for f in c:
            comp_of[f] = ci
    bridge = [f for f in (set(range(len(me.polygons))) - face_set)
              if len({comp_of[nb] for nb in fadj.get(f, ()) if nb in comp_of}) >= 2]
    if not bridge:
        break
    face_set.update(bridge)
hole_comps = face_comps({p.index for p in me.polygons} - face_set)
for comp in hole_comps[1:]:
    nbrs = {nb for f in comp for nb in fadj.get(f, ())} - comp
    if len(comp) < 40 and nbrs and nbrs <= face_set:
        face_set |= comp
print(f"FACES clean={len(face_set)}")

# --- 頂點指示場＋均曲率流循環（擴散→二值化→再擴散）---
# 每循環=邊界曲線一步曲率流：波浪熨平、直邊零曲率不動（不縮不脹、窄帶不斷）；
# 純加大擴散輪數會侵蝕窄帶（v43 教訓：補償移中點→整件膨脹）
CYCLES = int(argv[3]) if len(argv) > 3 else 3
ne = len(me.edges)
E = np.empty((ne, 2), np.int64)
me.edges.foreach_get("vertices", E.ravel())
deg = np.zeros(n_v)
np.add.at(deg, E[:, 0], 1)
np.add.at(deg, E[:, 1], 1)
deg[deg == 0] = 1

def diffuse(f0, rounds):
    f = f0.copy()
    for it in range(rounds):
        acc = np.zeros(n_v)
        np.add.at(acc, E[:, 0], f[E[:, 1]])
        np.add.at(acc, E[:, 1], f[E[:, 0]])
        f += 0.5 * (acc / deg - f)
    return f

ind = np.zeros(n_v, np.float64)
for p in me.polygons:
    if p.index in face_set:
        for li in p.loop_indices:
            ind[me.loops[li].vertex_index] = 1.0
for cyc in range(CYCLES):
    f = diffuse(ind, ROUNDS)
    ind = (f >= 0.5).astype(np.float64)
    print(f"MCF cycle {cyc}: region_verts={int(ind.sum())}")
field = diffuse(ind, ROUNDS)
core = [field[me.loops[li].vertex_index] for p in me.polygons if p.index in face_set
        for li in p.loop_indices]
print(f"FIELD rounds={ROUNDS} cycles={CYCLES} core p10={np.percentile(core,10):.3f} p50={np.percentile(core,50):.3f}")

# --- 等值線抽取（marching triangles @ field=0.5）：
#     擴散場的 0.5 等值線＝標記邊界的平滑版；之後距離給銳度、場給符號 ---
ISO = 0.5
me.calc_loop_triangles()
Pw_all = np.empty((n_v, 3), np.float64)
me.vertices.foreach_get("co", Pw_all.ravel())
M = np.array(mw)
Pw_all = Pw_all @ M[:3, :3].T + M[:3, 3]
iso_segs = []
iso_hints = []   # 每段的內側提示：父三角形中場最高的頂點方向（局部、無鏈接、平滑後仍可靠）
for tri in me.loop_triangles:
    vi = [me.loops[li].vertex_index for li in tri.loops]
    fv = [field[v] for v in vi]
    pts = []
    for a, b in ((0, 1), (1, 2), (2, 0)):
        fa, fb = fv[a], fv[b]
        if (fa - ISO) * (fb - ISO) < 0:
            t = (ISO - fa) / (fb - fa)
            p = Pw_all[vi[a]] * (1 - t) + Pw_all[vi[b]] * t
            pts.append(p)
    if len(pts) == 2:
        iso_segs.append([*pts[0], *pts[1]])
        mid = (pts[0] + pts[1]) / 2
        vin = Pw_all[vi[int(np.argmax(fv))]]
        h = vin - mid
        n = np.linalg.norm(h)
        iso_hints.append(list(h / n if n > 1e-9 else h))
print(f"ISO segs={len(iso_segs)}")

# --- 導出：場>0.005 的面（uv+3D pos+場值）＋其餘面 uv（禁區）---
uv0 = me.uv_layers["UVMap"]
tris, other_uv = [], []
for tri in me.loop_triangles:
    uvr, fr, pr = [], [], []
    keep = False
    for li in tri.loops:
        u, v = uv0.data[li].uv
        uvr += [u, v]
        vi = me.loops[li].vertex_index
        fv = field[vi]
        fr.append(fv)
        pr += list(Pw_all[vi])
        if fv > 0.005:
            keep = True
    if keep:
        tris.append({"uv": uvr, "f": fr, "pos": pr})
    else:
        other_uv.append(uvr)
print(f"TRIS field={len(tris)} other={len(other_uv)}")
with open(OUT, "w") as f:
    json.dump({"tris": tris, "other_uv": other_uv, "iso_segs": iso_segs,
               "iso_hints": iso_hints}, f)
print("DUMPED", OUT)
