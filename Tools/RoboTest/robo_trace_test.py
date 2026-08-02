# 醉夢描圖測試 v1（2026-08-02 SPEC v4.0 定案 #49/#50/#51）
# 受害者強制 seat1＝遠端 client（RPC 走真網路）。契約：
#   t1 開局發夢：受害者本地 traceActive=1、圖形參數＝難度檔（perim/band/點數）
#   t2 生成統計：DebugTraceStats 三檔各 200 種子——minSelfDist ≥ 2.6×band（投影窗
#      唯一性＝生成器鐵律）、重試率 sane
#   t3 他端零情報：server/observer 端受害者複本 traceActive=0（夢不複製）
#   t4 自動沿線描：DebugAutopilot 走真實追趕/判定路徑——進度 ≈ v_max×t、零失敗
#   t5 搖晃攻擊：DebugRoboShake → 攻擊者扣款 500＋受害者端 shake=1＋冷卻內
#      再砸不扣款；搖晃中抬針＝安全（fails 不動）
#   t6 越線重來：DebugVeerOff → fails+1、進度歸零、針回起點
#   t7 描完＝無聲甦醒：autopilot 走完全程 → sent=1 → server 端 bEyesOpen=1
#      → DebugRoboEmerge 現身收束進巡禮
#   t8 噴射封存（#51）：DebugRoboSpray → 零投射物、SprayCharges 恆 0
# 產出：Saved/robo_trace_result.txt
import os
import re
import time
import traceback

import unreal

OUT = "C:/games/Unreal Engine/nice_ink/Saved/robo_trace_result.txt"
os.makedirs(os.path.dirname(OUT), exist_ok=True)
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[TRACETEST] " + str(msg))
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(LINES))


def get_world(tag):
    for w in unreal.ObjectIterator(unreal.World):
        if tag in w.get_path_name():
            return w
    return None


def gs_of(w):
    return unreal.GameplayStatics.get_game_state(w)


def find_char(w, pid):
    for c in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NiceInkCharacter):
        ps = c.get_editor_property("player_state")
        if ps and ps.get_editor_property("player_id") == pid:
            return c
    return None


WORLD_TAGS = ("UEDPIE_0", "UEDPIE_1", "UEDPIE_2")


def local_world_of(pid):
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
    for k in ("traceActive", "pen", "curS", "prog", "total", "fails", "shake",
              "sent", "seed", "retries", "pts", "band", "perim", "maxR", "motif", "closed"):
        m = re.search(r"\b" + k + r"=(-?[\d.]+)", s)
        if m:
            d[k] = float(m.group(1))
    return d


