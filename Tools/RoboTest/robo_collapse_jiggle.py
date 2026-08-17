# 崩塌期軟肉彈簧診斷（2026-08-16；user 抓「倒下時肚子浮誇地形變」）
# 假說：翻倒＝整具網格在世界空間繞 ~90° 高速掃掠，而 jiggle 是**世界空間彈簧**
#       ⇒ 遠離樞軸的肚骨錨點被甩出巨大速度＝激勵遠超步行調校值，撞上 JiggleMaxCm
#       鉗位（切向分量再換成繞脊椎最多 25° 的旋轉＝大面積蒙皮形變）。
#       舊制看不到是因為它**傳送**落地——彈簧有「單幀 >100cm 直接貼齊」的傳送保護。
# 量：Drink→Collapse→入睡後 +6s 逐 tick 取 jBelly；A/B 截圖（jiggle 開 vs 關）。
# 產出：Saved/robo_collapse_jiggle.txt + Screenshots/WindowsEditor/jig_*.png
import unreal, time, ctypes, re, traceback

OUT = r"C:\games\Unreal Engine\nice_ink\Saved\robo_collapse_jiggle.txt"
LINES = []
CAM_LOC = (215.0, -195.0, 195.0)
CAM_LOOK = (430.0, 40.0, 55.0)


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[JIG] " + str(msg))
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(LINES))


def get_world(tag):
    for w in unreal.ObjectIterator(unreal.World):
        if tag in w.get_path_name():
            return w
    return None


def step_of(gs):
    v = gs.get_editor_property("ceremony_step")
    try:
        return int(v.value)
    except Exception:
        m = re.search(r"(\d+)", str(v))
        return int(m.group(1)) if m else 0


def focus_main_window():
    user32 = ctypes.windll.user32

    def cb(h, _):
        buf = ctypes.create_unicode_buffer(256)
        user32.GetWindowTextW(h, buf, 256)
        if "NiceInk" in buf.value and "Unreal Editor" in buf.value:
            user32.SetForegroundWindow(h)
            return False
        return True
    P = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    user32.EnumWindows(P(cb), 0)


def gait(char):
    s = str(char.call_method("DebugRoboGaitStats", ()))
    d = {}
    for k, v in re.findall(r"(\w+)=([-\d.]+)", s):
        try:
            d[k] = float(v)
        except ValueError:
            pass
    return d


class Probe:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.samples = []       # (step, t_since_collapse_start, jBelly)
        self.collapse_t0 = None
        self.shots = 0
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    def advance(self, st):
        self.stage = st
        self.stage_t = time.monotonic()
        log("STAGE -> " + st)

    def elapsed(self):
        return time.monotonic() - self.stage_t

    def tick(self, dt):
        try:
            self.step()
        except Exception:
            log("EXC:\n" + traceback.format_exc())
            self.finish()

    def victim(self, w):
        gs = unreal.GameplayStatics.get_game_state(w)
        vid = gs.get_editor_property("victim_player_id") if gs else -1
        for c in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NiceInkCharacter):
            ps = c.get_editor_property("player_state")
            if ps and ps.get_editor_property("player_id") == vid:
                return c
        return None

    def host(self, w):
        for c in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NiceInkCharacter):
            ctl = c.get_controller()
            if ctl and ctl.is_local_player_controller():
                return c
        return None

    def shot(self, w, name):
        unreal.SystemLibrary.execute_console_command(w, f"HighResShot 1280x720 filename=jig_{name}")
        log("SHOT " + name)

    def step(self):
        s = self.stage
        if s == "boot":
            if time.monotonic() - self.t0 > 8.0:
                eps = unreal.find_object(None, "/Script/UnrealEd.Default__EditorPerformanceSettings")
                if eps:
                    eps.set_editor_property("bThrottleCPUWhenNotForeground", False)
                unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
                self.advance("wait_pie")
        elif s == "wait_pie":
            w = get_world("UEDPIE_0")
            gm = unreal.GameplayStatics.get_game_mode(w) if w else None
            if gm:
                gm.set_editor_property("DebugForcedVictimSeat", 0)  # 受害者=host=停靠視口
                focus_main_window()
                self.advance("watch")
            elif self.elapsed() > 90:
                log("FAIL no GM")
                self.finish()
        elif s == "watch":
            w = get_world("UEDPIE_0")
            gs = unreal.GameplayStatics.get_game_state(w) if w else None
            if not gs:
                return
            hc = self.host(w)
            if hc:
                hc.call_method("DebugRoboViewAt", CAM_LOC + CAM_LOOK)
            st = step_of(gs)
            v = self.victim(w)
            if st == 6 and self.collapse_t0 is None:
                self.collapse_t0 = time.monotonic()
                log("collapse begin")
            if self.collapse_t0 is not None and v:
                dt2 = time.monotonic() - self.collapse_t0
                d = gait(v)
                self.samples.append((st, dt2, d.get("jBelly", -1.0)))
                # 截圖：崩塌中 / 崩塌末 / +1.5s / +4s
                plan = [(0.55, "a_collapse_mid"), (1.05, "b_collapse_end"),
                        (2.5, "c_after_1p5s"), (5.0, "d_after_4s")]
                if self.shots < len(plan) and dt2 >= plan[self.shots][0]:
                    self.shot(w, plan[self.shots][1])
                    self.shots += 1
                if dt2 > 6.5:
                    self.report()
                    # A/B：關掉 jiggle 再拍一張（同機位同姿勢）
                    v.set_editor_property("jiggle_enabled", False)
                    log("jiggle disabled for A/B")
                    self.advance("ab")
            elif self.elapsed() > 150:
                log("FAIL: collapse never observed")
                self.finish()
        elif s == "ab":
            w = get_world("UEDPIE_0")
            hc = self.host(w)
            if hc:
                hc.call_method("DebugRoboViewAt", CAM_LOC + CAM_LOOK)
            if self.elapsed() > 1.2:
                self.shot(w, "e_jiggle_off")
                v = self.victim(w)
                if v:
                    log("jBelly with jiggle off = %.2f" % gait(v).get("jBelly", -1.0))
                self.finish()

    def report(self):
        col = [x for x in self.samples if x[0] == 6]
        aft = [x for x in self.samples if x[0] != 6]
        def stat(rows, label):
            if not rows:
                log(f"{label}: (no samples)")
                return
            vals = [r[2] for r in rows]
            log("%s: n=%d max=%.2f mean=%.2f last=%.2f" % (
                label, len(vals), max(vals), sum(vals) / len(vals), vals[-1]))
        stat(col, "jBelly during COLLAPSE")
        stat(aft, "jBelly AFTER (asleep)")
        # 衰減曲線（每 0.5s 取一點）
        marks = []
        for target in [0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 5.0, 6.0]:
            best = min(self.samples, key=lambda r: abs(r[1] - target), default=None)
            if best:
                marks.append("t+%.1f=%.2f" % (target, best[2]))
        log("jBelly curve: " + "  ".join(marks))
        log("NOTE JiggleMaxCm 是鉗位；貼到鉗位＝彈簧飽和＝繞脊椎轉角撞 25° 上限")

    def finish(self):
        log("DONE")
        try:
            unreal.unregister_slate_post_tick_callback(self.handle)
        except Exception:
            pass


Probe()
