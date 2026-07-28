# 作畫姿勢脖子探針（2026-07-26）：user viewport 抓「lean-lock 第三人稱後頸異常凸起＋
# 喉嚨異常褶皺」——本探針把模特兒鎖到三個代表點（肚頂=深彎/臉=高位/側腹=側彎），
# 逐點拍後頸側與喉側特寫＋傾印脖管幾何 CSV（DumpNeckMesh）＋骨骼/弦長現場數字。
# 產出：Saved/robo_neckdraw_result.txt + Saved/neckdraw_<pt>.csv
#      + Saved/Screenshots/WindowsEditor/neckdraw_<pt>_{nape,throat}.png
import ctypes
import math
import time
import traceback

import unreal

OUT = "C:/games/Unreal Engine/nice_ink/Saved/robo_neckdraw_result.txt"
CSV_DIR = "C:/games/Unreal Engine/nice_ink/Saved"
import os
os.makedirs(os.path.dirname(OUT), exist_ok=True)
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[NECKDRAW] " + str(msg))
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
    return bow.get_bone_transform_by_name(name, unreal.BoneSpaces.WORLD_SPACE)


# 鎖定點（受害者本地：腳底原點、臉朝 +Y）＝深彎/高位/側彎三代表
POINTS = [
    ("belly", unreal.Vector(0.0, 26.0, 95.0), unreal.Vector(0.0, 1.0, 0.0)),
    ("face",  unreal.Vector(0.0, 16.0, 150.0), unreal.Vector(0.0, 1.0, 0.0)),
    ("flank", unreal.Vector(30.0, 8.0, 95.0), unreal.Vector(1.0, 0.0, 0.0)),
]


class Probe:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.victim_pid = None
        self.host_pid = None
        self.model_pid = None
        self.pt_i = 0
        self.shot_i = 0
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

    def server(self):
        return get_world("UEDPIE_0")

    def host_char(self):
        return unreal.GameplayStatics.get_player_pawn(self.server(), 0)

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
                # 攝影師閃開＋藏身
                host = self.host_char()
                hl = host.get_actor_location()
                host.set_actor_location(unreal.Vector(hl.x + 350.0, hl.y + 350.0, hl.z), False, True)
                for prop in ("Body", "BowBody"):
                    comp = host.get_editor_property(prop)
                    if comp:
                        comp.set_visibility(False, True)
                focus_main_window()
                self.advance("lock")
            elif self.elapsed() > 40.0:
                log("FAIL: drawing phase never came")
                self.finish()
        elif s == "lock":
            if self.elapsed() < 0.8:
                return
            name, lp, ln = POINTS[self.pt_i]
            victim = find_char(self.server(), self.victim_pid)
            bt = victim.get_editor_property("Body").get_world_transform()
            p = bt.transform_location(lp)
            n = bt.transform_direction(ln)
            model = find_char(self.server(), self.model_pid)
            ok = model.call_method("DebugRoboEnterLean", (victim, p, n))
            log(f"[{name}] DebugRoboEnterLean -> {ok}")
            self.advance("probe")
        elif s == "probe":
            if self.elapsed() < 2.5:
                return  # 等姿勢收斂＋複製沉降
            name = POINTS[self.pt_i][0]
            model = find_char(self.server(), self.model_pid)
            # --- 數字探針 ---
            neck = model.get_editor_property("NeckStretch")
            log(f"[{name}] neck: " + str(neck.call_method("GetDebugSummary", ())))
            ht = bone_w(model, "Head")
            nt = bone_w(model, "Neck")
            hr = ht.rotation.rotator()
            log(f"[{name}] HeadW=({ht.translation.x:.1f},{ht.translation.y:.1f},{ht.translation.z:.1f}) "
                f"rot=({hr.pitch:.1f},{hr.yaw:.1f},{hr.roll:.1f}) "
                f"NeckW=({nt.translation.x:.1f},{nt.translation.y:.1f},{nt.translation.z:.1f})")
            csv = f"{CSV_DIR}/neckdraw_{name}.csv"
            ok = neck.call_method("DumpNeckMesh", (csv,))
            log(f"[{name}] DumpNeckMesh -> {ok} ({csv})")
            self.shot_i = 0
            self.advance("shots")
        elif s == "shots":
            if self.elapsed() < 1.2 + self.shot_i * 3.0:
                return
            name = POINTS[self.pt_i][0]
            model = find_char(self.server(), self.model_pid)
            ht = bone_w(model, "Head")
            hloc = ht.translation
            hips = bone_w(model, "Hips").translation
            # 後頸側=從髖後上方越過背拍頭；喉側=從頭前下方仰拍——兩個病灶各一機位
            back = unreal.Vector(hips.x - hloc.x, hips.y - hloc.y, 0.0)
            L = max((back.x ** 2 + back.y ** 2) ** 0.5, 1.0)
            back = unreal.Vector(back.x / L, back.y / L, 0.0)
            if self.shot_i == 0:
                cam = unreal.Vector(hloc.x + back.x * 130.0, hloc.y + back.y * 130.0, hloc.z + 95.0)
                tag = "nape"
            elif self.shot_i == 1:
                cam = unreal.Vector(hloc.x - back.x * 110.0 - back.y * 60.0,
                                    hloc.y - back.y * 110.0 + back.x * 60.0, hloc.z - 15.0)
                tag = "throat"
            else:
                # 側面機位（垂直於背向）：張開/互穿過渡帶的摺痕在這裡最可見
                cam = unreal.Vector(hloc.x - back.y * 120.0, hloc.y + back.x * 120.0, hloc.z + 35.0)
                tag = "side"
            host = self.host_char()
            pc = unreal.GameplayStatics.get_player_controller(self.server(), 0)
            host.set_actor_location(cam, False, True)
            look = unreal.MathLibrary.find_look_at_rotation(
                unreal.Vector(cam.x, cam.y, cam.z + 62.0),
                unreal.Vector(hloc.x, hloc.y, hloc.z + 5.0))
            pc.set_control_rotation(look)
            unreal.SystemLibrary.execute_console_command(
                self.server(), f"HighResShot 1280x720 filename=neckdraw_{name}_{tag}")
            self.shot_i += 1
            if self.shot_i >= 3:
                self.advance("next")
        elif s == "next":
            if self.elapsed() < 2.0:
                return
            model = find_char(self.server(), self.model_pid)
            self.pt_i += 1
            if self.pt_i < len(POINTS):
                model.call_method("ServerExitLean", ())
                self.advance("lock")
            else:
                log("DONE (PIE left running)")
                self.finish()

    def finish(self):
        log("HARNESS END")
        if self.handle:
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.handle = None


_p = Probe()
log("harness registered")
