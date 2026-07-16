# 受害者=client 的跨端一致性測試（2026-07-16 bug 迴歸：server 對 autonomous proxy
# 的傳送 yaw 不會推回 owning client——受害者自己畫面上的身體整個旋轉）。
# seat1 強制受害者落在 client1，比對 server 世界 vs 受害者本地世界：
#   入睡 actor yaw/位置一致 → 睜眼 φ=90 頭骨位置一致 → 現身回座 yaw 一致。
# 產出：Saved/robo_seat1_result.txt
import math
import os
import time
import traceback

import unreal

OUT = "C:/games/Unreal Engine/nice_ink/Saved/robo_seat1_result.txt"
os.makedirs(os.path.dirname(OUT), exist_ok=True)
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[SEAT1SYNC] " + str(msg))
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
                return w, tag
    return None, None


def head_w(char):
    return char.get_editor_property("bow_body").get_bone_location_by_name(
        "Head", unreal.BoneSpaces.WORLD_SPACE)


def dyaw(a, b):
    return abs(((a - b + 180.0) % 360.0) - 180.0)


class Test:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.victim_pid = None
        self.victim_local = None
        self.pass_count = 0
        self.fail_count = 0
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    def advance(self, s):
        self.stage = s
        self.stage_t = time.monotonic()
        log("STAGE -> " + s)

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

    def server_victim(self):
        return find_char(get_world("UEDPIE_0"), self.victim_pid)

    def compare(self, tag, tol_pos=6.0, tol_yaw=2.0):
        sv = self.server_victim()
        lv = self.victim_local
        sp = sv.get_actor_location()
        lp = lv.get_actor_location()
        sy = sv.get_actor_rotation().yaw
        ly = lv.get_actor_rotation().yaw
        dp = math.sqrt((sp.x - lp.x) ** 2 + (sp.y - lp.y) ** 2 + (sp.z - lp.z) ** 2)
        dy = dyaw(sy, ly)
        self.check(f"{tag}: actor pos matches", dp < tol_pos, f"d={dp:.1f}cm")
        self.check(f"{tag}: actor yaw matches (the bug)", dy < tol_yaw,
                   f"server={sy:.1f} local={ly:.1f} d={dy:.1f}deg")

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
            elif self.elapsed() > 60.0:
                log("FAIL: no PIE")
                self.finish()
        elif s == "wait_drawing":
            gs = unreal.GameplayStatics.get_game_state(get_world("UEDPIE_0"))
            if gs and str(gs.get_editor_property("CurrentPhase")).endswith("DRAWING: 3>"):
                self.victim_pid = gs.get_editor_property("VictimPlayerId")
                self.advance("find_victim")
            elif self.elapsed() > 60.0:
                log("FAIL: no drawing")
                self.finish()
        elif s == "find_victim":
            if self.elapsed() < 2.0:
                return
            w, tag = local_world_of(self.victim_pid)
            self.check("victim on a remote client", tag in ("UEDPIE_1", "UEDPIE_2"), f"tag={tag}")
            if not w:
                self.finish()
                return
            self.victim_local = find_char(w, self.victim_pid)
            self.compare("asleep")
            # 鋪一筆讓現身有巡禮可進
            unreal.GameplayStatics.get_game_mode(get_world("UEDPIE_0")).debug_robo_stroke(
                unreal.Vector2D(0.45, 0.45), unreal.Vector2D(0.50, 0.47), 2)
            self.victim_local.get_editor_property("DreamMaze").debug_trigger_exit()
            self.advance("wake_orbit")
        elif s == "wake_orbit":
            if self.elapsed() < 2.0:
                return
            self.victim_local.debug_robo_sleep_look(90.0, 80.0)
            self.advance("verify_orbit")
        elif s == "verify_orbit":
            if self.elapsed() < 2.0:
                return
            sv = self.server_victim()
            lv = self.victim_local
            hs = head_w(sv)
            hl = head_w(lv)
            dd = math.sqrt((hs.x - hl.x) ** 2 + (hs.y - hl.y) ** 2 + (hs.z - hl.z) ** 2)
            self.check("orbit head pos matches across ends", dd < 3.0, f"d={dd:.2f}cm")

            # 旋轉一致性（頭骨位置=樞軸、對方位不敏感——位置檢查抓不到方位錯，測試盲區補洞）
            def head_fwd(char):
                r = char.get_editor_property("bow_body").get_bone_rotation_by_name(
                    "Head", unreal.BoneSpaces.WORLD_SPACE)
                return r.get_forward_vector()

            def ang(a, b):
                d = max(-1.0, min(1.0, a.x * b.x + a.y * b.y + a.z * b.z))
                return math.degrees(math.acos(d))

            fs = head_fwd(sv)
            fl = head_fwd(lv)
            self.check("head bone ROTATION matches server<->victim", ang(fs, fl) < 3.0,
                       f"d={ang(fs, fl):.1f}deg")
            w2 = get_world("UEDPIE_2")
            third = find_char(w2, self.victim_pid) if w2 else None
            if third:
                f3 = head_fwd(third)
                self.check("head bone ROTATION matches victim<->observer", ang(fl, f3) < 3.0,
                           f"d={ang(fl, f3):.1f}deg")
                az3 = third.get_editor_property("SleepAimAzDeg")
                tl3 = third.get_editor_property("SleepAimTiltDeg")
                azs = sv.get_editor_property("SleepAimAzDeg")
                tls = sv.get_editor_property("SleepAimTiltDeg")
                self.check("replicated aim values match on observer",
                           abs(dyaw(az3, azs)) < 1.0 and abs(tl3 - tls) < 1.0,
                           f"srv=({azs:.1f},{tls:.1f}) obs=({az3:.1f},{tl3:.1f})")
            # 相機 vs 頭骨 forward 的夾角（骨參考系有固定偏移，僅記錄供診斷）
            cam = lv.get_editor_property("first_person_camera")
            cf = cam.get_socket_rotation("None").get_forward_vector()
            log(f"INFO camera-vs-headbone forward angle = {ang(cf, fl):.1f}deg (fixed ref offset expected)")
            self.advance("aim_at_host")
        elif s == "aim_at_host":
            # 對視鏈終極驗證：幾何反解 (az,tilt) 對準主機玩家的頭，量臉指向誤差
            if self.elapsed() < 0.5:
                return
            lv = self.victim_local
            host = unreal.GameplayStatics.get_player_pawn(get_world("UEDPIE_1"), 0) \
                if False else find_char(get_world("UEDPIE_1"),
                    unreal.GameplayStatics.get_player_pawn(get_world("UEDPIE_0"), 0)
                    .get_editor_property("player_state").get_editor_property("player_id"))
            bow = lv.get_editor_property("bow_body")
            bt = bow.get_world_transform()
            hw = head_w(lv)
            target = host.get_actor_location()
            target = unreal.Vector(target.x, target.y, target.z + 60.0)  # 站姿頭部近似
            d = unreal.Vector(target.x - hw.x, target.y - hw.y, target.z - hw.z)
            dl = math.sqrt(d.x ** 2 + d.y ** 2 + d.z ** 2)
            d = unreal.Vector(d.x / dl, d.y / dl, d.z / dl)
            dcs = bt.inverse_transform_direction(d)
            upcs = bt.inverse_transform_direction(unreal.Vector(0, 0, 1))
            # feet = -(Z - (Z·up)up)（與 C++ 同構）
            zc = unreal.Vector(0, 0, 1)
            zdot = zc.x * upcs.x + zc.y * upcs.y + zc.z * upcs.z
            feet = unreal.Vector(-(zc.x - zdot * upcs.x), -(zc.y - zdot * upcs.y),
                                 -(zc.z - zdot * upcs.z))
            fl2 = math.sqrt(feet.x ** 2 + feet.y ** 2 + feet.z ** 2)
            feet = unreal.Vector(feet.x / fl2, feet.y / fl2, feet.z / fl2)
            e = unreal.Vector(upcs.y * feet.z - upcs.z * feet.y,
                              upcs.z * feet.x - upcs.x * feet.z,
                              upcs.x * feet.y - upcs.y * feet.x)  # up×feet
            udot = dcs.x * upcs.x + dcs.y * upcs.y + dcs.z * upcs.z
            tilt = math.degrees(math.acos(max(-1.0, min(1.0, udot))))
            hd = unreal.Vector(dcs.x - udot * upcs.x, dcs.y - udot * upcs.y, dcs.z - udot * upcs.z)
            hl2 = math.sqrt(hd.x ** 2 + hd.y ** 2 + hd.z ** 2)
            fd = hd.x * feet.x + hd.y * feet.y + hd.z * feet.z
            ed = hd.x * e.x + hd.y * e.y + hd.z * e.z
            azoff = math.degrees(math.atan2(ed, fd)) if hl2 > 1e-4 else 0.0
            self.aim_target = target
            self.aim_candidates = [(180.0 + azoff) % 360.0, (180.0 - azoff) % 360.0]
            self.aim_tilt = tilt
            lv.debug_robo_sleep_look(self.aim_candidates[0], tilt)
            self.advance("aim_check1")
        elif s == "aim_check1":
            if self.elapsed() < 1.0:
                return
            cam = self.victim_local.get_editor_property("first_person_camera")
            cl = cam.get_socket_location("None")
            cf = cam.get_socket_rotation("None").get_forward_vector()
            t = self.aim_target
            dv = unreal.Vector(t.x - cl.x, t.y - cl.y, t.z - cl.z)
            dl = math.sqrt(dv.x ** 2 + dv.y ** 2 + dv.z ** 2)
            dot = (cf.x * dv.x + cf.y * dv.y + cf.z * dv.z) / dl
            self.err1 = math.degrees(math.acos(max(-1.0, min(1.0, dot))))
            self.victim_local.debug_robo_sleep_look(self.aim_candidates[1], self.aim_tilt)
            self.advance("aim_check2")
        elif s == "aim_check2":
            if self.elapsed() < 1.0:
                return
            cam = self.victim_local.get_editor_property("first_person_camera")
            cl = cam.get_socket_location("None")
            cf = cam.get_socket_rotation("None").get_forward_vector()
            t = self.aim_target
            dv = unreal.Vector(t.x - cl.x, t.y - cl.y, t.z - cl.z)
            dl = math.sqrt(dv.x ** 2 + dv.y ** 2 + dv.z ** 2)
            dot = (cf.x * dv.x + cf.y * dv.y + cf.z * dv.z) / dl
            err2 = math.degrees(math.acos(max(-1.0, min(1.0, dot))))
            best = min(self.err1, err2)
            sign = "+" if self.err1 <= err2 else "-"
            self.check("face can aim at a real player (gaze chain)", best < 6.0,
                       f"err+={self.err1:.1f} err-={err2:.1f} (az sign {sign})")
            unreal.GameplayStatics.get_game_mode(get_world("UEDPIE_0")).debug_robo_emerge()
            self.advance("verify_emerge")
        elif s == "verify_emerge":
            if self.elapsed() < 2.5:
                return
            sv = self.server_victim()
            self.check("emerged", sv.get_editor_property("bAsleep") is False)
            self.compare("seat-restore")
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
log("seat1 sync test armed")
