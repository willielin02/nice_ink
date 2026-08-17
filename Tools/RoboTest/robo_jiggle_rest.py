# 軟肉彈跳「停止後是否回到原位」探針（2026-08-18）
# user 回報：暫停一切動作時肥肉常定格在非原位。這支去量「停步 N 秒後，五顆
# Jiggle 骨的 CS 位置有沒有回到 pose 層的真實 rest」。
#
# 量測面刻意不是 LastSpringCm（既有 gait c6_settle 用的那個）——彈簧位移量在
# 「錨點被污染成偏移後的位置」時會讀成 0＝空洞契約（CLAUDE.md 除錯方法論 #6）。
# 真正的量測面＝骨頭 CS 位置 vs 關掉彈跳時的 pose 層 rest。
#
# 產出：Saved/robo_jiggle_rest.txt
import unreal, time, ctypes, re, traceback

OUT = r"C:\games\Unreal Engine\nice_ink\Saved\robo_jiggle_rest.txt"
LINES = []
BONES = ["Jiggle_Belly", "Jiggle_Chest_L", "Jiggle_Chest_R", "Jiggle_Butt_L", "Jiggle_Butt_R"]
CTRL = ["Hips", "Spine2"]          # 對照組：pose 層自己的骨，停步後必須逐位相同
WALK_SECS = 1.1
SETTLE_SECS = 5.0
CYCLES = 3


def log(m):
    LINES.append(str(m))
    unreal.log_warning("[JIG] " + str(m))
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(LINES))


def get_world(tag):
    for w in unreal.ObjectIterator(unreal.World):
        if tag in w.get_path_name():
            return w
    return None


def focus():
    u = ctypes.windll.user32

    def cb(h, _):
        b = ctypes.create_unicode_buffer(256)
        u.GetWindowTextW(h, b, 256)
        if "NiceInk" in b.value and "Unreal Editor" in b.value:
            u.SetForegroundWindow(h)
            return False
        return True
    P = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    u.EnumWindows(P(cb), 0)


def stats(c):
    s = str(c.call_method("DebugRoboGaitStats", ()))
    d = {}
    for k, v in re.findall(r"(\w+)=([-\d.]+)", s):
        try:
            d[k] = float(v)
        except ValueError:
            pass
    return s, d


