# 灰紗（可畫域 veil）出現耗時量測（2026-08-25；臨時儀器，量完連同 C++ 診斷碼拆除）
# 問題：按下右鍵後「不可畫區域」多久會出現？——64² 漸進採樣是幀預算制（3ms/tick），
# 沒有固定秒數，只能量。四個鎖點各量一次（第四次＝同點重鎖＝tri-cache 暖機對照）。
# 產出：Saved/robo_veiltime_result.txt
import os
import time
import traceback

import unreal

OUT = "C:/games/Unreal Engine/nice_ink/Saved/robo_veiltime_result.txt"
os.makedirs(os.path.dirname(OUT), exist_ok=True)
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[VEILTIME] " + str(msg))
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


# 鎖點＝robo_reachradius 同三點（可比對既有 R100 量測）＋belly 重鎖
LOCKS = [
    ("belly", (0.0, 26.0, 95.0)),
    ("mid", (0.0, 26.0, 60.0)),
    ("low", (-27.0, 100.0, 43.0)),
    ("belly_relock", (0.0, 26.0, 95.0)),
]


class Probe:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.victim_pid = None
        self.host_pid = None
        self.lock_i = 0
        self.lock_wall = 0.0
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

    def step(self):
        s = self.stage
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
                log(f"drawing phase; victim={self.victim_pid} host={self.host_pid}")
                self.advance("lock")
            elif self.elapsed() > 40.0:
                log("FAIL: drawing phase never came")
                self.finish()
        elif s == "lock":
            if self.elapsed() < 1.5:
                return  # 上一鎖收乾淨
            name, local = LOCKS[self.lock_i]
            victim = find_char(self.server(), self.victim_pid)
            bt = victim.get_editor_property("Body").get_world_transform()
            p = bt.transform_location(unreal.Vector(*local))
            n = bt.transform_direction(unreal.Vector(0.0, 1.0, 0.0))
            host = find_char(self.server(), self.host_pid)
            ok = host.call_method("DebugRoboEnterLean", (victim, p, n))
            self.lock_wall = time.monotonic()
            log(f"[{name}] enter={ok}")
            self.advance("wait_veil")
        elif s == "wait_veil":
            name, _ = LOCKS[self.lock_i]
            host = find_char(self.server(), self.host_pid)
            st = host.call_method("DebugRoboVeilTiming", ())
            if "ready=1" in st:
                wall = (time.monotonic() - self.lock_wall) * 1000.0
                log(f"[{name}] {st} | pyWall={wall:.0f}ms")
                host.call_method("ServerExitLean", ())
                self.lock_i += 1
                if self.lock_i >= len(LOCKS):
                    log("DONE (PIE left running)")
                    self.finish()
                else:
                    self.advance("lock")
            elif self.elapsed() > 30.0:
                log(f"[{name}] FAIL timeout 30s; last={st}")
                host.call_method("ServerExitLean", ())
                self.lock_i += 1
                if self.lock_i >= len(LOCKS):
                    log("DONE (timeouts)")
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
