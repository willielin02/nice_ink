# -*- coding: utf-8 -*-
# veil 閃爍探針：鎖肚頂→深俯角（畫面大半=veil）→同機位隔 1s 連拍兩張。
# 逐像素差分離線做（PIL/numpy）。產出 veilflk_a/b.png + result 檔。
import time
import traceback

import unreal

OUT = "C:/games/Unreal Engine/nice_ink/Saved/robo_veilflicker_result.txt"
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[VFLK] " + str(msg))
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
        u32 = ctypes.windll.user32
        wins = []
        EnumWindowsProc = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)

        def cb(h, l):
            buf = ctypes.create_unicode_buffer(256)
            u32.GetWindowTextW(h, buf, 256)
            if "NiceInk" in buf.value or "Preview" in buf.value:
                wins.append(h)
            return True

        u32.EnumWindows(EnumWindowsProc(cb), 0)
        if wins:
            u32.SetForegroundWindow(wins[0])
    except Exception:
        pass


class Probe:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.victim_pid = None
        self.host_pid = None
        perf = unreal.find_object(None, "/Script/UnrealEd.Default__EditorPerformanceSettings")
        for prop in ("throttle_cpu_when_not_foreground", "bThrottleCPUWhenNotForeground"):
            try:
                perf.set_editor_property(prop, False)
                break
            except Exception:
                pass
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    def advance(self, stage):
        self.stage = stage
        self.stage_t = time.monotonic()
        log("STAGE -> " + stage)

    def elapsed(self):
        return time.monotonic() - self.stage_t

    def tick(self, dt):
        try:
            self.step()
        except Exception:
            log("EXC:\n" + traceback.format_exc())
            self.finish()

    def server(self):
        return get_world("UEDPIE_0")

    def step(self):
        s = self.stage
        if s == "boot":
            if time.monotonic() - self.t0 > 10.0:
                sub = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
                if sub:
                    sub.editor_request_begin_play()
                    self.advance("wait_pie")
        elif s == "wait_pie":
            server = self.server()
            if server and unreal.GameplayStatics.get_game_mode(server):
                gm = unreal.GameplayStatics.get_game_mode(server)
                try:
                    gm.set_editor_property("DebugForcedVictimSeat", 1)
                except Exception:
                    return
                self.advance("wait_drawing")
        elif s == "wait_drawing":
            server = self.server()
            gs = unreal.GameplayStatics.get_game_state(server) if server else None
            if gs and str(gs.get_editor_property("CurrentPhase")).endswith("DRAWING: 3>"):
                self.victim_pid = gs.get_editor_property("VictimPlayerId")
                self.host_pid = unreal.GameplayStatics.get_player_pawn(server, 0) \
                    .get_editor_property("player_state").get_editor_property("player_id")
                self.advance("lock")
            elif self.elapsed() > 40.0:
                log("FAIL: drawing never came")
                self.finish()
        elif s == "lock":
            if self.elapsed() < 0.8:
                return
            victim = find_char(self.server(), self.victim_pid)
            bt = victim.get_editor_property("Body").get_world_transform()
            p = bt.transform_location(unreal.Vector(0.0, 26.0, 95.0))
            n = bt.transform_direction(unreal.Vector(0.0, 1.0, 0.0))
            host = find_char(self.server(), self.host_pid)
            log(f"lean={host.call_method('DebugRoboEnterLean', (victim, p, n))}")
            self.advance("settle")
        elif s == "settle":
            if self.elapsed() < 2.0:
                return
            host = find_char(self.server(), self.host_pid)
            raw = str(host.call_method("DebugLeanSummary", ()))
            if "maskOn=1" not in raw:
                if self.elapsed() > 30.0:
                    log("FAIL mask: " + raw)
                    self.finish()
                return
            self.advance("aim")
        elif s == "aim":
            if self.elapsed() < 0.5:
                return
            host = find_char(self.server(), self.host_pid)
            d = str(host.call_method("DebugLeanSummary", ()))
            import re as _re
            m = _re.search(r"az=(-?[\d.]+)", d)
            az = float(m.group(1)) if m else 0.0
            host.call_method("DebugRoboDrawAim", (az, 75.0))
            focus_pie()
            self.advance("shot_a")
        elif s == "shot_a":
            if self.elapsed() < 1.5:
                return
            unreal.SystemLibrary.execute_console_command(
                self.server(), "HighResShot 1 filename=veilflk_a")
            self.advance("shot_b")
        elif s == "shot_b":
            if self.elapsed() < 1.0:
                return
            unreal.SystemLibrary.execute_console_command(
                self.server(), "HighResShot 1 filename=veilflk_b")
            self.advance("lock2_exit")
        elif s == "lock2_exit":
            if self.elapsed() < 1.0:
                return
            find_char(self.server(), self.host_pid).call_method("ServerExitLean", ())
            self.advance("lock2_enter")
        elif s == "lock2_enter":
            if self.elapsed() < 1.2:
                return
            victim = find_char(self.server(), self.victim_pid)
            bt = victim.get_editor_property("Body").get_world_transform()
            p = bt.transform_location(unreal.Vector(0.0, 26.0, 60.0))
            n = bt.transform_direction(unreal.Vector(0.0, 1.0, 0.0))
            host = find_char(self.server(), self.host_pid)
            log(f"lock2={host.call_method('DebugRoboEnterLean', (victim, p, n))}")
            self.advance("lock2_settle")
        elif s == "lock2_settle":
            if self.elapsed() < 2.0:
                return
            host = find_char(self.server(), self.host_pid)
            raw = str(host.call_method("DebugLeanSummary", ()))
            if "maskOn=1" not in raw:
                if self.elapsed() > 30.0:
                    log("FAIL lock2 mask: " + raw)
                    self.finish()
                return
            self.advance("lock2_aim")
        elif s == "lock2_aim":
            if self.elapsed() < 0.5:
                return
            host = find_char(self.server(), self.host_pid)
            d = str(host.call_method("DebugLeanSummary", ()))
            import re as _re
            m = _re.search(r"az=(-?[\d.]+)", d)
            az = float(m.group(1)) if m else 0.0
            host.call_method("DebugRoboDrawAim", (az, 75.0))
            focus_pie()
            self.advance("lock2_shot")
        elif s == "lock2_shot":
            if self.elapsed() < 1.5:
                return
            unreal.SystemLibrary.execute_console_command(
                self.server(), "HighResShot 1 filename=veilflk_c")
            self.advance("done")
        elif s == "done":
            if self.elapsed() < 2.0:
                return
            log("DONE")
            self.finish()

    def finish(self):
        log("HARNESS END")
        if self.handle:
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.handle = None


_p = Probe()
log("harness registered")
