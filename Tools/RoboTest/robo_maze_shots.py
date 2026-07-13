# 醉夢迷宮 UI 連拍（Python StartupScript）：行走中／死亡畫面／旋轉中三態截圖
import time
import traceback

import unreal

OUT = r"C:\Users\willi\AppData\Local\Temp\claude\c--games-Unreal-Engine-nice-ink\5ab1a0e6-ed8c-4492-b195-1d23c704a160\scratchpad\robo_maze_shots_result.txt"
import os
os.makedirs(os.path.dirname(OUT), exist_ok=True)
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[MAZESHOT] " + str(msg))
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(LINES))


def get_world(tag):
    for w in unreal.ObjectIterator(unreal.World):
        if tag in w.get_path_name():
            return w
    return None


def chars_of(w):
    return list(unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NiceInkCharacter))


def find_char(w, pid):
    for c in chars_of(w):
        ps = c.get_editor_property("player_state")
        if ps and ps.get_editor_property("player_id") == pid:
            return c
    return None


class Test:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.victim_pid = None
        self.vworld = None
        self.maze = None
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    def advance(self, stage):
        self.stage = stage
        self.stage_t = time.monotonic()
        log("STAGE -> " + stage)

    def elapsed(self):
        return time.monotonic() - self.stage_t

    def shot(self, name):
        unreal.SystemLibrary.execute_console_command(
            self.vworld, f"HighResShot 1280x720 filename={name}")
        log("shot " + name)

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
                unreal.GameplayStatics.get_game_mode(server).set_editor_property("DebugForcedVictimSeat", 1)
                self.advance("wait_drawing")
            elif self.elapsed() > 60.0:
                log("FAIL: no PIE")
                self.finish()
        elif s == "wait_drawing":
            gs = unreal.GameplayStatics.get_game_state(get_world("UEDPIE_0"))
            if gs and str(gs.get_editor_property("CurrentPhase")).endswith("DRAWING: 3>"):
                self.victim_pid = gs.get_editor_property("VictimPlayerId")
                self.vworld = get_world("UEDPIE_1")  # seat1 強制＝client1 世界
                victim = find_char(self.vworld, self.victim_pid)
                self.maze = victim.get_editor_property("DreamMaze")
                self.advance("shot_walking")
            elif self.elapsed() > 60.0:
                log("FAIL: no drawing")
                self.finish()
        elif s == "shot_walking":
            if self.elapsed() < 2.0:
                return
            self.shot("mazeui_walking")
            self.advance("trap")
        elif s == "trap":
            if self.elapsed() < 3.0:
                return
            self.maze.debug_trigger_trap(0)
            self.advance("shot_death")
        elif s == "shot_death":
            if self.elapsed() < 0.7:
                return
            self.shot("mazeui_death")   # DeathScreen 1.2s 窗內
            unreal.GameplayStatics.get_game_mode(get_world("UEDPIE_0")).debug_robo_maze_dial(200.0)
            self.advance("shot_rotate")
        elif s == "shot_rotate":
            if self.elapsed() < 3.2:
                return
            self.shot("mazeui_rotating")  # DeathScreen 1.2 + dial 0.1 + 動畫 3.4s 窗內
            self.advance("shot_after")
        elif s == "shot_after":
            if self.elapsed() < 4.0:
                return
            self.shot("mazeui_after")
            self.advance("done")
        elif s == "done":
            if self.elapsed() < 3.0:
                return
            log("summary: " + self.maze.get_debug_summary())
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
