# 作畫姿勢/穿膜/偷瞄改制截圖矩陣自查（2026-07-17）：
# 第一人稱（host=作畫者、聚焦主視窗拍）×多鎖定點＋偷瞄；第三人稱（client2=模特、host=攝影機）
# ×作畫/偷瞄。含數值斷言：頭到落筆點、腳貼地、膝蓋朝前（y-mirror 驗證）、FP 相機=view target
# 本體、偷瞄鏡頭對準受害者「替身真頭」、偷瞄頭部升高。
# 產出：Saved/robo_drawpose_result.txt + Saved/Screenshots/WindowsEditor/drawpose_*.png
import ctypes
import math
import time
import traceback

import unreal

OUT = "C:/games/Unreal Engine/nice_ink/Saved/robo_drawpose_result.txt"
import os
os.makedirs(os.path.dirname(OUT), exist_ok=True)
LINES = []
PASS_N = [0]
FAIL_N = [0]


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[DRAWPOSE] " + str(msg))
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(LINES))


def check(name, ok, detail=""):
    (PASS_N if ok else FAIL_N)[0] += 1
    log(("PASS " if ok else "FAIL ") + name + ((" | " + detail) if detail else ""))


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


def vq(v):
    q = unreal.Vector_NetQuantize()
    q.set_editor_property("x", v.x)
    q.set_editor_property("y", v.y)
    q.set_editor_property("z", v.z)
    return q


def nq(v):
    q = unreal.Vector_NetQuantizeNormal()
    q.set_editor_property("x", v.x)
    q.set_editor_property("y", v.y)
    q.set_editor_property("z", v.z)
    return q


def dist(a, b):
    return ((a.x - b.x) ** 2 + (a.y - b.y) ** 2 + (a.z - b.z) ** 2) ** 0.5


def bone_w(char, name):
    bow = char.get_editor_property("BowBody")
    return bow.get_bone_transform_by_name(name, unreal.BoneSpaces.WORLD_SPACE).translation


# 鎖定點矩陣：受害者本地座標（腳底原點、臉朝 +Y）＋本地外法線
FP_POINTS = [
    ("bellytop", unreal.Vector(0.0, 26.0, 95.0), unreal.Vector(0.0, 1.0, 0.0)),
    ("face",     unreal.Vector(0.0, 16.0, 150.0), unreal.Vector(0.0, 1.0, 0.0)),
    ("flank",    unreal.Vector(30.0, 8.0, 95.0), unreal.Vector(1.0, 0.0, 0.0)),
    ("thigh",    unreal.Vector(16.0, 8.0, 58.0), unreal.Vector(0.0, 1.0, 0.0)),
]


