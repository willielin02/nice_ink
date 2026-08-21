# 皮膚凹陷/褲帶凸起 真兇 A/B（2026-08-21）：user 兩處缺陷在 Blender 幾何全綠後依然在
# ⇒ 在引擎內對「他實際看的東西」量：①射線網格量真實幾何（垂直=大腿頂面、水平=褲帶側面）
# ②同機位 AO 開/關截圖 A/B（暗斑消失=SSAO 定罪、不消=幾何/法線）。
# 前置：play_mode_robo.ps1 ＋ ini 掛 +StartupScripts=<本檔>；測完移除
# 產出：Saved/robo_skin_ab_result.txt ＋ Saved/robo_skin_grid.csv ＋ Screenshots/WindowsEditor/ab_*.png
import unreal, time, ctypes, traceback, os, glob

OUT = r"C:\games\Unreal Engine\nice_ink\Saved\robo_skin_ab_result.txt"
CSV = r"C:\games\Unreal Engine\nice_ink\Saved\robo_skin_grid.csv"
SHOTDIR = r"C:\games\Unreal Engine\nice_ink\Saved\Screenshots\WindowsEditor"
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[AB] " + str(msg))
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


def add(*vs):
    x = y = z = 0.0
    for v in vs:
        x += v.x; y += v.y; z += v.z
    return unreal.Vector(x, y, z)


