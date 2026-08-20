# 褌光滑板（fundoshi_plate.py）引擎內截圖矩陣（2026-08-21）：睡姿受害者貼臉倍率
# 前置：play_mode_robo.ps1（編輯器關閉時跑）＋ ini 掛 +StartupScripts=<本檔>；測完移除 ini 行
# 產出：Saved/robo_fundoshi_plate_result.txt ＋ Saved/Screenshots/WindowsEditor/fp_*.png
import unreal, time, ctypes, traceback, os, glob

OUT = r"C:\games\Unreal Engine\nice_ink\Saved\robo_fundoshi_plate_result.txt"
SHOTDIR = r"C:\games\Unreal Engine\nice_ink\Saved\Screenshots\WindowsEditor"
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[FP] " + str(msg))
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


def V(a):
    return unreal.Vector(float(a[0]), float(a[1]), float(a[2]))


def add(*vs):
    x = y = z = 0.0
    for v in vs:
        x += v.x; y += v.y; z += v.z
    return unreal.Vector(x, y, z)


def mul(v, k):
    return unreal.Vector(v.x * k, v.y * k, v.z * k)


def sub(a, b):
    return unreal.Vector(a.x - b.x, a.y - b.y, a.z - b.z)


def norm(v):
    l = (v.x ** 2 + v.y ** 2 + v.z ** 2) ** 0.5 or 1.0
    return unreal.Vector(v.x / l, v.y / l, v.z / l)


class Probe:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.host_pid = None
        self.step_i = 0
        self.shots = []
        for p in glob.glob(os.path.join(SHOTDIR, "fp_*.png")):
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

    def host(self):
        w = get_world("UEDPIE_0")
        return find_char(w, self.host_pid) if w else None

    def tick(self, dt):
        try:
            self.step()
        except Exception:
            log("EXC:\n" + traceback.format_exc())
            self.finish()

    def plan_shots(self, server):
        gs = unreal.GameplayStatics.get_game_state(server)
        vpid = gs.get_editor_property("VictimPlayerId")
        vic = find_char(server, vpid)
        body = vic.get_editor_property("Body")
        origin, ext, rad = unreal.SystemLibrary.get_component_bounds(body)
        log(f"Body bounds origin={origin} ext={ext}")
        # 仰躺（SPEC #18 褌朝上）：腹側=世界+Z；身體軸=包圍盒最長軸（頭向依 X 正）
        A = unreal.Vector(1, 0, 0) if ext.x >= ext.y else unreal.Vector(0, 1, 0)
        Lf = unreal.Vector(0, 1, 0) if ext.x >= ext.y else unreal.Vector(-1, 0, 0)
        U = unreal.Vector(0, 0, 1)
        H = add(origin, mul(A, 12.0), unreal.Vector(0, 0, 4.0))   # 骨盆≈包圍盒中心往腳 12cm（實測：頭在 −A、腳在 +A）
        log(f"H={H} A={A} L={Lf}")
        def surf(off_a, off_l):
            start = add(H, mul(A, off_a), mul(Lf, off_l), unreal.Vector(0, 0, 150))
            end = add(H, mul(A, off_a), mul(Lf, off_l), unreal.Vector(0, 0, -50))
            hit = unreal.SystemLibrary.line_trace_single(server, start, end, unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,
                                                         False, [], unreal.DrawDebugTrace.NONE, True, unreal.LinearColor.RED, unreal.LinearColor.GREEN, 0.0)
            if hit and hit.to_tuple()[0]:
                t = hit.to_tuple()
                return t[4], t[7]      # location, impact_normal
            return add(H, mul(A, off_a), mul(Lf, off_l), unreal.Vector(0, 0, 30)), unreal.Vector(0, 0, 1)
        def shot(name, off_a, off_l, dist, tilt=None):
            p, n = surf(off_a, off_l)
            d = norm(add(n, tilt)) if tilt else n
            self.shots.append((name, p, add(p, mul(d, dist))))
            log(f"plan {name}: surf={p} n={n}")
        shot("fp_00_overview", 0, 0, 130)
        # 扇形掃描：機位精確落點不可知（lie transform 難反推）＝沿身體軸每 10cm、三條側線全拍
        for off_a in (-50, -40, -30, -20, -10, 0, 10, 20, 30, 40, 50):
            for off_l, tag in ((0, "c"), (24, "l"), (-24, "r")):
                shot(f"fp_scan_{tag}_{off_a:+03d}", off_a, off_l, 22)

    def step(self):
        s = self.stage
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
            server = get_world("UEDPIE_0")
            if server and unreal.GameplayStatics.get_game_mode(server):
                unreal.GameplayStatics.get_game_mode(server).set_editor_property("DebugForcedVictimSeat", 1)
                self.advance("wait_drawing")
        elif s == "wait_drawing":
            server = get_world("UEDPIE_0")
            gs = unreal.GameplayStatics.get_game_state(server)
            ph = str(gs.get_editor_property("CurrentPhase")) if gs else "NO_GAMESTATE"
            if ph != getattr(self, "_last_phase", None):
                self._last_phase = ph
                n = unreal.GameplayStatics.get_game_state(server)
                cnt = len(n.get_editor_property("player_array")) if n else -1
                log(f"  phase={ph} players={cnt} t={self.elapsed():.1f}s")
            if gs and ph.endswith("DRAWING: 3>"):
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
            elif self.elapsed() > 120.0:
                log("FAIL: no drawing phase")
                self.finish()
        elif s == "setup":
            if self.elapsed() < 2.0:
                return
            server = get_world("UEDPIE_0")
            self.plan_shots(server)
            focus_main_window()
            self.advance("shots")
        elif s == "shots":
            server = get_world("UEDPIE_0")
            c = self.host()
            k = self.step_i // 2
            if k >= len(self.shots):
                if self.elapsed() >= 1.5:
                    log("RESULT DONE-PASS")
                    self.finish()
                return
            name, focus, cam = self.shots[k]
            if self.step_i % 2 == 0:
                c.call_method("DebugRoboViewAt", (cam.x, cam.y, cam.z, focus.x, focus.y, focus.z))
                log(f"  view {name} cam={cam} focus={focus}")
                self.step_i += 1
                self.stage_t = time.monotonic()
            elif self.elapsed() >= 1.4:
                unreal.SystemLibrary.execute_console_command(server, f"HighResShot 1600x900 filename={name}")
                log(f"  shot {name}")
                self.step_i += 1
                self.stage_t = time.monotonic()

    def finish(self):
        try:
            unreal.unregister_slate_post_tick_callback(self.handle)
        except Exception:
            pass


Probe()
