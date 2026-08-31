# 甦醒臉指向測試 v4（2026-07-16 三改版：滑鼠 X/Y 直接指向 (az,tilt)＋UNeckStretch）
# 驗證：閉眼盲瞄不動骨/不漏指向→睜眼 FOV 102/零代打/rest 脖子收合隱藏
#       →(az180,tilt100) 深壓腳側：複製、抬升 46、pitch≈-10、roll≈0、眉心錨、
#         他端一致、脖子可見→(az0,tilt60) 頭側仰看：抬升 46、pitch≈+30
#       →tilt 鉗位（要 170 給 100）→現身 FOV 還原；尾聲八方位截圖（aim_azXXX）
# 產出：Saved/robo_orbit_result.txt
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


def neck_of(char):
    return char.get_editor_property("neck_stretch")


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
            # --- 閉眼盲瞄迴歸：骨頭不動、φ 不動、相機隱形轉、脖子收合 ---
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
            tl = sv.get_editor_property("SleepAimTiltDeg")
            self.check("blind-aim does not leak aim", abs(tl) < 0.5, f"tilt={tl:.1f}")
            yaw1 = cam_rot(self.victim_local).yaw
            dyaw = abs(((yaw1 - self.cam_yaw_closed0 + 180.0) % 360.0) - 180.0)
            self.check("blind-aim turns invisible camera", dyaw > 20.0,
                       f"yaw {self.cam_yaw_closed0:.1f}->{yaw1:.1f}")
            nk = neck_of(self.victim_local)
            self.check("neck collapsed while asleep-closed",
                       nk is not None and not nk.is_visible(),
                       f"vis={nk.is_visible() if nk else 'None'}")
            # 歸零盲瞄再睜眼（殘值不得上骨）
            self.victim_local.debug_robo_sleep_look(0.0, 0.0)
            # P0-3（2026-08-31）：喚醒改走 server hook（時間下限拒收早到的 Complete）
            self.server_gm().debug_robo_wake()
            self.advance("verify_wake")
        elif s == "verify_wake":
            if self.elapsed() < 1.5:
                return
            sv = self.server_victim()
            self.check("eyes open (server)", sv.get_editor_property("bEyesOpen") is True)
            fov = cam_of(self.victim_local).get_editor_property("field_of_view")
            self.check("wake FOV 72 (gaze contract)", abs(fov - 72.0) < 0.5, f"fov={fov}")
            tl = sv.get_editor_property("SleepAimTiltDeg")
            h = head_w(self.victim_local)
            self.check("no involuntary pose at wake",
                       abs(tl) < 0.5 and abs(h.z - self.rest_head.z) < 0.5,
                       f"tilt={tl:.1f} head dz={h.z - self.rest_head.z:.2f}")
            r = cam_rot(self.victim_local)
            self.check("rest gaze = straight up (pitch 90)", abs(r.pitch - 90.0) < 2.0,
                       f"pitch={r.pitch:.1f}")
            nk = neck_of(self.victim_local)
            self.check("neck hidden at rest (tilt=0, chord~0)",
                       nk is not None and not nk.is_visible(),
                       f"vis={nk.is_visible() if nk else 'None'}")
            # 深壓腳側：az=180、tilt=100（量測域上限）
            self.victim_local.debug_robo_sleep_look(180.0, 100.0)
            self.advance("verify_deep")
        elif s == "verify_deep":
            if self.elapsed() < 1.5:
                return
            sv = self.server_victim()
            az = sv.get_editor_property("SleepAimAzDeg")
            tl = sv.get_editor_property("SleepAimTiltDeg")
            self.check("aim replicated to server", abs(az - 180.0) < 1.0 and abs(tl - 100.0) < 1.0,
                       f"az={az:.1f} tilt={tl:.1f}")
            h = head_w(self.victim_local)
            rise = h.z - self.rest_head.z
            self.check("lift at deep tilt ~= 46cm (plateau)", abs(rise - 46.0) < 3.0,
                       f"rise={rise:.1f}")
            r = cam_rot(self.victim_local)
            self.check("gaze ~10deg below horizon (tilt=100)", abs(r.pitch + 10.0) < 4.0,
                       f"pitch={r.pitch:.1f}")
            self.check("no roll (crown stays up)", abs(r.roll) < 15.0,
                       f"roll={r.roll:.1f}")
            c = cam_loc(self.victim_local)
            dist = math.sqrt((c.x - h.x) ** 2 + (c.y - h.y) ** 2 + (c.z - h.z) ** 2)
            self.check("camera anchored at brow (15.26cm)", abs(dist - 15.26) < 1.0,
                       f"dist={dist:.2f}")
            hs = head_w(self.server_victim())
            dd = math.sqrt((hs.x - h.x) ** 2 + (hs.y - h.y) ** 2 + (hs.z - h.z) ** 2)
            self.check("other-end pose matches (same functions)", dd < 2.0, f"d={dd:.2f}")
            nk = neck_of(self.victim_local)
            self.check("neck visible when stretched", nk is not None and nk.is_visible())
            nks = neck_of(self.server_victim())
            self.check("neck visible on other end too",
                       nks is not None and nks.is_visible())
            self.victim_local.debug_robo_sleep_look(0.0, 60.0)
            self.advance("verify_crown")
        elif s == "verify_crown":
            if self.elapsed() < 1.5:
                return
            h = head_w(self.victim_local)
            rise = h.z - self.rest_head.z
            self.check("lift at crown-side ~= 46cm (plateau)", abs(rise - 46.0) < 2.5,
                       f"rise={rise:.1f}")
            r = cam_rot(self.victim_local)
            self.check("gaze ~30deg above horizon (tilt=60)", abs(r.pitch - 30.0) < 4.0,
                       f"pitch={r.pitch:.1f}")
            # 鉗位：要 170 只給量測上限（az180 帶=100）
            self.victim_local.debug_robo_sleep_look(180.0, 170.0)
            self.advance("verify_clamp")
        elif s == "verify_clamp":
            if self.elapsed() < 1.5:
                return
            sv = self.server_victim()
            tl = sv.get_editor_property("SleepAimTiltDeg")
            self.check("tilt clamped to measured domain (170->100)", abs(tl - 100.0) < 1.5,
                       f"tilt={tl:.1f}")
            self.victim_local.debug_robo_sleep_look(180.0, 0.0)
            focus_editor()
            self.shot_i = 0
            self.shot_sub = "set"
            self.advance("shots")
        elif s == "shots":
            if self.shot_i >= len(SHOT_PHIS):
                self.server_gm().debug_robo_emerge()
                self.advance("verify_emerge")
                return
            if self.shot_sub == "set":
                self.victim_local.debug_robo_sleep_look(float(SHOT_PHIS[self.shot_i]), 85.0)
                self.shot_sub = "wait"
                self.shot_t = time.monotonic()
            elif self.shot_sub == "wait":
                if time.monotonic() - self.shot_t >= 1.0:
                    az = SHOT_PHIS[self.shot_i]
                    sv = self.server_victim()
                    saz = sv.get_editor_property("SleepAimAzDeg")
                    hz = head_w(self.victim_local).z
                    log(f"SHOT az={az} server_az={saz:.1f} head_z={hz:.1f}")
                    unreal.SystemLibrary.execute_console_command(
                        self.victim_world, f"HighResShot 1280x720 filename=aim_az{az:03d}")
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
            nk = neck_of(self.victim_local)
            self.check("neck hidden after emerge", nk is not None and not nk.is_visible())
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
