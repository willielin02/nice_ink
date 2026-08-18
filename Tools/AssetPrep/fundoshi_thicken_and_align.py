"""褌三修（2026-08-18）：加厚 + 布紋走向對齊 + 巨觀色調（頂點色）。

**露膚度是硬約束**：內層（貼在身上那一面）一顆頂點都不動 ⇒ 覆蓋輪廓恆等，
加厚全部往**外**長。腳本結束會逐項對賬印出來。

1) 加厚：實測現況厚度中位數僅 0.08cm、範圍 0.02~0.29cm（極不均勻）
   ⇒ 沿既有的內→外配對方向重設為統一 THICK_CM。
2) 布紋走向：UV 是 Smart UV Project 的產物（1377 島、走向 std 21.7°）。
   這條帶子的網格極窄（橫向 2~3 頂點、rim 單一連通），所以「沿纏繞方向」
   直接用 **rim 切線** 估計最穩（帶子的緣本來就沿著帶子跑）。
   逐島把 UV 旋轉到 +u 對齊該方向——只動 UV、不動幾何。
3) 巨觀色調：稽古廻し不洗只曬，會有使用痕跡。寫進頂點色 **G 通道**
   （R=FaceMask 是身體材質在用的，不能碰；褌有自己的材質讀 G）。
   註：黑布上色調變化本來就不明顯，這項收益有限，誠實記帳。

幾何改動後 **custom split normals 會過期** ⇒ 跑完必須重跑
fundoshi_normal_smooth.py（屁溝禁區遮罩照舊）。

Run: blender --background <master.blend> --python fundoshi_thicken_and_align.py
"""
import bpy
import numpy as np
import os
import shutil
from collections import defaultdict

ROOT = r"C:\games\Unreal Engine\nice_ink"
MASTER = os.path.join(ROOT, "SourceAssets", "sumo_character_master.blend")
BACKUP = os.path.join(ROOT, "SourceAssets", "masters",
                      "sumo_character_master_v17_prethicken.blend")

# 2026-08-18 裁決：UV 走向對齊**放棄**（DO_UV_ALIGN=False）。兩個理由，都有量測：
# (a) 實測對齊後誤差 40.6 度 ~= 隨機(45)——這條帶子橫向只有 2~3 頂點、rim 單一連通，
#     rim 鄰居有一半跨到對面邊 => 拿 rim 切線當「沿纏繞方向」的估計本身是壞的；
# (b) 就算修好也幾乎看不到：遊戲距離下織紋週期僅約 2px，法線/細節被 mip 平均，
#     robo A/B 實測 HF 倍率 1.00x。低效益 x 高風險 => 不硬修。
# 程式留著（改 True 即復活），要做的正解是先解出纏繞方向場，不是 rim 切線。
DO_UV_ALIGN = False

THICK_CM = 1.50      # 08-18 二調：5mm 的圓角在螢幕上不到 1px＝看不見。15mm 才讓圓角有 ~4px
TONE_AMP = 0.10      # 頂點色 G 的色調振幅（±10%）
TONE_SCALE = 0.25    # 色調變化的空間尺度（公尺）

if not os.path.exists(BACKUP):
    shutil.copy2(MASTER, BACKUP)
    print(f"BACKUP -> {BACKUP}")

bpy.ops.wm.open_mainfile(filepath=MASTER)
if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')

ob = bpy.data.objects["Fundoshi"]
me = ob.data
n = len(me.vertices)
half = n // 2
assert n == 7134 and half == 3567, f"unexpected vert count {n}"

co = np.empty(n * 3)
me.vertices.foreach_get("co", co)
co = co.reshape(n, 3)
inner_before = co[half:].copy()

# ---------------------------------------------------------------- 1) 加厚
d = co[:half] - co[half:]
L = np.linalg.norm(d, axis=1)
print(f"厚度 before (cm): p1={np.percentile(L,1)*100:.3f} p50={np.percentile(L,50)*100:.3f} "
      f"p99={np.percentile(L,99)*100:.3f}")
