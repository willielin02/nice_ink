# 全站 UI 稽核用的真機截圖（2026-09-05；2026-09-07 二版補齊缺的畫面）。
# robo_ui_shots.py 只拍三態（大廳／站著／鎖定），而 ui_coverage.py 的缺口全在
# **後半場相位**（巡禮／指認／判決／場間刺青房）——這支把整局跑完，沿路每個相位各拍一張。
#
# 二版補拍（一版拍不到的五個面）：
#   墨杯盤開著（一版直設 bInkTrayOpen，下一 tick 就被 RMB 輪詢收掉 ⇒ 改走 DebugRoboInkTray 鉤子）
#   搖夢：受害者端的搖晃＋攻擊者顯名／作畫者端的回執（DebugRoboShake）
#   睜眼未現身／環視／裝睡黑屏（DebugRoboWake → DebugRoboSleepLook → DebugRoboFeignSleep）
#   場間刺青房（一版比對字串寫 "POSTGAME"，引擎印的是 "POST_GAME" ⇒ 永遠比不中）
#   ESC 選單再試一次（一版拍到的是大廳，開了沒畫上）；作畫站著時也拍一張
#   猜錯（真作者上碳黑）＝兩人房做不到（DebugRoboAccuse(false) 需要第三人；3-client PIE OOM 鐵坑）
#
# 兩種角色分歧要跑兩次（HighResShot 只拍伺服器視窗）：
#   VICTIM_SEAT=1 → host 是作畫者（本檔預設；拍前半場＋巡禮／指認旁觀／判決／刺青房）
#   VICTIM_SEAT=0 → host 是受害者（拍醉夢描圖／搖晃／睜眼／裝睡／指認本人）
# 由環境變數 NI_VICTIM_SEAT 指定，預設 1。
#
# 產出：Saved/robo_ui_shots_all_<tag>.txt + Saved/Screenshots/WindowsEditor/uiall_*.png
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

    def host(self):
        w = self.server()
        return unreal.GameplayStatics.get_player_pawn(w, 0) if w else None

    def local_pawns(self):
        # 截圖視窗會隨焦點在 server／client 之間跳（2026-09-07 五輪實錘），
        # 純本地旗標（ESC／墨杯盤／臉指向）一律同時設在兩個世界的本地角色上。
        out = []
        for w in unreal.ObjectIterator(unreal.World):
            if "UEDPIE_" not in w.get_path_name():
                continue
            for c in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NiceInkCharacter):
                if c.is_locally_controlled():
                    out.append(c)
        if not out:
            out = [self.host()]
        return out

    def call_local(self, name, args=()):
        for c in self.local_pawns():
            self.call(c, name, args)

    def phase(self):
        gs = self.gs()
        if not gs:
            return "-"
        return str(gs.get_editor_property("CurrentPhase"))

    def shot(self, name):
        if os.environ.get("NI_NOSHOT"):
            log("SKIPSHOT %s" % name)   # 診斷：不拍圖，看 PIE 本身活不活
            return
        # **HighResShot 不含 Slate UI**（2026-09-08 血價：局內 HUD 全面 Slate 化之後，
        # 這條驗證線拍出來的每一張都沒有 HUD，而畫面本身是好的——**工具對受測物是瞎的**）。
        # 'Shot showui' 必須走 PlayerController 的 exec 鏈（NiShot 的註解早就寫了），
        # 所以 execute_console_command 要帶第三個參數 specific_player。
        pc = unreal.GameplayStatics.get_player_controller(self.server(), 0)
        unreal.SystemLibrary.execute_console_command(
            self.server(), "Shot showui filename=uiall_%s_%s" % (TAG, name), pc)
        log("SHOT uiall_%s_%s" % (TAG, name))

    def probe_menu(self):
        try:
            pc = unreal.GameplayStatics.get_player_controller(self.server(), 0)
            log("probe show_mouse_cursor=%s" % pc.get_editor_property("show_mouse_cursor"))
        except Exception as e:
            log("probe menu failed: %s" % e)

    def call(self, obj, name, args=()):
        try:
            r = obj.call_method(name, args)
            log("%s%s -> %s" % (name, args, r))
            return r
        except Exception as e:
            log("%s failed: %s" % (name, e))
            return None

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
        up = ph.upper()

        if s == "boot":
            if self.elapsed() > 8.0:
                # 編輯器失焦節流（陷阱年鑑：Use Less CPU in Background ⇒ 3~6fps）：user 在機時編輯器
                # 永遠在背景；壞掉的 log 全有「max tick rate 3」、接著 D3D12 E_OUTOFMEMORY。
                # 屬性名要用原始 bThrottleCPUWhenNotForeground（snake 名 5.7 解析失敗）。
                try:
                    cdo = unreal.find_object(None, "/Script/UnrealEd.Default__EditorPerformanceSettings")
                    if cdo:
                        cdo.set_editor_property("bThrottleCPUWhenNotForeground", False)
                        log("throttle off")
                    else:
                        log("WARN EditorPerformanceSettings CDO not found")
                except Exception as e:
                    log("WARN throttle off failed: %s" % e)
                les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
                if les is None:
                    if self.elapsed() > 120.0:
                        log("FAIL LevelEditorSubsystem never came up")
                        self.finish()
                    return
                les.editor_request_begin_play()
                self.advance("wait_pie")
            return

        if s == "wait_pie":
            gm = self.gm()
            if gm:
                gm.set_editor_property("DebugForcedVictimSeat", VICTIM_SEAT)
                try:
                    gm.set_editor_property("tour_seconds_per_work", 6.0)
                    gm.set_editor_property("resolution_seconds", 8.0)
                    gm.set_editor_property("finale_seconds", 8.0)
                except Exception as e:
                    log("timing tweak skipped: %s" % e)
                if os.environ.get("NI_HUDOFF"):
                    for w2 in unreal.ObjectIterator(unreal.World):
                        if "UEDPIE_" in w2.get_path_name():
                            unreal.SystemLibrary.execute_console_command(w2, "showhud")
                    log("HUD OFF (diag)")
                self.advance("lobby")
            elif self.elapsed() > 90:
                log("FAIL no PIE")
                self.finish()
            return

        if s == "lobby":
            if self.elapsed() > 1.0:
                self.shot("01_lobby")
                # ESC 只在作畫者路線拍（受害者路線在這一站卡死過一次：無崩潰、無 EXC、tick 停擺）
                self.advance("esc_open" if VICTIM_SEAT == 1 else "wait_spin")
            return

        # ESC 系統選單：鉤子開→等 2.5s→拍→關（一版 1.5s 拍到的是沒開的大廳）
        if s == "esc_open":
            if self.elapsed() > 1.0:
                self.call_local("DebugRoboSystemMenu", (True,))
                self.advance("esc_shot")
            return

        if s == "esc_shot":
            if self.elapsed() > 2.5:
                self.probe_menu()
                self.shot("01b_esc_menu")
                self.advance("esc_close")
            return

        if s == "esc_close":
            if self.elapsed() > 0.6:
                self.call_local("DebugRoboSystemMenu", (False,))
                self.advance("wait_spin")
            return

        if s == "wait_spin":
            if os.environ.get("NI_MEMREPORT") and not getattr(self, "_memrep", False) and self.elapsed() > 2.0:
                self._memrep = True
                unreal.SystemLibrary.execute_console_command(self.server(), "memreport -full")
                log("MEMREPORT issued")
            if "BOTTLESPIN" in up:
                self.shot("02_bottlespin")
                self.advance("wait_seating")
            elif "SEATING" in up:
                self.advance("wait_seating")
            elif "DRAWING" in up:
                self.advance("draw_stand")
            elif self.elapsed() > 90:
                self.advance("wait_draw")
            return

        if s == "wait_seating":
            if "SEATING" in up:
                if self.elapsed() > 1.5:
                    self.shot("03_seating")
                    self.advance("wait_draw")
            elif "DRAWING" in up:
                self.advance("draw_stand")
            elif self.elapsed() > 60:
                self.advance("wait_draw")
            return

        if s == "wait_draw":
            if "DRAWING" in up:
                self.advance("draw_stand")
            elif self.elapsed() > 120:
                log("FAIL never reached Drawing (phase=%s)" % ph)
                self.finish()
            return

        # ---------------- 受害者路線（host 是受害者）----------------
        if s == "draw_stand" and VICTIM_SEAT == 0:
            if self.elapsed() > 3.0:
                self.shot("04_dream_trace")
                self.advance("vic_shake")
            return

        # 搖夢：攻擊者＝第一位非受害者；受害者端要看到搖晃＋攻擊者顯名
        if s == "vic_shake":
            if self.elapsed() > 2.0:
                self.call(self.gm(), "DebugRoboShake", ())
                self.advance("vic_shake_shot")
            return

        if s == "vic_shake_shot":
            if self.elapsed() > 0.7:
                self.shot("05_dream_shaken")
                self.advance("vic_wake")
            return

        # 睜眼未現身：第一人稱、滑鼠＝臉指向、WASD 才現身
        if s == "vic_wake":
            if self.elapsed() > 2.5:
                self.call(self.gm(), "DebugRoboWake", ())
                self.advance("vic_awake_shot")
            return

        if s == "vic_awake_shot":
            if self.elapsed() > 4.0:
                self.shot("06_awake_first_look")
                self.call_local("DebugRoboSleepLook", (180.0, 10.0))
                self.advance("vic_look_shot")
            return

        if s == "vic_look_shot":
            if self.elapsed() > 4.0:
                self.shot("06b_awake_look_turned")
                self.call_local("DebugRoboSleepLook", (0.0, 40.0))
                self.advance("vic_look_shot2")
            return

        if s == "vic_look_shot2":
            if self.elapsed() > 4.0:
                self.shot("06d_awake_look_down")
                self.call(self.host(), "DebugRoboFeignSleep", (True,))
                self.advance("vic_feign_shot")
            return

        if s == "vic_feign_shot":
            if self.elapsed() > 1.5:
                self.shot("06c_feign_sleep")
                self.call(self.host(), "DebugRoboFeignSleep", (False,))
                self.advance("mkwork")
            return

        # ---------------- 作畫者路線 ----------------
        if s == "draw_stand":
            if self.elapsed() > 2.5:
                self.shot("04_draw_standing")
                self.advance("esc_open2")
            return

        if s == "esc_open2":
            if self.elapsed() > 0.5:
                self.call_local("DebugRoboSystemMenu", (True,))
                self.advance("esc_shot2")
            return

        if s == "esc_shot2":
            if self.elapsed() > 2.5:
                self.probe_menu()
                self.shot("04b_esc_menu_ingame")
                self.advance("esc_page")
            return

        if s == "esc_page":
            if self.elapsed() > 0.6:
                self.call_local("DebugRoboSystemMenuPage", (1,))
                self.advance("esc_howto")
            return

        if s == "esc_howto":
            if self.elapsed() > 1.5:
                self.shot("04c_esc_howto")
                self.advance("esc_close2")
            return

        if s == "esc_close2":
            if self.elapsed() > 0.6:
                self.call_local("DebugRoboSystemMenuPage", (0,))
                self.call_local("DebugRoboSystemMenu", (False,))
                self.advance("enter_lean")
            return

        if s == "enter_lean":
            host = self.host()
            vid = gs.get_editor_property("VictimPlayerId") if gs else -1
            victim = find_char(w, vid)
            if host and victim and self.elapsed() > 1.0:
                bt = victim.get_editor_property("Body").get_world_transform()
                p = bt.transform_location(unreal.Vector(0.0, 26.0, 95.0))
                n = bt.transform_direction(unreal.Vector(0.0, 1.0, 0.0))
                self.call(host, "DebugRoboEnterLean", (victim, p, n))
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

        # 墨杯盤：模擬按住 RMB（鉤子與真鍵 OR，由同一條輪詢消化）
        if s == "tray":
            if self.elapsed() > 0.5:
                self.call_local("DebugRoboInkTray", (True,))
                self.advance("tray_shot")
            return

        if s == "tray_shot":
            if self.elapsed() > 1.5:
                self.shot("06_ink_tray")
                self.advance("tray_close")
            return

        if s == "tray_close":
            if self.elapsed() > 0.6:
                self.call_local("DebugRoboInkTray", (False,))
                self.advance("art_shake")
            return

        # 搖夢回執（攻擊者本人 2s）
        if s == "art_shake":
            if self.elapsed() > 1.0:
                self.call(self.gm(), "DebugRoboShake", ())
                self.advance("art_shake_shot")
            return

        if s == "art_shake_shot":
            if self.elapsed() > 0.7:
                self.shot("06b_shake_receipt")
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
            if "TOUR" in up:
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
            if "ACCUSATION" in up:
                self.advance("accuse_shot")
            elif self.elapsed() > 60:
                log("WARN never reached Accusation (phase=%s)" % ph)
                self.finish()
            return

        if s == "accuse_shot":
            if self.elapsed() > 2.5:
                self.shot("08_accusation")
                self.advance("carbonize")
            return

        if s == "carbonize":
            if self.elapsed() > 0.6:
                self.call(self.gm(), "DebugRoboCarbonize", ())
                self.advance("accuse_do")
            return

        if s == "accuse_do":
            if self.elapsed() > 2.0:
                self.gm().call_method("DebugRoboAccuse", (True,))
                self.advance("wait_resolution")
            return

        if s == "wait_resolution":
            if "RESOLUTION" in up:
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

        if s == "wait_after":
            if "POST" in up:
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

        # 結局／場間：兩人房走不到三杯 ⇒ 鉤子直進 Finale
        if s == "force_finale":
            if self.elapsed() > 2.0:
                self.gm().call_method("DebugRoboFinale", ())
                self.advance("wait_finale")
            return

        if s == "wait_finale":
            if "FINALE" in up:
                if self.elapsed() > 3.5:
                    self.shot("10_finale")
                    self.advance("wait_postgame")
            elif self.elapsed() > 30:
                log("WARN never reached Finale (phase=%s)" % ph)
                self.advance("done")
            return

        if s == "wait_postgame":
            if "POST" in up:
                self.advance("postgame_shot")
            elif self.elapsed() > 40:
                log("WARN never reached PostGame (phase=%s)" % ph)
                self.advance("done")
            return

        if s == "postgame_shot":
            if self.elapsed() > 4.0:
                self.shot("11_postgame")
                self.advance("done")
            return

        # 右下角比分：規則塊與比分分時共用同一個角，判準是受害者的罰酒杯 > 0。
        # 兩人房生不出杯子 ⇒ 直接把杯數寫進受害者的 PlayerState 來拍**顯示**。
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
