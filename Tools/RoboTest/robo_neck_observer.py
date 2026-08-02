# 轆轤首伸縮脖旁觀截圖（Run B；2026-07-16）：DebugForcedVictimSeat=1（受害者在 client1），
# 主機（server 視窗＝可靠聚焦）瞬移到受害者頭側當觀察者——拍的是「真複製鏈」的他端外觀。
# 產出：Saved/robo_neckobs_result.txt ＋ Screenshots/neckobs_phiXXX_{side,close}.png
import ctypes
import os
import time
import traceback

import unreal

OUT = "C:/games/Unreal Engine/nice_ink/Saved/robo_neckobs_result.txt"
os.makedirs(os.path.dirname(OUT), exist_ok=True)
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[NECKOBS] " + str(msg))
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(LINES))


def get_world(tag):
    for w in unreal.ObjectIterator(unreal.World):
        if tag in w.get_path_name():
            return w
    return None


def chars_of(w):
    return list(unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NiceInkCharacter))


def find_char(w, pid):
    for c in chars_of(w):
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


# (az, tilt, 機位)：side=頭側全景、close=脖子特寫（臉指向制：az=方位、tilt=俯仰）
SHOTS = [
    (180, 0, "side"), (180, 40, "side"), (180, 100, "side"), (180, 100, "close"),
    (240, 90, "side"), (240, 90, "close"), (300, 90, "side"), (0, 60, "side"),
    (0, 60, "close"), (120, 90, "side"), (60, 130, "side"),
]


class Test:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.victim_pid = None
        self.victim_local = None
        self.shot_i = 0
        self.shot_sub = "set"
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

    def place_observer(self, mode):
        # 主機 pawn 瞬移到受害者頭側；close＝貼近脖子
        server = get_world("UEDPIE_0")
        host = unreal.GameplayStatics.get_player_pawn(server, 0)
        sv = find_char(server, self.victim_pid)
        h = head_w(sv)
        if mode == "side":
            pos = unreal.Vector(h.x + 90.0, h.y + 130.0, h.z + 45.0)
        else:
            pos = unreal.Vector(h.x + 45.0, h.y + 65.0, h.z + 10.0)
        host.set_actor_location(pos, False, True)
        eye = unreal.Vector(pos.x, pos.y, pos.z + 64.0)
        rot = unreal.MathLibrary.find_look_at_rotation(eye, h)
        ctrl = host.get_controller()
        ctrl.set_control_rotation(rot)

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
                log("FAIL: PIE never started")
                self.finish()
        elif s == "wait_drawing":
            gs = unreal.GameplayStatics.get_game_state(get_world("UEDPIE_0"))
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
            log(f"victim local tag={tag}")
            if not w:
                log("FAIL: victim world missing")
                self.finish()
                return
            self.victim_local = find_char(w, self.victim_pid)
            trace = self.victim_local.get_editor_property("DreamTrace")
            trace.debug_force_complete()  # 睜眼＝軌道啟用（v4.0 描圖取代迷宮）
            focus_editor()
            self.advance("shots")
        elif s == "shots":
            if self.shot_i >= len(SHOTS):
                self.advance("done")
                return
            az, tilt, mode = SHOTS[self.shot_i]
            if self.shot_sub == "set":
                self.victim_local.debug_robo_sleep_look(float(az), float(tilt))
                self.shot_sub = "place"
                self.shot_t = time.monotonic()
            elif self.shot_sub == "place":
                if time.monotonic() - self.shot_t >= 1.2:  # 等複製與擺骨落定
                    self.place_observer(mode)
                    self.shot_sub = "shoot"
                    self.shot_t = time.monotonic()
            elif self.shot_sub == "shoot":
                if time.monotonic() - self.shot_t >= 0.6:
                    unreal.SystemLibrary.execute_console_command(
                        get_world("UEDPIE_0"),
                        f"HighResShot 1280x720 filename=neckobs_az{az:03d}_t{tilt:03d}_{mode}")
                    log(f"SHOT az={az} tilt={tilt} {mode}")
                    self.shot_sub = "flush"
                    self.shot_t = time.monotonic()
            elif self.shot_sub == "flush":
                if time.monotonic() - self.shot_t >= 0.6:
                    self.shot_i += 1
                    self.shot_sub = "set"
        elif s == "done":
            log("DONE")
            self.finish()

    def finish(self):
        try:
            unreal.unregister_slate_post_tick_callback(self.handle)
        except Exception:
            pass


Test()
log("neck observer armed")
