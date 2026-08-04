# 摺り足步態＋軟肉彈跳探針（2026-08-04 骨骼站姿改制的驗收儀器）
# 驗六契約：c1 顯示換軌（Body 隱/BowBody 顯）、c2 雙腳恆貼地、c3 撐地腳世界釘住、
#          c4 沉腰屈膝、c5 彈跳激勵（走路把肚子晃起來）、c6 停步收斂＋姿勢歸位。
# 之後側視相機連拍步態截圖矩陣（robo play 模式；HighResShot 只認聚焦視窗）。
# 產出：Saved/robo_gait_result.txt + Saved/Screenshots/WindowsEditor/gait_*.png
import unreal, time, ctypes, re, traceback

OUT = r"C:\games\Unreal Engine\nice_ink\Saved\robo_gait_result.txt"
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[GAIT] " + str(msg))
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


def focus_main_window():
    user32 = ctypes.windll.user32

    def enum_cb(h, _):
        buf = ctypes.create_unicode_buffer(256)
        user32.GetWindowTextW(h, buf, 256)
        if "NiceInk" in buf.value and "Unreal Editor" in buf.value:
            user32.SetForegroundWindow(h)
            return False
        return True
    WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    user32.EnumWindows(WNDENUMPROC(enum_cb), 0)


