# FOV 軸語義探針（2026-08-01）：量「實際可視半角」vs LeanLockedFov 假設。
# 背景：稿筆邊緣推擠帶用 LeanLockedFov*0.5（假設 FOV=水平軸、16:9）算角度邊界；
# user 抓「筆還沒碰到邊緣就開始移動視野」——若引擎把 FOV 套在另一軸/另一長寬比，
# 邊界會落在畫面內側。方法=deproject 畫面中心/右緣/下緣三條射線，實測夾角。
# 產出：Saved/robo_fovaxis_result.txt
import math
import time
import traceback

import unreal

OUT = "C:/games/Unreal Engine/nice_ink/Saved/robo_fovaxis_result.txt"
import os
os.makedirs(os.path.dirname(OUT), exist_ok=True)
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[FOVAXIS] " + str(msg))
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


def ang_between(a, b):
    d = a.dot(b) / max(a.length() * b.length(), 1e-9)
    return math.degrees(math.acos(max(-1.0, min(1.0, d))))


class Probe:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.victim_pid = None
        self.host_pid = None
        perf = unreal.find_object(None, "/Script/UnrealEd.Default__EditorPerformanceSettings")
        for prop in ("throttle_cpu_when_not_foreground", "bThrottleCPUWhenNotForeground"):
            try:
                perf.set_editor_property(prop, False)
                break
            except Exception:
                pass
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    def advance(self, stage):
        self.stage = stage
        self.stage_t = time.monotonic()

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
            if time.monotonic() - self.t0 > 10.0:
                sub = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
                if sub:
                    sub.editor_request_begin_play()
                    self.advance("wait_pie")
        elif s == "wait_pie":
            server = get_world("UEDPIE_0")
            if server and unreal.GameplayStatics.get_game_mode(server):
                gm = unreal.GameplayStatics.get_game_mode(server)
                try:
                    gm.set_editor_property("DebugForcedVictimSeat", 1)
                except Exception:
                    return
                self.advance("wait_drawing")
        elif s == "wait_drawing":
            server = get_world("UEDPIE_0")
            gs = unreal.GameplayStatics.get_game_state(server) if server else None
            if gs and str(gs.get_editor_property("CurrentPhase")).endswith("DRAWING: 3>"):
                self.victim_pid = gs.get_editor_property("VictimPlayerId")
                self.host_pid = unreal.GameplayStatics.get_player_pawn(server, 0) \
                    .get_editor_property("player_state").get_editor_property("player_id")
                self.advance("lock")
            elif self.elapsed() > 40.0:
                log("FAIL: drawing phase never came")
                self.finish()
        elif s == "lock":
            if self.elapsed() < 1.0:
                return
            server = get_world("UEDPIE_0")
            victim = find_char(server, self.victim_pid)
            host = find_char(server, self.host_pid)
            bt = victim.get_editor_property("Body").get_world_transform()
            p = bt.transform_location(unreal.Vector(0.0, 26.0, 95.0))
            n = bt.transform_direction(unreal.Vector(0.0, 1.0, 0.0))
            ok = host.call_method("DebugRoboEnterLean", (victim, p, n))
            log(f"enter={ok}")
            self.advance("measure")
        elif s == "measure":
            if self.elapsed() < 3.0:
                return  # 眼錨定沉降
            server = get_world("UEDPIE_0")
            pc = unreal.GameplayStatics.get_player_controller(server, 0)
            vp = unreal.WidgetLayoutLibrary.get_viewport_size(server)
            scale = unreal.WidgetLayoutLibrary.get_viewport_scale(server)
            # get_viewport_size 回 UMG 座標；deproject 吃像素——乘 scale 還原像素
            w = vp.x * scale
            h = vp.y * scale
            cx, cy = w * 0.5, h * 0.5
            rays = {}
            for name, sx, sy in (("center", cx, cy), ("right", w - 1.0, cy),
                                 ("bottom", cx, h - 1.0), ("left", 1.0, cy),
                                 ("top", cx, 1.0)):
                r = unreal.GameplayStatics.deproject_screen_to_world(
                    pc, unreal.Vector2D(sx, sy))
                # 回傳 (bool, world_pos, world_dir) 或 (world_pos, world_dir) 依版本
                if isinstance(r, tuple) and len(r) == 3:
                    rays[name] = r[2]
                else:
                    rays[name] = r[1]
            half_h_r = ang_between(rays["center"], rays["right"])
            half_h_l = ang_between(rays["center"], rays["left"])
            half_v_b = ang_between(rays["center"], rays["bottom"])
            half_v_t = ang_between(rays["center"], rays["top"])
            fov = find_char(server, self.host_pid).get_editor_property("LeanLockedFov")
            log(f"viewport={w:.0f}x{h:.0f} (umg {vp.x:.0f}x{vp.y:.0f} scale {scale:.2f})")
            log(f"LeanLockedFov={fov} → 程式假設 halfH={fov*0.5:.2f} "
                f"halfV={math.degrees(math.atan(math.tan(math.radians(fov*0.5))*9.0/16.0)):.2f}")
            log(f"實測 halfH R/L={half_h_r:.2f}/{half_h_l:.2f} "
                f"halfV B/T={half_v_b:.2f}/{half_v_t:.2f}")
            log("DONE")
            self.finish()

    def finish(self):
        log("HARNESS END")
        if self.handle:
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.handle = None


_p = Probe()
log("harness registered")
