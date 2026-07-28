# 旁觀端作畫抖動/筆墨對齊時間序列探針（2026-07-26）：user viewport 抓「第三人稱全身
# 抖動（位置來回閃爍）＋筆尖延長不及時/不跟出墨」。本探針讓 client 作畫者（m2）用真實
# 管線畫（DebugRoboDrawAim 掃動＋DebugRoboPaintHold），在 server 世界（=旁觀端）逐 tick
# 錄 DebugLeanSummary——量化：hips/head 逐 tick 位移、solveYaw/hipDeg 跳變、針長切換、
# reach 翻轉、trig 延遲。同步錄 owner 端（m2）作對照＝「抖動是旁觀端特有還是本體就有」。
# 產出：Saved/robo_remotejitter_result.txt + Saved/remotejitter_{remote,owner}.csv
import ctypes
import math
import re
import time
import traceback

import unreal

OUT = "C:/games/Unreal Engine/nice_ink/Saved/robo_remotejitter_result.txt"
CSV_R = "C:/games/Unreal Engine/nice_ink/Saved/remotejitter_remote.csv"
CSV_O = "C:/games/Unreal Engine/nice_ink/Saved/remotejitter_owner.csv"
import os
os.makedirs(os.path.dirname(OUT), exist_ok=True)
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[RJIT] " + str(msg))
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


NUM = r"(-?[\d.]+)"
PAT = re.compile(
    r"az=" + NUM + r" tilt=" + NUM + r".*?hips=\(" + NUM + "," + NUM + "," + NUM +
    r"\).*?solveYaw=" + NUM + r" hipDeg=" + NUM + r" ankleDeg=" + NUM +
    r".*?reach=(\d).*?needle=" + NUM + r" trig=(\d) nSolve=" + NUM)


def parse(s):
    m = PAT.search(s)
    if not m:
        return None
    g = [float(x) for x in m.groups()]
    return {"az": g[0], "tilt": g[1], "hips": (g[2], g[3], g[4]),
            "yaw": g[5], "hip": g[6], "ankle": g[7], "reach": int(g[8]),
            "needle": g[9], "trig": int(g[10]), "nsolve": g[11]}


