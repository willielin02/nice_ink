# 伸縮脖幾何探針（一次性診斷）：seat0 受害者睜眼 φ=45，倒出 NeckStretch 現場數字
# ＋Head 骨世界座標＋BowBody 元件世界座標——判定管子到底畫在哪。
import os
import time
import traceback

import unreal

OUT = "C:/games/Unreal Engine/nice_ink/Saved/robo_neckprobe_result.txt"
os.makedirs(os.path.dirname(OUT), exist_ok=True)
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[NECKPROBE] " + str(msg))
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


class Probe:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.victim_pid = None
        self.victim = None
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

    def dump(self, tag, phi):
        c = self.victim
        bow = c.get_editor_property("bow_body")
        nk = c.get_editor_property("neck_stretch")
        hw = bow.get_bone_location_by_name("Head", unreal.BoneSpaces.WORLD_SPACE)
        hc = bow.get_bone_location_by_name("Head", unreal.BoneSpaces.COMPONENT_SPACE)
        bt = bow.get_world_transform()
        log(f"[{tag} phi={phi}] headW=({hw.x:.0f},{hw.y:.0f},{hw.z:.0f}) "
            f"headCS=({hc.x:.0f},{hc.y:.0f},{hc.z:.0f}) "
            f"bowW=({bt.translation.x:.0f},{bt.translation.y:.0f},{bt.translation.z:.0f})")
        log(f"[{tag}] neck: {nk.get_debug_summary()}")

    def step(self):
        s = self.stage
        if s == "boot":
            if time.monotonic() - self.t0 > 8.0:
                unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
                self.advance("wait_pie")
        elif s == "wait_pie":
            server = get_world("UEDPIE_0")
            if server and unreal.GameplayStatics.get_game_mode(server):
                unreal.GameplayStatics.get_game_mode(server).set_editor_property(
                    "DebugForcedVictimSeat", 0)
                self.advance("wait_drawing")
            elif self.elapsed() > 60.0:
                log("FAIL: no PIE")
                self.finish()
        elif s == "wait_drawing":
            gs = unreal.GameplayStatics.get_game_state(get_world("UEDPIE_0"))
            if gs and str(gs.get_editor_property("CurrentPhase")).endswith("DRAWING: 3>"):
                self.victim_pid = gs.get_editor_property("VictimPlayerId")
                self.advance("wake")
            elif self.elapsed() > 60.0:
                log("FAIL: no drawing")
                self.finish()
        elif s == "wake":
            if self.elapsed() < 1.5:
                return
            self.victim = find_char(get_world("UEDPIE_0"), self.victim_pid)
            self.victim.get_editor_property("DreamMaze").debug_trigger_exit()
            self.advance("rest_dump")
        elif s == "rest_dump":
            if self.elapsed() < 1.5:
                return
            self.dump("rest", 0)
            self.victim.debug_robo_sleep_look(180.0, 100.0)
            self.advance("phi45_dump")
        elif s == "phi45_dump":
            if self.elapsed() < 1.5:
                return
            self.dump("phi45", 45)
            nk45 = self.victim.get_editor_property("neck_stretch")
            ok = nk45.dump_neck_mesh(
                "C:/games/Unreal Engine/nice_ink/Saved/neck_mesh_phi45.csv")
            log(f"mesh dump phi45 -> {ok}")
            # 相機現場＋視線 trace（打世界＋身體；PMC 無碰撞不會擋 trace）
            cam = self.victim.get_editor_property("first_person_camera")
            cl = cam.get_socket_location("None")
            cr = cam.get_socket_rotation("None")
            fwd = cr.get_forward_vector()
            end = unreal.Vector(cl.x + fwd.x * 500, cl.y + fwd.y * 500, cl.z + fwd.z * 500)
            hit = unreal.SystemLibrary.line_trace_single(
                get_world("UEDPIE_0"), cl, end,
                unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [],
                unreal.DrawDebugTrace.NONE, True)
            if hit:
                ha = hit.to_tuple()
                log(f"camW=({cl.x:.0f},{cl.y:.0f},{cl.z:.0f}) fwd=({fwd.x:.2f},{fwd.y:.2f},{fwd.z:.2f}) "
                    f"traceHit dist={ha[3]:.0f} comp={ha[9].get_name() if ha[9] else 'none'}")
            import ctypes
            try:
                user32 = ctypes.windll.user32
                buf = ctypes.create_unicode_buffer(256)
                def cb(h, l):
                    user32.GetWindowTextW(h, buf, 255)
                    if "NiceInk" in buf.value and "Unreal Editor" in buf.value:
                        user32.SetForegroundWindow(h)
                        return False
                    return True
                P = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
                user32.EnumWindows(P(cb), 0)
            except Exception:
                pass
            unreal.SystemLibrary.execute_console_command(
                get_world("UEDPIE_0"), "HighResShot 1280x720 filename=neckab_with_tube")
            self.advance("phi45_ab")
        elif s == "phi45_ab":
            if self.elapsed() < 1.5:
                return
            nk = self.victim.get_editor_property("neck_stretch")
            nk.set_hidden_in_game(True, False)
            self.advance("phi45_ab2")
        elif s == "phi45_ab2":
            if self.elapsed() < 1.0:
                return
            unreal.SystemLibrary.execute_console_command(
                get_world("UEDPIE_0"), "HighResShot 1280x720 filename=neckab_no_tube")
            self.advance("phi45_ab3")
        elif s == "phi45_ab3":
            if self.elapsed() < 1.5:
                return
            self.victim.get_editor_property("neck_stretch").set_hidden_in_game(False, False)
            self.victim.debug_robo_sleep_look(0.0, 60.0)
            self.advance("phi180_dump")
        elif s == "phi180_dump":
            if self.elapsed() < 1.5:
                return
            self.dump("phi180", 180)
            log("DONE")
            self.finish()

    def finish(self):
        try:
            unreal.unregister_slate_post_tick_callback(self.handle)
        except Exception:
            pass


Probe()
log("neck probe armed")
