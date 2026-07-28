# 搆不到區域診斷（2026-07-28）：user 抓「不遠的區域（大腿）畫不到=out of reach」。
# 兩個假說：A=解算域真邊界（hip/ankle 鉗位下低近點的筆軸俯角搆不到——靜態地圖、
# 同 aim 恆同結果）；B=解算器黏死（07-26 重啟動門檻讓警告卡在壞盆地——回到原本
# reachable 的 aim 仍 unreachable）。實驗：鎖肚頂→tilt 由基準逐步下掃到鉗位→逐步
# 掃回基準，記每步 reach/tipErr/nSolve；回程與去程同 tilt 的 reach 不一致＝B 實錘。
# 產出：Saved/robo_reachmap_result.txt
import re
import time
import traceback

import unreal

OUT = "C:/games/Unreal Engine/nice_ink/Saved/robo_reachmap_result.txt"
import os
os.makedirs(os.path.dirname(OUT), exist_ok=True)
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[REACH] " + str(msg))
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
PAT = re.compile(r"az=" + NUM + r" tilt=" + NUM +
                 r".*?tipErr=" + NUM + r" reach=(\d).*?nSolve=" + NUM)


def parse(s):
    m = PAT.search(s)
    if not m:
        return None
    g = [float(x) for x in m.groups()]
    return {"az": g[0], "tilt": g[1], "err": g[2], "reach": int(g[3]), "nsolve": g[4]}


class Probe:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.victim_pid = None
        self.model_pid = None
        self.base_az = None
        self.base_tilt = None
        self.seq = []      # (label, tilt) 掃描步序
        self.seq_i = 0
        self.rows = []
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

    def m2(self):
        return find_char(get_world("UEDPIE_2"), self.model_pid)

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
                host_pid = unreal.GameplayStatics.get_player_pawn(server, 0) \
                    .get_editor_property("player_state").get_editor_property("player_id")
                for c in unreal.GameplayStatics.get_all_actors_of_class(server, unreal.NiceInkCharacter):
                    pid = c.get_editor_property("player_state").get_editor_property("player_id")
                    if pid != self.victim_pid and pid != host_pid:
                        self.model_pid = pid
                        break
                log(f"victim={self.victim_pid} model={self.model_pid}")
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
            log(f"enter lean -> {ms.call_method('DebugRoboEnterLean', (victim, p, n))}")
            self.advance("baseline")
        elif s == "baseline":
            if self.elapsed() < 2.0:
                return
            raw = str(self.m2().call_method("DebugLeanSummary", ()))
            log("RAW " + raw)  # 原始 summary（tblCols/tblHi 等新欄位靠這行可見）
            d = parse(raw)
            self.base_az = d["az"]
            self.base_tilt = d["tilt"]
            log(f"base az={self.base_az:.1f} tilt={self.base_tilt:.1f} reach={d['reach']}")
            # 去程：tilt 從基準每 6° 下掃 8 步（往腳側低處=大腿方向）；回程原路返回
            down = [("down", self.base_tilt + 6.0 * k) for k in range(1, 9)]
            up = [("back", t) for _, t in reversed(down[:-1])] + [("back", self.base_tilt)]
            self.seq = [("base", self.base_tilt)] + down + up
            self.seq_i = 0
            self.advance("sweep_set")
        elif s == "sweep_set":
            label, tilt = self.seq[self.seq_i]
            self.m2().call_method("DebugRoboDrawAim", (self.base_az, tilt))
            self.advance("sweep_read")
        elif s == "sweep_read":
            if self.elapsed() < 0.8:
                return  # 讓解算/追趕沉降
            label, tilt = self.seq[self.seq_i]
            d = parse(str(self.m2().call_method("DebugLeanSummary", ())))
            self.rows.append((label, tilt, d))
            log(f"[{label}] tilt={tilt:.1f} -> reach={d['reach']} tipErr={d['err']:.2f} "
                f"nSolve={d['nsolve']:.1f} (eff tilt={d['tilt']:.1f})")
            self.seq_i += 1
            if self.seq_i < len(self.seq):
                self.advance("sweep_set")
            else:
                self.analyze()
        # analyze() 內 finish

    def analyze(self):
        # 黏死檢定：同 tilt 的去程/回程 reach 必須一致（不一致=解算器狀態依賴=B 假說）
        go = {round(t, 1): d["reach"] for l, t, d in self.rows if l in ("base", "down")}
        sticky = []
        for l, t, d in self.rows:
            if l == "back":
                k = round(t, 1)
                if k in go and go[k] != d["reach"]:
                    sticky.append((k, go[k], d["reach"]))
        if sticky:
            log("FAIL hysteresis-stickiness detected: " +
                " ".join(f"tilt={t}({a}->{b})" for t, a, b in sticky))
        else:
            log("PASS no stickiness: same tilt -> same reach both directions")
        unreach = [t for l, t, d in self.rows if d["reach"] == 0]
        log(f"unreachable tilts: {sorted(set(round(t,1) for t in unreach))}")
        log("DONE (PIE left running)")
        self.finish()

    def finish(self):
        log("HARNESS END")
        if self.handle:
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.handle = None


_p = Probe()
log("harness registered")
