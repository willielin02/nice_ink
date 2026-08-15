# 甦醒視野 FOV 探針（2026-08-04 user「以前沒這麼寬敞」）：量運行時實際 FOV——
# 定值 72 從 07-16 沒動過，但若啟用鏈沒跑到、相機停在站姿 90＝比 72 寬。
# 受害者強制 seat1（遠端 client）：入睡（閉眼）讀一次 → 描完睜眼讀一次。
# 實錘結果（08-04）：ASLEEP fov=90（畫面被夢蓋住無感）→ AWAKE fov=72.0 ✓
# ＝零變動；user 體感來源候選=yaw 修正後首次看到正確朝向＋視窗寬比放大水平視野。
# 產出：Saved/robo_wakefov_result.txt
import os
import time
import math
import traceback

import unreal

OUT = "C:/games/Unreal Engine/nice_ink/Saved/robo_wakefov_result.txt"
os.makedirs(os.path.dirname(OUT), exist_ok=True)
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[WAKEFOV] " + str(msg))
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


def local_world_of(pid):
    for tag in ("UEDPIE_0", "UEDPIE_1", "UEDPIE_2"):
        w = get_world(tag)
        if not w:
            continue
        pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
        if pawn:
            ps = pawn.get_editor_property("player_state")
            if ps and ps.get_editor_property("player_id") == pid:
                return w
    return None


def fov_of(char):
    cam = char.get_editor_property("first_person_camera")
    return float(cam.get_editor_property("field_of_view")) if cam else -1.0


class Probe:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.victim_pid = None
        self.victim_local = None
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
                    "DebugForcedVictimSeat", 1)
                self.advance("wait_drawing")
        elif s == "wait_drawing":
            server = get_world("UEDPIE_0")
            gs = unreal.GameplayStatics.get_game_state(server) if server else None
            if gs and str(gs.get_editor_property("CurrentPhase")).endswith("DRAWING: 3>"):
                self.victim_pid = gs.get_editor_property("VictimPlayerId")
                self.advance("asleep_read")
            elif self.elapsed() > 60.0:
                log("FAIL: no drawing phase")
                self.finish()
        elif s == "asleep_read":
            if self.elapsed() < 2.0:
                return
            w = local_world_of(self.victim_pid)
            if not w:
                log("FAIL: victim local world not found")
                self.finish()
                return
            self.victim_local = find_char(w, self.victim_pid)
            log(f"ASLEEP fov={fov_of(self.victim_local):.1f} "
                f"eyes={int(self.victim_local.get_editor_property('bEyesOpen'))}")
            self.victim_local.get_editor_property("DreamTrace").debug_force_complete()
            self.advance("awake_read")
        elif s == "awake_read":
            if self.elapsed() < 3.0:
                return
            fov = fov_of(self.victim_local)
            eyes = int(self.victim_local.get_editor_property("bEyesOpen"))
            log(f"AWAKE fov={fov:.1f} eyes={eyes}")
            log(f"{'PASS' if eyes == 1 and abs(fov - 72.0) < 0.5 else 'FAIL'} "
                f"wake fov equals SleepWakeFov 72 (spec #42)")
            # 08-15 朝向對賬（user 抓「醒來全員鏡像到對側」）：本人端 actor yaw / 控制器 yaw /
            # 相機 yaw 必須與 server 端 actor yaw 一致（差 <5°）——180° 差=世界讀感整個翻面
            server = get_world("UEDPIE_0")
            sv = find_char(server, self.victim_pid)
            lv = self.victim_local
            sy = sv.get_actor_rotation().yaw
            ly = lv.get_actor_rotation().yaw
            pc = lv.get_controller()
            cy = pc.get_control_rotation().yaw if pc else 999.0
            cam = lv.get_editor_property("FirstPersonCamera")
            camy = cam.get_world_rotation().yaw if cam else 999.0
            def d(a, b):
                x = (a - b + 180.0) % 360.0 - 180.0
                return abs(x)
            log(f"YAW server_actor={sy:.1f} local_actor={ly:.1f} local_ctrl={cy:.1f} local_cam={camy:.1f}")
            log(f"{'PASS' if d(sy, ly) < 5.0 else 'FAIL'} local actor yaw == server actor yaw (d={d(sy, ly):.1f})")
            log(f"{'PASS' if d(sy, cy) < 5.0 else 'FAIL'} local controller yaw == server actor yaw (d={d(sy, cy):.1f})")
            # 相機朝向＝臉指向（睜眼初始 az=180 對躺姿？）——只記錄不斷言；旁觀者位置對賬見下
            # 旁觀者對賬：server 世界的其他角色相對受害者的方位角，本人端同角色的方位角必須相同
            for c in unreal.GameplayStatics.get_all_actors_of_class(server, unreal.NiceInkCharacter):
                pid = c.get_editor_property("player_state").get_editor_property("player_id")
                if pid == self.victim_pid:
                    continue
                lc = find_char(lv.get_world(), pid)
                if not lc:
                    continue
                sd = c.get_actor_location() - sv.get_actor_location()
                ld = lc.get_actor_location() - lv.get_actor_location()
                sa = math.degrees(math.atan2(sd.y, sd.x)); la = math.degrees(math.atan2(ld.y, ld.x))
                log(f"{'PASS' if d(sa, la) < 5.0 else 'FAIL'} bystander pid={pid} bearing server={sa:.1f} local={la:.1f} (d={d(sa, la):.1f})")
            self.finish()

    def finish(self):
        log("DONE")
        unreal.unregister_slate_post_tick_callback(self.handle)


Probe()
