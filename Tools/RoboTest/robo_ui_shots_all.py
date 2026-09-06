# 全站 UI 稽核用的真機截圖（2026-09-05）。robo_ui_shots.py 只拍三態（大廳／站著／
# 鎖定），而 ui_coverage.py 的缺口全在**後半場相位**（巡禮／指認／判決／場間刺青房）
# ——那幾個面至今零截圖證據。這支把整局跑完，沿路每個相位各拍一張。
#
# 兩種角色分歧要跑兩次（HighResShot 只拍伺服器視窗）：
#   VICTIM_SEAT=1 → host 是作畫者（本檔預設；拍前半場＋巡禮／指認旁觀／判決／刺青房）
#   VICTIM_SEAT=0 → host 是受害者（拍醉夢描圖／裝睡／指認本人）
# 由環境變數 NI_VICTIM_SEAT 指定，預設 1。
#
# 產出：Saved/robo_ui_shots_all_result.txt + Saved/Screenshots/WindowsEditor/uiall_*.png
import os
import time
import traceback

import unreal

VICTIM_SEAT = int(os.environ.get("NI_VICTIM_SEAT", "1"))
TAG = "art" if VICTIM_SEAT == 1 else "vic"
OUT = "C:/games/Unreal Engine/nice_ink/Saved/robo_ui_shots_all_%s.txt" % TAG
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[UIALL] " + str(msg))
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
        self.t0 = time.monotonic()
        self.strokes = 0
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    def advance(self, st):
        self.stage = st
        self.stage_t = time.monotonic()
        log("STAGE -> %s  (phase=%s, t=%.0f)" % (st, self.phase(), time.monotonic() - self.t0))

    def elapsed(self):
        return time.monotonic() - self.stage_t

    def server(self):
        return get_world("UEDPIE_0")

    def gs(self):
        w = self.server()
        return unreal.GameplayStatics.get_game_state(w) if w else None

    def gm(self):
        w = self.server()
        return unreal.GameplayStatics.get_game_mode(w) if w else None

    def phase(self):
        gs = self.gs()
        if not gs:
            return "-"
        return str(gs.get_editor_property("CurrentPhase"))

    def shot(self, name):
        unreal.SystemLibrary.execute_console_command(
            self.server(), "HighResShot 1920x1080 filename=uiall_%s_%s" % (TAG, name))
        log("SHOT uiall_%s_%s" % (TAG, name))

    def tick(self, dt):
        try:
            self.step()
        except Exception:
            log("EXC:\n" + traceback.format_exc())
            self.finish()

    # 每一站都自帶逾時：拍不到就往下走，不要讓整輪一站卡死全部無圖
    def step(self):
        s = self.stage
        w = self.server()
        gs = self.gs()
        ph = self.phase()

        if s == "boot":
            if self.elapsed() > 8.0:
                unreal.get_editor_subsystem(
                    unreal.LevelEditorSubsystem).editor_request_begin_play()
                self.advance("wait_pie")
            return

        if s == "wait_pie":
            gm = self.gm()
            if gm:
                gm.set_editor_property("DebugForcedVictimSeat", VICTIM_SEAT)
                # 演出計時放長一點才拍得到（預設會一閃而過）
                try:
                    gm.set_editor_property("tour_seconds_per_work", 6.0)
                    gm.set_editor_property("resolution_seconds", 8.0)
                    gm.set_editor_property("finale_seconds", 8.0)
                except Exception as e:
                    log("timing tweak skipped: %s" % e)
                self.advance("lobby")
            elif self.elapsed() > 90:
                log("FAIL no PIE")
                self.finish()
            return

        if s == "lobby":
            if self.elapsed() > 1.0:
                self.shot("01_lobby")
                self.advance("esc_open")
            return

        # ESC 系統選單（模態面板；2026-09-06 補拍）：鉤子開→拍→關
        if s == "esc_open":
            if self.elapsed() > 1.0:
                host = unreal.GameplayStatics.get_player_pawn(w, 0)
                try:
                    host.call_method("DebugRoboSystemMenu", (True,))
                except Exception as e:
                    log("esc open failed: %s" % e)
                self.advance("esc_shot")
            return

        if s == "esc_shot":
            if self.elapsed() > 1.5:
                self.shot("01b_esc_menu")
                host = unreal.GameplayStatics.get_player_pawn(w, 0)
                try:
                    host.call_method("DebugRoboSystemMenu", (False,))
                except Exception:
                    pass
                self.advance("wait_spin")
            return

        # 開場儀式：轉瓶／入座各拍一張（ui_coverage 說這兩個相位祈使句有、操作列無）
        if s == "wait_spin":
            if "BOTTLESPIN" in ph.upper():
                self.shot("02_bottlespin")
                self.advance("wait_seating")
            elif "SEATING" in ph.upper():
                self.advance("wait_seating")
            elif "DRAWING" in ph.upper():
                self.advance("draw_stand")
            elif self.elapsed() > 90:
                self.advance("wait_draw")
            return

        if s == "wait_seating":
            if "SEATING" in ph.upper():
                if self.elapsed() > 1.5:
                    self.shot("03_seating")
                    self.advance("wait_draw")
            elif "DRAWING" in ph.upper():
                self.advance("draw_stand")
            elif self.elapsed() > 60:
                self.advance("wait_draw")
            return

        if s == "wait_draw":
            if "DRAWING" in ph.upper():
                self.advance("draw_stand")
            elif self.elapsed() > 120:
                log("FAIL never reached Drawing (phase=%s)" % ph)
                self.finish()
            return

        # --- 受害者路線（host 是受害者：沉睡描圖畫面）---
        if s == "draw_stand" and VICTIM_SEAT == 0:
            if self.elapsed() > 3.0:
                self.shot("04_dream_trace")
                self.advance("dream2")
            return

        if s == "dream2":
            if self.elapsed() > 6.0:
                self.shot("05_dream_trace_b")
                self.advance("mkwork")
            return

        # --- 作畫者路線 ---
        if s == "draw_stand":
            if self.elapsed() > 2.5:
                self.shot("04_draw_standing")
                self.advance("enter_lean")
            return

        if s == "enter_lean":
            host = unreal.GameplayStatics.get_player_pawn(w, 0)
            vid = gs.get_editor_property("VictimPlayerId") if gs else -1
            victim = find_char(w, vid)
            if host and victim:
                bt = victim.get_editor_property("Body").get_world_transform()
                p = bt.transform_location(unreal.Vector(0.0, 26.0, 95.0))
                n = bt.transform_direction(unreal.Vector(0.0, 1.0, 0.0))
                log("DebugRoboEnterLean -> %s" % host.call_method("DebugRoboEnterLean", (victim, p, n)))
                self.advance("draw_locked")
            elif self.elapsed() > 20:
                log("WARN no host/victim; skip lean")
                self.advance("mkwork")
            return

        if s == "draw_locked":
            if self.elapsed() > 2.5:
                self.shot("05_draw_locked")
                self.advance("tray")
            return

        # 墨杯盤＝唯一的模態面板（規範說「模態才有面板」）——直接翻旗標拍一張
        if s == "tray":
            if self.elapsed() > 0.5:
                host = unreal.GameplayStatics.get_player_pawn(w, 0)
                try:
                    host.set_editor_property("bInkTrayOpen", True)
                    log("tray opened")
                except Exception as e:
                    log("tray open failed: %s" % e)
                self.advance("tray_shot")
            return

        if s == "tray_shot":
            if self.elapsed() > 1.5:
                self.shot("06_ink_tray")
                host = unreal.GameplayStatics.get_player_pawn(w, 0)
                try:
                    host.set_editor_property("bInkTrayOpen", False)
                except Exception:
                    pass
                self.advance("mkwork")
            return

        # 巡禮要有作品才進得去：下幾筆
        if s == "mkwork":
            if self.elapsed() > 1.0:
                gm = self.gm()
                gm.call_method("DebugRoboStroke",
                               (unreal.Vector2D(0.40, 0.40), unreal.Vector2D(0.52, 0.52), 2))
                gm.call_method("DebugRoboStroke",
                               (unreal.Vector2D(0.52, 0.52), unreal.Vector2D(0.44, 0.58), 5))
                self.strokes += 1
                self.advance("emerge")
            return

        if s == "emerge":
            if self.elapsed() > 2.0:
                self.gm().call_method("DebugRoboEmerge", ())
                self.advance("wait_tour")
            return

        if s == "wait_tour":
            if "TOUR" in ph.upper():
                self.advance("tour_shot")
            elif self.elapsed() > 40:
                log("WARN never reached Tour (phase=%s)" % ph)
                self.advance("wait_accuse")
            return

        if s == "tour_shot":
            if self.elapsed() > 2.5:
                self.shot("07_tour")
                self.advance("wait_accuse")
            return

        if s == "wait_accuse":
            if "ACCUSATION" in ph.upper():
                self.advance("accuse_shot")
            elif self.elapsed() > 60:
                log("WARN never reached Accusation (phase=%s)" % ph)
                self.finish()
            return

        if s == "accuse_shot":
            if self.elapsed() > 2.5:
                self.shot("08_accusation")
                self.advance("accuse_do")
            return

        if s == "accuse_do":
            if self.elapsed() > 2.0:
                self.gm().call_method("DebugRoboAccuse", (True,))
                self.advance("wait_resolution")
            return

        if s == "wait_resolution":
            if "RESOLUTION" in ph.upper():
                self.advance("resolution_shot")
            elif self.elapsed() > 30:
                log("WARN never reached Resolution (phase=%s)" % ph)
                self.finish()
            return

        if s == "resolution_shot":
            if self.elapsed() > 2.0:
                self.shot("09_resolution")
                self.advance("wait_after")
            return

        # 猜對＝受害者換人；猜錯三次才進 Finale/PostGame。這裡只要拍到下一個面就好
        if s == "wait_after":
            up = ph.upper()
            if "POSTGAME" in up:
                self.advance("postgame_shot")
            elif "FINALE" in up:
                self.shot("10_finale")
                self.advance("wait_postgame")
            elif "SEATING" in up or "DRAWING" in up:
                self.shot("10_next_round")
                self.advance("force_finale")
            elif self.elapsed() > 40:
                log("WARN stuck after resolution (phase=%s)" % ph)
                self.advance("done")
            return

        # 結局／場間（2026-09-06 補拍）：兩人房走不到三杯 ⇒ 鉤子直進 Finale
        if s == "force_finale":
            if self.elapsed() > 2.0:
                self.gm().call_method("DebugRoboFinale", ())
                self.advance("wait_finale")
            return

        if s == "wait_finale":
            if "FINALE" in ph.upper():
                if self.elapsed() > 2.5:
                    self.shot("10_finale")
                    self.advance("wait_postgame")
            elif self.elapsed() > 30:
                log("WARN never reached Finale (phase=%s)" % ph)
                self.advance("done")
            return

        if s == "wait_postgame":
            if "POSTGAME" in ph.upper():
                self.advance("postgame_shot")
            elif self.elapsed() > 40:
                self.advance("done")
            return

        if s == "postgame_shot":
            if self.elapsed() > 2.5:
                self.shot("11_postgame")
                self.advance("done")
            return

        # 右下角比分（2026-09-05）：規則塊與比分**分時共用同一個角**，判準是
        # 受害者的罰酒杯 > 0。猜對不會生杯子、猜錯需要第三位玩家（PIE 只有兩位）
        # ⇒ 直接把杯數寫進受害者的 PlayerState 來拍**顯示**。
        # 這是拍畫面不是驗規則：規則（誰在什麼時候生杯子）由 fullloop 測試守。
        if s == "done":
            if self.elapsed() > 2.0:
                gs = self.gs()
                vid = gs.get_editor_property("VictimPlayerId") if gs else -1
                for ps in gs.get_editor_property("player_array") if gs else []:
                    if ps.get_editor_property("player_id") == vid:
                        ps.set_editor_property("PenaltyCups", 2)
                        log("forced PenaltyCups=2 on victim %d" % vid)
                        break
                self.advance("score_shot")
            return

        if s == "score_shot":
            if self.elapsed() > 2.0:
                self.shot("12_score_corner")
                self.advance("wrap")
            return

        if s == "wrap":
            if self.elapsed() > 3.0:
                log("DONE")
                self.finish()
            return

    def finish(self):
        log("HARNESS END")
        if self.handle:
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.handle = None


_s = Shots()
log("harness registered (VICTIM_SEAT=%d, TAG=%s)" % (VICTIM_SEAT, TAG))