class Probe:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.victim_pid = None
        self.host_pid = None
        self.model_pid = None
        self.rows_r = []
        self.rows_o = []
        self.base_az = None
        self.base_tilt = None
        perf = unreal.find_object(None, "/Script/UnrealEd.Default__EditorPerformanceSettings")
        for prop in ("throttle_cpu_when_not_foreground", "bThrottleCPUWhenNotForeground"):
            try:
                perf.set_editor_property(prop, False)
                log("editor bg-throttle disabled via " + prop)
                break
            except Exception:
                pass
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    def advance(self, stage):
        self.stage = stage
        self.stage_t = time.monotonic()
        log("STAGE -> " + stage)

    def elapsed(self):
        return time.monotonic() - self.stage_t

    def tick(self, dt):
        try:
            self.step(dt)
        except Exception:
            log("EXC:\n" + traceback.format_exc())
            self.finish()

    def server(self):
        return get_world("UEDPIE_0")

    def step(self, dt):
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
                host = unreal.GameplayStatics.get_player_pawn(server, 0)
                self.host_pid = host.get_editor_property("player_state").get_editor_property("player_id")
                for c in unreal.GameplayStatics.get_all_actors_of_class(server, unreal.NiceInkCharacter):
                    pid = c.get_editor_property("player_state").get_editor_property("player_id")
                    if pid != self.victim_pid and pid != self.host_pid:
                        self.model_pid = pid
                        break
                log(f"victim={self.victim_pid} host={self.host_pid} model={self.model_pid}")
                self.advance("lock")
            elif self.elapsed() > 40.0:
                log("FAIL: drawing phase never came")
                self.finish()
        elif s == "lock":
            if self.elapsed() < 0.8:
                return
            victim = find_char(self.server(), self.victim_pid)
            bt = victim.get_editor_property("Body").get_world_transform()
            p = bt.transform_location(unreal.Vector(0.0, 26.0, 95.0))
            n = bt.transform_direction(unreal.Vector(0.0, 1.0, 0.0))
            ms = find_char(self.server(), self.model_pid)
            ok = ms.call_method("DebugRoboEnterLean", (victim, p, n))
            log(f"enter lean -> {ok}")
            self.advance("tool")
        elif s == "tool":
            if self.elapsed() < 1.5:
                return
            m2 = find_char(get_world("UEDPIE_2"), self.model_pid)
            # 首鎖視野契約（07-27 user 抓「第一次右鍵超近超小視野」）：這是本 session
            # 的第一次入鎖——owner 眼錨到鎖點距離必須在正常帶（病態=2~3cm 貼膚）。
            cam = m2.get_editor_property("FirstPersonCamera").get_world_location()
            lp = m2.get_editor_property("LeanPoint")
            d_cam = ((cam.x - lp.x) ** 2 + (cam.y - lp.y) ** 2 + (cam.z - lp.z) ** 2) ** 0.5
            # 承重下界=不得貼膚（bug=眼錨定格在皮膚 2~3cm）；上界寬鬆——直接畫制
            # 剛臂+伸縮針下 60cm 級眼錨=合法幾何（directdraw 遠點伸針同域）
            ok = 5.0 <= d_cam <= 90.0
            log(("PASS " if ok else "FAIL ") +
                f"first-lock camera distance sane | dCamToPoint={d_cam:.1f}cm (expect 5~90)")
            m2.call_method("DebugRoboNeedle", (0,))  # Liner=刺青機（伸縮針受測）
            self.advance("baseline")
        elif s == "baseline":
            if self.elapsed() < 1.0:
                return
            # 開筆前先讀 owner 端當前 aim 當掃動中心
            m2 = find_char(get_world("UEDPIE_2"), self.model_pid)
            d = parse(str(m2.call_method("DebugLeanSummary", ())))
            if not d:
                log("FAIL: owner summary unparsable")
                self.finish()
                return
            self.base_az = d["az"]
            self.base_tilt = d["tilt"]
            log(f"sweep center az={self.base_az:.1f} tilt={self.base_tilt:.1f}")
            m2.call_method("DebugRoboPaintHold", (True,))
            self.advance("record")
        elif s == "record":
            t = self.elapsed()
            m2 = find_char(get_world("UEDPIE_2"), self.model_pid)
            ms = find_char(self.server(), self.model_pid)
            # 真人手速量級的緩慢畫圈（±5°、~0.4Hz）——巡航/針解/姿勢全鏈活動
            az = self.base_az + 5.0 * math.sin(t * 2.5)
            tilt = self.base_tilt + 3.5 * math.sin(t * 1.7 + 1.3)
            m2.call_method("DebugRoboDrawAim", (az, tilt))
            # 機器針作畫中段截圖（07-28 二修「第三人稱筆與筆跡脫鉤」自查：
            # 針尖應恆釘在墨的落點上）——host=旁觀者（非 victim 非 model）挪去當機位
            if 3.9 <= t < 4.1 and not getattr(self, "shot_liner", False):
                self.shot_liner = True
                host = unreal.GameplayStatics.get_player_pawn(self.server(), 0)
                pc = unreal.GameplayStatics.get_player_controller(self.server(), 0)
                hand = ms.get_editor_property("LeanPoint")  # 作畫域中心（P 在其附近）
                mloc = ms.get_actor_location()
                cam = unreal.Vector(hand.x + (mloc.x - hand.x) * 0.3 + 70.0,
                                    hand.y + (mloc.y - hand.y) * 0.3 - 90.0, hand.z + 55.0)
                host.set_actor_location(cam, False, True)
                look = unreal.MathLibrary.find_look_at_rotation(
                    unreal.Vector(cam.x, cam.y, cam.z + 62.0),
                    unreal.Vector(hand.x, hand.y, hand.z))
                pc.set_control_rotation(look)
                unreal.SystemLibrary.execute_console_command(
                    self.server(), "HighResShot 1280x720 filename=jitter_liner_needle")
            dr = parse(str(ms.call_method("DebugLeanSummary", ())))
            do = parse(str(m2.call_method("DebugLeanSummary", ())))
            if dr:
                dr["t"] = t
                self.rows_r.append(dr)
            if do:
                do["t"] = t
                self.rows_o.append(do)
            if t >= 8.0:
                m2.call_method("DebugRoboPaintHold", (False,))
                self.advance("analyze")
        elif s == "analyze":
            if self.elapsed() < 1.0:
                return
            for name, rows, path in (("remote", self.rows_r, CSV_R), ("owner", self.rows_o, CSV_O)):
                with open(path, "w", encoding="utf-8") as f:
                    f.write("t,az,tilt,hx,hy,hz,yaw,hip,ankle,reach,needle,trig,nsolve\n")
                    for r in rows:
                        f.write(f"{r['t']:.3f},{r['az']:.2f},{r['tilt']:.2f},"
                                f"{r['hips'][0]:.2f},{r['hips'][1]:.2f},{r['hips'][2]:.2f},"
                                f"{r['yaw']:.2f},{r['hip']:.2f},{r['ankle']:.2f},"
                                f"{r['reach']},{r['needle']:.2f},{r['trig']},{r['nsolve']:.1f}\n")
                self.report(name, rows)
            # 稿筆握持檢查（07-27 user 抓「筆沒握在手心」）：切 Stencil 開畫，
            # 旁觀端量「手骨→筆尾」距離（兩點構造契約：筆尾恆錨掌心）
            m2 = find_char(get_world("UEDPIE_2"), self.model_pid)
            m2.call_method("DebugRoboNeedle", (2,))
            m2.call_method("DebugRoboPaintHold", (True,))
            self.advance("stencil_check")
        elif s == "stencil_check":
            t = self.elapsed()
            if t < 2.0:
                m2 = find_char(get_world("UEDPIE_2"), self.model_pid)
                az = self.base_az + 4.0 * math.sin(t * 2.0)
                m2.call_method("DebugRoboDrawAim", (az, self.base_tilt))
                return
            ms = find_char(self.server(), self.model_pid)
            bow = ms.get_editor_property("BowBody")
            # 骨軸量測（07-27 握持方向修）：RightHandProp/RightHand 三軸世界向＋前臂向
            # ——選「模型定義的持物軸」用，不猜
            fa = bow.get_bone_transform_by_name("RightForeArm", unreal.BoneSpaces.WORLD_SPACE).translation
            for bn in ("RightHandProp", "RightHand"):
                bt = bow.get_bone_transform_by_name(bn, unreal.BoneSpaces.WORLD_SPACE)
                loc = bt.translation
                for axn, axv in (("X", unreal.Vector(1, 0, 0)), ("Y", unreal.Vector(0, 1, 0)), ("Z", unreal.Vector(0, 0, 1))):
                    w = bt.transform_direction(axv)
                    log(f"[axis] {bn}.{axn}=({w.x:.2f},{w.y:.2f},{w.z:.2f})")
                fd = unreal.Vector(loc.x - fa.x, loc.y - fa.y, loc.z - fa.z)
                L = max((fd.x ** 2 + fd.y ** 2 + fd.z ** 2) ** 0.5, 1e-3)
                log(f"[axis] forearm->{bn}=({fd.x / L:.2f},{fd.y / L:.2f},{fd.z / L:.2f}) at=({loc.x:.1f},{loc.y:.1f},{loc.z:.1f})")
            hand = bow.get_bone_transform_by_name("RightHandProp", unreal.BoneSpaces.WORLD_SPACE).translation
            # MarkerPen 屬性 protected——用元件迭代找 SM_Marker（可見那支）
            pen = None
            for c in ms.get_components_by_class(unreal.StaticMeshComponent):
                sm = c.get_editor_property("static_mesh")
                if sm and "Marker" in sm.get_name() and c.is_visible():
                    pen = c
                    break
            if not pen:
                log("FAIL marker pen component not found/visible")
                self.finish()
                return
            pt = pen.get_world_transform()
            tip = pt.translation
            zax = pt.transform_direction(unreal.Vector(0.0, 0.0, 1.0))
            L = (zax.x ** 2 + zax.y ** 2 + zax.z ** 2) ** 0.5
            zax = unreal.Vector(zax.x / L, zax.y / L, zax.z / L)
            span = pt.scale3d.z * 13.0
            tail = unreal.Vector(tip.x + zax.x * span, tip.y + zax.y * span, tip.z + zax.z * span)
            d_tail = ((hand.x - tail.x) ** 2 + (hand.y - tail.y) ** 2 + (hand.z - tail.z) ** 2) ** 0.5
            # 骨軸制契約（07-27 四調 user 定值 19）：筆尾＝骨原點外露 19cm；筆軸＝HandProp -Y
            ok = 16.0 <= d_tail <= 23.0
            log(("PASS " if ok else "FAIL ") +
                f"marker protrudes past fist | dTail={d_tail:.1f}cm (expect~19) span={span:.1f} "
                f"vis={pen.is_visible()}")
            hpt = bow.get_bone_transform_by_name("RightHandProp", unreal.BoneSpaces.WORLD_SPACE)
            by = hpt.transform_direction(unreal.Vector(0.0, 1.0, 0.0))
            dot = -(zax.x * by.x + zax.y * by.y + zax.z * by.z)  # 筆尾軸=-骨Y
            ok2 = dot > 0.95
            log(("PASS " if ok2 else "FAIL ") + f"marker straight on bone axis | dot={dot:.3f}")
            # 旁觀截圖
            hloc = bow.get_bone_transform_by_name("Head", unreal.BoneSpaces.WORLD_SPACE).translation
            hips = bow.get_bone_transform_by_name("Hips", unreal.BoneSpaces.WORLD_SPACE).translation
            back = unreal.Vector(hips.x - hloc.x, hips.y - hloc.y, 0.0)
            L = max((back.x ** 2 + back.y ** 2) ** 0.5, 1.0)
            host = unreal.GameplayStatics.get_player_pawn(self.server(), 0)
            cam = unreal.Vector(hloc.x - back.y / L * 120.0, hloc.y + back.x / L * 120.0, hloc.z + 40.0)
            host.set_actor_location(cam, False, True)
            pc = unreal.GameplayStatics.get_player_controller(self.server(), 0)
            pc.set_control_rotation(unreal.MathLibrary.find_look_at_rotation(
                unreal.Vector(cam.x, cam.y, cam.z + 62.0), hloc))
            unreal.SystemLibrary.execute_console_command(
                self.server(), "HighResShot 1280x720 filename=jitter_stencil_grip")
            self.advance("stencil_done")
        elif s == "stencil_done":
            if self.elapsed() < 2.0:
                return
            log("DONE (PIE left running)")
            self.finish()

    def report(self, name, rows):
        if len(rows) < 20:
            log(f"[{name}] FAIL: only {len(rows)} samples")
            return
        dh = []
        dyaw = []
        for a, b in zip(rows, rows[1:]):
            dh.append(math.dist(a["hips"], b["hips"]))
            dyaw.append(abs(b["yaw"] - a["yaw"]))
        dh_sorted = sorted(dh)
        n = len(dh)
        jumps = sum(1 for x in dh if x > 2.0)
        yawjumps = sum(1 for x in dyaw if x > 3.0)
        reach_flips = sum(1 for a, b in zip(rows, rows[1:]) if a["reach"] != b["reach"])
        needle_flips = sum(1 for a, b in zip(rows, rows[1:])
                           if abs(a["needle"] - b["needle"]) > 2.0)
        trig_on = next((r["t"] for r in rows if r["trig"]), -1.0)
        log(f"[{name}] n={n} hipsD/tick: p50={dh_sorted[n // 2]:.2f} p95={dh_sorted[int(n * 0.95)]:.2f} "
            f"max={dh_sorted[-1]:.2f} jumps>2cm={jumps} yawJumps>3deg={yawjumps} "
            f"reachFlips={reach_flips} needleFlips>2cm={needle_flips} trigSeenAt={trig_on:.2f}s")

    def finish(self):
        log("HARNESS END")
        if self.handle:
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.handle = None


_p = Probe()
log("harness registered")
