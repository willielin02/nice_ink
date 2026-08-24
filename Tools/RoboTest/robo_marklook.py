# 把 user NiMark 的兩個 UV 在**跑起來的遊戲裡**變成世界座標，架鏡頭多角度拍下來自己看。
# 起因：離線把模型每一層都查完（幾何/法線/UV/權重/所有貼圖層/姿勢）都解釋不了圈2，
# 那就別再從檔案找——直接看引擎畫出來的那一塊皮膚。
# 每個機位拍兩張：出貨組態 + 假光全關（把「著色」與「非著色」分開）。
# 前置：play_mode_robo.ps1 ＋ ini 掛 +StartupScripts=<本檔>；測完移除
import unreal
import time
import ctypes
import traceback
import os
import glob
import math

OUT = r"C:\games\Unreal Engine\nice_ink\Saved\robo_marklook_result.txt"
SHOTDIR = r"C:\games\Unreal Engine\nice_ink\Saved\Screenshots\WindowsEditor"
MARKS = [("m1", 0.11341, 0.50206), ("m2", 0.18297, 0.19234)]
LINES = []


def log(m):
    LINES.append(str(m))
    unreal.log_warning("[LOOK] " + str(m))
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


def V(x, y, z):
    return unreal.Vector(x, y, z)


class Probe:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.host_pid = None
        self.step_i = 0
        self.shots = []
        self.orig = {}
        for p in glob.glob(os.path.join(SHOTDIR, "lk_*.png")):
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

    def mids(self):
        out = []
        for tag in ("UEDPIE_0", "UEDPIE_1", "UEDPIE_2"):
            w = get_world(tag)
            if not w:
                continue
            for c in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NiceInkCharacter):
                for comp in c.get_components_by_class(unreal.MeshComponent):
                    for mi in range(comp.get_num_materials()):
                        m = comp.get_material(mi)
                        if isinstance(m, unreal.MaterialInstanceDynamic):
                            try:
                                m.get_scalar_parameter_value("HeadlightFloor")
                            except Exception:
                                continue
                            if m not in out:
                                out.append(m)
        return out

    def flat(self, on):
        ms = self.mids()
        if not self.orig and ms:
            self.orig = {p: ms[0].get_scalar_parameter_value(p) for p in ("HeadlightFloor", "HeadlightPower")}
            log("orig %s (mids=%d)" % (self.orig, len(ms)))
        for m in ms:
            m.set_scalar_parameter_value("HeadlightFloor", 1.0 if on else self.orig["HeadlightFloor"])
            m.set_scalar_parameter_value("HeadlightPower", 0.0 if on else self.orig["HeadlightPower"])

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
            ink = vic.get_editor_property("Body")
            vloc = vic.get_actor_location()
            for name, u, v in MARKS:
                wp = ink.call_method("ResolveUVToWorld", args=(unreal.Vector2D(u, v),))
                if wp is None or (abs(wp.x) + abs(wp.y) + abs(wp.z)) < 1e-6:
                    log("%s: ResolveUVToWorld FAILED for uv(%.5f,%.5f)" % (name, u, v))
                    continue
                log("%s uv(%.5f,%.5f) -> world (%.1f, %.1f, %.1f)" % (name, u, v, wp.x, wp.y, wp.z))
                # 表面法線：從遠處往該點打射線
                out = unreal.Vector(wp.x - vloc.x, wp.y - vloc.y, wp.z - vloc.z)
                ln = math.sqrt(out.x ** 2 + out.y ** 2 + out.z ** 2) or 1.0
                out = unreal.Vector(out.x / ln, out.y / ln, out.z / ln)
                st = unreal.Vector(wp.x + out.x * 60, wp.y + out.y * 60, wp.z + out.z * 60)
                hit = unreal.SystemLibrary.line_trace_single(
                    server, st, wp, unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, [],
                    unreal.DrawDebugTrace.NONE, True, unreal.LinearColor.RED, unreal.LinearColor.GREEN, 0.0)
                t = hit.to_tuple() if hit is not None else None
                nrm = t[7] if (t and t[0]) else out
                log("   surface normal (%.2f, %.2f, %.2f)  hit=%s  朝上程度 z=%.2f" % (
                    nrm.x, nrm.y, nrm.z, bool(t and t[0]), nrm.z))
                # 三個角度：正對 / 45 度 / 掠射 75 度；都看同一點
                side = unreal.Vector(-nrm.y, nrm.x, 0.0)
                sl = math.sqrt(side.x ** 2 + side.y ** 2) or 1.0
                side = unreal.Vector(side.x / sl, side.y / sl, 0.0)
                for ang, dist in ((0, 26), (45, 30), (75, 42)):
                    a = math.radians(ang)
                    d = unreal.Vector(nrm.x * math.cos(a) + side.x * math.sin(a),
                                      nrm.y * math.cos(a) + side.y * math.sin(a),
                                      nrm.z * math.cos(a))
                    dl = math.sqrt(d.x ** 2 + d.y ** 2 + d.z ** 2) or 1.0
                    cpos = unreal.Vector(wp.x + d.x / dl * dist, wp.y + d.y / dl * dist, wp.z + d.z / dl * dist)
                    self.shots.append(("lk_%s_%02d" % (name, ang), wp, cpos, 40))
            log("planned %d cameras" % len(self.shots))
            focus_main_window()
            self.step_i = 0
            self.advance("shots")
        elif s == "shots":
            if self.elapsed() < 1.2:
                return
            k = self.step_i // 4
            sub = self.step_i % 4
            if k >= len(self.shots):
                self.flat(False)
                log("RESULT DONE-PASS")
                self.finish()
                return
            name, foc, cpos, fov = self.shots[k]
            c = find_char(server, self.host_pid)
            if sub == 0:
                c.call_method("DebugRoboViewAt", (cpos.x, cpos.y, cpos.z, foc.x, foc.y, foc.z))
                unreal.SystemLibrary.execute_console_command(server, "fov %d" % fov)
                self.flat(False)
            elif sub == 1:
                unreal.SystemLibrary.execute_console_command(server, "HighResShot 1920x1080 filename=%s_ship" % name)
            elif sub == 2:
                self.flat(True)
            elif sub == 3:
                unreal.SystemLibrary.execute_console_command(server, "HighResShot 1920x1080 filename=%s_flat" % name)
                self.flat(False)
            self.step_i += 1
            self.stage_t = time.monotonic()

    def finish(self):
        try:
            unreal.unregister_slate_post_tick_callback(self.handle)
        except Exception:
            pass


Probe()
