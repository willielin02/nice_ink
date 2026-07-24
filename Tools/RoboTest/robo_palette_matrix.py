# -*- coding: utf-8 -*-
# 色盤提案引擎內驗證（2026-07-24）：10 提案色 × 雙膚色截圖矩陣。
# 方法：①真實玩家管線（鎖肚皮+DebugRoboDrawAim 掃帶）在 10 個 tilt 槽各掃一趟
#   =收集 10 組「保證合法」的皮膚 UV 點列（不猜 UV=不踩島縫）；
# ②WashAllMarker 洗掉收集趟；③每組 UV 用提案色直接 InkCanvas 重放 ×3 趟
#   （≈91% 近實墨=誠實的對比評估濃度）；
# ④victim Body MID 的 SkinTone 設 MST-2（淺）截圖 → 設 MST-9（深）截圖
#   ——同一批墨換膚色重拍=控制變因。
# 產出：Saved/robo_palette_result.txt + Screenshots/palette_light/dark.png
import ctypes
import json
import time
import traceback

import unreal

OUT = "C:/games/Unreal Engine/nice_ink/Saved/robo_palette_result.txt"
PROPOSAL = "C:/games/Unreal Engine/nice_ink/Saved/palette_proposal.json"
import os
os.makedirs(os.path.dirname(OUT), exist_ok=True)
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[PALETTE] " + str(msg))
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
    handles = []

    def enum_cb(h, _):
        buf = ctypes.create_unicode_buffer(256)
        user32.GetWindowTextW(h, buf, 256)
        if "NiceInk" in buf.value or "Unreal Editor" in buf.value:
            handles.append(h)
        return True

    WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    user32.EnumWindows(WNDENUMPROC(enum_cb), None)
    if handles:
        user32.SetForegroundWindow(handles[0])


BELLY = unreal.Vector(0.0, 26.0, 95.0)
BELLY_N = unreal.Vector(0.0, 1.0, 0.0)
# MST-2 / MST-9 進引擎 SkinTone 域：raw linear 直塞會過曝（首輪實錘全白）——
# 引擎域錨定=現行預設 SkinTone (0.4,0.22,0.13) 渲染正確 ⇒ MST 線性值統一乘
# k=0.4/0.895（把 MST-2 的峰值縮到預設峰值），保留 MST 自身的色比與階差
SKIN_LIGHT = unreal.LinearColor(0.400, 0.356, 0.316, 1.0)   # MST-2 × 0.447
SKIN_DARK = unreal.LinearColor(0.0197, 0.0143, 0.0107, 1.0)  # MST-9 × 0.447
# 槽距 1.8°（首輪 2.2° 超出 FOV36 垂直視野=頂部帶被裁）
SLOT_TILTS = [-7.2, -5.4, -3.6, -1.8, 0.0, 1.8, 3.6, 5.4, 7.2, 9.0]


