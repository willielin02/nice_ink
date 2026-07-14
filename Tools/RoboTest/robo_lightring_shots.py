# 光圈式局部顯示 v3.5 截圖自查（Python StartupScript）：
# ①原點（驗收判準：五道等距門剛好在光圈內可讀）②走廊中段（技能點傳送）③旋轉中④旋轉後
# seat0＝受害者在主機停靠視口＋SetForegroundWindow（HighResShot 只有聚焦視窗會處理）
import ctypes
import time
import traceback

import unreal

OUT = "C:/games/Unreal Engine/nice_ink/Saved/robo_lightring_result.txt"
import os
os.makedirs(os.path.dirname(OUT), exist_ok=True)
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[LIGHTRING] " + str(msg))
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


def focus_main_window():
    user32 = ctypes.windll.user32
    hwnd = user32.FindWindowW(None, None)
    # 找主編輯器視窗（標題含 NiceInk）
    def enum_cb(h, _):
        buf = ctypes.create_unicode_buffer(256)
        user32.GetWindowTextW(h, buf, 256)
        if "NiceInk" in buf.value and "Unreal Editor" in buf.value:
            user32.SetForegroundWindow(h)
            return False
        return True
    WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    user32.EnumWindows(WNDENUMPROC(enum_cb), 0)


class Test:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
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
                unreal.GameplayStatics.get_game_mode(server).set_editor_property("DebugForcedVictimSeat", 0)
                self.advance("wait_drawing")
            elif self.elapsed() > 60.0:
                log("FAIL: no PIE")
                self.finish()
        elif s == "wait_drawing":
            gs = unreal.GameplayStatics.get_game_state(get_world("UEDPIE_0"))
            if gs and str(gs.get_editor_property("CurrentPhase")).endswith("DRAWING: 3>"):
                vpid = gs.get_editor_property("VictimPlayerId")
                self.vworld = get_world("UEDPIE_0")  # seat0＝主機停靠視口
                victim = find_char(self.vworld, vpid)
                self.maze = victim.get_editor_property("DreamMaze")
                focus_main_window()
                self.advance("shot_origin")
            elif self.elapsed() > 60.0:
                log("FAIL: no drawing")
                self.finish()
        elif s == "shot_origin":
            if self.elapsed() < 2.5:
                return
            self.shot("lightring_origin")  # 驗收判準：五道等距門剛好在光圈內
            self.advance("goto_corridor")
        elif s == "goto_corridor":
            if self.elapsed() < 2.5:
                return
            self.maze.debug_trigger_checkpoint(0)  # 傳送到噴射拾取點＝迷宮中段
            self.advance("shot_corridor")
        elif s == "shot_corridor":
            if self.elapsed() < 1.5:
                return
            self.shot("lightring_corridor")
            self.advance("trap")
        elif s == "trap":
            if self.elapsed() < 2.5:
                return
            self.maze.debug_trigger_trap(0)
            self.advance("dial")
        elif s == "dial":
            if self.elapsed() < 0.6:
                return
            unreal.GameplayStatics.get_game_mode(get_world("UEDPIE_0")).debug_robo_maze_dial(170.0)
            self.advance("shot_rotate")
        elif s == "shot_rotate":
            if self.elapsed() < 2.6:  # DeathScreen 1.2 + 動畫中段
                return
            self.shot("lightring_rotating")
            self.advance("shot_after")
        elif s == "shot_after":
            if self.elapsed() < 4.5:
                return
            self.shot("lightring_after")
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