class Test:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.victim_pid = None
        self.victim_local = None
        self.observer_tag = None
        self.attacker_ps = None
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

    def trace(self):
        return self.victim_local.get_editor_property("DreamTrace")

    def summary(self):
        return parse_summary(str(self.trace().call_method("GetDebugSummary", ())))

    def non_victim_cash_sum(self):
        # 全部非受害者錢包總和：迭代順序無關＋跨場存檔殘留（RestoreCharacter 載舊
        # cash）無關——只驗 delta（扣 500／冷卻不扣）
        gs = gs_of(get_world("UEDPIE_0"))
        total = 0
        for ps in gs.get_editor_property("player_array"):
            if ps.get_editor_property("player_id") != self.victim_pid:
                total += ps.get_editor_property("cash")
        return total

    def step(self):
        s = self.stage
        if s == "boot":
            if time.monotonic() - self.t0 > 8.0:
                unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
                self.advance("wait_pie")
        elif s == "wait_pie":
            server = get_world("UEDPIE_0")
            if server and unreal.GameplayStatics.get_game_mode(server):
                gm = unreal.GameplayStatics.get_game_mode(server)
                gm.set_editor_property("DebugForcedVictimSeat", 1)  # 遠端受害者＝RPC 走真網路
                # 固定種子＝固定圖案（42/7=6, 6%3=0 → cup0 池[0]=櫻花・閉合）——
                # 圖案池隨機下 autopilot 契約零 flake
                gm.set_editor_property("DebugForcedTraceSeed", 42)
                self.advance("wait_drawing")
            elif self.elapsed() > 60.0:
                log("FAIL: PIE never started")
                self.finish()
        elif s == "wait_drawing":
            gs = gs_of(get_world("UEDPIE_0"))
            if gs and str(gs.get_editor_property("CurrentPhase")).endswith("DRAWING: 3>"):
                self.victim_pid = gs.get_editor_property("VictimPlayerId")
                log(f"victim={self.victim_pid}")
                self.advance("find_victim")
            elif self.elapsed() > 60.0:
                log("FAIL: drawing phase never came")
                self.finish()
        elif s == "find_victim":
            if self.elapsed() < 1.5:
                return
            w, tag = local_world_of(self.victim_pid)
            self.check("victim local world found", w is not None, f"tag={tag}")
            if not w:
                self.finish()
                return
            self.victim_local = find_char(w, self.victim_pid)
            for t in WORLD_TAGS:
                if t != tag and t != "UEDPIE_0":
                    self.observer_tag = t
                    break
            # 鋪一筆墨讓現身有巡禮可進
            self.server_gm().debug_robo_stroke(
                unreal.Vector2D(0.45, 0.45), unreal.Vector2D(0.50, 0.47), 2)
            self.advance("t1_active")
        elif s == "t1_active":
            if self.elapsed() < 1.0:
                return
            d = self.summary()
            log("SUMMARY | " + str(self.trace().call_method("GetDebugSummary", ())))
            self.check("t1 trace active on victim client", d.get("traceActive") == 1.0, str(d))
            # cup0 難度檔：統一 60s×v_max 1.8＝線長 108；帶半寬=筆寬 0.30（user
            # 定案「筆寬兩倍當帶寬」＝發夢當下從受害者 TattooNibDiameterCm 導出
            # ——改筆寬/TattooDotHz 會連動這兩個值＝契約要跟著重算）
            self.check("t1 figure matches cup0 params (60s x vmax; band=nib)",
                       abs(d.get("perim", 0) - 108.0) < 1.0 and abs(d.get("band", 0) - 0.30) < 0.01,
                       f"perim={d.get('perim')} band={d.get('band')}")
            self.check("t1 figure dense enough", d.get("pts", 0) >= 150, f"pts={d.get('pts')}")
            self.check("t1 progress starts zero", abs(d.get("prog", 9)) < 0.01 and d.get("fails") == 0.0, str(d))
            # 固定種子 42 → cup0 池[(42/7)%5=1]＝Lantern＝烘焙表索引 6（閉合；
            # 08-03 難度重排池序）——選圖函數/表序/池序改了要重對
            self.check("t1 forced seed picks Lantern (deterministic baked motif)",
                       d.get("motif") == 6.0 and d.get("closed") == 1.0,
                       f"motif={d.get('motif')} closed={d.get('closed')}")
            self.advance("t2_stats")
        elif s == "t2_stats":
            gm = self.server_gm()
            ok_all = True
            for cup in (0, 1, 2):
                rep = str(gm.call_method("DebugTraceStats", (200, cup)))
                log(f"STATS cup{cup} | {rep}")
                m = re.search(r"minSelfDist=([\d.]+) \(need ([\d.]+)\)", rep)
                r = re.search(r"retriedFigs=(\d+)", rep)
                b = re.search(r"blobFallback=(\d+)", rep)
                v = re.search(r"autoDevViol=(\d+)", rep)
                mo = re.search(r"motifs\[([^\]]*)\]", rep)
                ok = m and float(m.group(1)) >= float(m.group(2))
                # 重試率 sane：<40%（重試是機制不是病；太高＝模板和帶寬打架）；
                # 保底 Blob 退路零使用＝全部日式標的模板自己站得住；
                # pursuit 可描性模擬零超帶＝每個樣本都描得完（曲率鐵閘）
                ok = ok and r and int(r.group(1)) < 80
                ok = ok and b and int(b.group(1)) == 0
                ok = ok and v and int(v.group(1)) == 0
                # 圖案池三員都出場（種子選圖的覆蓋）
                ok = ok and mo and len([t for t in mo.group(1).split() if t]) >= 3
                ok_all = ok_all and bool(ok)
            self.check("t2 gen stats: self-dist/retry/no-fallback/pool coverage (3 cups x200)", ok_all)
            self.advance("t3_privacy")
        elif s == "t3_privacy":
            srv = parse_summary(str(self.server_victim().get_editor_property("DreamTrace")
                                    .call_method("GetDebugSummary", ())))
            obs_char = find_char(get_world(self.observer_tag), self.victim_pid) if self.observer_tag else None
            obs = parse_summary(str(obs_char.get_editor_property("DreamTrace")
                                    .call_method("GetDebugSummary", ()))) if obs_char else {}
            self.check("t3 dream not replicated (server copy inactive)",
                       srv.get("traceActive") == 0.0, str(srv))
            self.check("t3 dream not replicated (observer copy inactive)",
                       obs.get("traceActive") == 0.0, str(obs))
            self.prog0 = self.summary().get("prog", 0.0)
            self.trace().call_method("DebugAutopilot", (6.0,))
            self.advance("t4_autopilot")
        elif s == "t4_autopilot":
            # 牆鐘 8s ⊇ 自動描 6「遊戲秒」（robo 編輯器遊戲時間 ~0.5-1.0× 牆鐘）
            if self.elapsed() < 8.0:
                return
            d = self.summary()
            gained = abs(d.get("prog", 0.0)) - abs(self.prog0)
            # v_max=1.8cm/s×6 遊戲秒=10.8cm 封頂；下限容 0.5× 時間膨脹
            self.check("t4 autopilot traces at machine speed", 6.5 <= gained <= 14.0,
                       f"gained={gained:.2f}")
            self.check("t4 autopilot stays in band (no fails)", d.get("fails") == 0.0, str(d))
            self.cash0 = self.non_victim_cash_sum()
            self.fails_at_shake = d.get("fails", 0)
            self.server_gm().call_method("DebugRoboShake", ())
            self.advance("t5_shake")
        elif s == "t5_shake":
            if self.elapsed() < 1.0:
                return
            d = self.summary()
            cash = self.non_victim_cash_sum()
            self.check("t5 shake reaches victim dream", d.get("shake") == 1.0, str(d))
            self.check("t5 attacker paid 500", cash == self.cash0 - 500,
                       f"sum={cash} was={self.cash0}")
            # 抬針中搖晃＝安全（fails 相對搖晃前不增——用 delta 不用絕對值：
            # 前段任何 fail 不得污染本契約）
            self.check("t5 pen-up during shake is safe (no new fails)",
                       d.get("fails", 0) == self.fails_at_shake, str(d))
            self.server_gm().call_method("DebugRoboShake", ())  # 冷卻內再砸
            self.advance("t5_cooldown")
        elif s == "t5_cooldown":
            if self.elapsed() < 1.0:
                return
            cash = self.non_victim_cash_sum()
            self.check("t5 cooldown rejects second shake (no charge)",
                       cash == self.cash0 - 500, f"sum={cash}")
            self.advance("t6_veer_wait")
        elif s == "t6_veer_wait":
            # 等搖晃完全結束（偏移歸零）再做越線——隔離變因
            if self.elapsed() < 4.0:
                return
            self.trace().call_method("DebugVeerOff", (1.2,))
            self.advance("t6_veer")
        elif s == "t6_veer":
            if self.elapsed() < 1.8:
                return
            d = self.summary()
            self.check("t6 veer off band = fail", d.get("fails", 0) >= 1.0, str(d))
            # curS 是環上位置：起點附近可讀成 total-ε（wrap）——距離用環域量
            cur_s = d.get("curS", 9.0)
            total = d.get("total", 45.0)
            s_from_start = min(cur_s % total, total - (cur_s % total))
            self.check("t6 fail resets progress to start",
                       abs(d.get("prog", 9)) < 0.5 and s_from_start < 0.5,
                       f"prog={d.get('prog')} sFromStart={s_from_start:.2f}")
            self.fails_before_run = d.get("fails", 0)
            # 描完全程：108cm / 1.8cm/s ≈ 60 遊戲秒（統一 60s 定案；+重來餘量與
            # 遊戲時間膨脹→autopilot 90 遊戲秒、牆鐘上限 150s）
            self.trace().call_method("DebugAutopilot", (90.0,))
            self.advance("t7_complete")
        elif s == "t7_complete":
            d = self.summary()
            if d.get("sent") != 1.0:
                if self.elapsed() > 150.0:
                    self.check("t7 full trace completes", False, f"timeout | {d}")
                    self.finish()
                return
            self.check("t7 full trace completes & sends", True,
                       f"prog={d.get('prog'):.1f}/{d.get('total'):.1f} fails={d.get('fails')} "
                       f"(runFails={d.get('fails', 0) - self.fails_before_run:.0f})")
            self.advance("t7_wake")
        elif s == "t7_wake":
            if self.elapsed() < 1.0:
                return
            sv = self.server_victim()
            self.check("t7 server eyes open (silent wake)",
                       bool(sv.get_editor_property("eyes_open")), "")
            # 噴射封存（#51）：喚醒前最後驗——投射物零生成、charges 恆 0
            self.server_gm().debug_robo_spray(90.0, 0)
            self.advance("t8_spray_gate")
        elif s == "t8_spray_gate":
            if self.elapsed() < 1.0:
                return
            projectiles = list(unreal.GameplayStatics.get_all_actors_of_class(
                get_world("UEDPIE_0"), unreal.InkSprayProjectile))
            sv = self.server_victim()
            self.check("t8 spray gated: zero projectiles", len(projectiles) == 0,
                       f"n={len(projectiles)}")
            self.check("t8 spray charges stay zero",
                       sv.get_editor_property("spray_charges") == 0, "")
            self.server_gm().debug_robo_emerge()
            self.advance("t9_emerge")
        elif s == "t9_emerge":
            if self.elapsed() < 2.0:
                return
            gs = gs_of(get_world("UEDPIE_0"))
            phase = str(gs.get_editor_property("CurrentPhase"))
            self.check("t9 emerge ends drawing (tour begins)",
                       "TOUR" in phase or "ACCUSATION" in phase, phase)
            log(f"DONE pass={self.pass_count} fail={self.fail_count}")
            self.finish()

    def finish(self):
        log("HARNESS END")
        if self.handle:
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.handle = None


_t = Test()
log("harness registered")
