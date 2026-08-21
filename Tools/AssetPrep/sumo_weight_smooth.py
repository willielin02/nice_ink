# 蒙皮權重場平滑（2026-08-22 終破案）：睡者顯示=BowBody(SK)＝躺姿蒙皮變形後的皮膚。
# 權重當年畫在 3cm 粗網格、細分只線性內插＝權重場帶原始粗格稜 × 躺姿髖部大旋轉
# ＝皮膚矩形明暗斑（user 的「凹陷」）＋布邊逐格扭（「一排凸起」）。
# 修＝大轉角骨（Hips/Spine*/UpLeg/Leg）權重 Taubin 平滑（同面判準、z 0.06~1.28、
# 頭頸/Jiggle 不碰）→ 逐頂點重歸一。身體與布都做（布=同骨蒙皮）。
import bpy
import os
import shutil
import numpy as np
from mathutils import Vector

ROOT = r"C:\games\Unreal Engine\nice_ink"
MASTER = os.path.join(ROOT, "SourceAssets", "sumo_character_master.blend")
BK = os.path.join(ROOT, "SourceAssets", "masters", "sumo_character_master_v32_preweightsmooth.blend")
BONES = ["Hips", "Spine", "Spine1", "Spine2",
         "LeftUpLeg", "RightUpLeg", "LeftLeg", "RightLeg"]
PAIRS = 30
Z_LO, Z_HI = 0.06, 1.25

def P(*a):
    print(*a, flush=True)

bpy.ops.wm.open_mainfile(filepath=MASTER)
if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')
if not os.path.exists(BK):
    shutil.copy2(MASTER, BK)
    P("backup ->", BK)

for obname in ("SumoRetopo", "Fundoshi"):
    ob = bpy.data.objects[obname]
    me = ob.data
    N = len(me.vertices)
    co = np.array([v.co[:] for v in me.vertices])
    in_reg = (co[:, 2] > Z_LO) & (co[:, 2] < Z_HI)
    w_reg = np.clip(np.minimum(co[:, 2] - Z_LO, Z_HI - co[:, 2]) / 0.04, 0.0, 1.0)
    w_reg = np.where(in_reg, w_reg, 0.0)
    w_reg = w_reg * w_reg * (3 - 2 * w_reg)
    vn = np.zeros((N, 3))
    for poly in me.polygons:
        n_ = np.array(poly.normal)
        for vi in poly.vertices:
            vn[vi] += n_
    l_ = np.linalg.norm(vn, axis=1); l_[l_ == 0] = 1
    vn /= l_[:, None]
    pr = []
    for e in me.edges:
        a, b = e.vertices
        if np.dot(vn[a], vn[b]) > 0.3:
            pr.append((a, b)); pr.append((b, a))
    pr = np.array(pr, dtype=np.int64)
    dst, src = pr[:, 0], pr[:, 1]
    deg = np.zeros(N)
    np.add.at(deg, dst, 1.0)

    gidx = {g.name: g.index for g in ob.vertex_groups}
    target = [b for b in BONES if b in gidx]
    # 讀全部群組權重（含非目標＝歸一用）
    all_names = [g.name for g in ob.vertex_groups]
    W = np.zeros((len(all_names), N))
    for v in me.vertices:
        for ge in v.groups:
            W[ge.group, v.index] = ge.weight
    tot0 = W.sum(0)
    changed = np.zeros(N)
    for bname in target:
        gi = gidx[bname]
        x = W[gi].copy()
        for _it in range(PAIRS):
            for lam in (0.5, -0.53):
                s_ = np.zeros(N)
                np.add.at(s_, dst, x[src])
                avg = s_ / np.maximum(deg, 1.0)
                dx = avg - x
                dx[deg == 0] = 0.0
                x = x + lam * (w_reg * dx)
        x = np.clip(x, 0.0, 1.0)
        changed = np.maximum(changed, np.abs(x - W[gi]))
        W[gi] = x
    # 逐頂點重歸一（保總和=原總和；未動骨照比例吸收）
    tot1 = W.sum(0)
    scale = np.where(tot1 > 1e-8, tot0 / np.maximum(tot1, 1e-8), 1.0)
    W *= scale[None, :]
    # 寫回（只寫目標骨＋因歸一被縮放的群組＝全部群組直接重寫最保險）
    for g in ob.vertex_groups:
        garr = W[g.index]
        for vi in range(N):
            wv = float(garr[vi])
            if wv > 1e-6:
                g.add([vi], wv, 'REPLACE')
            else:
                try:
                    g.remove([vi])
                except RuntimeError:
                    pass
    me.update()
    ch = changed[changed > 1e-4]
    P(f"{obname}: verts={N} 目標骨={target} 權重變化>1e-4: {len(ch)} p50 {np.percentile(ch,50):.4f} max {changed.max():.4f}")
    unweighted = sum(1 for v in me.vertices if not v.groups)
    P(f"{obname}: unweighted={unweighted}")
    assert unweighted == 0

bpy.ops.wm.save_mainfile(filepath=MASTER)
P("WEIGHT SMOOTH SAVED")
