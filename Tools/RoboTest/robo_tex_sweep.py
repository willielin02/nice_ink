# 皮膚斑塊逐貼圖定罪（2026-08-22）：fp_scan_c_+30 機位、皮膚 MID 的每個貼圖參數
# 輪流換成引擎白圖→截圖→還原＝斑塊屬於哪張貼圖一次判死。
# 產出：Saved/robo_tex_sweep_result.txt + Screenshots/WindowsEditor/tx_*.png
import unreal, time, ctypes, traceback, os, glob

OUT = r"C:\games\Unreal Engine\nice_ink\Saved\robo_tex_sweep_result.txt"
SHOTDIR = r"C:\games\Unreal Engine\nice_ink\Saved\Screenshots\WindowsEditor"
LINES = []


def log(m):
    LINES.append(str(m))
    unreal.log_warning("[TX] " + str(m))
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

    def cb(h, _):
        b = ctypes.create_unicode_buffer(256)
        user32.GetWindowTextW(h, b, 256)
        if "NiceInk" in b.value and "Unreal Editor" in b.value:
            user32.SetForegroundWindow(h)
            return False
        return True
    P = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    user32.EnumWindows(P(cb), 0)


class Probe:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.host_pid = None
        self.step_i = 0
        self.params = None
        self.orig = {}
        for p in glob.glob(os.path.join(SHOTDIR, "tx_*.png")):
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

    def victim_mid(self, w):
        gs = unreal.GameplayStatics.get_game_state(w)
        vic = find_char(w, gs.get_editor_property("VictimPlayerId"))
        return vic, vic.get_editor_property("Body").get_material(0)

    def skin_mids_all_worlds(self):
        mids = []
        for tag in ("UEDPIE_0", "UEDPIE_1", "UEDPIE_2"):
            w = get_world(tag)
            if not w:
                continue
            gs = unreal.GameplayStatics.get_game_state(w)
            vic = find_char(w, gs.get_editor_property("VictimPlayerId")) if gs else None
            if not vic:
                continue
            for comp in vic.get_components_by_class(unreal.MeshComponent):
                if not comp.is_visible():
                    continue
                for mi in range(comp.get_num_materials()):
                    m = comp.get_material(mi)
                    if isinstance(m, unreal.MaterialInstanceDynamic):
                        try:
                            m.get_scalar_parameter_value("SkinBrightness")
                            mids.append(m)
                        except Exception:
                            pass
        return mids

    def all_victim_mids(self):
        # 多客戶端 PIE：截圖=聚焦視窗的世界（不一定=UEDPIE_0）⇒ 開關要打到所有世界
        mids = []
        for tag in ("UEDPIE_0", "UEDPIE_1", "UEDPIE_2"):
            w = get_world(tag)
            if not w:
                continue
            gs = unreal.GameplayStatics.get_game_state(w)
            if not gs:
                continue
            vic = find_char(w, gs.get_editor_property("VictimPlayerId"))
            if vic:
                m = vic.get_editor_property("Body").get_material(0)
                if m:
                    mids.append(m)
        return mids

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
            elif self.elapsed() > 120.0:
                log("FAIL: no drawing phase")
                self.finish()
        elif s == "setup":
            if self.elapsed() < 2.0:
                return
            server = get_world("UEDPIE_0")
            vic, mid = self.victim_mid(server)
            body = vic.get_editor_property("Body")
            origin, ext, rad = unreal.SystemLibrary.get_component_bounds(body)
            A = unreal.Vector(1, 0, 0) if ext.x >= ext.y else unreal.Vector(0, 1, 0)
            Lf = unreal.Vector(0, 1, 0) if ext.x >= ext.y else unreal.Vector(-1, 0, 0)
            H = unreal.Vector(origin.x + A.x * 12, origin.y + A.y * 12, origin.z + 4)
            p0 = unreal.Vector(H.x + A.x * 30, H.y + A.y * 30, H.z + 150)
            p1 = unreal.Vector(H.x + A.x * 30, H.y + A.y * 30, H.z - 50)
            hit = unreal.SystemLibrary.line_trace_single(server, p0, p1, unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,
                                                         True, [], unreal.DrawDebugTrace.NONE, True,
                                                         unreal.LinearColor.RED, unreal.LinearColor.GREEN, 0.0)
            t = hit.to_tuple()
            loc = t[4] if t[0] else unreal.Vector(H.x + A.x * 30, H.y + A.y * 30, H.z + 30)
            n_ = t[7] if t[0] else unreal.Vector(0, 0, 1)
            self.cam = unreal.Vector(loc.x + n_.x * 22, loc.y + n_.y * 22, loc.z + n_.z * 22)
            self.foc = loc
            # 貼圖參數列舉（MID 上直接列）
            names = []
            try:
                vals = mid.get_editor_property("texture_parameter_values")
                for pv in vals:
                    names.append(str(pv.get_editor_property("parameter_info").get_editor_property("name")))
            except Exception as e:
                log(f"enum via MID fail: {e}")
            if not names:
                par = mid.get_editor_property("parent")
                try:
                    names = [str(n) for n in unreal.MaterialEditingLibrary.get_texture_parameter_names(par)]
                except Exception as e:
                    log(f"enum via parent fail: {e}")
            self.params = names
            log(f"texture params: {names}")
            self.white = unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture")
            c = find_char(server, self.host_pid)
            c.call_method("DebugRoboViewAt", (self.cam.x, self.cam.y, self.cam.z, self.foc.x, self.foc.y, self.foc.z))
            unreal.SystemLibrary.execute_console_command(server, "fov 70")
            focus_main_window()
            self.step_i = -1   # 先拍 base
            self.advance("shots")
        elif s == "shots":
            server = get_world("UEDPIE_0")
            k = self.step_i
            if self.elapsed() < 1.2:
                return
            if k == 0:
                unreal.SystemLibrary.execute_console_command(server, "HighResShot 1600x900 filename=sw_bowbody")
                log("shot bowbody(變形版)")
                self.step_i = 1
                self.stage_t = time.monotonic()
            elif k == 1:
                n_ = 0
                for tag in ("UEDPIE_0", "UEDPIE_1", "UEDPIE_2"):
                    w2 = get_world(tag)
                    if not w2:
                        continue
                    gs2 = unreal.GameplayStatics.get_game_state(w2)
                    vic2 = find_char(w2, gs2.get_editor_property("VictimPlayerId")) if gs2 else None
                    if not vic2:
                        continue
                    body2 = vic2.get_editor_property("Body")
                    bow2 = vic2.get_editor_property("BowBody")
                    body2.set_visibility(True)
                    body2.set_owner_no_see(False)
                    bow2.set_visibility(False)
                    n_ += 1
                log(f"swapped display in {n_} worlds: Body(剛體靜態) ON / BowBody OFF")
                self.step_i = 2
                self.stage_t = time.monotonic()
            elif k == 2:
                unreal.SystemLibrary.execute_console_command(server, "HighResShot 1600x900 filename=sw_staticbody")
                log("shot staticbody(剛體版)")
                self.step_i = 3
                self.stage_t = time.monotonic()
            elif self.elapsed() >= 1.5:
                log("RESULT DONE-PASS")
                self.finish()

    def finish(self):
        try:
            unreal.unregister_slate_post_tick_callback(self.handle)
        except Exception:
            pass


Probe()
