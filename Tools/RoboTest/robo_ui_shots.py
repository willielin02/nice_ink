# UI 全站改版的真機截圖（2026-09-04；user：「全部完成並都有截圖量測清楚確認無誤」）。
# 拍三個狀態：大廳／作畫相位站著／鎖定作畫。截圖之後由
# Tools/UiCheck/shot_measure.py 離線量版面不變量（邊距、單一右緣、列距、對比）。
# 產出：Saved/robo_ui_shots_result.txt + Saved/Screenshots/WindowsEditor/uishot_*.png
import time
import traceback

import unreal

OUT = "C:/games/Unreal Engine/nice_ink/Saved/robo_ui_shots_result.txt"
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[UISHOT] " + str(msg))
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(LINES))


def get_world(tag):
    for w in unreal.ObjectIterator(unreal.World):
        if tag in w.get_path_name():
            return w
    return None


def find_char(w, pid):
    for c in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NiceInkCharacter):
        ps = c.get_editor_property("player_state")
        if ps and ps.get_editor_property("player_id") == pid:
            return c
    return None


class Shots:
    def __init__(self):
        self.stage = "boot"
        self.stage_t = time.monotonic()
        self.shot_i = 0
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    def advance(self, st):
        self.stage = st
        self.stage_t = time.monotonic()
        log("STAGE -> " + st)

    def elapsed(self):
        return time.monotonic() - self.stage_t

    def server(self):
        return get_world("UEDPIE_0")

    def gs(self):
        w = self.server()
        return unreal.GameplayStatics.get_game_state(w) if w else None

    def shot(self, name):
        # **HighResShot 不含 Slate UI**（2026-09-08 血價；09-09 才發現這一支沒跟上——
        # robo_ui_shots_all.py 當天改好了，兄弟檔沒改 ⇒ 本檔從那天起拍的每一張都沒有 HUD，
        # 而畫面本身是好的＝**工具對受測物是瞎的，卻照樣交出綠燈**）。
        # 'Shot showui' 必須走 PlayerController 的 exec 鏈 ⇒ 第三個參數 specific_player。
        pc = unreal.GameplayStatics.get_player_controller(self.server(), 0)
        unreal.SystemLibrary.execute_console_command(
            self.server(), "Shot showui filename=uishot_%s" % name, pc)
        log("SHOT uishot_%s" % name)

    def tick(self, dt):
        try:
            self.step()
        except Exception:
            log("EXC:\n" + traceback.format_exc())
            self.finish()

    def step(self):
        w = self.server()
        if not w and self.stage not in ("boot", "wait_pie"):
            if self.elapsed() > 60:
                log("FAIL no PIE world")
                self.finish()
            return
        gs = self.gs()
        phase = gs.get_editor_property("CurrentPhase") if gs else None

        if self.stage == "boot":
            # 既有 robo 腳本的作法：等 8 秒讓編輯器安定，再由腳本自己開 PIE
            if self.elapsed() > 8.0:
                unreal.get_editor_subsystem(
                    unreal.LevelEditorSubsystem).editor_request_begin_play()
                self.advance("wait_pie")
            return

        if self.stage == "wait_pie":
            if gs:
                # **主機必須是作畫者**：不指定的話 host 有機會被抽中當受害者，
                # 那一輪的「站著／鎖定」兩張就會通通拍成夢裡的畫面（實際踩過）。
                gm = unreal.GameplayStatics.get_game_mode(w)
                if gm:
                    gm.set_editor_property("DebugForcedVictimSeat", 1)
                self.advance("lobby")
            elif self.elapsed() > 60:
                log("FAIL PIE never came up")
                self.finish()
            return

        if self.stage == "lobby":
            # 大廳：拍一張（PIE 會自動開局，所以要趁早）
            self.shot("01_lobby")
            self.advance("wait_draw")
            return

        if self.stage == "wait_draw":
            if str(phase).endswith("DRAWING: 3>"):
                self.advance("draw_stand")
            elif self.elapsed() > 90:
                log("FAIL never reached Drawing (phase=%s)" % phase)
                self.finish()
            return

        if self.stage == "draw_stand":
            if self.elapsed() > 2.0:
                self.shot("02_draw_standing")
                self.advance("enter_lean")
            return

        if self.stage == "enter_lean":
            host = unreal.GameplayStatics.get_player_pawn(w, 0)
            vid = gs.get_editor_property("VictimPlayerId")
            victim = find_char(w, vid)
            if host and victim:
                bt = victim.get_editor_property("Body").get_world_transform()
                p = bt.transform_location(unreal.Vector(0.0, 26.0, 95.0))
                n = bt.transform_direction(unreal.Vector(0.0, 1.0, 0.0))
                ok = host.call_method("DebugRoboEnterLean", (victim, p, n))
                log("DebugRoboEnterLean -> %s" % ok)
                self.advance("draw_locked")
            elif self.elapsed() > 20:
                log("FAIL no host/victim")
                self.finish()
            return

        if self.stage == "draw_locked":
            if self.elapsed() > 2.5:
                self.shot("03_draw_locked")
                self.advance("done")
            return

        if self.stage == "done":
            if self.elapsed() > 2.0:
                log("DONE 3 shots")
                self.finish()
            return

    def finish(self):
        log("HARNESS END")
        if self.handle:
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.handle = None


_s = Shots()
log("harness registered")
