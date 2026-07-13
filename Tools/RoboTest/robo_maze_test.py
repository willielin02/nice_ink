# 醉夢圓形迷宮全事件流測試（Python StartupScript 掛載；tick 驅動狀態機）
# 驗證：ClientStartMaze 決定性重建（摘要可讀）→ 踩陷阱（真實 client 偵測→Server RPC）
#       → 只有兇手收到轉盤（第三人零通知）→ DebugRoboMazeDial 送度數 → 受害者旋轉
#       精確收斂＋重生回原點 → 存檔點授 SprayCharges → 再踩陷阱重生回存檔點
#       → 出口睜眼 → 現身進巡禮 → 生成器統計（fuzz violations=0）。
# 產出：scratchpad/robo_maze_result.txt
import re
import time
import traceback

import unreal

OUT = r"C:\Users\willi\AppData\Local\Temp\claude\c--games-Unreal-Engine-nice-ink\9463ca47-69c8-4642-8bfc-b7fd02de08bd\scratchpad\robo_maze_result.txt"
import os
os.makedirs(os.path.dirname(OUT), exist_ok=True)
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[MAZETEST] " + str(msg))
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


WORLD_TAGS = ("UEDPIE_0", "UEDPIE_1", "UEDPIE_2")


def local_world_of(pid):
    """回傳該玩家本地控制所在的 PIE 世界（player 0 pawn 的 id 比對）。"""
    for tag in WORLD_TAGS:
        w = get_world(tag)
        if not w:
            continue
        pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
        if pawn:
            ps = pawn.get_editor_property("player_state")
            if ps and ps.get_editor_property("player_id") == pid:
                return w, tag
    return None, None


def parse_summary(s):
    d = {}
    m = re.search(r"state=(\w+)", s)
    d["state"] = m.group(1) if m else "?"
    for key in ("pos", "respawn", "cpS", "cpK", "exit"):
        m = re.search(key + r"=\((-?[\d.]+),(-?[\d.]+)\)", s)
        d[key] = (float(m.group(1)), float(m.group(2))) if m else None
    m = re.search(r"angle=(-?[\d.]+)", s)
    d["angle"] = float(m.group(1)) if m else None
    d["owners"] = [int(x) for x in re.findall(r"owner=(-?\d+)", s)]
    m = re.search(r"active=(\d)", s)
    d["active"] = m.group(1) == "1" if m else False
    m = re.search(r"exitSent=(\d)", s)
    d["exitSent"] = m.group(1) == "1" if m else False
    return d


def near(a, b, tol=0.06):
    return a is not None and b is not None and abs(a[0] - b[0]) < tol and abs(a[1] - b[1]) < tol


