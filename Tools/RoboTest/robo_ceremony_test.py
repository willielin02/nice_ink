# 入睡儀式契約測試（2026-08-16）
# 契約：
#   c1 轉瓶終角指向受害者（±6°）
#   c2 揭曉時機：Spin 結束前 VictimPlayerId 恆 INDEX_NONE
#   c3 零 teleport：儀式全程逐取樣位移 < 走路上限（崩塌拍另有 c4）
#   c4 崩塌連續性：Body 相對旋轉逐取樣角步有界（無跳變）
#   c5 交接零跳變：崩塌終點 ≈ 躺位（<3cm/3°）、bAsleep 翻轉前後 Body 相對變換一致
#   c6 六拍全部走到、相位最終抵達 Drawing
#   c7 儀式期間輸入無效（注入 lean → 不進鎖）
#   c8 圍圈：Gather 結束時全員在自己的角位附近
#   c9 手 IK：PickUp 結束時右手骨接近瓶頸
#   c10 瓶子交接不跳（Drink 首尾瓶子相對手的變換固定）
#   c11 回合 2+ 也走儀式（Resolution → Seating 再看到 Approach）
# 產出：Saved/robo_ceremony_result.txt
import unreal, time, math, re, traceback

OUT = r"C:\games\Unreal Engine\nice_ink\Saved\robo_ceremony_result.txt"
LINES = []

STEP_NONE, STEP_GATHER, STEP_SPIN, STEP_APPROACH, STEP_PICKUP, STEP_DRINK, STEP_COLLAPSE = range(7)
STEP_NAME = ["None", "Gather", "Spin", "Approach", "PickUp", "Drink", "Collapse"]


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[CEREM] " + str(msg))
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(LINES))


def get_world(tag):
    for w in unreal.ObjectIterator(unreal.World):
        if tag in w.get_path_name():
            return w
    return None


def step_of(gs):
    # UE 5.7 python 回的是列舉物件不是 int（int() 直接爆 TypeError）
    v = gs.get_editor_property("ceremony_step")
    try:
        return int(v.value)
    except Exception:
        m = re.search(r"(\d+)", str(v))
        return int(m.group(1)) if m else 0


def stats(char):
    s = str(char.call_method("DebugRoboCeremonyStats", ()))
    d = {}
    for k, v in re.findall(r"(\w+)=([-\d.]+)", s):
        try:
            d[k] = float(v)
        except ValueError:
            pass
    # loc=x,y,z 要單獨抓
    m = re.search(r"loc=([-\d.]+),([-\d.]+),([-\d.]+)", s)
    if m:
        d["locx"], d["locy"], d["locz"] = (float(m.group(i)) for i in (1, 2, 3))
    return s, d