def mul(v, k):
    return unreal.Vector(v.x * k, v.y * k, v.z * k)


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
        self.tasks = []       # (kind, side, ia, ib, start, end)
        self.rows = []        # csv rows
        self.zmap = {}        # (ia,il) -> z
        self.latmap = {}      # (side,ia,ih) -> lateral dist from centre plane
        self.shots = []
        for p in glob.glob(os.path.join(SHOTDIR, "ab_*.png")):
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

    def trace(self, w, start, end):
        hit = unreal.SystemLibrary.line_trace_single(
            w, start, end, unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,
            True, [], unreal.DrawDebugTrace.NONE, True,
            unreal.LinearColor.RED, unreal.LinearColor.GREEN, 0.0)
        if hit:
            t = hit.to_tuple()
            if t[0]:
                return t[4], t[9] if len(t) > 9 else None   # location, component?
        return None, None

    def setup_frames(self, server):
        gs = unreal.GameplayStatics.get_game_state(server)
        vpid = gs.get_editor_property("VictimPlayerId")
        vic = find_char(server, vpid)
        body = vic.get_editor_property("Body")
        try:
            sm = body.get_editor_property("static_mesh")
            log(f"Body SM asset = {sm.get_path_name()} tris_lod0 = {sm.get_num_triangles(0)}")
        except Exception as e:
            log(f"SM info fail: {e}")
        origin, ext, rad = unreal.SystemLibrary.get_component_bounds(body)
        A = unreal.Vector(1, 0, 0) if ext.x >= ext.y else unreal.Vector(0, 1, 0)
        Lf = unreal.Vector(0, 1, 0) if ext.x >= ext.y else unreal.Vector(-1, 0, 0)
        self.A, self.L = A, Lf
        self.H = add(origin, mul(A, 12.0), unreal.Vector(0, 0, 4.0))
        log(f"H={self.H} A={A} L={Lf} ext={ext}")
        # 垂直網格（大腿/骨盆頂面）：off_a −30..+50、off_l −40..+40，2cm 步
        for ia in range(-15, 26):
            for il in range(-20, 21):
                oa, ol = ia * 2.0, il * 2.0
                s = add(self.H, mul(A, oa), mul(Lf, ol), unreal.Vector(0, 0, 150))
                e = add(self.H, mul(A, oa), mul(Lf, ol), unreal.Vector(0, 0, -50))
                self.tasks.append(("top", 0, ia, il, s, e))
        # 水平網格（左右側面＝褲帶側壁）：h −6..+30，2cm 步
        for side in (1, -1):
            for ia in range(-15, 26):
                for ih in range(-3, 16):
                    oa, h = ia * 2.0, ih * 2.0
                    s = add(self.H, mul(A, oa), mul(Lf, side * 70.0), unreal.Vector(0, 0, h))
                    e = add(self.H, mul(A, oa), mul(Lf, side * 0.0), unreal.Vector(0, 0, h))
                    self.tasks.append(("side", side, ia, ih, s, e))
        log(f"trace tasks: {len(self.tasks)}")

    def analyze(self):
        def detrend(m, label):
            out = []
            for (a, b), v in m.items():
                nb = []
                for da in range(-2, 3):
                    for db in range(-2, 3):
                        w = m.get((a + da, b + db))
                        if w is not None:
                            nb.append(w)
                if len(nb) >= 15:
                    out.append((abs(v - sum(nb) / len(nb)), v - sum(nb) / len(nb), a, b))
            out.sort(reverse=True)
            log(f"[{label}] pts={len(out)} residual p50 {sorted(o[0] for o in out)[len(out)//2]*10:.1f} top:")
            for mag, r, a, b in out[:10]:
                log(f"   {r*10:+6.1f}mm at grid a={a} b={b}")
            return out
        top = {}
        for (s, a, b), z in self.zmap.items():
            top[(a, b)] = z
        detrend(top, "top z(頂面高度)")
        for side in (1, -1):
            m = {}
            for (s, a, b), d in self.latmap.items():
                if s == side:
                    m[(a, b)] = d
            detrend(m, f"side{'+' if side > 0 else '-'} lat(側面深度)")
        with open(CSV, "w", encoding="utf-8") as f:
            f.write("kind,side,ia,ib,x,y,z\n")
            for r in self.rows:
                f.write(",".join(str(x) for x in r) + "\n")
        log(f"csv rows: {len(self.rows)} -> {CSV}")

    def skin_mid(self):
        w = get_world("UEDPIE_0")
        gs = unreal.GameplayStatics.get_game_state(w)
        vic = find_char(w, gs.get_editor_property("VictimPlayerId"))
        m = vic.get_editor_property("Body").get_material(0)
        return m if isinstance(m, unreal.MaterialInstanceDynamic) else None

    def plan_shots(self):
        H, A, Lf = self.H, self.A, self.L
        def cam(name, cpos, foc, fov=70):
            self.shots.append((name, foc, cpos, fov))
        # 大腿頂面特寫（左右）＋褲帶側面沿掃（左右、他截圖的視角）
        # 貼臉同規格（user 視角重現：FOV36）＋每機位三連拍開關手術
        cam("ab_close_thigh_l", add(H, mul(A, 30.0), mul(Lf, 21.0), unreal.Vector(0, 0, 44)), add(H, mul(A, 28.0), mul(Lf, 18.0), unreal.Vector(0, 0, 26)), 36)
        cam("ab_close_thigh_r", add(H, mul(A, 30.0), mul(Lf, -21.0), unreal.Vector(0, 0, 44)), add(H, mul(A, 28.0), mul(Lf, -18.0), unreal.Vector(0, 0, 26)), 36)
        cam("ab_close_band_l", add(H, mul(A, 30.0), mul(Lf, 46.0), unreal.Vector(0, 0, 26)), add(H, mul(A, 2.0), mul(Lf, 29.0), unreal.Vector(0, 0, 16)), 36)
        cam("ab_close_band_r", add(H, mul(A, 30.0), mul(Lf, -46.0), unreal.Vector(0, 0, 26)), add(H, mul(A, 2.0), mul(Lf, -29.0), unreal.Vector(0, 0, 16)), 36)

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
            if gs and ph.endswith("DRAWING: 3>"):
                vpid = gs.get_editor_property("VictimPlayerId")
                for c in unreal.GameplayStatics.get_all_actors_of_class(server, unreal.NiceInkCharacter):
                    pid = c.get_editor_property("player_state").get_editor_property("player_id")
                    if pid != vpid and c.is_player_controlled() and c.get_controller() and \
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
            self.setup_frames(server)
            self.tasks = []
            self.advance("probe")
        elif s == "probe":
            server = get_world("UEDPIE_0")
            n0 = self.step_i
            while self.step_i < len(self.tasks) and self.step_i - n0 < 250:
                kind, side, ia, ib, st, en = self.tasks[self.step_i]
                loc, _ = self.trace(server, st, en)
                if loc is not None:
                    self.rows.append((kind, side, ia, ib, round(loc.x, 2), round(loc.y, 2), round(loc.z, 2)))
                    if kind == "top":
                        self.zmap[(kind, ia, ib)] = loc.z
                    else:
                        rel = add(loc, mul(self.H, -1.0))
                        lat = rel.x * self.L.x + rel.y * self.L.y
                        self.latmap[(side, ia, ib)] = lat * side
                self.step_i += 1
            if self.step_i >= len(self.tasks):
                log(f"traced {len(self.rows)} hits / {len(self.tasks)}")
                if self.rows:
                    self.analyze()
                self.plan_shots()
                self.step_i = 0
                focus_main_window()
                self.advance("shots")
        elif s == "shots":
            server = get_world("UEDPIE_0")
            c = self.host()
            k = self.step_i // 6
            if k >= len(self.shots):
                if self.elapsed() >= 1.5:
                    log("RESULT DONE-PASS")
                    self.finish()
                return
            name, focus, campos, fov = self.shots[k]
            mid = self.skin_mid()
            ph = self.step_i % 6
            if ph == 0:
                c.call_method("DebugRoboViewAt", (campos.x, campos.y, campos.z, focus.x, focus.y, focus.z))
                unreal.SystemLibrary.execute_console_command(server, f"fov {fov}")
                if mid and not hasattr(self, "orig"):
                    self.orig = {n: mid.get_scalar_parameter_value(n)
                                 for n in ("ChromaStrength", "HeadlightPower", "HeadlightFloor")}
                    log(f"skin MID orig: {self.orig}")
                self.step_i += 1
                self.stage_t = time.monotonic()
            elif ph == 1 and self.elapsed() >= 1.4:
                unreal.SystemLibrary.execute_console_command(server, f"HighResShot 1600x900 filename={name}_v0def")
                self.step_i += 1
                self.stage_t = time.monotonic()
            elif ph == 2 and self.elapsed() >= 1.0:
                if mid:
                    mid.set_scalar_parameter_value("ChromaStrength", 0.0)
                self.step_i += 1
                self.stage_t = time.monotonic()
            elif ph == 3 and self.elapsed() >= 0.6:
                unreal.SystemLibrary.execute_console_command(server, f"HighResShot 1600x900 filename={name}_v1nochroma")
                self.step_i += 1
                self.stage_t = time.monotonic()
            elif ph == 4 and self.elapsed() >= 1.0:
                if mid:
                    mid.set_scalar_parameter_value("ChromaStrength", self.orig["ChromaStrength"])
                    mid.set_scalar_parameter_value("HeadlightPower", 0.0)
                    mid.set_scalar_parameter_value("HeadlightFloor", 1.0)
                self.step_i += 1
                self.stage_t = time.monotonic()
            elif ph == 5 and self.elapsed() >= 0.6:
                unreal.SystemLibrary.execute_console_command(server, f"HighResShot 1600x900 filename={name}_v2flat")
                if mid:
                    mid.set_scalar_parameter_value("HeadlightPower", self.orig["HeadlightPower"])
                    mid.set_scalar_parameter_value("HeadlightFloor", self.orig["HeadlightFloor"])
                log(f"  shot {name} x3")
                self.step_i += 1
                self.stage_t = time.monotonic()

    def finish(self):
        try:
            unreal.unregister_slate_post_tick_callback(self.handle)
        except Exception:
            pass


Probe()
