# 電視節目影格自查（2026-08-28）：把 NiceInkTvFilm 的 5 分鏡 × 6 時點排成一張 PNG。
#
# **不用開 PIE**——RenderFrame 是純 CPU 函式，開了編輯器就能叫。低解析度美術要迭代，
# 這是唯一划算的閘門（每改一格跑一輪 PIE 開場動畫＝四分鐘）。
#
# 鐵坑（2026-08-28 實測）：**headless `-ExecutePythonScript` 看不到遊戲模組的類別**
#   （`dir(unreal)` 裡一個 NiceInk* 都沒有、`load_class("/Script/NiceInk.…")` 回 None，
#   連既有 robo 腳本天天在用的 `unreal.NiceInkCharacter` 也不存在）。
#   既有的 headless 腳本（資產重匯入那批）只碰引擎／編輯器類別，所以從來沒踩到。
#   ⇒ 凡是要叫專案自己 UFUNCTION 的自駕，一律走 **GUI 編輯器 + StartupScripts**。
#
# 流程（照 CLAUDE.md 的 robo 流程，只是不進 PIE）：
#   1. 把 `+StartupScripts=<本檔絕對路徑>` 加進 Config/DefaultEngine.ini 的
#      `[/Script/PythonScriptPlugin.PythonScriptPluginSettings]`（用 Edit 精準替換）
#   2. 先刪 Saved/Autosaves，再啟動 UnrealEditor.exe
#   3. 等 Saved/TvFilm/robo_tv_filmsheet.txt 出現 DONE/EXC
#   4. **把 ini 那行移除**——提交的 config 永遠不能帶著它
#
# 產出：Saved/TvFilm/tv_film_sheet.png（2× 放大 ≈ user 特寫時的真實表觀大小：
#       RT 512 貼到畫面約 290px ⇒ 一個影格像素 ≈ 2.3 螢幕像素）
import os
import time
import traceback

import unreal

OUT_DIR = r"C:\games\Unreal Engine\nice_ink\Saved\TvFilm"
PNG = os.path.join(OUT_DIR, "tv_film_sheet.png")
TXT = os.path.join(OUT_DIR, "robo_tv_filmsheet.txt")
FRAME_DIR = os.path.join(OUT_DIR, "frames")
LINES = []

os.makedirs(OUT_DIR, exist_ok=True)


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[TVFILM] " + str(msg))
    with open(TXT, "w", encoding="utf-8") as f:
        f.write("\n".join(LINES))


class Sheet:
    def __init__(self):
        self.t0 = time.monotonic()
        self.done = False
        self.handle = unreal.register_slate_post_tick_callback(self.tick)
        log("BOOT")

    def tick(self, dt):
        if self.done or time.monotonic() - self.t0 < 5.0:
            return
        self.done = True
        try:
            # 舊圖先清掉：殘留的上一版會讓人以為已經改好（HighResShot 同名不覆蓋的同族陷阱）
            if os.path.exists(PNG):
                os.remove(PNG)
            if os.path.isdir(FRAME_DIR):
                for f in os.listdir(FRAME_DIR):
                    if f.endswith(".png"):
                        os.remove(os.path.join(FRAME_DIR, f))
            unreal.NiceInkTvSet.dump_film_contact_sheet(PNG, 2)
            if os.path.exists(PNG):
                log("WROTE %s (%d bytes)" % (PNG, os.path.getsize(PNG)))
            else:
                log("FAIL: no sheet written")
            # 逐格傾印（細看用）：每分鏡 9 張、6× 最近鄰放大、秒數＝真實鏡長
            unreal.NiceInkTvSet.dump_film_frames(FRAME_DIR, 6, 9, 10.4)
            n = len([f for f in os.listdir(FRAME_DIR) if f.endswith(".png")]) if os.path.isdir(FRAME_DIR) else 0
            log("FRAMES %d -> %s" % (n, FRAME_DIR))
            log("DONE" if n > 0 else "FAIL: no frames written")
        except Exception as exc:
            log("EXC " + repr(exc))
            log(traceback.format_exc())
        try:
            unreal.unregister_slate_post_tick_callback(self.handle)
        except Exception:
            pass
        unreal.SystemLibrary.quit_editor()  # 不在 PIE 裡 ⇒ 直接 quit 安全


Sheet()
