# 甦醒視線制測試（2026-07-16 改版：眉心相機＋廣角＋姿勢系統自動）
# 驗證：睜眼→FOV 102→DebugRoboSleepLook 設世界視線→相機朝向=視線且 roll=0
#       →頭部姿態追趕（twist/bend 經真實 ServerUpdateSleepLook 鏈上 server）
#       →低頭被擋時抬頭檔（SleepLiftCm>0）→瞳孔殘差複製→現身 FOV 還原 90
# 產出：scratchpad/robo_sleepgaze_result.txt
import time
import traceback

import unreal

OUT = "C:/games/Unreal Engine/nice_ink/Saved/robo_sleepgaze_result.txt"
import os
os.makedirs(os.path.dirname(OUT), exist_ok=True)
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[GAZETEST] " + str(msg))
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
    cam = cam_of(char)
    for name in ("k2_get_component_rotation", "get_component_rotation", "get_world_rotation"):
        fn = getattr(cam, name, None)
        if fn:
            return fn()
    return cam.get_socket_rotation("None")  # SceneComponent.GetSocketRotation(NAME_None)=元件世界旋轉


class Test:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.victim_pid = None
        self.victim_world = None
        self.victim_local = None
        self.gaze1 = None  # (yaw,pitch) 水平側向注視
        self.pass_count = 0
        self.fail_count = 0
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
                unreal.GameplayStatics.get_game_mode(server).set_editor_property("DebugForcedVictimSeat", 1)
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
            # 鋪一筆麥克筆讓現身有巡禮可進（沿用迷宮測試慣例）
            self.server_gm().debug_robo_stroke(unreal.Vector2D(0.45, 0.45), unreal.Vector2D(0.50, 0.47), 2)
            maze = self.victim_local.get_editor_property("DreamMaze")
            maze.debug_trigger_exit()
            self.advance("verify_wake")
        elif s == "verify_wake":
            if self.elapsed() < 1.5:
                return
            eyes = self.server_victim().get_editor_property("bEyesOpen")
            self.check("eyes open (server)", eyes is True)
            fov = cam_of(self.victim_local).get_editor_property("field_of_view")
            self.check("wake FOV wide 102", abs(fov - 102.0) < 0.5, f"fov={fov}")
            # 假破綻迴歸：睜眼瞬間頭必須一動不動（初版把初始視線鉗進舒適帶＝
            # 差 25° 觸發追趕＝系統代打的頭動；無聲甦醒的破綻只能出自玩家操作）
            sv = self.server_victim()
            tw0 = sv.get_editor_property("SleepTwistDeg")
            bd0 = sv.get_editor_property("SleepBendDeg")
            lf0 = sv.get_editor_property("SleepLiftCm")
            self.check("no involuntary pose at wake", abs(tw0) < 1.0 and bd0 < 1.0 and lf0 < 1.0,
                       f"twist={tw0:.1f} bend={bd0:.1f} lift={lf0:.1f}")
            self.rest_head_z = sv.get_editor_property("bow_body").get_bone_location_by_name(
                "Head", unreal.BoneSpaces.WORLD_SPACE).z
            # 視線 1：世界水平側向（yaw=受害者身體 yaw+90 → 對仰躺者是側邊）
            body_yaw = self.victim_local.get_actor_rotation().yaw
            self.gaze1 = (body_yaw + 90.0, 0.0)
            self.victim_local.debug_robo_sleep_look(self.gaze1[0], self.gaze1[1])
            self.advance("verify_gaze1")
        elif s == "verify_gaze1":
            if self.elapsed() < 2.0:
                return
            r = cam_rot(self.victim_local)
            want_yaw = ((self.gaze1[0] + 180.0) % 360.0) - 180.0
            dyaw = abs(((r.yaw - want_yaw + 180.0) % 360.0) - 180.0)
            self.check("cam yaw == gaze yaw", dyaw < 1.0, f"cam={r.yaw:.1f} want={want_yaw:.1f}")
            self.check("cam pitch == gaze pitch", abs(r.pitch - 0.0) < 1.0, f"pitch={r.pitch:.1f}")
            self.check("cam roll == 0 (no let-it-be)", abs(r.roll) < 1.0, f"roll={r.roll:.1f}")
            sv = self.server_victim()
            tw = sv.get_editor_property("SleepTwistDeg")
            bd = sv.get_editor_property("SleepBendDeg")
            self.check("head chased gaze (server got pose)", abs(tw) > 5.0 or bd > 5.0,
                       f"twist={tw:.1f} bend={bd:.1f}")
            # 視線 2：看自己的肚子／跪在腳邊的作畫者＝腳向方位＋仰角 +25°
            #（頭在地板上，身體在水平線之上——低頭朝腳被胸口擋＝抬頭檔應啟動；
            # 俯角朝腳=看腳後方的地板，臉翻面即可合法達成，不會觸發護欄——上輪教訓）
            bb = self.victim_local.get_editor_property("bow_body")
            crown = unreal.MathLibrary.get_up_vector(bb.get_socket_rotation("None"))
            feet_yaw = unreal.MathLibrary.atan2(-crown.y, -crown.x) * 180.0 / 3.14159265
            self.feet_yaw = feet_yaw
            log(f"feet_yaw={feet_yaw:.1f}")
            self.victim_local.debug_robo_sleep_look(feet_yaw, 25.0)
            self.advance("verify_gaze2")
        elif s == "verify_gaze2":
            if self.elapsed() < 3.0:
                return
            r = cam_rot(self.victim_local)
            self.check("cam pitch tracks +25", abs(r.pitch - 25.0) < 1.0, f"pitch={r.pitch:.1f}")
            self.check("cam roll still 0", abs(r.roll) < 1.0, f"roll={r.roll:.1f}")
            sv = self.server_victim()
            lift = sv.get_editor_property("SleepLiftCm")
            bd = sv.get_editor_property("SleepBendDeg")
            py = sv.get_editor_property("SleepPupilYawDeg")
            pp = sv.get_editor_property("SleepPupilPitchDeg")
            # 仰躺：臉精確對準（瞳孔≈0）＋演出性抬頭（bend65→lift=36 拿滿，2026-07-16
            # user 定案「別人要看得到頭」＝訊號高度需求）
            self.check("face converged exactly (supine bend arc clears chest)",
                       abs(bd - 65.0) < 3.0 and abs(py) < 2.0 and abs(pp) < 2.0,
                       f"bend={bd:.1f} pupil=({py:.1f},{pp:.1f})")
            self.check("theatrical lift (crane while looking at own body)", lift > 25.0,
                       f"lift={lift:.1f}")
            crane_head_z = sv.get_editor_property("bow_body").get_bone_location_by_name(
                "Head", unreal.BoneSpaces.WORLD_SPACE).z
            self.check("head raised for signal visibility (dz>30 vs rest)",
                       crane_head_z - self.rest_head_z > 30.0,
                       f"restZ={self.rest_head_z:.1f} craneZ={crane_head_z:.1f}")
            self.check("pupil residual clamped", abs(py) <= 35.01 and abs(pp) <= 35.01,
                       f"pupil=({py:.1f},{pp:.1f})")
            log(f"pose@low-gaze: twist={sv.get_editor_property('SleepTwistDeg'):.1f} "
                f"bend={bd:.1f} lift={lift:.1f}")
            # 視線 3：回望天花板（pitch 80）——抬頭應回落、頭回正
            self.victim_local.debug_robo_sleep_look(self.gaze1[0], 80.0)
            self.advance("verify_gaze3")
        elif s == "verify_gaze3":
            if self.elapsed() < 3.5:
                return
            sv = self.server_victim()
            bd = sv.get_editor_property("SleepBendDeg")
            lift = sv.get_editor_property("SleepLiftCm")
            self.check("head settles back up (bend small)", bd < 25.0, f"bend={bd:.1f}")
            self.check("lift stays zero", lift < 1.0, f"lift={lift:.1f}")
            # 截圖（受害者視窗要有焦點才會處理——非致命）
            unreal.SystemLibrary.execute_console_command(
                self.victim_world, "HighResShot 1280x720 filename=sleepgaze_victim")
            # 翻身→趴姿：低頭立刻鑽地板＝護欄擋 0°＝抬頭檔的真主場
            self.server_gm().debug_robo_flip()
            self.advance("flip_gaze")
        elif s == "flip_gaze":
            if self.elapsed() < 1.5:
                return
            fd = self.server_victim().get_editor_property("bBodyFaceDown")
            self.check("body flipped face-down", fd is True)
            # 趴姿貓頭鷹（脖底切盤）：看向天花板＝臉整個翻上來（twist≈±180、bend≈0）
            self.victim_local.debug_robo_sleep_look(self.feet_yaw + 90.0, 70.0)
            self.advance("verify_flip_gaze")
        elif s == "verify_flip_gaze":
            if self.elapsed() < 3.5:
                return
            sv = self.server_victim()
            tw = sv.get_editor_property("SleepTwistDeg")
            py = sv.get_editor_property("SleepPupilYawDeg")
            pp = sv.get_editor_property("SleepPupilPitchDeg")
            tw_n = ((tw + 180.0) % 360.0) - 180.0
            r = cam_rot(self.victim_local)
            self.check("prone owl: face flipped skyward (|twist|>140)", abs(tw_n) > 140.0,
                       f"twist={tw:.1f}")
            self.check("prone owl: face converged (pupils near 0)",
                       abs(py) < 8.0 and abs(pp) < 8.0, f"pupil=({py:.1f},{pp:.1f})")
            self.check("prone: cam gaze-locked no roll",
                       abs(r.pitch - 70.0) < 1.0 and abs(r.roll) < 1.0,
                       f"pitch={r.pitch:.1f} roll={r.roll:.1f}")
            self.advance("emerge")
        elif s == "emerge":
            if self.elapsed() < 1.5:
                return
            self.server_gm().debug_robo_emerge()
            self.advance("verify_emerge")
        elif s == "verify_emerge":
            if self.elapsed() < 2.5:
                return
            phase = str(gs_of(get_world("UEDPIE_0")).get_editor_property("CurrentPhase"))
            self.check("emerge -> tour", "TOUR" in phase.upper(), f"phase={phase}")
            fov = cam_of(self.victim_local).get_editor_property("field_of_view")
            self.check("FOV restored 90", abs(fov - 90.0) < 0.5, f"fov={fov}")
            self.advance("done")
        elif s == "done":
            if self.elapsed() < 1.0:
                return
            log(f"CHECKS: {self.pass_count} pass / {self.fail_count} fail")
            log("DONE" if self.fail_count == 0 else "FAIL")
            self.finish()

    def finish(self):
        if self.handle:
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.handle = None


Test()
