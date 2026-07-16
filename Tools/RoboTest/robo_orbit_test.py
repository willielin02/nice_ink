# 甦醒環繞軌道測試（2026-07-16 脖底切盤軌道制，取代 robo_sleepgaze_test.py）
# 驗證：閉眼盲瞄不動骨→睜眼 FOV 102/零代打→φ 經真實 ServerUpdateSleepOrbit 鏈
#       →相機剛體含 roll 錨眉心→抬頭曲線（φ=180 頭升 >25cm）→他端姿勢一致
#       →現身 FOV 還原 90；尾聲八方位截圖（號誌/外觀自查）
# 產出：Saved/robo_orbit_result.txt ＋ Screenshots/orbitv2_phiXXX.png
import ctypes
import math
import os
import time
import traceback

import unreal

OUT = "C:/games/Unreal Engine/nice_ink/Saved/robo_orbit_result.txt"
os.makedirs(os.path.dirname(OUT), exist_ok=True)
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[ORBITTEST] " + str(msg))
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(LINES))


def get_world(tag):
    for w in unreal.ObjectIterator(unreal.World):
        if tag in w.get_path_name():
            return w
    return None


def gs_of(w):
    return unreal.GameplayStatics.get_game_state(w)


def chars_of(w):
    return list(unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NiceInkCharacter))


def find_char(w, pid):
    for c in chars_of(w):
        ps = c.get_editor_property("player_state")
        if ps and ps.get_editor_property("player_id") == pid:
            return c
    return None


WORLD_TAGS = ("UEDPIE_0", "UEDPIE_1", "UEDPIE_2")


def local_world_of(pid):
    for tag in WORLD_TAGS:
        w = get_world(tag)
        if not w:
            continue
        pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
        if pawn:
            ps = pawn.get_editor_property("player_state")
            if ps and ps.get_editor_property("player_id") == pid:
                return w, tag
    return None, None


def cam_of(char):
    return char.get_editor_property("first_person_camera")


def cam_rot(char):
    return cam_of(char).get_socket_rotation("None")


def cam_loc(char):
    return cam_of(char).get_socket_location("None")


def head_w(char):
    return char.get_editor_property("bow_body").get_bone_location_by_name(
        "Head", unreal.BoneSpaces.WORLD_SPACE)


def focus_editor():
    try:
        user32 = ctypes.windll.user32
        buf = ctypes.create_unicode_buffer(256)

        def enum_cb(h, l):
            user32.GetWindowTextW(h, buf, 255)
            if "NiceInk" in buf.value and "Unreal Editor" in buf.value:
                user32.SetForegroundWindow(h)
                return False
            return True
        WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
        user32.EnumWindows(WNDENUMPROC(enum_cb), 0)
    except Exception:
        pass


SHOT_PHIS = [0, 45, 90, 135, 180, 225, 270, 315]


