# 破圖指標（2026-08-27 從 pose_domain_sweep.py 原樣抽出）：掃描器與 implicit skinning
# 修正器共用**同一把尺**。
#
# 抽出的鐵律（褌邊六連敗的總結）：優化／修正的目標函數，必須就是驗收時量的那個函數。
# 所以這個檔案是 verbatim 搬移，不是重寫——搬完必須用 ladder 模式重跑、與舊 ladder.tsv
# **逐位元比對**（見 pose_domain_backcheck.py）。
import numpy as np
from mathutils.bvhtree import BVHTree

_topo = {}


def eval_arrays(obj):
    import bpy
    dg = bpy.context.evaluated_depsgraph_get()
    ev = obj.evaluated_get(dg)
    me = ev.to_mesh()
    n = len(me.vertices)
    co = np.empty(n * 3, dtype=np.float64)
    me.vertices.foreach_get("co", co)
    co = co.reshape(n, 3)
    key = obj.name
    if key not in _topo:
        me.calc_loop_triangles()
        t = np.empty(len(me.loop_triangles) * 3, dtype=np.int32)
        me.loop_triangles.foreach_get("vertices", t)
        _topo[key] = t.reshape(-1, 3)
    ev.to_mesh_clear()
    mw = np.array(obj.matrix_world)
    co = co @ mw[:3, :3].T + mw[:3, 3]
    return co, _topo[key]


def tri_normals_area(co, tris):
    a, b, c = co[tris[:, 0]], co[tris[:, 1]], co[tris[:, 2]]
    cr = np.cross(b - a, c - a)
    ln = np.linalg.norm(cr, axis=1)
    safe = np.where(ln == 0.0, 1.0, ln)
    return cr / safe[:, None], ln * 0.5


def build_edge_adjacency(tris):
    d = {}
    for ti in range(tris.shape[0]):
        a, b, c = int(tris[ti, 0]), int(tris[ti, 1]), int(tris[ti, 2])
        for u, v in ((a, b), (b, c), (c, a)):
            k = (u, v) if u < v else (v, u)
            lst = d.get(k)
            if lst is None:
                d[k] = [ti]
            elif len(lst) < 2:
                lst.append(ti)
    pairs = [(v[0], v[1]) for v in d.values() if len(v) == 2]
    return np.array(pairs, dtype=np.int32)


def volume(co, tris):
    a, b, c = co[tris[:, 0]], co[tris[:, 1]], co[tris[:, 2]]
    return abs(float(np.sum(np.einsum('ij,ij->i', a, np.cross(b, c))))) / 6.0


def self_pairs(co, tris):
    """回傳 (穿透對數, 涉入的三角形 id 集合)。共頂點＝鄰接，不算穿透。"""
    tree = BVHTree.FromPolygons(co.tolist(), tris.tolist(), all_triangles=True, epsilon=0.0)
    out = 0
    ids = set()
    for a, b in tree.overlap(tree):
        if a >= b:
            continue
        ta = tris[a]
        tb = tris[b]
        if ta[0] in tb or ta[1] in tb or ta[2] in tb:
            continue
        out += 1
        ids.add(a)
        ids.add(b)
    return out, ids


def cluster_tris(tri_ids, tris, co):
    """把相連的三角形聚成團（共頂點即相連）。回傳 [(對角線 cm, 三角形數, 質心)]，大→小。
    **一團＝一條摺**：散在各處的 26 個小凹點與肚子上一條 10cm 塌陷，
    在「摺邊數」上同樣是 26，聚團之後才分得開。"""
    tri_ids = list(tri_ids)
    if not tri_ids:
        return []
    parent = {t: t for t in tri_ids}

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    vmap = {}
    for t in tri_ids:
        for v in tris[t]:
            v = int(v)
            r = vmap.get(v)
            if r is None:
                vmap[v] = t
            else:
                ra, rb = find(t), find(r)
                if ra != rb:
                    parent[ra] = rb
    groups = {}
    for t in tri_ids:
        groups.setdefault(find(t), []).append(t)
    out = []
    for g in groups.values():
        vs = np.unique(tris[g].ravel())
        p = co[vs]
        diag = float(np.linalg.norm(p.max(axis=0) - p.min(axis=0))) * 100.0
        out.append((diag, len(g), p.mean(axis=0)))
    out.sort(key=lambda x: -x[0])
    return out


def body_part(p):
    """把世界座標粗略翻成部位名（-Y＝正面；身高約 1.74m）。"""
    z, y, x = float(p[2]), float(p[1]), float(p[0])
    side = "左" if x > 0.05 else ("右" if x < -0.05 else "中")
    face = "前" if y < -0.02 else ("後" if y > 0.02 else "側")
    if z < 0.30:
        part = "小腿/腳"
    elif z < 0.62:
        part = "膝/大腿下"
    elif z < 0.85:
        part = "大腿上/鼠蹊"
    elif z < 1.05:
        part = "腹下/髖"
    elif z < 1.25:
        part = "腹"
    elif z < 1.45:
        part = "胸/背"
    elif z < 1.60:
        part = "肩/腋"
    else:
        part = "頸/頭"
    return "%s%s%s" % (side, face, part)