class Test:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.victim_pid = None
        self.host_pid = None
        self.slot = 0
        self.colors = []
        with open(PROPOSAL, encoding="utf-8") as f:
            for c in json.load(f)["colors"]:
                self.colors.append((c["name"], unreal.LinearColor(
                    c["linear"][0], c["linear"][1], c["linear"][2], 1.0)))
        log(f"proposal colors loaded: {len(self.colors)}")
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

    def canvas(self):
        return find_char(self.server(), self.victim_pid).get_editor_property("InkCanvas")

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
                focus_main_window()
                self.advance("lock")
        elif s == "lock":
            if self.elapsed() < 1.0:
                return
            host = find_char(self.server(), self.host_pid)
            victim = find_char(self.server(), self.victim_pid)
            bt = victim.get_editor_property("Body").get_world_transform()
            p = bt.transform_location(BELLY)
            n = bt.transform_direction(BELLY_N)
            ok = host.call_method("DebugRoboEnterLean", (victim, p, n))
            log(f"enter lean -> {ok}")
            host.call_method("DebugRoboNeedle", (1,))  # Shader
            self.advance("harvest_aim")
        elif s == "harvest_aim":
            # 每槽：定位→按住→勻速掃 3.2s（8°/s=25.6°）→放開＝一條合法 UV 帶
            if self.elapsed() < 1.0:
                return
            host = find_char(self.server(), self.host_pid)
            raw = str(host.call_method("DebugLeanSummary", ()))
            if self.slot == 0 and not hasattr(self, "base_az"):
                import re
                m = re.search(r"rawAz=([-\d.]+)", raw)
                t = re.search(r"tilt=([-\d.]+)", raw)
                self.base_az = float(m.group(1)) - 13.0
                self.base_tilt = float(t.group(1))
                log(f"base az={self.base_az:.1f} tilt={self.base_tilt:.1f}")
            host.call_method("DebugRoboPaintHold", (False,))
            host.call_method("DebugRoboDrawAim",
                             (self.base_az, self.base_tilt + SLOT_TILTS[self.slot]))
            self.advance("harvest_sweep")
        elif s == "harvest_sweep":
            t = self.elapsed()
            host = find_char(self.server(), self.host_pid)
            if t < 0.7:
                return
            if not getattr(self, "hold_on", False):
                self.hold_on = True
                host.call_method("DebugRoboPaintHold", (True,))
                return
            if t < 3.9:
                host.call_method("DebugRoboDrawAim",
                                 (self.base_az + (t - 0.7) * 8.0,
                                  self.base_tilt + SLOT_TILTS[self.slot]))
                return
            host.call_method("DebugRoboPaintHold", (False,))
            self.hold_on = False
            self.slot += 1
            if self.slot < len(SLOT_TILTS):
                self.advance("harvest_aim")
            else:
                self.advance("harvest_collect")
        elif s == "harvest_collect":
            if self.elapsed() < 1.0:
                return
            works = self.canvas().get_works()
            strokes = []
            for w in works:
                for st in w.get_editor_property("Strokes"):
                    pts = list(st.get_editor_property("Points"))
                    if len(pts) >= 8:
                        strokes.append(pts)
            log(f"harvested strokes: {len(strokes)} "
                f"(pts: {[len(p) for p in strokes]})")
            if len(strokes) < len(self.colors):
                log(f"FAIL not enough valid strokes ({len(strokes)}/{len(self.colors)})")
                self.finish()
                return
            self.stroke_pts = strokes[:len(self.colors)]
            self.canvas().wash_all_marker()
            self.advance("replay")
        elif s == "replay":
            if self.elapsed() < 0.5:
                return
            cv = self.canvas()
            for i, (name, col) in enumerate(self.colors):
                pts = self.stroke_pts[i]
                for rep in range(3):  # 3 趟 ≈91% 近實墨
                    author = 9000 + i * 10 + rep
                    cv.begin_stroke(author, col, pts[0], True, unreal.InkNeedle.SHADER, 255)
                    for p in pts[1:]:
                        cv.add_stroke_point(author, p, 255)
                    cv.end_stroke(author)
                log(f"replayed {name} on slot {i} ({len(pts)} pts x3)")
            self.advance("skin_light")
        elif s == "skin_light":
            if self.elapsed() < 1.0:
                return
            # 取景回正（末槽收工時 aim 停在邊緣=「out of reach」入鏡）
            host = find_char(self.server(), self.host_pid)
            host.call_method("DebugRoboDrawAim", (self.base_az + 13.0, self.base_tilt + 1.0))
            victim = find_char(self.server(), self.victim_pid)
            mid = victim.get_editor_property("Body").get_material(0)
            mid.set_vector_parameter_value("SkinTone", SKIN_LIGHT)
            focus_main_window()
            self.advance("shot_light")
        elif s == "shot_light":
            if self.elapsed() < 1.2:
                return
            unreal.SystemLibrary.execute_console_command(
                self.server(), "HighResShot 1600x900 filename=palette_light")
            self.advance("skin_dark")
        elif s == "skin_dark":
            if self.elapsed() < 1.5:
                return
            victim = find_char(self.server(), self.victim_pid)
            mid = victim.get_editor_property("Body").get_material(0)
            mid.set_vector_parameter_value("SkinTone", SKIN_DARK)
            self.advance("shot_dark")
        elif s == "shot_dark":
            if self.elapsed() < 1.2:
                return
            unreal.SystemLibrary.execute_console_command(
                self.server(), "HighResShot 1600x900 filename=palette_dark")
            self.advance("wrap")
        elif s == "wrap":
            if self.elapsed() < 1.5:
                return
            log("DONE (PIE left running)")
            self.finish()

    def finish(self):
        try:
            unreal.unregister_slate_post_tick_callback(self.handle)
        except Exception:
            pass
        log("HARNESS END")


Test()
