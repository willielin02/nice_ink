# Jiggle_Belly 權重背側淡出（2026-08-05 user 抓「走路時背部也會跳」）
#
# 定罪數據（審計腳本量測）：肚骨權重 ~13% 質量在身體背側（後腰 107 個 w>=0.3
# 頂點、最高爬到 z=1.26）＋褌背帶 42.5% 質量——成年男性背部不堆脂肪，
# 背側不該跟肚彈簧跳。臀骨（100% 臀帶）與胸骨（100% 前側）權重乾淨不動；
# 褌的臀骨權重（100% 背側）＝丁字帶蓋臀＝正確不動。
#
# 手術：SumoRetopo＋Fundoshi 兩網格的 Jiggle_Belly 權重乘 y 淡出因子——
#   y <= 0.00（前半身）: 1.0（前腹/側腹的肚跳全保留）
#   y >= 0.08（背側）  : 0.0
#   之間線性（側腹→背的平滑過渡帶，防硬邊）
# 權重歸零後該頂點自動回落其餘骨（Blender 骨修飾器/UE 匯入都做逐頂點正規化）。
# 神經縫安全：NeckSeamData 烘焙環的 Jiggle_Belly 權重全在前側（審計 zmaxBack=1.26
# < 環 z1.29）——本手術不觸環頂點、header 免重生。
#
# Run: blender --background --python sumo_jiggle_belly_backfade.py
# 之後照舊：build_sumo_skeletal_fbx.py → ue_import（SK only）
import bpy

MASTER = r"C:\games\Unreal Engine\nice_ink\SourceAssets\sumo_character_master.blend"
Y_FULL = 0.00   # 此前全保留
Y_ZERO = 0.08   # 此後全歸零
GROUP = "Jiggle_Belly"

bpy.ops.wm.open_mainfile(filepath=MASTER)
if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')


def fade(ob):
    g = ob.vertex_groups.get(GROUP)
    assert g is not None, f"{ob.name}: no {GROUP}"
    gi = g.index
    touched = 0
    removed = 0
    for v in ob.data.vertices:
        w = None
        for ge in v.groups:
            if ge.group == gi:
                w = ge.weight
                break
        if w is None or w <= 0.0:
            continue
        y = v.co.y
        if y <= Y_FULL:
            continue
        f = max(0.0, 1.0 - (y - Y_FULL) / (Y_ZERO - Y_FULL))
        nw = w * f
        if nw < 0.005:
            g.remove([v.index])
            removed += 1
        else:
            g.add([v.index], nw, 'REPLACE')
            touched += 1
    print(f"{ob.name}: faded={touched} removed={removed}")


for name in ("SumoRetopo", "Fundoshi"):
    fade(bpy.data.objects[name])

# 術後自檢：真背側（y>Y_ZERO）殘餘質量必須歸零；過渡帶（0.05~Y_ZERO 淡出坡）另報不斷言
for name in ("SumoRetopo", "Fundoshi"):
    ob = bpy.data.objects[name]
    gi = ob.vertex_groups[GROUP].index
    back = 0.0
    band = 0.0
    total = 0.0
    for v in ob.data.vertices:
        for ge in v.groups:
            if ge.group == gi and ge.weight > 0.02:
                total += ge.weight
                if v.co.y > Y_ZERO:
                    back += ge.weight
                elif v.co.y > 0.05:
                    band += ge.weight
    bpct = 100.0 * back / max(total, 1e-9)
    tpct = 100.0 * band / max(total, 1e-9)
    print(f"POST {name}: mass={total:.1f} trueBack%={bpct:.2f} fadeBand%={tpct:.2f}")
    assert bpct < 0.05, f"{name} true back mass not cleared: {bpct}"

bpy.ops.wm.save_mainfile()
print("BACKFADE DONE (master saved)")