def stats(char):
    s = str(char.call_method("DebugRoboGaitStats", ()))
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
        self.rest = {}
        self.walk_samples = []
        self.post_peak_belly = 0.0
        self.checks = []
        self.shot_i = 0
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

    def host(self):
        w = get_world("UEDPIE_0")
        return find_char(w, self.host_pid) if w else None

    def walk_to_center(self, c, away=False, secs=1.3):
        # 朝（或背）房間中心走——方向由當下位置實算（不猜座位地形）
        loc = c.get_actor_location()
        dx, dy = -50.0 - loc.x, -25.0 - loc.y
        n = max((dx * dx + dy * dy) ** 0.5, 1.0)
        sgn = -1.0 if away else 1.0
        c.call_method("DebugRoboWalk", (sgn * dx / n, sgn * dy / n, secs))
        log(f"walk dir=({sgn * dx / n:.2f},{sgn * dy / n:.2f}) from ({loc.x:.0f},{loc.y:.0f})")

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
                # 背景節流殺手（陷阱年鑑）：失焦編輯器被壓到 3~6fps＝牆鐘×頻率敏感的
                # 探針全滅（首輪實錘：三個 moving 樣本 phase 完全相同＝巨量 dt 幀混疊）。
                # 屬性名要用原始 bThrottleCPUWhenNotForeground（snake 名 5.7 解析失敗）。
                eps = unreal.find_object(None, "/Script/UnrealEd.Default__EditorPerformanceSettings")
                if eps:
                    eps.set_editor_property("bThrottleCPUWhenNotForeground", False)
                    log("throttle disabled")
                unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
                self.advance("wait_pie")
        elif s == "wait_pie":
            server = get_world("UEDPIE_0")
            if server and unreal.GameplayStatics.get_game_mode(server):
                # 受害者踢到 seat1：主機 seat0 要當走路的模特＋攝影機視角
                unreal.GameplayStatics.get_game_mode(server).set_editor_property("DebugForcedVictimSeat", 1)
                self.advance("wait_drawing")
        elif s == "wait_drawing":
            server = get_world("UEDPIE_0")
            gs = unreal.GameplayStatics.get_game_state(server)
            if gs and str(gs.get_editor_property("CurrentPhase")).endswith("DRAWING: 3>"):
                victim_pid = gs.get_editor_property("VictimPlayerId")
                for c in unreal.GameplayStatics.get_all_actors_of_class(server, unreal.NiceInkCharacter):
                    pid = c.get_editor_property("player_state").get_editor_property("player_id")
                    if pid != victim_pid and c.get_editor_property("Body"):
                        # 主機本人（locally controlled）才吃 PollMove 注入
                        if c.is_player_controlled() and c.get_controller() and \
                           c.get_controller().is_local_player_controller():
                            self.host_pid = pid
                            break
                if self.host_pid is None:
                    log("FAIL: no locally controlled artist found")
                    self.finish()
                    return
                log(f"host artist pid={self.host_pid}")
                self.advance("rest_sample")
            elif self.elapsed() > 40.0:
                log("FAIL: drawing phase never came")
                self.finish()
        elif s == "rest_sample":
            if self.elapsed() < 2.0:
                return
            c = self.host()
            raw, d = stats(c)
            log("rest: " + raw)
            self.rest = d
            # c1 顯示換軌：站立時靜態 Body 隱形、骨骼 BowBody 可見
            self.check("c1_display_swap",
                       d.get("bodyVis") == 0.0 and d.get("bowVis") == 1.0 and d.get("stand") == 1.0,
                       f"bodyVis={d.get('bodyVis')} bowVis={d.get('bowVis')} stand={d.get('stand')}")
            self.walk_to_center(c)
            self.advance("walk_sample")
        elif s == "walk_sample":
            c = self.host()
            # 折返走（道場 X 域僅 ~440cm——直走必撞牆＝速度歸零假樣本）：
            # 每段重新朝/背房間中心走（方向由當下位置算，不猜地形）
            e = self.elapsed()
            if e >= 1.3 and not getattr(self, "leg2", False):
                self.leg2 = True
                self.walk_to_center(c, away=True)
            elif e >= 2.6 and not getattr(self, "leg3", False):
                self.leg3 = True
                self.walk_to_center(c)
            raw, d = stats(c)
            if d.get("speed", 0.0) > 60.0:
                self.walk_samples.append(d)
            # 無聲失敗自報告：每 0.5s 一行原始數據
            if e - getattr(self, "last_dump", -1.0) >= 0.5:
                self.last_dump = e
                log(f"walk t={e:.1f}: " + raw)
            if self.elapsed() >= 4.2:
                n = len(self.walk_samples)
                if n < 20:
                    self.check("c2_feet_grounded", False, f"samples={n} (too few — walk never ran?)")
                    self.finish()
                    return
                rest_foot = max(self.rest.get("footLz", 8.0), self.rest.get("footRz", 8.0))
                max_foot = max(max(x["footLz"], x["footRz"]) for x in self.walk_samples)
                # c2 貼地：走路全程雙腳踝骨高不超過 rest+3cm（構造保證的實測面）
                self.check("c2_feet_grounded", max_foot <= rest_foot + 3.0,
                           f"maxFootZ={max_foot:.1f} rest={rest_foot:.1f} samples={n}")
                # c3 撐地腳釘住：任一時刻較慢的那隻腳的世界速度 < 體速的一半（好樣本佔比≥75%）
                steady = [x for x in self.walk_samples if x["speed"] > 120.0 and
                          (x["footLspd"] > 0.0 or x["footRspd"] > 0.0)]
                good = sum(1 for x in steady
                           if min(x["footLspd"], x["footRspd"]) < 0.5 * x["speed"])
                frac = good / max(len(steady), 1)
                self.check("c3_support_pinned", len(steady) >= 10 and frac >= 0.75,
                           f"pinnedFrac={frac:.2f} steady={len(steady)}")
                # c4 沉腰：走路平均髖高比 rest 低 ≥ 5cm（GaitStanceDropCm=10 的一半以上）
                rest_hip = self.rest.get("hipsH", 0.0)
                walk_hip = sum(x["hipsH"] for x in self.walk_samples) / n
                self.check("c4_stance_drop", walk_hip <= rest_hip - 5.0,
                           f"restHip={rest_hip:.1f} walkHip={walk_hip:.1f}")
                # c5 彈跳激勵：走路中肚彈簧偏移峰值 ≥ 0.4cm（步點/起步的動量可見）
                max_belly = max(x["jBelly"] for x in self.walk_samples)
                max_butt = max(max(x["jBuL"], x["jBuR"]) for x in self.walk_samples)
                self.check("c5_jiggle_excited", max_belly >= 0.4,
                           f"maxBelly={max_belly:.2f} maxButt={max_butt:.2f}")
                # c7 防飽和：肚彈簧釘死鉗位（≥7.99）的樣本佔比 <50%——絕對速度阻尼的
                # 等速拖尾病（穩態 2ζv/ω 恆撞鉗位＝「被風吹住」非跳動）迴歸籠
                sat = sum(1 for x in self.walk_samples if x["jBelly"] >= 7.99)
                self.check("c7_not_saturated", sat / n < 0.5,
                           f"satFrac={sat / n:.2f}")
                self.advance("stop_sample")
        elif s == "stop_sample":
            c = self.host()
            raw, d = stats(c)
            if self.elapsed() < 1.0:
                self.post_peak_belly = max(self.post_peak_belly, d.get("jBelly", 0.0))
                return
            if self.elapsed() < 3.0:
                return
            log("post-stop: " + raw)
            # c6 停步收斂：3 秒後肚彈簧靜下來（<0.3cm）、髖回 rest（±2cm）、顯示不回退
            hips_back = abs(d.get("hipsH", 0.0) - self.rest.get("hipsH", 0.0)) <= 2.0
            self.check("c6_settle",
                       d.get("jBelly", 9.9) < 0.3 and hips_back and
                       d.get("bodyVis") == 0.0 and d.get("bowVis") == 1.0,
                       f"belly={d.get('jBelly'):.2f} hipsH={d.get('hipsH'):.1f} "
                       f"rest={self.rest.get('hipsH'):.1f} stopPeakBelly={self.post_peak_belly:.2f} "
                       f"bodyVis={d.get('bodyVis')}")
            # 側視截圖矩陣：走一段路連拍 4 張＋停步慣性 1 張
            focus_main_window()
            c.call_method("DebugRoboSideView", (True,))
            self.walk_to_center(c, secs=1.4)
            self.advance("shots")
        elif s == "shots":
            server = get_world("UEDPIE_0")
            c = self.host()
            # 折返（同 walk_sample 的撞牆對策）；鏡框內來回走
            if self.elapsed() >= 1.5 and not getattr(self, "shot_leg2", False):
                self.shot_leg2 = True
                self.walk_to_center(c, away=True, secs=1.4)
            # 0.9s 起每 0.35s 一張（跨步相位錯開）；3.2s＝停步後慣性擺
            plan = [0.9, 1.25, 1.6, 1.95, 3.2]
            if self.shot_i < len(plan) and self.elapsed() >= plan[self.shot_i]:
                unreal.SystemLibrary.execute_console_command(
                    server, f"HighResShot 1280x720 filename=gait_{self.shot_i}")
                self.shot_i += 1
            if self.shot_i >= len(plan) and self.elapsed() > 4.5:
                c = self.host()
                if c:
                    c.call_method("DebugRoboSideView", (False,))
                self.finish()

    def finish(self):
        n_pass = sum(1 for x in self.checks if x)
        n_fail = len(self.checks) - n_pass
        log(f"RESULT {n_pass}/{n_fail} " + ("DONE-PASS" if n_fail == 0 else "DONE-FAIL"))
        try:
            unreal.unregister_slate_post_tick_callback(self.handle)
        except Exception:
            pass


Probe()
