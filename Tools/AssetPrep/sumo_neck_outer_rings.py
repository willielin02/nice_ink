# 短脖程序化「頸皮」的原料抽取（2026-08-16；user 定案：站立/作畫另設程序化脖子）
# 從切開版 master 取 seam 兩側各 STEPS 圈「外環」：seam 頂點沿邊走一步（挑最沿 seam
# 平面法線方向的鄰點）→ 再走一步；每環 84 點、與 seam 環逐點對應。
# 輸出 SourceAssets/neck_outer_rings.json：head_out[k][step]/body_out[k][step]
# = {pos(m, blender), nrm}；供 sumo_neck_export_header 生成 NeckSeamData 的 Outer 表。
import bpy, json, collections
from mathutils import Vector

MASTER = r"C:/games/Unreal Engine/nice_ink/SourceAssets/sumo_character_master.blend"
RINGS = r"C:/games/Unreal Engine/nice_ink/SourceAssets/neck_seam_rings.json"
OUT = r"C:/games/Unreal Engine/nice_ink/SourceAssets/neck_outer_rings.json"
STEPS = 4

bpy.ops.wm.open_mainfile(filepath=MASTER)
body = bpy.data.objects["SumoRetopo"]; me = body.data
rings = json.load(open(RINGS, encoding="utf-8"))
n_plane = Vector(rings["plane_n"])
head_ring = [r["v"] for r in rings["head_ring"]]
body_ring = [r["v"] for r in rings["body_ring"]]
seam_set = set(head_ring) | set(body_ring)

adj = collections.defaultdict(set)
for e in me.edges:
    a, b = e.vertices; adj[a].add(b); adj[b].add(a)

me.calc_normals_split() if hasattr(me, "calc_normals_split") else None
vnrm = {}
for p in me.polygons:
    for li in p.loop_indices:
        vi = me.loops[li].vertex_index
        vnrm.setdefault(vi, Vector((0,0,0)))
        vnrm[vi] += Vector(me.corner_normals[li].vector)

MAX_STEP_M = 0.02   # 單步上限 2cm：超過=繞頸長邊（不是外走）→視為無路
MIN_COS = 0.45      # 位移與 seam 法線夾角 < ~63° 才算「外走」
def step_out(vi, direction, exclude):
    # 合格鄰點中取「最短」（不是最沿法線）：短邊=真正相鄰的一圈；長邊常是繞頸/跨區
    best, bestlen = None, 1e9
    p0 = me.vertices[vi].co
    for nb in adj[vi]:
        if nb in exclude: continue
        d = (me.vertices[nb].co - p0)
        if d.length < 1e-9 or d.length > MAX_STEP_M: continue
        if d.normalized().dot(direction) < MIN_COS: continue
        if d.length < bestlen: bestlen, best = d.length, nb
    return best

def walk(ring, direction):
    out = []
    for vi in ring:
        chain = []; cur = vi; excl = set(seam_set)
        for s in range(STEPS):
            nx = step_out(cur, direction, excl)
            if nx is None: nx = cur
            chain.append(nx); excl.add(nx); cur = nx
        out.append(chain)
    return out

head_out = walk(head_ring, n_plane)          # 朝頭側
body_out = walk(body_ring, -n_plane)         # 朝身側

def pack(chains):
    res = []
    for chain in chains:
        row = []
        for vi in chain:
            co = me.vertices[vi].co; nr = vnrm.get(vi, Vector((0,0,1))).normalized()
            row.append({"v": vi, "pos": [round(co.x,7),round(co.y,7),round(co.z,7)],
                        "nrm": [round(nr.x,6),round(nr.y,6),round(nr.z,6)]})
        res.append(row)
    return res

data = {"note": "outer rings for short-neck procedural skin; blender mesh space (m), UE = (x,-y,z)*100",
        "steps": STEPS, "count": len(head_ring),
        "head_out": pack(head_out), "body_out": pack(body_out)}
json.dump(data, open(OUT, "w", encoding="utf-8"), indent=1)
# 統計：每步平均距離
import statistics as st
def avg_step(chains, ring):
    d1=[(me.vertices[c[0]].co - me.vertices[r].co).length for c,r in zip(chains,ring)]
    d2=[(me.vertices[c[1]].co - me.vertices[c[0]].co).length for c in chains]
    return st.mean(d1)*100, st.mean(d2)*100
print("head_out step cm:", avg_step(head_out, head_ring)); print("body_out step cm:", avg_step(body_out, body_ring))
print("JSON ->", OUT)
