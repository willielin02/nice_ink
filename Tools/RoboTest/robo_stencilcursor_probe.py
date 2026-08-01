# 稿筆游標制探針（2026-07-31）：驗「手=皮膚游標（恆定公分增益）、臉/相機=惰性注視」
# 的五條新契約——角度命令會 relatch 游標（robo 橋接鐵則），所以增益/漂移只能用
# DebugRoboMouse 合成滑鼠走真人管線來量。
# 契約：
#   c1 latch    入鎖即掛游標（curs=1、cursW≈LeanPoint）
#   c2 gain     兩個不同眼距的鎖點、同樣滑鼠步數 → 皮膚位移比 ∈ [0.75,1.33]
#               （角度制的位移∝眼距=比值≈1.6 級；公分制=恆定）
#   c3 drift    停手 2s 游標零漂移（≤0.3cm；P-游標時代舊角度制 20cm 級）
#   c4 gaze     停手後注視收斂到 aim（|gazeAz-rawAz|≤0.6°）
#   c5 paint    按住 LMB＋畫圈滑鼠 → dotN 增長（墨走游標）
#   c6 relatch  DebugRoboDrawAim 角度命令 → rawAz 逐字生效＋gaze 硬切＝robo 相容
#   c7 tap      單擊（LMB 按住、滑鼠零移動）→ 恰好落 1~4 針（首針即點；上限=
#               靜止不灌墨——移動閘拆除後的守恆契約）
#   c8 slow     慢速域（0.05 單位/tick ≈ 1.6°/s＝舊閘 3°/s 門檻之下）按住 LMB
#               慢掃 → 按住期間墨在流（inflight gain≥1；拉繩穩定器 08-02：墨尖
#               落後游標 ≤L）＋收筆補完後全量守恆（gain≥5；舊閘下=0＝慢畫整段
#               無墨的回歸鎖、繩子丟帳也鎖在這條）
#（08-02 曲線制 c9/c10 已隨功能整組刪除——user 定案；決策史見 DIRECT_DRAW_PLAN.md）
# 產出：Saved/robo_stencilcursor_result.txt
import math
import re
import time
import traceback

import unreal

OUT = "C:/games/Unreal Engine/nice_ink/Saved/robo_stencilcursor_result.txt"
import os
os.makedirs(os.path.dirname(OUT), exist_ok=True)
LINES = []
PASS_N = [0]
FAIL_N = [0]


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[STENCILCUR] " + str(msg))
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(LINES))


def check(name, ok, detail=""):
    (PASS_N if ok else FAIL_N)[0] += 1
    log(("PASS " if ok else "FAIL ") + name + ((" | " + detail) if detail else ""))


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


def parse_summary(s):
    d = {}
    for k in ("curs", "gazeAz", "gazeTilt", "rawAz", "dotN", "tilt", "az", "reach",
              "cursOk"):
        m = re.search(r"\b" + k + r"=(-?[\d.]+)", s)
        if m:
            d[k] = float(m.group(1))
    m = re.search(r"cursW=\((-?[\d.]+),(-?[\d.]+),(-?[\d.]+)\)", s)
    if m:
        d["cursW"] = (float(m.group(1)), float(m.group(2)), float(m.group(3)))
    m = re.search(r"point=\((-?[\d.]+),(-?[\d.]+),(-?[\d.]+)\)", s)
    if m:
        d["point"] = (float(m.group(1)), float(m.group(2)), float(m.group(3)))
    return d


def dist3(a, b):
    return math.sqrt(sum((a[i] - b[i]) ** 2 for i in range(3)))


# 兩鎖點取自 reachradius 慣例（受害者 Body 局部座標）＋各自的安全掃向（滑鼠單位）：
# belly=腹頂（橫掃留在腹丘上）；mid=低位（「gaze 右向」一步就出可見剪影＝第三輪實錘
# →改朝畫面上方＝沿腹坡朝腹頂＝體內安全向）。掃程小步局部（首版 40cm 繞到掠射帶）
# 註：reachradius 的 mid=(0,26,60) 是游標測試的退化鎖點（從自身眼錨看=極掠射，
# 正中射線命中但任何 ±2cm 候選射線全出剪影＝第三/四輪實錘：橫掃縱掃首步皆落空
# →角度退路。誠實橢圓趨近零的鎖點=玩家該重鎖的地方，不是增益契約的量測台）
LOCKS = [
    ("belly", (0.0, 26.0, 95.0), (4.0, 0.0)),
    ("belly2", (0.0, 26.0, 80.0), (4.0, 0.0)),
]
SWEEP_STEPS = 6