class Test:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.victim_pid = None
        self.victim_world = None
        self.maze = None            # 受害者本地世界的 DreamMaze 元件
        self.owners = []
        self.summary0 = None
        self.pass_count = 0
        self.fail_count = 0
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    def advance(self, stage):
        self.stage = stage
        self.stage_t = time.monotonic()
        log("STAGE -> " + stage)

    def elapsed(self):
        return time.monotonic() - self.stage_t

    def check(self, name, ok, detail=""):
        if ok:
            self.pass_count += 1
        else:
            self.fail_count += 1
        log(f"{name}: {'PASS' if ok else 'FAIL'} {detail}")

    def tick(self, dt):
        try:
            self.step()
        except Exception:
            log("EXC:\n" + traceback.format_exc())
            self.finish()

    def server_gm(self):
        return unreal.GameplayStatics.get_game_mode(get_world("UEDPIE_0"))

    def server_victim(self):
        return find_char(get_world("UEDPIE_0"), self.victim_pid)

    def step(self):
        s = self.stage
        if s == "boot":
            if time.monotonic() - self.t0 > 8.0:
                unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
                self.advance("wait_pie")
        elif s == "wait_pie":
            server = get_world("UEDPIE_0")
            if server and unreal.GameplayStatics.get_game_mode(server):
                # 受害者強制 seat 1（遠端客戶端）——RPC 走真網路
                unreal.GameplayStatics.get_game_mode(server).set_editor_property("DebugForcedVictimSeat", 1)
                self.advance("wait_drawing")
            elif self.elapsed() > 60.0:
                log("FAIL: PIE never started")
                self.finish()
        elif s == "wait_drawing":
            gs = gs_of(get_world("UEDPIE_0"))
            if gs and str(gs.get_editor_property("CurrentPhase")).endswith("DRAWING: 3>"):
                self.victim_pid = gs.get_editor_property("VictimPlayerId")
                log(f"victim={self.victim_pid}")
                self.advance("find_maze")
            elif self.elapsed() > 60.0:
                log("FAIL: drawing phase never came")
                self.finish()
        elif s == "find_maze":
            if self.elapsed() < 1.5:
                return
            w, tag = local_world_of(self.victim_pid)
            self.check("victim local world found", w is not None, f"tag={tag}")
            if not w:
                self.finish()
                return
            self.victim_world = w
            victim = find_char(w, self.victim_pid)
            self.maze = victim.get_editor_property("DreamMaze")
            summary = self.maze.get_debug_summary()
            log("summary0: " + summary)
            d = parse_summary(summary)
            self.summary0 = d
            self.owners = d["owners"]
            self.check("maze active+walking", d["active"] and d["state"] == "Walking")
            self.check("traps assigned to artists",
                       len(self.owners) >= 1 and self.victim_pid not in self.owners,
                       f"owners={self.owners}")
            self.check("checkpoints+exit placed",
                       d["cpS"] is not None and d["cpK"] is not None and d["exit"] is not None)
            # 先鋪一筆麥克筆（讓之後現身有巡禮可進）
            self.server_gm().debug_robo_stroke(unreal.Vector2D(0.45, 0.45), unreal.Vector2D(0.50, 0.47), 2)
            self.advance("trap0")
        elif s == "trap0":
            if self.elapsed() < 1.0:
                return
            self.maze.debug_trigger_trap(0)
            self.advance("verify_dial")
        elif s == "verify_dial":
            if self.elapsed() < 1.2:
                return
            # 只有兇手看到轉盤；其他作畫者零通知（SPEC 定案 #31）
            killer_pid = self.owners[0]
            kw, ktag = local_world_of(killer_pid)
            killer_local = find_char(kw, killer_pid) if kw else None
            dial = killer_local.get_editor_property("bTrapDialActive") if killer_local else None
            self.check("killer got dial", dial is True, f"killer={killer_pid}@{ktag}")
            for other_pid in self.owners[1:]:
                ow, otag = local_world_of(other_pid)
                other_local = find_char(ow, other_pid) if ow else None
                if other_local:
                    self.check("third player NOT notified",
                               other_local.get_editor_property("bTrapDialActive") is False,
                               f"pid={other_pid}@{otag}")
            # 代兇手送 170 度（真路徑會等 5 秒滾輪自動送出；robo 直接結案）
            self.server_gm().debug_robo_maze_dial(170.0)
            self.advance("verify_rotation")
        elif s == "verify_rotation":
            if self.elapsed() < 7.0:
                return  # DeathScreen 1.2s + RotAnim ~3s + 裕度
            d = parse_summary(self.maze.get_debug_summary())
            log("after trap0: " + self.maze.get_debug_summary())
            self.check("rotation exact 170", d["angle"] is not None and abs(d["angle"] - 170.0) < 0.1,
                       f"angle={d['angle']}")
            self.check("back to walking", d["state"] == "Walking")
            self.check("respawn at origin", near(d["pos"], (0.0, 0.0)) and near(d["respawn"], (0.0, 0.0)),
                       f"pos={d['pos']}")
            self.advance("checkpoint")
        elif s == "checkpoint":
            if self.elapsed() < 0.5:
                return
            self.maze.debug_trigger_checkpoint(0)
            self.advance("verify_checkpoint")
        elif s == "verify_checkpoint":
            if self.elapsed() < 1.5:
                return
            charges = self.server_victim().get_editor_property("SprayCharges")
            self.check("spray charge granted (server)", charges == 1, f"charges={charges}")
            # 再踩一次同一存檔點不重複授予由 server 去重（不另測，記錄語意）
            self.advance("trap1")
        elif s == "trap1":
            if self.elapsed() < 0.5:
                return
            self.maze.debug_trigger_trap(len(self.owners) - 1)  # 有第二個陷阱就踩第二個
            self.advance("dial1")
        elif s == "dial1":
            if self.elapsed() < 1.2:
                return
            self.server_gm().debug_robo_maze_dial(-90.0)
            self.advance("verify_respawn_cp")
        elif s == "verify_respawn_cp":
            if self.elapsed() < 7.0:
                return
            d = parse_summary(self.maze.get_debug_summary())
            log("after trap1: " + self.maze.get_debug_summary())
            self.check("angle cumulative 80", d["angle"] is not None and abs(d["angle"] - 80.0) < 0.1,
                       f"angle={d['angle']}")
            # 2026-07-13 定案：存檔點概念取消（技能點=純拾取）——被抓到一律回原點
            self.check("respawn always origin (checkpoints are pickups)",
                       near(d["pos"], (0.0, 0.0)) and near(d["respawn"], (0.0, 0.0)),
                       f"pos={d['pos']}")
            self.advance("exit")
        elif s == "exit":
            if self.elapsed() < 0.5:
                return
            self.maze.debug_trigger_exit()
            self.advance("verify_exit")
        elif s == "verify_exit":
            if self.elapsed() < 1.5:
                return
            eyes = self.server_victim().get_editor_property("bEyesOpen")
            self.check("silent wake (eyes open, server)", eyes is True)
            self.server_gm().debug_robo_emerge()
            self.advance("verify_tour")
        elif s == "verify_tour":
            if self.elapsed() < 2.5:
                return
            phase = str(gs_of(get_world("UEDPIE_0")).get_editor_property("CurrentPhase"))
            self.check("emerge -> tour", "TOUR" in phase.upper(), f"phase={phase}")
            self.advance("stats")
        elif s == "stats":
            report = self.server_gm().debug_maze_stats(300, 1)
            log("stats:\n" + str(report))
            self.check("fuzz violations=0", "violations=0" in str(report))
            self.advance("shot")
        elif s == "shot":
            # 受害者視窗截圖（HighResShot 只有聚焦視窗處理——非致命，拍不到就算了）
            if self.victim_world:
                unreal.SystemLibrary.execute_console_command(
                    self.victim_world, "HighResShot 1280x720 filename=mazeui_victim")
            self.advance("done")
        elif s == "done":
            if self.elapsed() < 2.0:
                return
            log(f"CHECKS: {self.pass_count} pass / {self.fail_count} fail")
            log("DONE" if self.fail_count == 0 else "FAIL: some checks failed")
            self.finish()

    def finish(self):
        if self.handle:
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.handle = None
        with open(OUT, "w", encoding="utf-8") as f:
            f.write("\n".join(LINES))


_t = Test()
log("harness registered")
