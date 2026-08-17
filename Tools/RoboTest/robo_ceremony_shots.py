# 入睡儀式截圖矩陣（2026-08-16）：六拍 × 固定機位自查
# 機位＝既有的 DebugRoboViewFrom 鉤子（python 在 PIE 世界沒有 spawn API），
#   受害者設成 host（seat 0）⇒ 鏡頭恆盯著正在做動作的那個人。
# 拍點：Gather 中 / Spin 中 / Spin 末（瓶口指人）/ PickUp 中 / PickUp 末 /
#       Drink 中 / Collapse t≈0.3,0.6,0.9 / 入睡後
# 產出：Saved/Screenshots/WindowsEditor/cerem_*.png + Saved/robo_ceremony_shots.txt
import unreal, time, ctypes, re, traceback

OUT = r"C:\games\Unreal Engine\nice_ink\Saved\robo_ceremony_shots.txt"
LINES = []
STEP_NAME = ["None", "Gather", "Spin", "Approach", "PickUp", "Drink", "Collapse"]

CAM_LOC = (215.0, -195.0, 195.0)    # 固定機位：舞台西南側高位
CAM_LOOK = (430.0, 40.0, 55.0)      # 注視舞台中心（酒瓶所在）


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[SHOTS] " + str(msg))
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
    (1, 0.50, "01_gather"),
    (2, 0.35, "02_spin"),
    (2, 0.95, "03_spin_end"),
    (3, 0.80, "04_approach_end"),
    (4, 0.50, "05_pickup"),
    (4, 0.88, "06_pickup_end"),
    (5, 0.55, "07_drink"),
    (6, 0.28, "08_collapse_a"),
    (6, 0.60, "09_collapse_b"),
    (6, 0.85, "10_collapse_c"),
    (0, 0.00, "11_asleep"),
]


class Shots:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.cam = None
        self.plan_i = 0
        self.seen_collapse = False
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

    def host_char(self, w):
        for c in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NiceInkCharacter):
            ctl = c.get_controller()
            if ctl and ctl.is_local_player_controller():
                return c
        return None

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
                gm.set_editor_property("DebugForcedVictimSeat", 0)  # 受害者＝host＝停靠視口
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
            # 每 tick 重申機位（遊戲的演出鏡頭在相位切換時會搶一次）
            hc = self.host_char(w)
            if hc:
                hc.call_method("DebugRoboViewAt", CAM_LOC + CAM_LOOK)
            if self.plan_i >= len(PLAN):
                log("ALL SHOTS DONE")
                self.finish()
                return
            step = step_of(gs)
            t = gs.call_method("GetCeremonyAlpha", ())
            if step == 6:
                self.seen_collapse = True
            want_step, want_t, name = PLAN[self.plan_i]
            hit = (step == want_step and t >= want_t) if want_step != 0 else \
                  (self.seen_collapse and step == 0)
            if hit:
                unreal.SystemLibrary.execute_console_command(
                    w, f"HighResShot 1280x720 filename=cerem_{name}")
                log(f"SHOT {name} :: step={STEP_NAME[step]} t={t:.2f}")
                self.plan_i += 1
            elif self.elapsed() > 25:
                log(f"TIMEOUT waiting for {PLAN[self.plan_i][2]} (now step={STEP_NAME[step]} t={t:.2f})")
                self.plan_i += 1
                self.stage_t = time.monotonic()

    def finish(self):
        try:
            unreal.unregister_slate_post_tick_callback(self.handle)
        except Exception:
            pass
        log("DONE")


Shots()
