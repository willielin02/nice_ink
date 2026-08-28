# 開場動畫截圖矩陣（2026-08-27）：五拍 × 實際導演鏡頭自查。
# 與 robo_ceremony_shots 不同：**不掛 DebugRoboViewAt**——要拍的就是 ViewIntro 的
# 分拍機位本身（拍別的機位＝驗了一個沒人會看到的畫面）。
# 需要 GameMode.bOpeningIntroForceInPIE=True（wait_pie 階段由本腳本翻旗）。
# 產出：Saved/Screenshots/WindowsEditor/intro_*.png + Saved/robo_intro_shots.txt
# 注意：主視窗＝host ⇒ Propose 那一拍拍到的是**反拍**（房主本人視角）；
# 標準腰上近景在 client 視窗（自查記帳；user viewport 四視窗全都看得到）。
import unreal, time, ctypes, re, traceback

OUT = r"C:\games\Unreal Engine\nice_ink\Saved\robo_intro_shots.txt"
LINES = []
STEP_NAME = ["None", "IntroSit", "IntroNotice", "IntroTvOff", "IntroPropose", "IntroRise",
             "Gather", "Spin", "Approach", "PickUp", "Drink", "Collapse"]


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[INTRO] " + str(msg))
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

    def enum_cb(h, _):
        buf = ctypes.create_unicode_buffer(256)
        user32.GetWindowTextW(h, buf, 256)
        if "NiceInk" in buf.value and "Unreal Editor" in buf.value:
            user32.SetForegroundWindow(h)
            return False
        return True
    WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    user32.EnumWindows(WNDENUMPROC(enum_cb), 0)


# (step, t 下限, 檔名)
PLAN = [
    (1, 0.30, "01_sit_wide"),
    (1, 0.85, "02_sit_wide_push"),
    (2, 0.50, "03_tv_closeup"),
    (3, 0.30, "04_tv_off_line"),
    (3, 0.80, "05_tv_off_end"),
    (4, 0.55, "06_propose"),
    (5, 0.50, "07_rise_wide"),
    (6, 0.50, "08_gather_handoff"),
    # 2026-08-28 user 回報「轉酒瓶時畫面區塊暫時變黑」：轉瓶段連拍抓瞬態
    (7, 0.05, "09_spin_a"),
    (7, 0.15, "10_spin_b"),
    (7, 0.25, "11_spin_c"),
    (7, 0.35, "12_spin_d"),
    (7, 0.45, "13_spin_e"),
    (7, 0.55, "14_spin_f"),
    (7, 0.65, "15_spin_g"),
    (7, 0.75, "16_spin_h"),
    (7, 0.85, "17_spin_i"),
    (7, 0.95, "18_spin_j"),
]


class Shots:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.plan_i = 0
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
                gm.set_editor_property("bOpeningIntroForceInPIE", True)
                gm.set_editor_property("DebugForcedVictimSeat", 0)
                focus_main_window()
                self.advance("shoot")
            elif self.elapsed() > 25:
                log("FAIL no GM")
                self.finish()
        elif s == "shoot":
            w = get_world("UEDPIE_0")
            gs = unreal.GameplayStatics.get_game_state(w) if w else None
            if not gs:
                return
            if self.plan_i >= len(PLAN):
                log("ALL SHOTS DONE")
                self.finish()
                return
            step = step_of(gs)
            t = gs.call_method("GetCeremonyAlpha", ())
            want_step, want_t, name = PLAN[self.plan_i]
            if step == want_step and t >= want_t:
                unreal.SystemLibrary.execute_console_command(
                    w, f"HighResShot 1280x720 filename=intro_{name}")
                log(f"SHOT {name} :: step={STEP_NAME[step]} t={t:.2f}")
                self.plan_i += 1
            elif self.elapsed() > 30:
                log(f"TIMEOUT waiting {PLAN[self.plan_i][2]} (now step={STEP_NAME[step]} t={t:.2f})")
                self.plan_i += 1
                self.stage_t = time.monotonic()

    def finish(self):
        try:
            unreal.unregister_slate_post_tick_callback(self.handle)
        except Exception:
            pass
        log("DONE")


Shots()
