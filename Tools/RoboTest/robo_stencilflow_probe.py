# 稿筆出墨連續性探針（2026-08-29；user 回報「時而正常、時而爆墨、時而短暫斷墨」）。
#
# 待證機理：稿筆的落點 P＝**每 tick 從眼睛重打的一條射線**（TraceAimToTarget，
# 無平滑、無連續性約束），而出墨是「沿 TipFrom→TipNow 的**世界直線**每 1.5mm 落一針、
# 每針再用 ±2.5/3cm 的筆軸射線投影回皮膚」。所以：
#   ・皮膚正對視線 ⇒ dP/dθ 正常 ⇒ 等距針 ⇒ 正常線
#   ・掠射／凹凸（肚臍緣、剪影）⇒ dP/dθ 爆炸或跳段 ⇒ 該 tick 的弦很長，
#     弦離開皮膚 ⇒ 針要嘛被壓回同一小片皮膚（爆墨）要嘛投影失敗（斷墨）
#
# 量法＝逐 tick 記錄 aim 與 P（DebugLeanSummary 的 point=），算 |ΔP|/tick；
# 三段同角速度掃描（平面／垂直／大幅掠射）各自 dump 麥克筆層，離線量線寬剖面，
# 把「墨的粗細與斷點」對上「那一 tick 的弦長」。
# 產出：Saved/robo_stencilflow_result.txt＋robo_stencilflow.csv＋robo_sf{1,2,3}_marker.png
import math
import re
import time
import traceback

import unreal

OUT = "C:/games/Unreal Engine/nice_ink/Saved/robo_stencilflow_result.txt"
CSV = "C:/games/Unreal Engine/nice_ink/Saved/robo_stencilflow.csv"
DUMP = "C:/games/Unreal Engine/nice_ink/Saved/robo_sf"
LINES = []
ROWS = ["sweep,t,az,tilt,px,py,pz,dotN,dotGapCm,reach,curs,cursOk,locked"]


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[SFLOW] " + str(msg))
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


def summary(char):
    s = str(char.call_method("DebugLeanSummary", ()))
    d = {}
    for k, v in re.findall(r"(\w+)=([-\d.]+)", s):
        try:
            d[k] = float(v)
        except ValueError:
            pass
    # point= 是鎖點錨（恆定），**不是** P；游標世界座標 cursW 才是每 tick 的落點
    m = re.search(r"cursW=\(([-\d.]+),([-\d.]+),([-\d.]+)\)", s)
    p = tuple(float(x) for x in m.groups()) if m else (0.0, 0.0, 0.0)
    return s, d, p


# 乳頭／乳暈＝網格上的獨立連通元件（170／266 頂點），Blender 局部座標
# 乳暈中心 (0.298, -0.284, 1.125) m ⇒ UE Body-local（y 翻轉）(29.8, 28.4, 112.5) cm。
# 給略內縮的錨點，真正的皮膚點由 DebugRoboEnterLean 的 trace 決定。
BELLY = unreal.Vector(29.8, 24.0, 112.5)
BELLY_N = unreal.Vector(0.0, 1.0, 0.0)

# (名稱, 掃描軸, 幅度度數, 秒數, 起始 tilt 偏移)
# 同一條路徑跑三種手速：慢／中／快甩——「弦長＝手速×幀時」是待證機理的自變數
# 手速三個量級＋「幾乎不動」——測「眼→P→姿勢→眼」自我參照迴圈：
# aim 幾乎不動時 P 還會不會自己走（走了就是畫面上沒人要的墨）
# 橫掃／縱掃乳頭，兩種手速；外加一段離開乳頭的同速對照
SWEEPS = [
    ("nip_h_slow", "az", 16.0, 3.2, 0.0),    # 5 deg/s 橫過乳頭
    ("nip_h_fast", "az", 16.0, 0.8, 0.0),    # 20 deg/s 同一條路徑
    ("nip_v_slow", "tilt", 14.0, 2.8, 0.0),  # 5 deg/s 縱過乳頭
    ("off_ctrl", "az", 16.0, 3.2, -9.0),     # 對照：同速度、偏離乳頭
]