def cross_pairs(coA, trisA, coB, trisB):
    ta = BVHTree.FromPolygons(coA.tolist(), trisA.tolist(), all_triangles=True, epsilon=0.0)
    tb = BVHTree.FromPolygons(coB.tolist(), trisB.tolist(), all_triangles=True, epsilon=0.0)
    return len(ta.overlap(tb))


class Metrics:
    """建構前呼叫者必須先把 armature 歸零並 view_layer.update()——基線量的是靜止態。"""

    def __init__(self, body, cloth):
        self.body = body
        self.cloth = cloth
        self.co0, self.tris = eval_arrays(body)
        self.edges = build_edge_adjacency(self.tris)
        n0, _ = tri_normals_area(self.co0, self.tris)
        self.dih0 = np.einsum('ij,ij->i', n0[self.edges[:, 0]], n0[self.edges[:, 1]])
        self.flat0 = self.dih0 > 0.5
        self.vol0 = volume(self.co0, self.tris)
        self.xs0, self.xs0_ids = self_pairs(self.co0, self.tris)
        self.cross0 = -1
        self.ctris = None
        if cloth is not None:
            cco0, self.ctris = eval_arrays(cloth)
            self.cross0 = cross_pairs(self.co0, self.tris, cco0, self.ctris)
        zmin = float(self.co0[:, 2].min())
        self.foot_mask = self.co0[:, 2] < zmin + 0.02
        self.foot_zmin = float(self.co0[self.foot_mask][:, 2].min())
        self.foot_cy = float(self.co0[self.foot_mask][:, 1].mean())
        print("REST tris=%d edges=%d vol=%.1fL xsect=%d cross=%d footverts=%d"
              % (len(self.tris), len(self.edges), self.vol0 * 1000.0, self.xs0,
                 self.cross0, int(self.foot_mask.sum())), flush=True)

    def measure(self, co_override=None, cco_override=None):
        """co_override＝直接量一組給定的世界座標（implicit skinning 修正後的頂點）。
        不給就照舊從 depsgraph 讀 LBS 結果。兩條路走同一段程式碼。"""
        if co_override is None:
            co, tris = eval_arrays(self.body)
        else:
            co, tris = co_override, self.tris
        n, area = tri_normals_area(co, tris)
        dih = np.einsum('ij,ij->i', n[self.edges[:, 0]], n[self.edges[:, 1]])
        fmask = (dih < -0.17) & self.flat0
        fold = int(np.sum(fmask))                            # 二面角 >100° 的邊數
        fold_hard = int(np.sum((dih < -0.70) & self.flat0))  # 二面角 >134°
        fclusters = cluster_tris(np.unique(self.edges[fmask].ravel()).tolist(), tris, co)
        vol = volume(co, tris)
        xs_n, xs_ids = self_pairs(co, tris)
        xs = xs_n - self.xs0
        xclusters = cluster_tris(sorted(xs_ids - self.xs0_ids), tris, co)
        cross = -1
        if self.cloth is not None:
            if cco_override is None:
                cco, ctris = eval_arrays(self.cloth)
            else:
                cco, ctris = cco_override, self.ctris
            cross = cross_pairs(co, tris, cco, ctris) - self.cross0
        cen = (co[tris[:, 0]] + co[tris[:, 1]] + co[tris[:, 2]]) / 3.0
        com = np.average(cen, axis=0, weights=area)
        fx = co[self.foot_mask][:, 0]
        fy = co[self.foot_mask][:, 1]
        dx = min(com[0] - fx.min(), fx.max() - com[0])
        dy = min(com[1] - fy.min(), fy.max() - com[1])
        balance = float(min(dx, dy))
        f0 = fclusters[0] if fclusters else (0.0, 0, np.zeros(3))
        x0 = xclusters[0] if xclusters else (0.0, 0, np.zeros(3))
        return dict(fold=fold, fold_hard=fold_hard, xsect=xs, cross=cross,
                    vol_loss=(1.0 - vol / self.vol0) * 100.0, balance=balance,
                    fold_groups=len(fclusters), fold_max_cm=f0[0],
                    fold_at=body_part(f0[2]) if fclusters else "-",
                    xs_groups=len(xclusters), xs_max_cm=x0[0],
                    xs_at=body_part(x0[2]) if xclusters else "-")


# 判定規則（實作者訂，使用者可否決）：
#   ① 任何皮膚自穿透或褌被頂穿 → 破（二元，真肉不會互穿）
#   ② 最大摺團 ≥ 2cm → 破；1~2cm → 灰色地帶交使用者裁決；< 1cm → 讀成正常肉褶
FOLD_BREAK_CM = 2.0
FOLD_GRAY_CM = 1.0


def verdict_of(m):
    if m['xsect'] > 0 or m['cross'] > 0:
        return "BREAK"
    if m['fold_max_cm'] >= FOLD_BREAK_CM:
        return "BREAK"
    if m['fold_max_cm'] >= FOLD_GRAY_CM:
        return "GRAY"
    return "OK"
