# 全迴圈迴歸（Python StartupScript 掛載；tick 驅動狀態機）
# 驗證：Lobby→(PIE 自動開局)→BottleSpin→Seating→Drawing→筆劃→現身→Tour→
#       Accusation→DebugRoboAccuse(False)×3（罰酒 1/2/3）→Finale→PostGame→
#       NiStart 重賽→BottleSpin。＝指認/結算/三杯終局/重賽的迴歸籠
#      （2026-07-17 上架衝刺新增：此前套件只驗到 Tour）。
# 產出：Saved/robo_fullloop_result.txt
import time
import traceback

import unreal

OUT = r"C:\games\Unreal Engine\nice_ink\Saved\robo_fullloop_result.txt"
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[FULLLOOP] " + str(msg))
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(LINES))


def get_world(tag):
    for w in unreal.ObjectIterator(unreal.World):
        if tag in w.get_path_name():
            return w
    return None


def phase_name(gs):
    try:
        return str(gs.get_editor_property("current_phase"))
    except Exception:
        return "?"


class Test:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.victim_pid = None
        self.wrong_count = 0
        self.pass_count = 0
        self.fail_count = 0
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    def advance(self, stage):
        self.stage = stage
        self.stage_t = time.monotonic()
        log("STAGE -> " + stage)

    def elapsed(self):
        return time.monotonic() - self.stage_t

    def check(self, name, ok, detail=""):
        if ok:
            self.pass_count += 1
        else:
            self.fail_count += 1
        log(f"{name}: {'PASS' if ok else 'FAIL'} {detail}")

    def server(self):
        return get_world("UEDPIE_0")

    def gm(self):
        w = self.server()
        return unreal.GameplayStatics.get_game_mode(w) if w else None

    def gs(self):
        w = self.server()
        return unreal.GameplayStatics.get_game_state(w) if w else None

    def victim_ps(self):
        gs = self.gs()
        if not gs:
            return None
        pid = gs.get_editor_property("victim_player_id")
        for ps in gs.get_editor_property("player_array"):
            if ps.get_editor_property("player_id") == pid:
                return ps
        return None

    def tick(self, dt):
        try:
            self.step()
        except Exception:
            log("EXC:\n" + traceback.format_exc())
            self.finish()

    def step(self):
        s = self.stage
        if s == "boot":
            if time.monotonic() - self.t0 > 8.0:
                unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
                self.advance("wait_gm")
        elif s == "wait_gm":
            gm = self.gm()
            if gm:
                gm.set_editor_property("DebugForcedVictimSeat", 0)
                # 演出計時全縮短（回歸不看戲）
                gm.set_editor_property("tour_seconds_per_work", 2.0)
                gm.set_editor_property("resolution_seconds", 2.5)
                gm.set_editor_property("finale_seconds", 3.0)
                self.advance("wait_drawing")
            elif self.elapsed() > 90:
                raise RuntimeError("no server GM")
        elif s == "wait_drawing":
            gs = self.gs()
            if gs and "DRAWING" in phase_name(gs):
                self.victim_pid = gs.get_editor_property("victim_player_id")
                self.check("reached Drawing", True, f"victim={self.victim_pid}")
                self.advance("stroke")
            elif self.elapsed() > 120:
                self.check("reached Drawing", False, phase_name(self.gs()) if self.gs() else "no GS")
                self.finish()
        elif s == "stroke":
            if self.elapsed() > 1.0:
                self.gm().debug_robo_stroke(unreal.Vector2D(0.40, 0.40), unreal.Vector2D(0.52, 0.52), 2)
                self.advance("emerge")
        elif s == "emerge":
            if self.elapsed() > 1.5:
                self.gm().debug_robo_emerge()
                self.advance("wait_tour")
        elif s == "wait_tour":
            gs = self.gs()
            if gs and "TOUR" in phase_name(gs):
                self.check("emerge -> Tour", True)
                self.advance("wait_accusation")
            elif gs and "SEATING" in phase_name(gs):
                # 沒有作品＝跳過巡禮重睡（不應發生——筆劃已下）
                self.check("emerge -> Tour", False, "re-seated (no works?)")
                self.finish()
            elif self.elapsed() > 30:
                self.check("emerge -> Tour", False, phase_name(gs) if gs else "no GS")
                self.finish()
        elif s == "wait_accusation":
            gs = self.gs()
            if gs and "ACCUSATION" in phase_name(gs):
                self.check("Tour -> Accusation (auto)", True)
                self.advance("accuse")
            elif self.elapsed() > 40:
                self.check("Tour -> Accusation (auto)", False, phase_name(gs) if gs else "no GS")
                self.finish()
        elif s == "accuse":
            if self.elapsed() > 1.0:
                self.gm().debug_robo_accuse(False)
                self.advance("wait_resolution")
        elif s == "wait_resolution":
            gs = self.gs()
            if gs and "RESOLUTION" in phase_name(gs):
                ps = self.victim_ps()
                cups = ps.get_editor_property("penalty_cups") if ps else -1
                self.wrong_count += 1
                self.check(f"wrong #{self.wrong_count} -> Resolution, cups={self.wrong_count}",
                           cups == self.wrong_count, f"cups={cups}")
                same = gs.get_editor_property("victim_player_id") == self.victim_pid
                self.check(f"victim unchanged after wrong #{self.wrong_count}", same)
                if self.wrong_count >= 3:
                    self.advance("wait_finale")
                else:
                    self.advance("wait_redrawing")
            elif self.elapsed() > 30:
                self.check("accuse -> Resolution", False, phase_name(gs) if gs else "no GS")
                self.finish()
        elif s == "wait_redrawing":
            gs = self.gs()
            if gs and "DRAWING" in phase_name(gs):
                self.advance("stroke")
            elif self.elapsed() > 40:
                self.check("Resolution -> next round Drawing", False, phase_name(gs) if gs else "no GS")
                self.finish()
        elif s == "wait_finale":
            gs = self.gs()
            if gs and "FINALE" in phase_name(gs):
                loser = gs.get_editor_property("loser_player_id")
                self.check("3rd cup -> Finale, loser=victim", loser == self.victim_pid, f"loser={loser}")
                self.advance("wait_postgame")
            elif self.elapsed() > 40:
                self.check("3rd cup -> Finale", False, phase_name(gs) if gs else "no GS")
                self.finish()
        elif s == "wait_postgame":
            gs = self.gs()
            if gs and "POST_GAME" in phase_name(gs).replace("POSTGAME", "POST_GAME"):
                self.check("Finale -> PostGame", True)
                self.advance("rematch")
            elif self.elapsed() > 40:
                self.check("Finale -> PostGame", False, phase_name(gs) if gs else "no GS")
                self.finish()
        elif s == "rematch":
            if self.elapsed() > 2.0:
                # 主機 console NiStart（host pawn exec；server 端直跑）
                pc = unreal.GameplayStatics.get_player_controller(self.server(), 0)
                unreal.SystemLibrary.execute_console_command(self.server(), "NiStart", pc)
                self.advance("wait_rematch")
        elif s == "wait_rematch":
            gs = self.gs()
            pn = phase_name(gs) if gs else "?"
            if gs and ("BOTTLE_SPIN" in pn or "BOTTLESPIN" in pn or "SEATING" in pn or "DRAWING" in pn):
                self.check("PostGame rematch (NiStart) -> new match", True, pn)
                self.finish()
            elif self.elapsed() > 20:
                self.check("PostGame rematch (NiStart) -> new match", False, pn)
                self.finish()

    def finish(self):
        log(f"SUMMARY pass={self.pass_count} fail={self.fail_count}")
        log("DONE" if self.fail_count == 0 else "FAIL")
        unreal.unregister_slate_post_tick_callback(self.handle)


Test()