class Test:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.victim_pid = None
        self.host_pid = None
        self.model_pid = None
        self.fp_i = 0
        self.tp_i = 0
        self.draw_head_z = None
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

    # --- helpers ---
    def server(self):
        return get_world("UEDPIE_0")

    def host_char(self):
        return unreal.GameplayStatics.get_player_pawn(self.server(), 0)

    def enter_lean(self, artist, name, lp, ln):
        # 錨點只是方位提示——真實鎖定點由 C++ 鉤子 trace 到皮膚表面（真流程=準星 trace；
        # 硬編體內錨點會把 10cm 眼位埋進肉裡＝r2 的「FP 全是地板」假警報）
        victim = find_char(self.server(), self.victim_pid)
        bt = victim.get_editor_property("Body").get_world_transform()
        p = bt.transform_location(lp)
        n = bt.transform_direction(ln)
        ok = artist.call_method("DebugRoboEnterLean", (victim, p, n))
        log(f"[{name}] DebugRoboEnterLean -> {ok}")
        self.cur_point = p
        self.cur_normal = n
        self.cur_name = name

    def step(self):
        s = self.stage
        if s == "boot":
            if time.monotonic() - self.t0 > 8.0:
                unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
                self.advance("wait_pie")
        elif s == "wait_pie":
            server = self.server()
            if server and unreal.GameplayStatics.get_game_mode(server):
                unreal.GameplayStatics.get_game_mode(server).set_editor_property("DebugForcedVictimSeat", 1)
                self.advance("wait_drawing")
        elif s == "wait_drawing":
            server = self.server()
            gs = unreal.GameplayStatics.get_game_state(server) if server else None
            if gs and str(gs.get_editor_property("CurrentPhase")).endswith("DRAWING: 3>"):
                self.victim_pid = gs.get_editor_property("VictimPlayerId")
                self.host_pid = self.host_char().get_editor_property("player_state").get_editor_property("player_id")
                for c in unreal.GameplayStatics.get_all_actors_of_class(server, unreal.NiceInkCharacter):
                    pid = c.get_editor_property("player_state").get_editor_property("player_id")
                    if pid != self.victim_pid and pid != self.host_pid:
                        self.model_pid = pid
                        break
                log(f"victim={self.victim_pid} host={self.host_pid} model={self.model_pid}")
                focus_main_window()
                self.advance("fp_lock")
            elif self.elapsed() > 40.0:
                log("FAIL: drawing phase never came")
                self.finish()
        # --- 第一人稱矩陣：host 逐點鎖定＋截圖＋數值斷言 ---
        elif s == "fp_lock":
            if self.elapsed() < 0.6:
                return
            name, lp, ln = FP_POINTS[self.fp_i]
            self.enter_lean(find_char(self.server(), self.host_pid), name, lp, ln)
            self.advance("fp_verify")
        elif s == "fp_verify":
            if self.elapsed() < 1.5:
                return
            host = find_char(self.server(), self.host_pid)
            name = self.cur_name
            locked = host.get_editor_property("bLeanLocked")
            check(f"[{name}] lean locked", bool(locked))
            if locked:
                lp2 = host.get_editor_property("LeanPoint")
                ln2 = host.get_editor_property("LeanNormal")
                self.cur_point = unreal.Vector(lp2.x, lp2.y, lp2.z)
                self.cur_normal = unreal.Vector(ln2.x, ln2.y, ln2.z)
                head = bone_w(host, "Head")
                d = dist(head, self.cur_point)
                check(f"[{name}] head-to-lockpoint 22cm", abs(d - 22.0) < 6.0, f"d={d:.1f}")
                # 蹲姿 sanity：腳貼地（y-mirror 錯會先在腿上炸開）
                ground = host.get_actor_location().z - 92.0
                fz = bone_w(host, "LeftFoot").z
                check(f"[{name}] foot on ground", abs(fz - ground - 8.3) < 12.0, f"foot_z-ground={fz - ground:.1f}")
                # 膝蓋朝落筆點（鏡射驗證：蹲姿膝在身前）
                knee = bone_w(host, "LeftLeg")
                fwd = host.get_actor_forward_vector()
                hips = bone_w(host, "Hips")
                kd = (knee.x - hips.x) * fwd.x + (knee.y - hips.y) * fwd.y
                check(f"[{name}] knees point forward", kd > 0.0, f"dot={kd:.1f}")
                # FP 相機＝本體承載（OwnerNoSee 生效的前提）：camera manager 的實際取景位置
                # 必須＝本體 FirstPersonCamera（view target 若還是 CinematicCamera 就對不上）
                cam = host.get_editor_property("FirstPersonCamera")
                cl = cam.get_world_location()
                cm = unreal.GameplayStatics.get_player_camera_manager(self.server(), 0)
                cml = cm.get_camera_location()
                check(f"[{name}] view rendered through pawn camera", dist(cml, cl) < 3.0,
                      f"err={dist(cml, cl):.1f}")
                eye_expect = unreal.Vector(
                    self.cur_point.x + self.cur_normal.x * 10.0,
                    self.cur_point.y + self.cur_normal.y * 10.0,
                    self.cur_point.z + self.cur_normal.z * 10.0)
                check(f"[{name}] eye ~10cm off skin", dist(cl, eye_expect) < 8.0,
                      f"err={dist(cl, eye_expect):.1f}")
                if name == "flank":
                    self.draw_head_z = head.z
            unreal.SystemLibrary.execute_console_command(
                self.server(), f"HighResShot 1280x720 filename=drawpose_fp_{name}")
            self.advance("fp_next")
        elif s == "fp_next":
            if self.elapsed() < 1.2:
                return
            host = find_char(self.server(), self.host_pid)
            self.fp_i += 1
            if self.fp_i < len(FP_POINTS):
                host.call_method("ServerExitLean", ())
                self.advance("fp_lock")
            else:
                # 留在最後一點（flank）進偷瞄——先叫受害者甦醒＋抬頭轉向
                self.advance("wake_victim")
        elif s == "wake_victim":
            if self.elapsed() < 0.5:
                return
            w1 = get_world("UEDPIE_1")
            v1 = find_char(w1, self.victim_pid)
            v1.get_editor_property("DreamTrace").call_method("DebugForceComplete", ())  # v4.0
            self.advance("victim_look")
        elif s == "victim_look":
            if self.elapsed() < 1.5:
                return
            w1 = get_world("UEDPIE_1")
            v1 = find_char(w1, self.victim_pid)
            v1.call_method("DebugRoboSleepLook", (90.0, 60.0))  # 側向抬頭＝真頭離開枕位
            self.advance("fp_peek_on")
        elif s == "fp_peek_on":
            if self.elapsed() < 1.5:
                return
            sv = find_char(self.server(), self.victim_pid)
            check("victim eyes open", bool(sv.get_editor_property("bEyesOpen")))
            host = find_char(self.server(), self.host_pid)
            host.call_method("DebugRoboPeekHold", (True,))
            self.advance("fp_peek_verify")
        elif s == "fp_peek_verify":
            if self.elapsed() < 1.5:
                return
            host = find_char(self.server(), self.host_pid)
            check("peek active (robo hold)", bool(host.get_editor_property("bPeeking")))
            head = bone_w(host, "Head")
            if self.draw_head_z is not None:
                check("peek raises head", head.z > self.draw_head_z + 20.0,
                      f"draw_z={self.draw_head_z:.1f} peek_z={head.z:.1f}")
            # 旗艦鏡頭：FP 相機前向必須對準受害者「替身真頭」（升起中的頭，非枕上空位）
            victim = find_char(self.server(), self.victim_pid)
            vhead = bone_w(victim, "Head")
            cam = host.get_editor_property("FirstPersonCamera")
            cl = cam.get_world_location()
            cr = cam.get_socket_rotation("None")
            f = unreal.MathLibrary.get_forward_vector(cr)
            to = unreal.Vector(vhead.x - cl.x, vhead.y - cl.y, vhead.z - cl.z)
            L = (to.x ** 2 + to.y ** 2 + to.z ** 2) ** 0.5
            cosang = (f.x * to.x + f.y * to.y + f.z * to.z) / max(L, 1e-3)
            ang = math.degrees(math.acos(max(-1.0, min(1.0, cosang))))
            check("peek camera aims at victim TRUE head", ang < 10.0, f"ang={ang:.1f}")
            unreal.SystemLibrary.execute_console_command(
                self.server(), "HighResShot 1280x720 filename=drawpose_fp_peek")
            self.advance("fp_peek_off")
        elif s == "fp_peek_off":
            if self.elapsed() < 1.2:
                return
            host = find_char(self.server(), self.host_pid)
            host.call_method("DebugRoboPeekHold", (False,))
            host.call_method("ServerExitLean", ())
            self.advance("tp_lock")
        # --- 第三人稱矩陣：client2 當模特，host 當攝影機 ---
        elif s == "tp_lock":
            if self.elapsed() < 1.0:
                return
            # 攝影師先閃開＋藏身（站在受害者旁會擋掉模特的表面 trace；俯拍也會穿自己胸腹）
            host = self.host_char()
            hl = host.get_actor_location()
            host.set_actor_location(unreal.Vector(hl.x + 350.0, hl.y + 350.0, hl.z), False, True)
            for prop in ("Body", "BowBody"):
                comp = host.get_editor_property(prop)
                if comp:
                    comp.set_visibility(False, True)
            model = find_char(self.server(), self.model_pid)
            self.enter_lean(model, "tp", unreal.Vector(0.0, 26.0, 95.0), unreal.Vector(0.0, 1.0, 0.0))
            self.tp_i = 0
            self.advance("tp_draw_shots")
        elif s == "tp_draw_shots":
            if self.elapsed() < 1.5 + self.tp_i * 3.0:
                return
            self.tp_shot("drawpose_tp_draw")
            if self.tp_i >= 3:
                w2 = get_world("UEDPIE_2")
                m2 = find_char(w2, self.model_pid)
                m2.call_method("DebugRoboPeekHold", (True,))
                self.tp_i = 0
                self.advance("tp_peek_shots")
        elif s == "tp_peek_shots":
            if self.elapsed() < 2.0 + self.tp_i * 3.0:
                return
            if self.tp_i == 0:
                model = find_char(self.server(), self.model_pid)
                check("tp peek active", bool(model.get_editor_property("bPeeking")))
            self.tp_shot("drawpose_tp_peek")
            if self.tp_i >= 3:
                log(f"SUMMARY pass={PASS_N[0]} fail={FAIL_N[0]}")
                log("DONE (PIE left running)")
                self.finish()

    def tp_shot(self, prefix):
        server = self.server()
        model = find_char(server, self.model_pid)
        host = self.host_char()
        pc = unreal.GameplayStatics.get_player_controller(server, 0)
        mloc = model.get_actor_location()
        ang = math.radians(self.tp_i * 120.0 + 30.0)
        cam = unreal.Vector(mloc.x + 260.0 * math.cos(ang), mloc.y + 260.0 * math.sin(ang), mloc.z + 110.0)
        host.set_actor_location(cam, False, True)
        look = unreal.MathLibrary.find_look_at_rotation(
            unreal.Vector(cam.x, cam.y, cam.z + 62.0), unreal.Vector(mloc.x, mloc.y, mloc.z - 50.0))
        pc.set_control_rotation(look)
        unreal.SystemLibrary.execute_console_command(
            server, f"HighResShot 1280x720 filename={prefix}_{self.tp_i}")
        self.tp_i += 1

    def finish(self):
        log("HARNESS END")
        if self.handle:
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.handle = None


_t = Test()
log("harness registered")
