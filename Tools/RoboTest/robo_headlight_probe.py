# 頭燈假光斷層診斷探針（2026-08-05 user 抓「左胸陰影斷層」）
# 同機位三張 A/B：skel=現行骨骼身體 / flat=關頭燈（HeadlightFloor=1）/ statue=舊雕像。
# 判讀：斷層在 flat 消失=著色（法線）問題；flat 仍在=貼圖（albedo/血色場）問題；
#      statue 沒有=SK 匯入法線 vs SM 差異；statue 也有=源網格法線本來如此。
# 產出：Saved/robo_headlight_result.txt + Screenshots/hl_{skel,flat,statue}.png
import unreal, time, ctypes, traceback

OUT = r"C:\games\Unreal Engine\nice_ink\Saved\robo_headlight_result.txt"
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[HL] " + str(msg))
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

    def enum_cb(h, _):
        buf = ctypes.create_unicode_buffer(256)
        user32.GetWindowTextW(h, buf, 256)
        if "NiceInk" in buf.value and "Unreal Editor" in buf.value:
            user32.SetForegroundWindow(h)
            return False
        return True
    WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    user32.EnumWindows(WNDENUMPROC(enum_cb), 0)


class Probe:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.host_pid = None
        self.step_i = 0
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    def advance(self, s):
        self.stage = s
        self.stage_t = time.monotonic()
        log("STAGE -> " + s)

    def elapsed(self):
        return time.monotonic() - self.stage_t

    def host(self):
        w = get_world("UEDPIE_0")
        return find_char(w, self.host_pid) if w else None

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
                eps = unreal.find_object(None, "/Script/UnrealEd.Default__EditorPerformanceSettings")
                if eps:
                    eps.set_editor_property("bThrottleCPUWhenNotForeground", False)
                unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
                self.advance("wait_pie")
        elif s == "wait_pie":
            server = get_world("UEDPIE_0")
            if server and unreal.GameplayStatics.get_game_mode(server):
                unreal.GameplayStatics.get_game_mode(server).set_editor_property("DebugForcedVictimSeat", 1)
                self.advance("wait_drawing")
        elif s == "wait_drawing":
            server = get_world("UEDPIE_0")
            gs = unreal.GameplayStatics.get_game_state(server)
            if gs and str(gs.get_editor_property("CurrentPhase")).endswith("DRAWING: 3>"):
                victim_pid = gs.get_editor_property("VictimPlayerId")
                for c in unreal.GameplayStatics.get_all_actors_of_class(server, unreal.NiceInkCharacter):
                    pid = c.get_editor_property("player_state").get_editor_property("player_id")
                    if pid != victim_pid and c.is_player_controlled() and c.get_controller() and \
                       c.get_controller().is_local_player_controller():
                        self.host_pid = pid
                        break
                if self.host_pid is None:
                    log("FAIL: no host artist")
                    self.finish()
                    return
                self.advance("setup")
            elif self.elapsed() > 40.0:
                log("FAIL: no drawing phase")
                self.finish()
        elif s == "setup":
            if self.elapsed() < 1.0:
                return
            c = self.host()
            loc = c.get_actor_location()
            c.set_actor_location(unreal.Vector(-50.0, -25.0, loc.z), False, False)
            pc = c.get_controller()
            if pc:
                pc.set_control_rotation(unreal.Rotator(0.0, 0.0, -80.0))  # 面向南牆機位
            # 近機位（胸口取景）：南向 150cm、高一點
            c.call_method("DebugRoboViewFrom", (25.0, -150.0, 45.0))
            focus_main_window()
            self.advance("shots")
        elif s == "shots":
            server = get_world("UEDPIE_0")
            c = self.host()
            bow = c.get_editor_property("BowBody")
            # 步驟表：時間點, 動作
            if self.step_i == 0 and self.elapsed() >= 2.0:
                unreal.SystemLibrary.execute_console_command(server, "HighResShot 1600x900 filename=hl_skel")
                log("shot skel (skeletal, headlight on)")
                self.step_i = 1
            elif self.step_i == 1 and self.elapsed() >= 3.2:
                bow.set_scalar_parameter_value_on_materials("HeadlightFloor", 1.0)
                log("HeadlightFloor -> 1.0 on BowBody")
                self.step_i = 2
            elif self.step_i == 2 and self.elapsed() >= 4.4:
                unreal.SystemLibrary.execute_console_command(server, "HighResShot 1600x900 filename=hl_flat")
                log("shot flat (headlight off)")
                self.step_i = 3
            elif self.step_i == 3 and self.elapsed() >= 5.6:
                bow.set_scalar_parameter_value_on_materials("HeadlightFloor", 0.55)
                c.set_editor_property("bSkeletalStandEnabled", False)  # 換舊雕像
                log("statue mode on (headlight restored)")
                self.step_i = 4
            elif self.step_i == 4 and self.elapsed() >= 7.0:
                unreal.SystemLibrary.execute_console_command(server, "HighResShot 1600x900 filename=hl_statue")
                log("shot statue")
                self.step_i = 5
            elif self.step_i == 5 and self.elapsed() >= 8.4:
                c.set_editor_property("bSkeletalStandEnabled", True)
                c.call_method("DebugRoboSideView", (False,))
                log("RESULT DONE-PASS")
                self.finish()

    def finish(self):
        try:
            unreal.unregister_slate_post_tick_callback(self.handle)
        except Exception:
            pass


Probe()
