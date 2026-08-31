# P0-2 沉睡期複製凍結探針（2026-08-31；帳本=Docs/ANTICHEAT_PLAN.md §3）
# 動機：凍結若根本沒生效（viewer 解析失誤等），所有既有測試照樣全綠——
# 「閘門必須能看見它要擋的那種失敗」＋「該凍的必須驗真的凍住」。契約：
#   c1 對照組：沉睡中、走路前，受害者端的作畫者複本 ≈ server 真位（自檢）
#   c2 下限：server 端作畫者真的走了 ≥ 30cm（「什麼都沒發生」不得 PASS）
#   c3 凍結：受害者（閉眼沉睡）世界裡的作畫者複本位移 < 5cm
#   c4 收斂：debug_robo_wake 後複本貼回 server 現位 < 20cm（ForceNetUpdate 生效）
# 產出：Saved/robo_sleepfreeze_result.txt
import os
import time
import traceback

import unreal

OUT = "C:/games/Unreal Engine/nice_ink/Saved/robo_sleepfreeze_result.txt"
os.makedirs(os.path.dirname(OUT), exist_ok=True)
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[SLEEPFREEZE] " + str(msg))
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(LINES))


def get_world(tag):
    for w in unreal.ObjectIterator(unreal.World):
        if tag in w.get_path_name():
            return w
    return None


def gs_of(w):
    return unreal.GameplayStatics.get_game_state(w)


def chars_of(w):
    return list(unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NiceInkCharacter))


def find_char(w, pid):
    for c in chars_of(w):
        ps = c.get_editor_property("player_state")
        if ps and ps.get_editor_property("player_id") == pid:
            return c
    return None


def dist3(a, b):
    return ((a.x - b.x) ** 2 + (a.y - b.y) ** 2 + (a.z - b.z) ** 2) ** 0.5


class Probe:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.victim_pid = None
        self.artist_pid = None
        self.pass_count = 0
        self.fail_count = 0
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
        log(f"STAGE -> {stage}")

    def elapsed(self):
        return time.monotonic() - self.stage_t

    def check(self, name, ok, detail=""):
        if ok:
            self.pass_count += 1
        else:
            self.fail_count += 1
        log(f"{name}: {'PASS' if ok else 'FAIL'} {detail}")

    def server_gm(self):
        return unreal.GameplayStatics.get_game_mode(get_world("UEDPIE_0"))

    def tick(self, dt):
        try:
            self.step()
        except Exception:
            log("EXC:\n" + traceback.format_exc())
            self.finish()

    def step(self):
        s = self.stage
        if s == "boot":
            if time.monotonic() - self.t0 > 8.0:
                unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
                self.advance("wait_pie")
        elif s == "wait_pie":
            server = get_world("UEDPIE_0")
            if server and unreal.GameplayStatics.get_game_mode(server):
                # 受害者強制 seat1＝遠端 client（凍結針對的是「有連線的受害者」；
                # listen 主機當受害者無連線＝不經此路）
                unreal.GameplayStatics.get_game_mode(server).set_editor_property(
                    "DebugForcedVictimSeat", 1)
                self.advance("wait_drawing")
            elif self.elapsed() > 60.0:
                log("FAIL: PIE never started")
                self.finish()
        elif s == "wait_drawing":
            gs = gs_of(get_world("UEDPIE_0"))
            if gs and str(gs.get_editor_property("CurrentPhase")).endswith("DRAWING: 3>"):
                self.victim_pid = gs.get_editor_property("VictimPlayerId")
                log(f"victim={self.victim_pid}")
                self.advance("find_actors")
            elif self.elapsed() > 60.0:
                log("FAIL: drawing phase never came")
                self.finish()
        elif s == "find_actors":
            if self.elapsed() < 2.0:
                return
            w0 = get_world("UEDPIE_0")
            # 作畫者＝listen 主機本人的角色（在 UEDPIE_0 本地控制、可用 DebugRoboWalk）
            host_pawn = unreal.GameplayStatics.get_player_pawn(w0, 0)
            self.artist_pid = host_pawn.get_editor_property("player_state").get_editor_property("player_id")
            log(f"artist(host)={self.artist_pid}")
            sv = find_char(w0, self.victim_pid)
            self.check("victim asleep+eyes-closed on server",
                       sv.get_editor_property("bAsleep") is True and
                       sv.get_editor_property("bEyesOpen") is False)
            w1 = get_world("UEDPIE_1")
            srv_artist = find_char(w0, self.artist_pid)
            cli_artist = find_char(w1, self.artist_pid)
            if not cli_artist:
                self.check("artist copy exists in victim world", False)
                self.finish_summary()
                return
            self.pre_srv = srv_artist.get_actor_location()
            self.pre_cli = cli_artist.get_actor_location()
            # c1 對照組：走路前兩端一致（探針自檢——不一致＝量錯了人或早已漂移）
            self.check("c1 baseline server==victim-copy (<10cm)",
                       dist3(self.pre_srv, self.pre_cli) < 10.0,
                       f"d={dist3(self.pre_srv, self.pre_cli):.1f}")
            # server 端主機作畫者走 2.5 秒（合成輸入＝與真鍵同入口）
            srv_artist.debug_robo_walk(1.0, 0.0, 2.5)
            self.advance("walking")
        elif s == "walking":
            if self.elapsed() < 4.0:
                return
            w0 = get_world("UEDPIE_0")
            w1 = get_world("UEDPIE_1")
            srv_artist = find_char(w0, self.artist_pid)
            cli_artist = find_char(w1, self.artist_pid)
            self.post_srv = srv_artist.get_actor_location()
            post_cli = cli_artist.get_actor_location()
            moved_srv = dist3(self.post_srv, self.pre_srv)
            moved_cli = dist3(post_cli, self.pre_cli)
            # c2 下限：server 真的有動
            self.check("c2 server artist moved (>=30cm)", moved_srv >= 30.0,
                       f"moved={moved_srv:.1f}")
            # c3 凍結：受害者世界的複本沒跟
            self.check("c3 victim-world copy frozen (<5cm)", moved_cli < 5.0,
                       f"moved={moved_cli:.1f} (server moved {moved_srv:.1f})")
            self.server_gm().debug_robo_wake()
            self.advance("verify_unfreeze")
        elif s == "verify_unfreeze":
            if self.elapsed() < 2.0:
                return
            w0 = get_world("UEDPIE_0")
            w1 = get_world("UEDPIE_1")
            sv = find_char(w0, self.victim_pid)
            self.check("victim eyes open after robo wake",
                       sv.get_editor_property("bEyesOpen") is True)
            srv_artist = find_char(w0, self.artist_pid)
            cli_artist = find_char(w1, self.artist_pid)
            now_srv = srv_artist.get_actor_location()
            now_cli = cli_artist.get_actor_location()
            # c4 收斂：睜眼後複本貼回 server 現位
            self.check("c4 victim-world copy snapped after wake (<20cm)",
                       dist3(now_srv, now_cli) < 20.0,
                       f"d={dist3(now_srv, now_cli):.1f}")
            self.finish_summary()

    def finish_summary(self):
        log(f"SUMMARY pass={self.pass_count} fail={self.fail_count}")
        log("DONE" if self.fail_count == 0 else "RESULT: FAIL")
        self.finish()

    def finish(self):
        try:
            unreal.unregister_slate_post_tick_callback(self.handle)
        except Exception:
            pass


Probe()
log("sleepfreeze probe armed")