class Test:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.victim_pid = None
        self.victim_world = None
        self.victim_local = None
        self.pass_count = 0
        self.fail_count = 0
        self.shot_i = 0
        self.shot_sub = "set"
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    def advance(self, stage):
        self.stage = stage
        self.stage_t = time.monotonic()
        log("STAGE -> " + stage)

    def elapsed(self):
        return time.monotonic() - self.stage_t

    def check(self, name, ok, detail=""):
        if ok:
            self.pass_count += 1
        else:
            self.fail_count += 1
        log(f"{name}: {'PASS' if ok else 'FAIL'} {detail}")

    def tick(self, dt):
        try:
            self.step()
        except Exception:
            log("EXC:\n" + traceback.format_exc())
            self.finish()

    def server_gm(self):
        return unreal.GameplayStatics.get_game_mode(get_world("UEDPIE_0"))

    def server_victim(self):
        return find_char(get_world("UEDPIE_0"), self.victim_pid)

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
                log("FAIL: PIE never started")
                self.finish()
        elif s == "wait_drawing":
            gs = gs_of(get_world("UEDPIE_0"))
            if gs and str(gs.get_editor_property("CurrentPhase")).endswith("DRAWING: 3>"):
                self.victim_pid = gs.get_editor_property("VictimPlayerId")
                log(f"victim={self.victim_pid}")
                self.advance("find_victim")
            elif self.elapsed() > 60.0:
                log("FAIL: drawing phase never came")
                self.finish()
        elif s == "find_victim":
            if self.elapsed() < 1.5:
                return
            w, tag = local_world_of(self.victim_pid)
            self.check("victim local world found", w is not None, f"tag={tag}")
            if not w:
                self.finish()
                return
            self.victim_world = w
            self.victim_local = find_char(w, self.victim_pid)
            # 鋪一筆麥克筆讓現身有巡禮可進
            self.server_gm().debug_robo_stroke(
                unreal.Vector2D(0.45, 0.45), unreal.Vector2D(0.50, 0.47), 2)
            # --- 閉眼盲瞄迴歸：骨頭不動、φ 不動、相機隱形轉 ---
            self.rest_head = head_w(self.victim_local)
            self.cam_yaw_closed0 = cam_rot(self.victim_local).yaw
            self.victim_local.debug_robo_sleep_look(45.0, 20.0)
            self.advance("verify_blindaim")
        elif s == "verify_blindaim":
            if self.elapsed() < 1.0:
                return
            h = head_w(self.victim_local)
            dz = abs(h.z - self.rest_head.z)
            self.check("blind-aim keeps bones frozen", dz < 0.5, f"head dz={dz:.2f}")
            sv = self.server_victim()
            orb = sv.get_editor_property("SleepOrbitDeg")
            self.check("blind-aim does not leak orbit", abs(orb) < 0.5, f"orbit={orb:.1f}")
            yaw1 = cam_rot(self.victim_local).yaw
            dyaw = abs(((yaw1 - self.cam_yaw_closed0 + 180.0) % 360.0) - 180.0)
            self.check("blind-aim turns invisible camera", dyaw > 20.0,
                       f"yaw {self.cam_yaw_closed0:.1f}->{yaw1:.1f}")
            # 歸零盲瞄再睜眼（殘值不得上骨）
            self.victim_local.debug_robo_sleep_look(0.0, 0.0)
            maze = self.victim_local.get_editor_property("DreamMaze")
            maze.debug_trigger_exit()
            self.advance("verify_wake")
        elif s == "verify_wake":
            if self.elapsed() < 1.5:
                return
            sv = self.server_victim()
            self.check("eyes open (server)", sv.get_editor_property("bEyesOpen") is True)
            fov = cam_of(self.victim_local).get_editor_property("field_of_view")
            self.check("wake FOV wide 102", abs(fov - 102.0) < 0.5, f"fov={fov}")
            orb = sv.get_editor_property("SleepOrbitDeg")
            h = head_w(self.victim_local)
            self.check("no involuntary pose at wake",
                       abs(orb) < 0.5 and abs(h.z - self.rest_head.z) < 0.5,
                       f"orbit={orb:.1f} head dz={h.z - self.rest_head.z:.2f}")
            r = cam_rot(self.victim_local)
            self.check("rest gaze = straight up (pitch 90)", abs(r.pitch - 90.0) < 2.0,
                       f"pitch={r.pitch:.1f}")
            # φ=90：滾頭側視
            self.victim_local.debug_robo_sleep_look(90.0, 0.0)
            self.advance("verify_phi90")
        elif s == "verify_phi90":
            if self.elapsed() < 1.5:
                return
            sv = self.server_victim()
            orb = sv.get_editor_property("SleepOrbitDeg")
            self.check("orbit replicated to server", abs(orb - 90.0) < 1.0, f"orbit={orb:.1f}")
            r = cam_rot(self.victim_local)
            self.check("rigid camera rolls with head (|roll|>45)", abs(r.roll) > 45.0,
                       f"roll={r.roll:.1f}")
            # 相機錨眉心：|cam-head| = sqrt(13^2+8^2) = 15.26
            h = head_w(self.victim_local)
            c = cam_loc(self.victim_local)
            dist = math.sqrt((c.x - h.x) ** 2 + (c.y - h.y) ** 2 + (c.z - h.z) ** 2)
            self.check("camera anchored at brow (15.26cm)", abs(dist - 15.26) < 1.0,
                       f"dist={dist:.2f}")
            # 他端一致性：server 世界的替身頭骨位置應與受害者本地一致（同軌道求值）
            hs = head_w(self.server_victim())
            dd = math.sqrt((hs.x - h.x) ** 2 + (hs.y - h.y) ** 2 + (hs.z - h.z) ** 2)
            self.check("other-end pose matches (same track)", dd < 2.0, f"d={dd:.2f}")
            self.victim_local.debug_robo_sleep_look(180.0, 0.0)
            self.advance("verify_phi180")
        elif s == "verify_phi180":
            if self.elapsed() < 1.5:
                return
            h = head_w(self.victim_local)
            rise = h.z - self.rest_head.z
            self.check("lift curve engaged at 180 (rise>25cm)", rise > 25.0,
                       f"rise={rise:.1f}")
            r = cam_rot(self.victim_local)
            self.check("gaze points below horizon at 180", r.pitch < -25.0,
                       f"pitch={r.pitch:.1f}")
            self.victim_local.debug_robo_sleep_look(0.0, 0.0)
            focus_editor()
            self.shot_i = 0
            self.shot_sub = "set"
            self.advance("shots")
        elif s == "shots":
            if self.shot_i >= len(SHOT_PHIS):
                # 現身：FOV 還原
                self.server_gm().debug_robo_emerge()
                self.advance("verify_emerge")
                return
            if self.shot_sub == "set":
                self.victim_local.debug_robo_sleep_look(float(SHOT_PHIS[self.shot_i]), 0.0)
                self.shot_sub = "wait"
                self.shot_t = time.monotonic()
            elif self.shot_sub == "wait":
                if time.monotonic() - self.shot_t >= 1.0:
                    phi = SHOT_PHIS[self.shot_i]
                    sv = self.server_victim()
                    orb = sv.get_editor_property("SleepOrbitDeg")
                    hz = head_w(self.victim_local).z
                    log(f"SHOT phi={phi} server_orbit={orb:.1f} head_z={hz:.1f}")
                    unreal.SystemLibrary.execute_console_command(
                        self.victim_world, f"HighResShot 1280x720 filename=orbitv2_phi{phi:03d}")
                    self.shot_sub = "flush"
                    self.shot_t = time.monotonic()
            elif self.shot_sub == "flush":
                if time.monotonic() - self.shot_t >= 0.6:
                    self.shot_i += 1
                    self.shot_sub = "set"
        elif s == "verify_emerge":
            if self.elapsed() < 2.0:
                return
            sv = self.server_victim()
            self.check("emerged (asleep false)", sv.get_editor_property("bAsleep") is False)
            fov = cam_of(self.victim_local).get_editor_property("field_of_view")
            self.check("FOV restored 90", abs(fov - 90.0) < 0.5, f"fov={fov}")
            self.advance("done")
        elif s == "done":
            log(f"SUMMARY pass={self.pass_count} fail={self.fail_count}")
            log("DONE" if self.fail_count == 0 else "FAIL")
            self.finish()

    def finish(self):
        try:
            unreal.unregister_slate_post_tick_callback(self.handle)
        except Exception:
            pass


Test()
log("orbit test armed")
