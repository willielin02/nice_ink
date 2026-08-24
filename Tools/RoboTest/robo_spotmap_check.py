# NiSpotMap 疊圖自驗（2026-08-24）：確認「普查圖真的貼上身體」＋「關掉會還原」。
# 這支存在的理由＝不准把沒驗過的工具交給 user（他要拿它去核對自己的眼睛）。
# 前置：play_mode_robo.ps1 ＋ ini 掛 +StartupScripts=<本檔>；測完移除
import unreal
import time
import ctypes
import traceback
import os
import glob

OUT = r"C:\games\Unreal Engine\nice_ink\Saved\robo_spotmap_result.txt"
SHOTDIR = r"C:\games\Unreal Engine\nice_ink\Saved\Screenshots\WindowsEditor"
LINES = []


def log(m):
    LINES.append(str(m))
    unreal.log_warning("[SM] " + str(m))
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
    u = ctypes.windll.user32

    def cb(h, _):
        b = ctypes.create_unicode_buffer(256)
        u.GetWindowTextW(h, b, 256)
        if "NiceInk" in b.value and "Unreal Editor" in b.value:
            u.SetForegroundWindow(h)
            return False
        return True
    P = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    u.EnumWindows(P(cb), 0)


class Probe:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.host_pid = None
        self.step_i = 0
        for p in glob.glob(os.path.join(SHOTDIR, "sm_*.png")):
            try:
                os.remove(p)
            except OSError:
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

    def step(self):
        s = self.stage
        server = get_world("UEDPIE_0")
        if s == "boot":
            if time.monotonic() - self.t0 > 8.0:
                eps = unreal.find_object(None, "/Script/UnrealEd.Default__EditorPerformanceSettings")
                if eps:
                    eps.set_editor_property("bThrottleCPUWhenNotForeground", False)
                les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
                if les is None:
                    return
                les.editor_request_begin_play()
                self.advance("wait_pie")
        elif s == "wait_pie":
            if server and unreal.GameplayStatics.get_game_mode(server):
                unreal.GameplayStatics.get_game_mode(server).set_editor_property("DebugForcedVictimSeat", 1)
                self.advance("wait_drawing")
        elif s == "wait_drawing":
            gs = unreal.GameplayStatics.get_game_state(server) if server else None
            ph = str(gs.get_editor_property("CurrentPhase")) if gs else "NO"
            if gs and ph.endswith("DRAWING: 3>"):
                vpid = gs.get_editor_property("VictimPlayerId")
                for c in unreal.GameplayStatics.get_all_actors_of_class(server, unreal.NiceInkCharacter):
                    pid = c.get_editor_property("player_state").get_editor_property("player_id")
                    if pid != vpid and c.is_player_controlled() and c.get_controller() and \
                       c.get_controller().is_local_player_controller():
                        self.host_pid = pid
                        break
                self.advance("setup")
            elif self.elapsed() > 260.0:
                log("FAIL: no drawing phase")
                self.finish()
        elif s == "setup":
            if self.elapsed() < 3.0:
                return
            gs = unreal.GameplayStatics.get_game_state(server)
            vic = find_char(server, gs.get_editor_property("VictimPlayerId"))
            bow = vic.get_editor_property("BowBody")
            origin, ext, rad = unreal.SystemLibrary.get_component_bounds(bow)
            A = unreal.Vector(1, 0, 0) if ext.x >= ext.y else unreal.Vector(0, 1, 0)
            self.cam = unreal.Vector(origin.x - A.x * 110, origin.y - A.y * 110, origin.z + 300)
            self.foc = unreal.Vector(origin.x, origin.y, origin.z + 20)
            c = find_char(server, self.host_pid)
            c.call_method("DebugRoboViewAt", (self.cam.x, self.cam.y, self.cam.z,
                                              self.foc.x, self.foc.y, self.foc.z))
            unreal.SystemLibrary.execute_console_command(server, "fov 55")
            focus_main_window()
            self.advance("shots")
        elif s == "shots":
            if self.elapsed() < 1.2:
                return
            k = self.step_i
            if k == 0:
                unreal.SystemLibrary.execute_console_command(server, "HighResShot 1600x900 filename=sm_off_before")
            elif k == 1:
                unreal.SystemLibrary.execute_console_command(server, "NiSpotMap 1")
            elif k == 2:
                unreal.SystemLibrary.execute_console_command(server, "HighResShot 1600x900 filename=sm_on")
            elif k == 3:
                unreal.SystemLibrary.execute_console_command(server, "NiSpotMap 0")
            elif k == 4:
                unreal.SystemLibrary.execute_console_command(server, "HighResShot 1600x900 filename=sm_off_after")
            else:
                log("RESULT DONE-PASS")
                self.finish()
                return
            self.step_i += 1
            self.stage_t = time.monotonic()

    def finish(self):
        try:
            unreal.unregister_slate_post_tick_callback(self.handle)
        except Exception:
            pass


Probe()
