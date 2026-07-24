# 作畫者後頸隆起調查探針（2026-07-21）：user viewport 抓到深摺埋頭的作畫者
# 後頸有腫塊。假說：臉向 delta（45°）把切縫弦長拉過 NeckHideChordCm=1.5 門檻
# → NeckStretch 橋接面在作畫者身上現身，而 NapeBulge=1.25/ChinTuck=0.88 是為
# 甦醒者伸長脖調的美學值＝埋頭姿上讀成腫塊。
# 量測：入座埋頭 → 弦長/可見性數字 + 後頸近拍；現場把橋參數改中性(1.0/1.0)再拍
# ＝A/B 對照（原因實錘＋修法預覽一次拿到）。
# 產出：Saved/robo_neckbulge_result.txt + Screenshots/neckbulge_*.png
import ctypes
import math
import time
import traceback

import unreal

OUT = "C:/games/Unreal Engine/nice_ink/Saved/robo_neckbulge_result.txt"
import os
os.makedirs(os.path.dirname(OUT), exist_ok=True)
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[NECKBULGE] " + str(msg))
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


def bone_w(char, name):
    bow = char.get_editor_property("BowBody")
    return bow.get_bone_transform_by_name(name, unreal.BoneSpaces.WORLD_SPACE).translation


def aim_towards(char, world_pt):
    cam = char.get_editor_property("FirstPersonCamera").get_world_location()
    dx, dy, dz = world_pt.x - cam.x, world_pt.y - cam.y, world_pt.z - cam.z
    L = max((dx * dx + dy * dy + dz * dz) ** 0.5, 1e-3)
    az = math.degrees(math.atan2(dy, dx))
    tilt = -math.degrees(math.asin(max(-1.0, min(1.0, dz / L))))
    return az, tilt


BELLY = unreal.Vector(0.0, 26.0, 95.0)
BELLY_N = unreal.Vector(0.0, 1.0, 0.0)


