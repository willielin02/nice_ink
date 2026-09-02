# -*- coding: utf-8 -*-
"""托盤游標增益的離線契約（不開引擎）。TrayCursorPixelsPerCount() 的 1:1 重現。
守的是三條無聲的失敗：FOV 一改就漂、玩家的靈敏度設定被忽略、解析度愈高愈鈍。"""
AXIS_SENS = 0.07       # DefaultInput.ini AxisConfig MouseX Sensitivity
FOVSCALE  = 0.011110   # DefaultInput.ini FOVScale
FOV_ON    = True       # bEnableFOVScaling

def engine_delta(raw, fov):
    """引擎 MassageAxisInput 交給 GetInputMouseDelta 的值"""
    return raw * AXIS_SENS * ((FOVSCALE * fov) if FOV_ON else 1.0)

def px_per_count(fov, ui_scale, gain=1.0, player=1.0, undo_fov=True):
    """TrayCursorPixelsPerCount()：像素／每一單位 GetInputMouseDelta 回傳值"""
    fov_mul = (FOVSCALE * fov) if (FOV_ON and undo_fov) else 1.0
    undo = max(AXIS_SENS, 1e-8) * max(fov_mul, 1e-8)
    return (1.0 / undo) * gain * player * max(ui_scale, 0.25)

def px_per_raw(fov, ui_scale, gain=1.0, player=1.0, undo_fov=True):
    """玩家真正感覺到的：實體滑鼠移動一單位 → 游標走幾個螢幕像素"""
    return engine_delta(1.0, fov) * px_per_count(fov, ui_scale, gain, player, undo_fov)

fails, checks = [], 0
def ck(name, ok, detail=""):
    global checks
    checks += 1
    if not ok:
        fails.append("FAIL %s  %s" % (name, detail))

# c16 **FOV 無關**：作畫 FOV 怎麼改，游標速度不准變（這是整條修正的目的）
base = px_per_raw(36.0, 1.0)
for fov in (24.0, 36.0, 60.0, 90.0, 110.0):
    v = px_per_raw(fov, 1.0)
    ck("c16 FOV-independent @%.0f" % fov, abs(v - base) < 1e-6,
       "px/raw=%.4f vs base %.4f" % (v, base))

# c17 **桌面指標對齊**：1080p、增益 1.0、玩家設定 1.0 ⇒ 1 像素/單位（Windows 預設同速）
ck("c17 desktop parity @1080p", abs(px_per_raw(36.0, 1.0) - 1.0) < 1e-6,
   "px/raw=%.4f" % px_per_raw(36.0, 1.0))

# c18 **跟著 UI 縮放**：格子隨解析度變大，橫越杯陣所需的實體移動量必須不變
CELL, GAP, COLS = 46.0, 6.0, 10
for vh in (720, 1080, 1398, 1440, 2160):
    S = max(vh / 1080.0, 0.25)
    grid_w = (COLS * CELL + (COLS - 1) * GAP) * S
    counts = grid_w / px_per_raw(36.0, S)
    ck("c18 constant hand travel @%dp" % vh, abs(counts - 514.0) < 1.0,
       "counts to cross grid=%.1f (want 514)" % counts)

# c19 玩家的 MouseSensitivityScale 必須生效（此前托盤是全遊戲唯一忽略它的地方）
for ps in (0.2, 1.0, 3.0):
    ck("c19 player sensitivity passes through x%.1f" % ps,
       abs(px_per_raw(36.0, 1.0, player=ps) - ps) < 1e-6,
       "px/raw=%.4f" % px_per_raw(36.0, 1.0, player=ps))

# c20 口味旋鈕線性
ck("c20 gain is a plain multiplier", abs(px_per_raw(36.0, 1.0, gain=2.5) - 2.5) < 1e-6)

print(chr(10).join(fails) if fails else "ALL PASS")
print("checks=%d  fail=%d" % (checks, len(fails)))
print()
print("--- 修前 / 修後（你的 2559x1398、800 DPI、杯陣寬 666px）---")
S = 1398/1080.0
old_px_raw = engine_delta(1.0, 36.0) * 1.6          # 舊：delta 直接 ×1.6
new_px_raw = px_per_raw(36.0, S)
for tag, v in (("修前", old_px_raw), ("修後", new_px_raw)):
    print("  %s: %.4f 像素/單位   橫越杯陣 %.0f 單位 = %.1f cm"
          % (tag, v, 666.0/v, 666.0/v/800*2.54))