class Probe:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.host_pid = None
        self.ref = {}
        self.cycle = 0
        self.series = []
        self.last_dump = -1.0
        self.checks = []
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    def adv(self, s):
        self.stage = s
        self.stage_t = time.monotonic()
        self.last_dump = -1.0
        log("STAGE -> " + s)

    def el(self):
        return time.monotonic() - self.stage_t

    def check(self, name, ok, detail):
        self.checks.append(bool(ok))
        log(("PASS " if ok else "FAIL ") + name + " :: " + detail)

    def tick(self, dt):
        try:
            self.step()
        except Exception:
            log("EXC:\n" + traceback.format_exc())
            self.finish()

    def host(self):
        w = get_world("UEDPIE_0")
        if not w:
            return None
        for c in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NiceInkCharacter):
            ps = c.get_editor_property("player_state")
            if ps and ps.get_editor_property("player_id") == self.host_pid:
                return c
        return None

    def bones(self, c):
        bow = c.get_editor_property("bow_body")
        out = {}
        for b in BONES + CTRL:
            t = bow.get_bone_transform_by_name(b, unreal.BoneSpaces.COMPONENT_SPACE)
            p = t.translation
            r = t.rotation.rotator()
            out[b] = (p.x, p.y, p.z, r.pitch, r.yaw, r.roll)
        return out

    def dev(self, cur, bone):
        a, b = cur[bone], self.ref[bone]
        d = ((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2) ** 0.5
        ang = max(abs(a[3] - b[3]), abs(a[4] - b[4]), abs(a[5] - b[5]))
        return d, ang

    def own_dev(self, cur, bone):
        """彈跳層自己貢獻的殘留＝該骨的位移**扣掉 pose 層共模位移**（用 Hips 當共模量測面）。
        姿勢層自己沒回到 rest 是另一個子系統的事，不能記在彈跳層帳上；反過來，舊 bug
        那種「Hips=0 但肚子歪 2.2cm」在這條下會原形畢露。"""
        a, b, h, hr = cur[bone], self.ref[bone], cur["Hips"], self.ref["Hips"]
        d = (((a[0] - b[0]) - (h[0] - hr[0])) ** 2 +
             ((a[1] - b[1]) - (h[1] - hr[1])) ** 2 +
             ((a[2] - b[2]) - (h[2] - hr[2])) ** 2) ** 0.5
        ang = max(abs(a[3] - b[3]), abs(a[4] - b[4]), abs(a[5] - b[5]))
        return d, ang

    def line(self, cur, d):
        parts = []
        for b in BONES:
            dd, aa = self.own_dev(cur, b)
            parts.append("%s=%.3fcm/%.2fdeg" % (b.replace("Jiggle_", ""), dd, aa))
        for b in CTRL:
            dd, _ = self.dev(cur, b)
            parts.append("%s=%.3f" % (b, dd))
        parts.append("spring[jBelly=%.2f jChL=%.2f jBuL=%.2f]" % (
            d.get("jBelly", -1), d.get("jChL", -1), d.get("jBuL", -1)))
        parts.append("speed=%.0f" % d.get("speed", -1))
        return " ".join(parts)

    def step(self):
        s = self.stage
        if s == "boot":
            if time.monotonic() - self.t0 > 8.0:
                eps = unreal.find_object(None, "/Script/UnrealEd.Default__EditorPerformanceSettings")
                if eps:
                    eps.set_editor_property("bThrottleCPUWhenNotForeground", False)
                    log("throttle disabled")
                unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
                self.adv("wait_pie")
        elif s == "wait_pie":
            w = get_world("UEDPIE_0")
            if w and unreal.GameplayStatics.get_game_mode(w):
                unreal.GameplayStatics.get_game_mode(w).set_editor_property("DebugForcedVictimSeat", 1)
                focus()
                self.adv("wait_drawing")
            elif self.el() > 90:
                log("FAIL no GM")
                self.finish()
        elif s == "wait_drawing":
            w = get_world("UEDPIE_0")
            gs = unreal.GameplayStatics.get_game_state(w) if w else None
            if gs and str(gs.get_editor_property("CurrentPhase")).endswith("DRAWING: 3>"):
                vid = gs.get_editor_property("victim_player_id")
                for c in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NiceInkCharacter):
                    ps = c.get_editor_property("player_state")
                    ctl = c.get_controller()
                    if ps and ps.get_editor_property("player_id") != vid and ctl and \
                       ctl.is_local_player_controller():
                        self.host_pid = ps.get_editor_property("player_id")
                        break
                if self.host_pid is None:
                    log("FAIL no local artist")
                    self.finish()
                    return
                log("host artist pid=%d" % self.host_pid)
                self.adv("idle_pre")
            elif self.el() > 120:
                log("FAIL drawing never came")
                self.finish()
        elif s == "idle_pre":
            if self.el() > 3.0:
                self.adv("ref_off")
        elif s == "ref_off":
            c = self.host()
            if self.el() < 0.05:
                c.set_editor_property("jiggle_enabled", False)   # 關閉＝pose 層 rest 露出來
                return
            if self.el() > 0.8:
                self.ref = self.bones(c)
                raw, d = stats(c)
                log("REF (jiggle OFF) " + raw)
                for b in BONES:
                    p = self.ref[b]
                    log("  ref %-16s cs=(%8.3f,%8.3f,%8.3f)" % (b, p[0], p[1], p[2]))
                c.set_editor_property("jiggle_enabled", True)
                self.adv("idle_post")
        elif s == "idle_post":
            # 開回來、完全不動 1.5s：這段的偏差就是「什麼都沒發生」的基線噪音
            c = self.host()
            if self.el() > 1.5:
                cur = self.bones(c)
                raw, d = stats(c)
                mx = max(self.dev(cur, b)[0] for b in BONES)
                log("baseline(no motion) " + self.line(cur, d))
                self.check("c0_idle_noise", mx < 0.05,
                           "maxDev=%.3fcm（沒有任何動作時應該逐位貼 rest）" % mx)
                self.cycle = 0
                self.adv("walk")
        elif s == "walk":
            c = self.host()
            if self.el() < 0.05:
                # 往復走：奇數圈反向，避免走出房間/撞人
                sign = 1.0 if (self.cycle % 2 == 0) else -1.0
                c.call_method("DebugRoboWalk", (sign * 1.0, 0.0, WALK_SECS))
                log("cycle %d: walk dir=%.0f" % (self.cycle, sign))
                return
            cur = self.bones(c)
            raw, d = stats(c)
            if self.el() - self.last_dump > 0.35:
                self.last_dump = self.el()
                log("  walk t=%.2f " % self.el() + self.line(cur, d))
            if self.el() > WALK_SECS + 0.05:
                self.series = []
                self.adv("settle")
        elif s == "settle":
            c = self.host()
            cur = self.bones(c)
            raw, d = stats(c)
            e = self.el()
            self.series.append((e, max(self.own_dev(cur, b)[0] for b in BONES),
                                max(self.own_dev(cur, b)[1] for b in BONES),
                                d.get("jBelly", -1)))
            if e - self.last_dump > 0.3:
                self.last_dump = e
                log("  settle t=%.2f " % e + self.line(cur, d))
            if e > SETTLE_SECS:
                mx = max(self.own_dev(cur, b)[0] for b in BONES)
                mxa = max(self.own_dev(cur, b)[1] for b in BONES)
                absmx = max(self.dev(cur, b)[0] for b in BONES)
                ctrl = max(self.dev(cur, b)[0] for b in CTRL)
                log("cycle %d FINAL " % self.cycle + self.line(cur, d))
                self.check("c%d_returns_to_rest" % (self.cycle + 1), mx < 0.02 and mxa < 0.02,
                           "停步 %.0fs 後彈跳層殘留 %.3fcm / %.3fdeg"
                           "（絕對位移 %.3fcm、其中 pose 層共模 %.3fcm）；彈簧讀數 jBelly=%.2f"
                           % (SETTLE_SECS, mx, mxa, absmx, ctrl, d.get("jBelly", -1)))
                # 上限之外也驗下限：這一段真的有被激勵過（空洞契約防呆）
                peak = max(x[1] for x in self.series)
                self.check("c%d_was_excited" % (self.cycle + 1), peak > 1.0,
                           "停步瞬間峰值殘留 %.2fcm（沒動過的話這條會 FAIL）" % peak)
                # 回到 rest 的時間＝殘留跌破 0.05cm 之後就沒再回頭
                rest_at = None
                for i, x in enumerate(self.series):
                    if x[1] < 0.05 and all(y[1] < 0.05 for y in self.series[i:]):
                        rest_at = x[0]
                        break
                log("cycle %d 回到 rest 的時間 t=%s（峰值 %.2fcm → <0.05cm）" % (
                    self.cycle, ("%.2fs" % rest_at) if rest_at is not None else "NEVER", peak))
                self.cycle += 1
                if self.cycle >= CYCLES:
                    self.adv("sleep_case")
                else:
                    self.adv("walk")
        elif s == "sleep_case":
            # 沉睡者：崩塌→睡著後 4s，躺著的身體有沒有停在非 rest
            w = get_world("UEDPIE_0")
            gs = unreal.GameplayStatics.get_game_state(w) if w else None
            vid = gs.get_editor_property("victim_player_id") if gs else -1
            vic = None
            for c in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NiceInkCharacter):
                ps = c.get_editor_property("player_state")
                if ps and ps.get_editor_property("player_id") == vid:
                    vic = c
                    break
            if vic and self.el() > 1.0:
                bow = vic.get_editor_property("bow_body")
                for b in BONES:
                    t = bow.get_bone_transform_by_name(b, unreal.BoneSpaces.COMPONENT_SPACE)
                    p = t.translation
                    log("victim %-16s cs=(%8.3f,%8.3f,%8.3f)" % (b, p.x, p.y, p.z))
                log("victim GAIT " + str(vic.call_method("DebugRoboGaitStats", ())))
                self.finish()
            elif self.el() > 6.0:
                log("(no victim found — skip sleep case)")
                self.finish()

    def finish(self):
        log("SUMMARY %d/%d PASS" % (sum(1 for x in self.checks if x), len(self.checks)))
        log("DONE")
        try:
            unreal.unregister_slate_post_tick_callback(self.handle)
        except Exception:
            pass


Probe()