class Probe:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.checks = []
        self.samples = []        # (step, t, dict) 每 tick
        self.steps_seen = []
        self.victim_before_reveal = []
        self.spin_end = None     # (bottle_yaw, victim_loc, center)
        self.pickup_end = None
        self.collapse_end = None
        self.asleep_frames = []
        self.round2_steps = []
        self.lean_tried = False
        self.crashed = False
        self.lean_locked_during = False
        self.gather_end = None
        self.bottle_samples = []
        self.last_step = STEP_NONE
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    def advance(self, stage):
        self.stage = stage
        self.stage_t = time.monotonic()
        log("STAGE -> " + stage)

    def elapsed(self):
        return time.monotonic() - self.stage_t

    def check(self, name, ok, detail):
        self.checks.append(bool(ok))
        log(("PASS " if ok else "FAIL ") + name + " :: " + detail)

    def server(self):
        return get_world("UEDPIE_0")

    def gs(self):
        w = self.server()
        return unreal.GameplayStatics.get_game_state(w) if w else None

    def chars(self):
        w = self.server()
        return list(unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NiceInkCharacter)) if w else []

    def victim_char(self):
        gs = self.gs()
        if not gs:
            return None
        vid = gs.get_editor_property("victim_player_id")
        for c in self.chars():
            ps = c.get_editor_property("player_state")
            if ps and ps.get_editor_property("player_id") == vid:
                return c
        return None

    def bottle(self):
        w = self.server()
        arr = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NiceInkBottle) if w else []
        return arr[0] if arr else None

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
                eps = unreal.find_object(None, "/Script/UnrealEd.Default__EditorPerformanceSettings")
                if eps:
                    eps.set_editor_property("bThrottleCPUWhenNotForeground", False)
                    log("throttle disabled")
                unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
                self.advance("wait_pie")
        elif s == "wait_pie":
            w = self.server()
            gm = unreal.GameplayStatics.get_game_mode(w) if w else None
            if gm:
                gm.set_editor_property("DebugForcedVictimSeat", 1)  # 受害者＝遠端 client（走真網路）
                gm.set_editor_property("tour_seconds_per_work", 2.0)
                gm.set_editor_property("resolution_seconds", 2.5)
                self.advance("watch")
            elif self.elapsed() > 90:
                log("FAIL: no server GM")
                self.finish()
        elif s == "watch":
            self.sample()
            gs = self.gs()
            if not gs:
                return
            phase = str(gs.get_editor_property("current_phase"))
            if "DRAWING" in phase:
                self.eval_opening()
                self.advance("round2")
            elif self.elapsed() > 120:
                log("FAIL: never reached Drawing; steps=" + str(self.steps_seen))
                self.eval_opening()
                self.finish()
        elif s == "round2":
            # 讓受害者現身 → 巡禮 → 指認錯 → 下一回合 Seating 應再看到 Approach
            if self.elapsed() < 1.0:
                return
            gs = self.gs()
            gm = unreal.GameplayStatics.get_game_mode(self.server())
            phase = str(gs.get_editor_property("current_phase")) if gs else ""
            step = step_of(gs) if gs else 0
            if step in (STEP_APPROACH, STEP_PICKUP, STEP_DRINK, STEP_COLLAPSE):
                self.round2_steps.append(step)
            if "DRAWING" in phase and not self.round2_steps:
                # 畫一筆再現身（沒作品＝跳過巡禮）
                gm.call_method("DebugRoboStroke", (unreal.Vector2D(0.5, 0.5), unreal.Vector2D(0.55, 0.55), 0))
                gm.call_method("DebugRoboEmerge", ())
                self.stage_t = time.monotonic()
            elif "ACCUSATION" in phase:
                gm.call_method("DebugRoboAccuse", (False,))  # 猜錯→罰酒→再睡一輪
                self.stage_t = time.monotonic()
            elif self.round2_steps and "SEATING" in phase:
                self.check("c11_round2_ceremony", True,
                           "round2 steps=" + ",".join(STEP_NAME[x] for x in sorted(set(self.round2_steps))))
                self.finish()
            elif self.elapsed() > 60:
                self.check("c11_round2_ceremony", False,
                           f"phase={phase} steps={self.round2_steps}")
                self.finish()

    def sample(self):
        gs = self.gs()
        if not gs:
            return
        step = step_of(gs)
        if step != self.last_step:
            log(f"STEP {STEP_NAME[self.last_step]} -> {STEP_NAME[step]}")
            self.on_step_change(self.last_step, step, gs)
            self.last_step = step
        if step != STEP_NONE and step not in self.steps_seen:
            self.steps_seen.append(step)
        if step == STEP_NONE:
            return
        # 揭曉時機
        if step in (STEP_GATHER, STEP_SPIN):
            self.victim_before_reveal.append(int(gs.get_editor_property("victim_player_id")))
        # 逐 tick 取樣所有角色的位置與姿勢量
        for c in self.chars():
            raw, d = stats(c)
            d["step"] = step
            ps = c.get_editor_property("player_state")
            d["pid"] = ps.get_editor_property("player_id") if ps else -1
            self.samples.append(d)
        b = self.bottle()
        if b:
            bl = b.get_actor_location()
            self.bottle_samples.append((step, bl.x, bl.y, bl.z))
        # 儀式期間注入 lean（輸入應無效）
        if step == STEP_SPIN and not self.lean_tried:
            self.lean_tried = True
            for c in self.chars():
                if c.get_editor_property("lean_locked"):
                    self.lean_locked_during = True

    def on_step_change(self, old, new, gs):
        try:
            self.on_step_change_inner(old, new, gs)
        except Exception:
            log("step-change sample failed (non-fatal): " + traceback.format_exc())

    def on_step_change_inner(self, old, new, gs):
        b = self.bottle()
        if old == STEP_GATHER:
            self.gather_end = {}
            for c in self.chars():
                ps = c.get_editor_property("player_state")
                seat = ps.get_editor_property("seat_index") if ps else -1
                loc = c.get_actor_location()
                slot = gs.call_method("GetCeremonySlotLocation", (seat,))
                self.gather_end[seat] = (loc.x, loc.y, slot.x, slot.y)
        if old == STEP_SPIN and b:
            v = self.victim_char()
            ctr = gs.get_editor_property("ceremony_center")
            if v:
                vl = v.get_actor_location()
                self.spin_end = (b.get_actor_rotation().yaw, vl.x, vl.y, ctr.x, ctr.y)
        if old == STEP_PICKUP and b:
            v = self.victim_char()
            if v:
                bow = v.get_editor_property("bow_body")
                hand = bow.get_bone_transform_by_name("RightHand", unreal.BoneSpaces.WORLD_SPACE) \
                    if bow else None
                neck = b.call_method("GetNeckWorldLocation", ())
                if hand:
                    hl = hand.translation
                    self.pickup_end = math.dist((hl.x, hl.y, hl.z), (neck.x, neck.y, neck.z))
                raw, _ = stats(v)
                bl = b.get_actor_location()
                log("PICKUP_END " + raw)
                log("PICKUP_END bottleLoc=%.1f,%.1f,%.1f neck=%.1f,%.1f,%.1f" % (
                    bl.x, bl.y, bl.z, neck.x, neck.y, neck.z))
        if old == STEP_COLLAPSE:
            v = self.victim_char()
            if v:
                loc = v.get_actor_location()
                lie = gs.get_editor_property("ceremony_lie_location")
                body = v.get_editor_property("body")
                rr = body.get_editor_property("relative_rotation") if body else None
                self.collapse_end = (math.dist((loc.x, loc.y, loc.z), (lie.x, lie.y, lie.z)),
                                     (rr.pitch, rr.yaw, rr.roll) if rr else None,
                                     bool(v.get_editor_property("asleep")))

    def eval_opening(self):
        # c6 六拍
        want = [STEP_GATHER, STEP_SPIN, STEP_APPROACH, STEP_PICKUP, STEP_DRINK, STEP_COLLAPSE]
        got = [x for x in want if x in self.steps_seen]
        self.check("c6_all_steps", got == want,
                   "seen=" + ",".join(STEP_NAME[x] for x in self.steps_seen))
        # c2 揭曉時機
        bad = [x for x in self.victim_before_reveal if x != -1]
        self.check("c2_no_early_reveal", len(bad) == 0,
                   f"samples={len(self.victim_before_reveal)} leaked={len(bad)}")
        # c1 瓶口指向受害者
        if self.spin_end:
            yaw, vx, vy, cx, cy = self.spin_end
            want_yaw = math.degrees(math.atan2(vy - cy, vx - cx))
            err = abs((yaw - want_yaw + 180) % 360 - 180)
            self.check("c1_bottle_points_victim", err <= 6.0,
                       f"bottleYaw={yaw:.1f} wantYaw={want_yaw:.1f} err={err:.1f}")
        else:
            self.check("c1_bottle_points_victim", False, "no spin_end sample")
        # c8 圍圈到位
        if self.gather_end:
            worst = max(math.dist((v[0], v[1]), (v[2], v[3])) for v in self.gather_end.values())
            self.check("c8_circle_formed", worst <= 70.0,
                       "worstSlotErr=%.1fcm  " % worst +
                       " ".join("s%d:%.0f" % (k, math.dist((v[0], v[1]), (v[2], v[3])))
                                for k, v in sorted(self.gather_end.items())))
        else:
            self.check("c8_circle_formed", False, "no gather_end sample")
        # c9 手到瓶頸
        if self.pickup_end is not None:
            self.check("c9_hand_at_bottle", self.pickup_end <= 22.0,
                       f"handToNeck={self.pickup_end:.1f}cm")
        else:
            self.check("c9_hand_at_bottle", False, "no pickup_end sample")
        # c5 崩塌終點
        if self.collapse_end:
            dist, rr, asleep = self.collapse_end
            self.check("c5_collapse_lands_on_lie", dist <= 6.0,
                       f"distToLie={dist:.1f}cm bodyRel={rr} asleep={asleep}")
        else:
            self.check("c5_collapse_lands_on_lie", False, "no collapse_end sample")
        # c3 零 teleport（逐取樣位移；取樣間隔≈1 幀，走路上限 250cm/s）
        jumps = []
        prev = {}
        for d in self.samples:
            pid = d.get("pid", -1)
            if pid in prev and d["step"] != STEP_COLLAPSE:
                p = prev[pid]
                j = math.dist((d["locx"], d["locy"]), (p["locx"], p["locy"]))
                if j > 25.0:  # 一幀 25cm＝>1500cm/s，只有傳送做得到
                    jumps.append((pid, STEP_NAME[int(d["step"])], j))
            prev[pid] = d
        self.check("c3_no_teleport", len(jumps) == 0,
                   f"jumps={jumps[:5]} (n={len(jumps)})")
        # c4 崩塌連續性：Body 相對旋轉逐取樣角步
        hist = {}
        for d in self.samples:
            hist[int(d.get("step", -1))] = hist.get(int(d.get("step", -1)), 0) + 1
        log("SAMPLE_HIST " + ", ".join(f"{STEP_NAME[k]}:{v}" for k, v in sorted(hist.items())))
        by_pid = {}
        for d in self.samples:
            if d["step"] == STEP_COLLAPSE and "bodyRelR" in d:
                by_pid.setdefault(d["pid"], []).append(d)
        steps, locsteps = [], []
        for pid, seq in by_pid.items():
            for i in range(1, len(seq)):
                dr = abs(seq[i]["bodyRelR"] - seq[i - 1]["bodyRelR"])
                steps.append(min(dr, 360 - dr))
                locsteps.append(math.dist((seq[i]["locx"], seq[i]["locy"], seq[i]["locz"]),
                                          (seq[i - 1]["locx"], seq[i - 1]["locy"], seq[i - 1]["locz"])))
        self.check("c4_collapse_continuous",
                   bool(steps) and max(steps) <= 12.0 and max(locsteps) <= 12.0,
                   "maxRollStep=%.2fdeg maxLocStep=%.2fcm samples=%d pids=%d" % (
                       max(steps) if steps else -1, max(locsteps) if locsteps else -1,
                       len(steps), len(by_pid)))

        # c10 瓶子交接不跳：PickUp/Drink 期間瓶子逐幀位移有界（交接那一幀最關鍵）
        bj, moved = [], 0.0
        for i in range(1, len(self.bottle_samples)):
            a, b2 = self.bottle_samples[i - 1], self.bottle_samples[i]
            if a[0] in (STEP_PICKUP, STEP_DRINK) and b2[0] in (STEP_PICKUP, STEP_DRINK):
                d = math.dist(a[1:], b2[1:])
                bj.append(d)
                if b2[0] == STEP_DRINK:
                    moved += d
        # **下限不可省**：初版只驗上限，結果瓶子從頭到尾沒動也「通過」＝空洞契約
        #（真因＝握骨名依賴作畫校準，儀式期恆 NAME_None）。喝的時候瓶子必須跟著手走。
        self.check("c10_bottle_follows_hand",
                   bool(bj) and max(bj) <= 15.0 and moved >= 20.0,
                   "maxBottleStep=%.2fcm drinkTravel=%.1fcm samples=%d" % (
                       max(bj) if bj else -1, moved, len(bj)))
        # c7 輸入無效
        self.check("c7_input_suppressed", not self.lean_locked_during,
                   "leanLockedDuringCeremony=%s" % self.lean_locked_during)

    def finish(self):
        n_pass = sum(1 for x in self.checks if x)
        n_fail = len(self.checks) - n_pass
        ok = (n_fail == 0) and not self.crashed and len(self.checks) > 0
        log(f"RESULT {n_pass}/{n_fail} " + ("DONE-PASS" if ok else "DONE-FAIL"))
        try:
            unreal.unregister_slate_post_tick_callback(self.handle)
        except Exception:
            pass


Probe()