bad = L < 1e-6
if bad.any():
    print(f"  退化配對 {int(bad.sum())} 顆 -> 用面法線補")
dirv = np.where(bad[:, None], np.array([0.0, 0.0, 1.0]), d / np.maximum(L, 1e-9)[:, None])
co[:half] = co[half:] + dirv * (THICK_CM / 100.0)
me.vertices.foreach_set("co", co.ravel())
me.update()
Lnew = np.linalg.norm(co[:half] - co[half:], axis=1)
print(f"厚度 after  (cm): p1={np.percentile(Lnew,1)*100:.3f} p50={np.percentile(Lnew,50)*100:.3f} "
      f"p99={np.percentile(Lnew,99)*100:.3f}")
moved_inner = np.abs(co[half:] - inner_before).max()
print(f"**內層位移 max = {moved_inner*1000:.6f} mm（必須是 0＝露膚度零損失）**")
assert moved_inner < 1e-9, "內層被動到了！"

# ---------------------------------------------------------------- 2) 布紋走向
if DO_UV_ALIGN:
  me.calc_loop_triangles()
  tris = np.empty(len(me.loop_triangles) * 3, dtype=np.int64)
  me.loop_triangles.foreach_get("vertices", tris)
  tris = tris.reshape(-1, 3)
  loops = np.empty(len(me.loop_triangles) * 3, dtype=np.int64)
  me.loop_triangles.foreach_get("loops", loops)
  loops = loops.reshape(-1, 3)
  uvl = me.uv_layers[0].data
  uv = np.array([[uvl[i].uv[0], uvl[i].uv[1]] for i in range(len(uvl))])

  # rim（外層中與內層相連者）＋其切線＝沿纏繞方向
  ed = np.empty(len(me.edges) * 2, dtype=np.int64)
  me.edges.foreach_get("vertices", ed)
  ed = ed.reshape(-1, 2)
  cross = ed[(ed[:, 0] < half) != (ed[:, 1] < half)]
  rim = np.unique(np.where(cross < half, cross, -1))
  rim = rim[rim >= 0]
  rimset = set(rim.tolist())
  radj = defaultdict(list)
  for a, b in ed:
      a, b = int(a), int(b)
      if a in rimset and b in rimset:
          radj[a].append(b)
          radj[b].append(a)
  rim_tan = {}
  for v in rim:
      v = int(v)
      nb = radj.get(v, [])
      if len(nb) >= 2:
          t = co[nb[-1]] - co[nb[0]]
      elif len(nb) == 1:
          t = co[nb[0]] - co[v]
      else:
          continue
      ln = np.linalg.norm(t)
      if ln > 1e-9:
          rim_tan[v] = t / ln
  rk = np.array(sorted(rim_tan.keys()))
  rv = np.array([rim_tan[int(k)] for k in rk])
  rpos = co[rk]
  print(f"rim 切線可用 {len(rk)}/{len(rim)}")


  def nearest_rim_dir(P):
      out = np.empty((len(P), 3))
      for i in range(0, len(P), 256):
          c = P[i:i + 256]
          j = np.argmin(((c[:, None, :] - rpos[None, :, :]) ** 2).sum(-1), axis=1)
          out[i:i + 256] = rv[j]
      return out


  # UV 島分群（以 uv 邊連通）
  edge2tri = defaultdict(list)
  for t in range(len(tris)):
      for k in range(3):
          a, b = loops[t, k], loops[t, (k + 1) % 3]
          ka = (round(float(uv[a][0]), 5), round(float(uv[a][1]), 5))
          kb = (round(float(uv[b][0]), 5), round(float(uv[b][1]), 5))
          edge2tri[tuple(sorted([ka, kb]))].append(t)
  tp = list(range(len(tris)))


  def tfind(x):
      while tp[x] != x:
          tp[x] = tp[tp[x]]
          x = tp[x]
      return x


  for e, ts in edge2tri.items():
      for t in ts[1:]:
          ra, rb = tfind(ts[0]), tfind(t)
          if ra != rb:
              tp[ra] = rb
  isl = defaultdict(list)
  for t in range(len(tris)):
      isl[tfind(t)].append(t)
  print(f"UV 島 {len(isl)}")

  tri_centre = co[tris].mean(axis=1)
  tgt_all = nearest_rim_dir(tri_centre)


  def face_frame(t):
      P = co[tris[t]]
      nrm = np.cross(P[1] - P[0], P[2] - P[0])
      ln = np.linalg.norm(nrm)
      return (nrm / ln) if ln > 1e-12 else None


  def uv_u_dir3d(t):
      P, U = co[tris[t]], uv[loops[t]]
      M = np.array([U[1] - U[0], U[2] - U[0]])
      det = M[0, 0] * M[1, 1] - M[0, 1] * M[1, 0]
      if abs(det) < 1e-12:
          return None
      inv = np.array([[M[1, 1], -M[0, 1]], [-M[1, 0], M[0, 0]]]) / det
      du = inv[0, 0] * (P[1] - P[0]) + inv[1, 0] * (P[2] - P[0])
      ln = np.linalg.norm(du)
      return (du / ln) if ln > 1e-9 else None


  uv_new = uv.copy()
  rot_deg = []
  for key, ts in isl.items():
      num = 0.0
      den = 0.0
      for t in ts:
          nrm = face_frame(t)
          du = uv_u_dir3d(t)
          if nrm is None or du is None:
              continue
          tg = tgt_all[t] - nrm * np.dot(tgt_all[t], nrm)
          ln = np.linalg.norm(tg)
          if ln < 1e-9:
              continue
          tg /= ln
          # du 與 tg 在面上的有號夾角（帶子沒有正反向之分 -> 用 2 倍角平均）
          c = np.clip(np.dot(du, tg), -1, 1)
          s = np.dot(np.cross(du, tg), nrm)
          ang = np.arctan2(s, c)
          num += np.sin(2 * ang)
          den += np.cos(2 * ang)
      if den == 0.0 and num == 0.0:
          continue
      phi = 0.5 * np.arctan2(num, den)      # 需要把 UV 轉這麼多，+u 才會指向 tg
      rot_deg.append(np.degrees(phi))
      ls = np.unique(loops[ts].ravel())
      c0 = uv[ls].mean(axis=0)
      ca, sa = np.cos(-phi), np.sin(-phi)   # UV 轉 -phi <=> 3D 像轉 +phi
      R = np.array([[ca, -sa], [sa, ca]])
      uv_new[ls] = (uv[ls] - c0) @ R.T + c0

  for i in range(len(uvl)):
      uvl[i].uv = (float(uv_new[i][0]), float(uv_new[i][1]))
  print(f"島旋轉量(度) |mean|={np.mean(np.abs(rot_deg)):.1f} max={np.max(np.abs(rot_deg)):.1f}")

# ---------------------------------------------------------------- 3) 巨觀色調 -> 頂點色 G
ca = me.color_attributes["FaceMask"]
arr = np.empty(len(ca.data) * 4)
ca.data.foreach_get("color", arr)
arr = arr.reshape(-1, 4)
rng = np.random.default_rng(20260818)
# 三個隨機低頻正弦疊加＝連續、無接縫、無重複週期問題（頂點色不平鋪）
tone = np.zeros(n)
for _ in range(3):
    k = rng.standard_normal(3)
    k /= np.linalg.norm(k)
    tone += np.sin(co @ k / TONE_SCALE * 2 * np.pi + rng.uniform(0, 6.28))
tone /= 3.0
g = np.clip(1.0 + TONE_AMP * tone, 0.0, 2.0)
arr[:, 1] = g
ca.data.foreach_set("color", arr.ravel())
print(f"頂點色 G: min={g.min():.3f} max={g.max():.3f} (R 未動＝FaceMask 保持 {arr[:,0].max():.3f})")

bpy.ops.wm.save_mainfile()
print("SAVED master")
print("NEXT: 重跑 fundoshi_normal_smooth.py（幾何動過，custom normals 已過期）")
