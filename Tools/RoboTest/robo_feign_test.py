# 裝睡測試 v1（2026-07-17 user 定案：無聲甦醒後按住 Shift＝回沉睡姿勢＋閉眼貼圖；
# 放開＝回到按下前的臉指向）
# 受害者強制 seat1＝遠端 client（RPC 走真網路）；三端驗證（本人/server/第三 client）：
#   睜眼→指向 (az90,tilt80) 抬升→feign ON：三端頭回枕上、閉眼貼圖、指向凍結不歸零
#   →feign OFF：頭硬切回原指向、睜眼貼圖→feign ON 中現身：狀態全清
# 產出：Saved/robo_feign_result.txt
import os
import time
import traceback

import unreal

OUT = "C:/games/Unreal Engine/nice_ink/Saved/robo_feign_result.txt"
os.makedirs(os.path.dirname(OUT), exist_ok=True)
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[FEIGNTEST] " + str(msg))
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


def head_w(char):
    return char.get_editor_property("bow_body").get_bone_location_by_name(
        "Head", unreal.BoneSpaces.WORLD_SPACE)


def eyes_closed(char):
    return char.get_editor_property("body").are_eyes_closed()


def dist3(a, b):
    return ((a.x - b.x) ** 2 + (a.y - b.y) ** 2 + (a.z - b.z) ** 2) ** 0.5


class Test:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.victim_pid = None
        self.victim_world = None
        self.victim_local = None
        self.observer_tag = None  # 第三 client（非受害者、非 server 標籤時仍讀 server world 的複本）
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

    def observer_victim(self):
        # 第三端（既非受害者本人、亦非 server）看到的受害者複本
        return find_char(get_world(self.observer_tag), self.victim_pid) if self.observer_tag else None

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
                    "DebugForcedVictimSeat", 1)  # 遠端受害者＝RPC 走真網路
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
            for t in WORLD_TAGS:
                if t != tag and t != "UEDPIE_0":
                    self.observer_tag = t
                    break
            log(f"observer world tag={self.observer_tag}")
            # 鋪一筆麥克筆讓現身有巡禮可進
            self.server_gm().debug_robo_stroke(
                unreal.Vector2D(0.45, 0.45), unreal.Vector2D(0.50, 0.47), 2)
            self.rest_head = head_w(self.victim_local)
            self.rest_head_srv = head_w(self.server_victim())
            maze = self.victim_local.get_editor_property("DreamMaze")
            maze.debug_trigger_exit()
            self.advance("verify_wake")
        elif s == "verify_wake":
            if self.elapsed() < 1.5:
                return
            sv = self.server_victim()
            self.check("eyes open (server)", sv.get_editor_property("bEyesOpen") is True)
            self.check("eyes-open texture on victim end", not eyes_closed(self.victim_local))
            # 指向 (az=90, tilt=80)：頭升起、他端跟上
            self.victim_local.debug_robo_sleep_look(90.0, 80.0)
            self.advance("verify_aim")
        elif s == "verify_aim":
            if self.elapsed() < 1.5:
                return
            h = head_w(self.victim_local)
            rise = h.z - self.rest_head.z
            self.check("aim pose lifted (plateau ~46)", abs(rise - 46.0) < 3.0, f"rise={rise:.1f}")
            sv = self.server_victim()
            az = sv.get_editor_property("SleepAimAzDeg")
            self.check("aim replicated (az~90)", abs(az - 90.0) < 1.5, f"az={az:.1f}")
            self.aim_head = h              # 放開 Shift 要回到的頭位置（user 規格的還原目標）
            self.aim_cam_pitch = cam_of(self.victim_local).get_socket_rotation("None").pitch
            # --- 裝睡 ON ---
            self.victim_local.debug_robo_feign_sleep(True)
            self.advance("verify_feign_on")
        elif s == "verify_feign_on":
            if self.elapsed() < 1.5:
                return
            sv = self.server_victim()
            self.check("feign replicated to server", sv.get_editor_property("bFeignSleep") is True)
            h = head_w(self.victim_local)
            self.check("own head back on pillow", dist3(h, self.rest_head) < 1.0,
                       f"d={dist3(h, self.rest_head):.2f}")
            hs = head_w(sv)
            self.check("server-end head back on pillow", dist3(hs, self.rest_head_srv) < 2.0,
                       f"d={dist3(hs, self.rest_head_srv):.2f}")
            ov = self.observer_victim()
            if ov:
                self.check("third-end head on pillow too", dist3(head_w(ov), self.rest_head_srv) < 3.0,
                           f"d={dist3(head_w(ov), self.rest_head_srv):.2f}")
                self.check("third-end eyes-closed texture", eyes_closed(ov))
            self.check("server-end eyes-closed texture", eyes_closed(sv))
            self.check("own-end eyes-closed texture", eyes_closed(self.victim_local))
            az = sv.get_editor_property("SleepAimAzDeg")
            self.check("aim frozen (not reset) while feigning", abs(az - 90.0) < 1.5, f"az={az:.1f}")
            r = cam_of(self.victim_local).get_socket_rotation("None")
            self.check("own camera back to rest gaze (pitch~90)", abs(r.pitch - 90.0) < 2.0,
                       f"pitch={r.pitch:.1f}")
            nk = self.victim_local.get_editor_property("neck_stretch")
            self.check("neck collapsed while feigning",
                       nk is not None and not nk.is_visible(),
                       f"vis={nk.is_visible() if nk else 'None'}")
            # --- 裝睡 OFF ---
            self.victim_local.debug_robo_feign_sleep(False)
            self.advance("verify_feign_off")
        elif s == "verify_feign_off":
            if self.elapsed() < 1.5:
                return
            sv = self.server_victim()
            self.check("feign cleared on server", sv.get_editor_property("bFeignSleep") is False)
            h = head_w(self.victim_local)
            self.check("head restored to pre-shift aim", dist3(h, self.aim_head) < 2.0,
                       f"d={dist3(h, self.aim_head):.2f}")
            r = cam_of(self.victim_local).get_socket_rotation("None")
            self.check("camera restored to pre-shift gaze",
                       abs(r.pitch - self.aim_cam_pitch) < 2.0,
                       f"pitch={r.pitch:.1f} want={self.aim_cam_pitch:.1f}")
            self.check("eyes-open texture restored (own)", not eyes_closed(self.victim_local))
            self.check("eyes-open texture restored (server)", not eyes_closed(sv))
            ov = self.observer_victim()
            if ov:
                self.check("eyes-open texture restored (third)", not eyes_closed(ov))
            # --- 裝睡中直接現身：狀態必須全清 ---
            self.victim_local.debug_robo_feign_sleep(True)
            self.advance("emerge_while_feigning")
        elif s == "emerge_while_feigning":
            if self.elapsed() < 1.0:
                return
            sv = self.server_victim()
            self.check("feign re-engaged before emerge", sv.get_editor_property("bFeignSleep") is True)
            self.server_gm().debug_robo_emerge()
            self.advance("verify_emerge")
        elif s == "verify_emerge":
            if self.elapsed() < 2.0:
                return
            sv = self.server_victim()
            self.check("emerged (asleep false)", sv.get_editor_property("bAsleep") is False)
            self.check("feign cleared by emerge", sv.get_editor_property("bFeignSleep") is False)
            self.check("eyes not closed after emerge (own)", not eyes_closed(self.victim_local))
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
log("feign test armed")
