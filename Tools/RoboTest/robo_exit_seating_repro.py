# user 實際路徑重現：Seating 階段衝到出口（TriggerExit 被相位擋、人停在軌道末端）
# → Drawing 開始後持續把游標指在門外 → 修復前=永遠卡死；修復後=立刻走出（eyes=True）。
import time
import traceback

import unreal

OUT = "C:/Users/willi/AppData/Local/Temp/claude/c--games-Unreal-Engine-nice-ink/521b79b4-5e34-41de-bd4e-05828b908a2a/scratchpad/exit_seating_result.txt"
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[SEATRUSH] " + str(msg))
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


def parse_exit(summary):
    import re
    m = re.search(r"exit=\(([-0-9.]+),([-0-9.]+)\)", summary)
    return (float(m.group(1)), float(m.group(2))) if m else (0.0, 0.0)


class Test:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.victim = None
        self.maze = None
        self.samples = 0
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    def advance(self, stage):
        self.stage = stage
        self.stage_t = time.monotonic()
        log("STAGE -> " + stage)

    def elapsed(self):
        return time.monotonic() - self.stage_t

    def phase_str(self):
        gs = unreal.GameplayStatics.get_game_state(get_world("UEDPIE_0"))
        return str(gs.get_editor_property("CurrentPhase")) if gs else "?"

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
                self.advance("wait_pie")
        elif s == "wait_pie":
            server = get_world("UEDPIE_0")
            if server and unreal.GameplayStatics.get_game_mode(server):
                unreal.GameplayStatics.get_game_mode(server).set_editor_property("DebugForcedVictimSeat", 0)
                self.advance("wait_seating")
            elif self.elapsed() > 60.0:
                log("FAIL: no PIE")
                self.finish()
        elif s == "wait_seating":
            # Seating 一開就要衝——搶在入座演出結束前把人放到出口軌末端
            if self.phase_str().endswith("SEATING: 2>"):
                gs = unreal.GameplayStatics.get_game_state(get_world("UEDPIE_0"))
                vpid = gs.get_editor_property("VictimPlayerId")
                self.victim = find_char(get_world("UEDPIE_0"), vpid)
                self.maze = self.victim.get_editor_property("DreamMaze")
                self.advance("rush")
            elif self.elapsed() > 90.0:
                log("FAIL: no seating")
                self.finish()
        elif s == "rush":
            if self.elapsed() < 0.6:
                return
            if "active=1" not in self.maze.get_debug_summary():
                if self.elapsed() > 20.0:
                    log("FAIL: maze not active in seating")
                    self.finish()
                return
            self.maze.debug_place_at_exit_cell()
            self.advance("push_seating")
        elif s == "push_seating":
            if self.elapsed() < 0.5:
                return
            summary = self.maze.get_debug_summary()
            ex, ey = parse_exit(summary)
            import math
            r = math.hypot(ex, ey)
            scale = (r + 1.5) / max(r, 0.001)
            self.target = unreal.Vector2D(ex * scale, ey * scale)
            # 30 秒持續推門：涵蓋 Seating 剩餘＋Drawing 開頭（＝user 的按住左鍵）
            self.maze.debug_robo_nav_to(self.target, 30.0)
            log("SEATING push begins: " + summary)
            self.advance("watch")
        elif s == "watch":
            if self.elapsed() < 1.0 * (self.samples + 1):
                return
            self.samples += 1
            phase = self.phase_str().split(".")[-1]
            eyes = self.victim.get_editor_property("bEyesOpen")
            log("t+%02ds phase=%s eyes=%s %s" % (self.samples, phase, eyes, self.maze.get_debug_summary()))
            if eyes:
                log("VERDICT: EXITED (fix works — walked out once Drawing began)")
                log("DONE")
                self.finish()
            elif self.samples >= 25:
                log("VERDICT: STUCK-AT-DOOR (bug reproduced / fix failed)")
                log("DONE")
                self.finish()

    def finish(self):
        if self.handle:
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.handle = None
        with open(OUT, "w", encoding="utf-8") as f:
            f.write("\n".join(LINES))


_t = Test()
log("harness registered")