class Probe:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.victim_pid = None
        self.host_pid = None
        self.model_pid = None
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

    def server(self):
        return get_world("UEDPIE_0")

    def neck_summary(self, char):
        ns = char.get_editor_property("NeckStretch")
        vis = ns.is_visible() if ns else False
        s = str(ns.call_method("GetDebugSummary", ())) if ns else "none"
        return f"vis={vis} {s}"

    def cam_on_nape(self, side=0.0):
        # host 相機：side=0 後上方；side=±1 側面平視（user 截圖同款視角——
        # 隆起在側面剪影上才讀得出來，正後方被背肉擋住）
        server = self.server()
        model = find_char(server, self.model_pid)
        host = find_char(server, self.host_pid)
        pc = unreal.GameplayStatics.get_player_controller(server, 0)
        mhead = bone_w(model, "Head")
        fwd = model.get_actor_forward_vector()
        if side == 0.0:
            cam = unreal.Vector(mhead.x - fwd.x * 110.0, mhead.y - fwd.y * 110.0, mhead.z + 50.0)
        else:
            cam = unreal.Vector(mhead.x - fwd.y * side * 130.0 - fwd.x * 30.0,
                                mhead.y + fwd.x * side * 130.0 - fwd.y * 30.0,
                                mhead.z + 18.0)
        host.set_actor_location(cam, False, True)
        pc.set_control_rotation(unreal.MathLibrary.find_look_at_rotation(
            unreal.Vector(cam.x, cam.y, cam.z + 62.0), mhead))

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
                hc = unreal.GameplayStatics.get_player_pawn(server, 0)
                self.host_pid = hc.get_editor_property("player_state").get_editor_property("player_id")
                for c in unreal.GameplayStatics.get_all_actors_of_class(server, unreal.NiceInkCharacter):
                    pid = c.get_editor_property("player_state").get_editor_property("player_id")
                    if pid != self.victim_pid and pid != self.host_pid:
                        self.model_pid = pid
                        break
                focus_main_window()
                self.advance("enter")
        elif s == "enter":
            if self.elapsed() < 0.8:
                return
            server = self.server()
            model = find_char(server, self.model_pid)
            victim = find_char(server, self.victim_pid)
            bt = victim.get_editor_property("Body").get_world_transform()
            p = bt.transform_location(BELLY)
            n = bt.transform_direction(BELLY_N)
            ok = model.call_method("DebugRoboEnterLean", (victim, p, n))
            log(f"enter belly -> {ok}")
            self.advance("baseline")
        elif s == "baseline":
            if self.elapsed() < 1.5:
                return
            server = self.server()
            model = find_char(server, self.model_pid)
            log("BASELINE(belly aim) " + self.neck_summary(model))
            log("BASELINE lean " + str(model.call_method("DebugLeanSummary", ())))
            # 重現 user 現場：中度前摺＋頭下壓、reach 成立的正常作畫（瞄近側可達低點；
            # 上一輪逼到 -40 極限摺=頭埋太深、腫塊區被身體擋住看不到）
            w2 = get_world("UEDPIE_2")
            m2 = find_char(w2, self.model_pid)
            victim = find_char(server, self.victim_pid)
            bt = victim.get_editor_property("Body").get_world_transform()
            low_p = bt.transform_location(unreal.Vector(20.0, 22.0, 82.0))
            az, tilt = aim_towards(model, low_p)
            m2.call_method("DebugRoboDrawAim", (az, min(tilt + 8.0, 79.0)))
            self.advance("steep")
        elif s == "steep":
            if self.elapsed() < 2.0:
                return
            server = self.server()
            model = find_char(server, self.model_pid)
            log("STEEP(head-down) " + self.neck_summary(model))
            log("STEEP lean " + str(model.call_method("DebugLeanSummary", ())))
            self.cam_on_nape()
            self.advance("shot_125")
        elif s == "shot_125":
            if self.elapsed() < 1.0:
                return
            unreal.SystemLibrary.execute_console_command(
                self.server(), "HighResShot 1280x720 filename=neckbulge_A_bulge125")
            self.advance("shot_125_side")
        elif s == "shot_125_side":
            if self.elapsed() < 2.5:
                return
            self.cam_on_nape(side=1.0)
            self.advance("shot_125_side_go")
        elif s == "shot_125_side_go":
            if self.elapsed() < 1.0:
                return
            unreal.SystemLibrary.execute_console_command(
                self.server(), "HighResShot 1280x720 filename=neckbulge_A_side")
            self.advance("neutralize")
        elif s == "neutralize":
            if self.elapsed() < 1.5:
                return
            # A/B：橋參數改中性（server 端實例＝host 視窗渲染的那份）
            server = self.server()
            model = find_char(server, self.model_pid)
            ns = model.get_editor_property("NeckStretch")
            ns.set_editor_property("NeckNapeBulge", 1.0)
            ns.set_editor_property("NeckChinTuck", 1.0)
            # 逼重算：nudge aim 一下（變化偵測吃頭位；直接動參數不觸發重建）
            w2 = get_world("UEDPIE_2")
            m2 = find_char(w2, self.model_pid)
            raw = str(model.call_method("DebugLeanSummary", ()))
            log("neutralized params; nudging aim | " + raw)
            m2.call_method("DebugRoboDrawAim", (float(raw.split("az=")[1].split(" ")[0]) + 2.0,
                                                float(raw.split("tilt=")[1].split(" ")[0])))
            self.advance("shot_100")
        elif s == "shot_100":
            if self.elapsed() < 1.5:
                return
            log("NEUTRAL " + self.neck_summary(find_char(self.server(), self.model_pid)))
            unreal.SystemLibrary.execute_console_command(
                self.server(), "HighResShot 1280x720 filename=neckbulge_B_bulge100")
            self.advance("shot_100_side")
        elif s == "shot_100_side":
            if self.elapsed() < 2.5:
                return
            self.cam_on_nape(side=1.0)
            self.advance("shot_100_side_go")
        elif s == "shot_100_side_go":
            if self.elapsed() < 1.0:
                return
            unreal.SystemLibrary.execute_console_command(
                self.server(), "HighResShot 1280x720 filename=neckbulge_B_side")
            self.advance("done")
        elif s == "done":
            if self.elapsed() < 1.5:
                return
            log("DONE")
            self.finish()

    def finish(self):
        log("HARNESS END")
        if self.handle:
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.handle = None


_p = Probe()
log("harness registered")