class Probe:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.sweep_i = 0
        perf = unreal.find_object(None, "/Script/UnrealEd.Default__EditorPerformanceSettings")
        for prop in ("throttle_cpu_when_not_foreground", "bThrottleCPUWhenNotForeground"):
            try:
                perf.set_editor_property(prop, False)
                break
            except Exception:
                pass
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    def advance(self, s):
        self.stage = s
        self.stage_t = time.monotonic()
        log("STAGE -> " + s)

    def elapsed(self):
        return time.monotonic() - self.stage_t

    def server(self):
        return get_world("UEDPIE_0")

    def host(self):
        return find_char(self.server(), self.host_pid)

    def tick(self, dt):
        try:
            self.step()
        except Exception:
            log("EXC:\n" + traceback.format_exc())
            self.finish()

    def aim_at(self, axis, amp, dtilt, f):
        """掃描路徑的唯一出處：起手（f=0）與逐 tick 都走這一條。"""
        a = self.az0 + (amp * (f - 0.5) if axis == "az" else 0.0)
        ti = self.tilt0 + dtilt + (amp * (f - 0.5) if axis == "tilt" else 0.0)
        return a, ti

    def sample(self, name, t):
        raw, d, p = summary(self.host())
        ROWS.append("%s,%.4f,%.3f,%.3f,%.3f,%.3f,%.3f,%.0f,%.4f,%.0f,%.0f,%.0f,%.0f" % (
            name, t, d.get("az", 0), d.get("tilt", 0), p[0], p[1], p[2],
            d.get("dotN", 0), d.get("dotGapCm", -1), d.get("reach", -1),
            d.get("curs", -1), d.get("cursOk", -1), d.get("locked", -1)))

    def step(self):
        s = self.stage
        if s == "boot":
            if time.monotonic() - self.t0 > 8.0:
                unreal.get_editor_subsystem(
                    unreal.LevelEditorSubsystem).editor_request_begin_play()
                self.advance("wait_pie")
        elif s == "wait_pie":
            server = self.server()
            if server and unreal.GameplayStatics.get_game_mode(server):
                unreal.GameplayStatics.get_game_mode(server).set_editor_property(
                    "DebugForcedVictimSeat", 1)
                self.advance("wait_drawing")
        elif s == "wait_drawing":
            server = self.server()
            gs = unreal.GameplayStatics.get_game_state(server) if server else None
            if gs and str(gs.get_editor_property("CurrentPhase")).endswith("DRAWING: 3>"):
                self.victim_pid = gs.get_editor_property("VictimPlayerId")
                self.host_pid = unreal.GameplayStatics.get_player_pawn(
                    server, 0).get_editor_property("player_state").get_editor_property("player_id")
                log("victim=%s host=%s" % (self.victim_pid, self.host_pid))
                self.advance("lock")
        elif s == "lock":
            if self.elapsed() < 0.8:
                return
            victim = find_char(self.server(), self.victim_pid)
            bt = victim.get_editor_property("Body").get_world_transform()
            p = bt.transform_location(BELLY)
            n = bt.transform_direction(BELLY_N)
            log("EnterLean -> %s" % self.host().call_method("DebugRoboEnterLean", (victim, p, n)))
            self.host().call_method("DebugRoboNeedle", (2,))  # Stencil
            self.advance("settle")
        elif s == "settle":
            if self.elapsed() < 1.5:
                return
            raw, d, p = summary(self.host())
            log("LOCKED | " + raw)
            self.az0 = d.get("az", 0.0)
            self.tilt0 = d.get("tilt", 45.0)
            log("az0=%.2f tilt0=%.2f  needleSel=%s" % (self.az0, self.tilt0, d.get("needleSel")))
            self.advance("sweep_prep")
        elif s == "sweep_prep":
            name, axis, amp, secs, dtilt = SWEEPS[self.sweep_i]
            if self.elapsed() < 0.5:
                return
            h = self.host()
            h.call_method("DebugRoboPaintHold", (False,))
            # 起手 aim 必須與 sweep_run 的 f=0 **完全同式**——首版兩處各寫一份，
            # tilt 掃描時起手少減了 az 的 amp/2 ⇒ 開筆首 tick aim 跳 7°＝我自己
            # 造出來的 6cm 爆墨（差點誤報成遊戲的 bug）。單一出處＝aim_at()
            a, t = self.aim_at(axis, amp, dtilt, 0.0)
            h.call_method("DebugRoboDrawAim", (a, t))
            self.advance("sweep_arm")
        elif s == "sweep_arm":
            if self.elapsed() < 0.6:
                return
            raw, d, p = summary(self.host())
            self.dot0 = d.get("dotN", 0.0)
            self.host().call_method("DebugRoboPaintHold", (True,))
            self.advance("sweep_run")
        elif s == "sweep_run":
            name, axis, amp, secs, dtilt = SWEEPS[self.sweep_i]
            t = self.elapsed()
            h = self.host()
            if t < secs:
                f = t / secs
                a, ti = self.aim_at(axis, amp, dtilt, f)
                h.call_method("DebugRoboDrawAim", (a, ti))
                self.sample(name, t)
                return
            h.call_method("DebugRoboPaintHold", (False,))
            raw, d, p = summary(h)
            log("SWEEP %s done | dots=%.0f | %s" % (name, d.get("dotN", 0) - self.dot0, raw))
            self.advance("sweep_dump")
        elif s == "sweep_dump":
            if self.elapsed() < 0.5:
                return
            victim = find_char(self.server(), self.victim_pid)
            ok = victim.get_editor_property("InkCanvas").call_method(
                "ExportLayersToPng", ("%s%d" % (DUMP, self.sweep_i + 1),))
            log("dump %d -> %s" % (self.sweep_i + 1, ok))
            self.sweep_i += 1
            self.advance("sweep_prep" if self.sweep_i < len(SWEEPS) else "done")
        elif s == "done":
            with open(CSV, "w", encoding="utf-8") as f:
                f.write("\n".join(ROWS))
            log("CSV rows=%d -> %s" % (len(ROWS) - 1, CSV))
            log("DONE")
            self.finish()

    def finish(self):
        try:
            unreal.unregister_slate_post_tick_callback(self.handle)
        except Exception:
            pass
        with open(CSV, "w", encoding="utf-8") as f:
            f.write("\n".join(ROWS))
        LINES.append("HARNESS END")
        with open(OUT, "w", encoding="utf-8") as f:
            f.write("\n".join(LINES))


Probe()