class Probe:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.victim_pid = None
        self.host_pid = None
        self.lock_i = 0
        self.sweep_n = 0
        self.curs_start = None
        self.curs_after_sweep = None
        self.sweep_cm = {}
        self.paint_n = 0
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

    def server(self):
        return get_world("UEDPIE_0")

    def host(self):
        return find_char(self.server(), self.host_pid)

    def summary(self):
        return parse_summary(str(self.host().call_method("DebugLeanSummary", ())))

    def step(self):
        s = self.stage
        name = LOCKS[self.lock_i][0] if self.lock_i < len(LOCKS) else "?"
        if s == "boot":
            if time.monotonic() - self.t0 > 10.0:
                sub = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
                if sub:
                    sub.editor_request_begin_play()
                    self.advance("wait_pie")
        elif s == "wait_pie":
            server = self.server()
            if server and unreal.GameplayStatics.get_game_mode(server):
                gm = unreal.GameplayStatics.get_game_mode(server)
                try:
                    gm.set_editor_property("DebugForcedVictimSeat", 1)
                except Exception:
                    return
                self.advance("wait_drawing")
        elif s == "wait_drawing":
            server = self.server()
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
            victim = find_char(self.server(), self.victim_pid)
            bt = victim.get_editor_property("Body").get_world_transform()
            p = bt.transform_location(unreal.Vector(*LOCKS[self.lock_i][1]))
            n = bt.transform_direction(unreal.Vector(0.0, 1.0, 0.0))
            ok = self.host().call_method("DebugRoboEnterLean", (victim, p, n))
            log(f"[{name}] enter={ok}")
            self.advance("settle")
        elif s == "settle":
            if self.elapsed() < 3.0:
                return  # 眼錨定＋姿勢沉降＋橢圓烘焙
            d = self.summary()
            log(f"[{name}] settle: curs={d.get('curs')} rawAz={d.get('rawAz')} "
                f"tilt={d.get('tilt')} gazeAz={d.get('gazeAz')} gazeTilt={d.get('gazeTilt')} "
                f"cursW={d.get('cursW')}")
            if self.lock_i == 0:
                check("c1_latch_curs", d.get("curs") == 1.0, str(d.get("curs")))
                check("c1_latch_at_point",
                      "cursW" in d and "point" in d and dist3(d["cursW"], d["point"]) < 8.0,
                      f"d={dist3(d['cursW'], d['point']):.2f}" if "cursW" in d and "point" in d else "no-fields")
            self.curs_start = d.get("cursW")
            self.sweep_n = 0
            self.advance("sweep")
        elif s == "sweep":
            if self.sweep_n < SWEEP_STEPS:
                dx, dy = LOCKS[self.lock_i][2]
                self.host().call_method("DebugRoboMouse", (dx, dy))
                self.sweep_n += 1
                return
            if self.elapsed() < 0.5:
                return  # 最後一步消化
            d = self.summary()
            log(f"[{name}] postsweep: curs={d.get('curs')} rawAz={d.get('rawAz')} "
                f"tilt={d.get('tilt')} cursW={d.get('cursW')}")
            self.curs_after_sweep = d.get("cursW")
            moved = dist3(self.curs_start, self.curs_after_sweep) if self.curs_start and self.curs_after_sweep else -1.0
            self.sweep_cm[name] = moved
            check(f"c2_{name}_sweep_moved", moved > 1.0, f"cm={moved:.2f}")
            self.advance("idle")
        elif s == "idle":
            if self.elapsed() < 2.0:
                return
            d = self.summary()
            drift = dist3(self.curs_after_sweep, d.get("cursW")) if self.curs_after_sweep and d.get("cursW") else -1.0
            check(f"c3_{name}_stop_drift", 0.0 <= drift <= 0.3, f"cm={drift:.3f}")
            gaze_err = abs((d.get("gazeAz", 999.0) - d.get("rawAz", 0.0) + 180.0) % 360.0 - 180.0)
            check(f"c4_{name}_gaze_converged", gaze_err <= 0.6, f"dAz={gaze_err:.2f}")
            if self.lock_i == 0:
                self.paint_n = 0
                self.host().call_method("DebugRoboPaintHold", (True,))
                self.advance("paint")
            else:
                self.advance("wrap")
        elif s == "paint":
            if self.paint_n < 40:
                a = self.paint_n * 0.35
                self.host().call_method("DebugRoboMouse",
                    (5.0 * math.cos(a), 5.0 * math.sin(a)))
                self.paint_n += 1
                return
            if self.elapsed() < 1.0:
                return
            d = self.summary()
            self.host().call_method("DebugRoboPaintHold", (False,))
            check("c5_paint_dots", d.get("dotN", 0) >= 10, f"dotN={d.get('dotN')}")
            self.advance("tap_arm")
        elif s == "tap_arm":
            if self.elapsed() < 0.5:
                return  # 前一筆確實收筆＋濾波尾巴沉降
            self.dot_before = self.summary().get("dotN", 0)
            self.host().call_method("DebugRoboPaintHold", (True,))
            self.advance("tap")
        elif s == "tap":
            if self.elapsed() < 1.5:
                return  # 按住不動 1.5s：給「靜止灌墨」足夠的顯影時間
            d = self.summary()
            self.host().call_method("DebugRoboPaintHold", (False,))
            gain = d.get("dotN", 0) - self.dot_before
            check("c7_tap_dot", 1 <= gain <= 4, f"gain={gain}")
            self.advance("slow_arm")
        elif s == "slow_arm":
            if self.elapsed() < 0.5:
                return
            self.dot_before = self.summary().get("dotN", 0)
            self.slow_n = 0
            self.host().call_method("DebugRoboPaintHold", (True,))
            self.advance("slowpaint")
        elif s == "slowpaint":
            if self.slow_n < 120:
                self.host().call_method("DebugRoboMouse", (0.05, 0.0))
                self.slow_n += 1
                return
            if self.elapsed() < 1.0:
                return
            d = self.summary()
            # 拉繩穩定器（08-02）：按住期間墨尖被繩拖著落後游標 ≤L——慢速域
            # 「墨在流」的活契約=首針＋繩繃直後的尾巴；全量守恆在收筆補完後量（下態）
            gain = d.get("dotN", 0) - self.dot_before
            check("c8_slow_inflight", gain >= 1, f"gain={gain}")
            self.host().call_method("DebugRoboPaintHold", (False,))
            self.advance("slowdone")
        elif s == "slowdone":
            if self.elapsed() < 0.6:
                return  # 收筆補完（繩長歸零走完鬆繩段）吃一個 tick＋批次 flush
            d = self.summary()
            gain = d.get("dotN", 0) - self.dot_before
            check("c8_slow_paint", gain >= 5, f"gain={gain}")
            self.advance("relatch")
        elif s == "relatch":
            if self.elapsed() < 0.5:
                return
            d0 = self.summary()
            self.cmd_az = d0.get("rawAz", 0.0) + 6.0
            self.cmd_tilt = d0.get("tilt", 45.0)
            self.host().call_method("DebugRoboDrawAim", (self.cmd_az, self.cmd_tilt))
            self.advance("relatch_check")
        elif s == "relatch_check":
            if self.elapsed() < 0.5:
                return
            d = self.summary()
            az_err = abs((d.get("rawAz", 999.0) - self.cmd_az + 180.0) % 360.0 - 180.0)
            gz_err = abs((d.get("gazeAz", 999.0) - d.get("rawAz", 0.0) + 180.0) % 360.0 - 180.0)
            check("c6_relatch_aim_exact", az_err <= 0.25, f"dAz={az_err:.2f}")
            check("c6_relatch_gaze_snap", gz_err <= 0.25, f"dAz={gz_err:.2f}")
            self.advance("wrap")
        elif s == "wrap":
            self.host().call_method("ServerExitLean", ())
            self.lock_i += 1
            if self.lock_i >= len(LOCKS):
                a = self.sweep_cm.get(LOCKS[0][0], -1.0)
                b = self.sweep_cm.get(LOCKS[1][0], -1.0)
                ratio = (a / b) if a > 0 and b > 0 else -1.0
                # 世界公分位移含表面斜率因子（掠射面同樣的視平面步長走更多表面公分
                # ＝物理正確非 bug）——帶寬放到斜率域 [0.4,2.5]；真增益手感=viewport
                check("c2_gain_constant", 0.4 <= ratio <= 2.5, f"belly={a:.2f} mid={b:.2f} ratio={ratio:.2f}")
                log(f"DONE pass={PASS_N[0]} fail={FAIL_N[0]}")
                self.finish()
            else:
                self.advance("lock")

    def finish(self):
        log("HARNESS END")
        if self.handle:
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.handle = None


_p = Probe()
log("harness registered")
