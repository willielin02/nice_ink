# 醉夢描圖截圖自查（2026-08-02 v4.0）：受害者=seat0（主機停靠視口＝HighResShot
# 可靠焦點，lightring_shots 範本）。三態連拍：描圖中（帶+墨+針+進度）、
# 搖晃中（顯名+偏移）、越線失敗（紅閃+SLIPPED）。
# 產出：Saved/Screenshots/WindowsEditor/trace_*.png＋Saved/robo_traceshots_result.txt
import os
import time
import traceback

import unreal

OUT = "C:/games/Unreal Engine/nice_ink/Saved/robo_traceshots_result.txt"
os.makedirs(os.path.dirname(OUT), exist_ok=True)
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[TRACESHOTS] " + str(msg))
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


def focus_pie():
    try:
        import ctypes
        hwnd = ctypes.windll.user32.GetForegroundWindow()
        ctypes.windll.user32.SetForegroundWindow(hwnd)
    except Exception:
        pass


def shot(world, name):
    focus_pie()
    unreal.SystemLibrary.execute_console_command(world, f"HighResShot 1280x720 filename={name}")


class Shots:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.victim_pid = None
        perf = unreal.find_object(None, "/Script/UnrealEd.Default__EditorPerformanceSettings")
        for prop in ("throttle_cpu_when_not_foreground", "bThrottleCPUWhenNotForeground"):
            try:
                perf.set_editor_property(prop, False)
                break
            except Exception:
                pass
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    def advance(self, s):
        self.stage = s
        self.stage_t = time.monotonic()
        log("STAGE -> " + s)

    def elapsed(self):
        return time.monotonic() - self.stage_t

    def tick(self, dt):
        try:
            self.step()
        except Exception:
            log("EXC:\n" + traceback.format_exc())
            self.finish()

    def trace(self):
        return find_char(get_world("UEDPIE_0"), self.victim_pid).get_editor_property("DreamTrace")

    def step(self):
        s = self.stage
        if s == "boot":
            if time.monotonic() - self.t0 > 8.0:
                unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
                self.advance("wait_pie")
        elif s == "wait_pie":
            server = get_world("UEDPIE_0")
            if server and unreal.GameplayStatics.get_game_mode(server):
                gm = unreal.GameplayStatics.get_game_mode(server)
                gm.set_editor_property("DebugForcedVictimSeat", 0)  # 主機＝受害者＝停靠視口
                # 固定種子 98（98/7=14, 14%3=2 → cup0 池[2]＝富士山・開放）——
                # 開放一筆畫的整合驗證（autopilot 沿開放路徑）＋圖案外觀自查
                gm.set_editor_property("DebugForcedTraceSeed", 98)
                self.advance("wait_drawing")
            elif self.elapsed() > 60.0:
                log("FAIL: PIE never started")
                self.finish()
        elif s == "wait_drawing":
            gs = unreal.GameplayStatics.get_game_state(get_world("UEDPIE_0"))
            if gs and str(gs.get_editor_property("CurrentPhase")).endswith("DRAWING: 3>"):
                self.victim_pid = gs.get_editor_property("VictimPlayerId")
                self.advance("autopilot")
            elif self.elapsed() > 60.0:
                log("FAIL: drawing never came")
                self.finish()
        elif s == "autopilot":
            if self.elapsed() < 1.0:
                return
            self.trace().call_method("DebugAutopilot", (8.0,))
            self.advance("shot_tracing")
        elif s == "shot_tracing":
            if self.elapsed() < 6.0:
                return
            # 開放路徑整合斷言（順帶）：autopilot 沿開放圖案有進度且零失敗
            summary = str(self.trace().call_method("GetDebugSummary", ()))
            log("OPEN-PATH MID | " + summary)
            shot(get_world("UEDPIE_0"), "trace_tracing")
            self.advance("shake")
        elif s == "shake":
            if self.elapsed() < 1.5:
                return
            unreal.GameplayStatics.get_game_mode(get_world("UEDPIE_0")).call_method("DebugRoboShake", ())
            self.advance("shot_shake")
        elif s == "shot_shake":
            if self.elapsed() < 1.2:
                return
            shot(get_world("UEDPIE_0"), "trace_shaking")
            self.advance("veer")
        elif s == "veer":
            if self.elapsed() < 3.0:
                return
            self.trace().call_method("DebugVeerOff", (0.6,))
            self.advance("shot_fail")
        elif s == "shot_fail":
            if self.elapsed() < 0.9:
                return
            shot(get_world("UEDPIE_0"), "trace_failflash")
            self.advance("wrap")
        elif s == "wrap":
            if self.elapsed() < 1.5:
                return
            log("DONE shots=trace_tracing/trace_shaking/trace_failflash")
            self.finish()

    def finish(self):
        log("HARNESS END")
        if self.handle:
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.handle = None


_s = Shots()
log("harness registered")
