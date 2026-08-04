# 摺り足步態＋軟肉彈跳探針（2026-08-04；二輪＝雙軌制/膝外開/防穿契約）
# 契約：c1 顯示換軌、c2 雙腳恆貼地、c3 撐地腳釘住（前走段）、c4 沉腰、
#      c5 彈跳激勵、c6 停步收斂、c7 防飽和、
#      c8 雙腳不越側帶（前/橫/斜/邊走邊轉全樣本）、c9 膝恆外開、c10 大腿骨段間距。
# 走法＝四段身體相對方向（前走/右橫移/斜走/邊走邊轉），每段先傳送回房中心。
# 截圖＝側視（前走）＋正面（橫移/轉身）＋停步慣性。
# 產出：Saved/robo_gait_result.txt + Saved/Screenshots/WindowsEditor/gait_*.png
import unreal, time, ctypes, re, math, traceback

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


CENTER = (-50.0, -25.0)
# (名稱, 身體相對方向 (fwdShare, rightShare), 邊走邊轉 deg/s)
LEGS = [
    ("fwd",  (1.0, 0.0),  0.0),
    ("lat",  (0.0, 1.0),  0.0),
    ("diag", (0.707, 0.707), 0.0),
    ("turn", (1.0, 0.0),  120.0),
]
LEG_SECS = 1.0
LEG_GAP = 0.9


class Probe:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.host_pid = None
        self.rest = {}
        self.samples = []          # (mode, dict)
        self.leg_i = -1
        self.leg_running = False
        self.turn_yaw0 = 0.0
        self.checks = []
        self.shot_i = 0
        self.shot_plan = []
        self.last_dump = -1.0
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

    def start_leg(self, c, leg):
        name, share, turn = leg
        loc = c.get_actor_location()
        c.set_actor_location(unreal.Vector(CENTER[0], CENTER[1], loc.z), False, False)
        yaw = c.get_actor_rotation().yaw
        self.turn_yaw0 = yaw
        r = math.radians(yaw)
        fwd = (math.cos(r), math.sin(r))
        # 身體右側＝yaw+90 方向（UE yaw 度、XY 平面）
        r90 = math.radians(yaw + 90.0)
        right = (math.cos(r90), math.sin(r90))
        dx = share[0] * fwd[0] + share[1] * right[0]
        dy = share[0] * fwd[1] + share[1] * right[1]
        n = max((dx * dx + dy * dy) ** 0.5, 1e-6)
        c.call_method("DebugRoboWalk", (dx / n, dy / n, LEG_SECS))
        log(f"leg {name}: yaw={yaw:.0f} dir=({dx / n:.2f},{dy / n:.2f}) turn={turn}")

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
                # 背景節流殺手（陷阱年鑑）：失焦編輯器 3~6fps＝頻率敏感探針全滅
                eps = unreal.find_object(None, "/Script/UnrealEd.Default__EditorPerformanceSettings")
                if eps:
                    eps.set_editor_property("bThrottleCPUWhenNotForeground", False)
                    log("throttle disabled")
                unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
                self.advance("wait_pie")
        elif s == "wait_pie":
            server = get_world("UEDPIE_0")
            if server and unreal.GameplayStatics.get_game_mode(server):
                unreal.GameplayStatics.get_game_mode(server).set_editor_property("DebugForcedVictimSeat", 1)
                self.advance("wait_drawing")
        elif s == "wait_drawing":
            server = get_world("UEDPIE_0")
            gs = unreal.GameplayStatics.get_game_state(server)
            if gs and str(gs.get_editor_property("CurrentPhase")).endswith("DRAWING: 3>"):
                victim_pid = gs.get_editor_property("VictimPlayerId")
                for c in unreal.GameplayStatics.get_all_actors_of_class(server, unreal.NiceInkCharacter):
                    pid = c.get_editor_property("player_state").get_editor_property("player_id")
                    if pid != victim_pid and c.is_player_controlled() and c.get_controller() and \
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
            self.check("c1_display_swap",
                       d.get("bodyVis") == 0.0 and d.get("bowVis") == 1.0 and d.get("stand") == 1.0,
                       f"bodyVis={d.get('bodyVis')} bowVis={d.get('bowVis')} stand={d.get('stand')}")
            self.leg_i = -1
            self.advance("legs")
        elif s == "legs":
            c = self.host()
            e = self.elapsed()
            if not self.leg_running:
                self.leg_i += 1
                if self.leg_i >= len(LEGS):
                    self.eval_contracts()
                    self.advance("stop_sample")
                    return
                self.start_leg(c, LEGS[self.leg_i])
                self.leg_running = True
                self.stage_t = time.monotonic()
                return
            name, share, turn = LEGS[self.leg_i]
            if turn > 0.0 and e < LEG_SECS:
                pc = c.get_controller()
                if pc:
                    # unreal.Rotator(roll, pitch, yaw)（陷阱年鑑參數順序）
                    pc.set_control_rotation(unreal.Rotator(0.0, 0.0, self.turn_yaw0 + turn * e))
            raw, d = stats(c)
            if d.get("speed", 0.0) > 120.0:
                self.samples.append((name, d))
            if e - self.last_dump >= 0.4:
                self.last_dump = e
                log(f"{name} t={e:.1f}: " + raw)
            if e >= LEG_SECS + LEG_GAP:
                self.leg_running = False
                self.last_dump = -1.0
        elif s == "stop_sample":
            c = self.host()
            raw, d = stats(c)
            if self.elapsed() < 3.0:
                return
            log("post-stop: " + raw)
            hips_back = abs(d.get("hipsH", 0.0) - self.rest.get("hipsH", 0.0)) <= 2.0
            self.check("c6_settle",
                       d.get("jBelly", 9.9) < 0.3 and hips_back and
                       d.get("bodyVis") == 0.0 and d.get("bowVis") == 1.0,
                       f"belly={d.get('jBelly'):.2f} hipsH={d.get('hipsH'):.1f} rest={self.rest.get('hipsH'):.1f}")
            focus_main_window()
            # 截圖全用南牆機位（唯一驗證過不撞旁觀角色的機位；房中心+前方 230=
            # 正好把相機塞進座位角色體內＝首輪 front 系全滅的教訓）。
            # 正面步態＝讓角色「面向鏡頭」再橫移/前走：faceYaw=朝南 -80°。
            # (名稱, 身體相對方向, 轉速, faceYaw or None, 快門秒點)
            self.shot_plan = [
                ("side_fwd", (1.0, 0.0), 0.0, None, [0.9, 1.35]),
                ("front_lat", (0.0, 1.0), 0.0, -80.0, [0.7, 1.1]),
                ("front_turn", (0.0, 1.0), 120.0, -80.0, [0.6, 1.0]),
                ("back_settle", (1.0, 0.0), 0.0, 100.0, [1.6]),
            ]
            self.shot_i = 0
            self.advance("shots_next")
        elif s == "shots_next":
            if self.shot_i >= len(self.shot_plan):
                c = self.host()
                if c:
                    c.call_method("DebugRoboSideView", (False,))
                self.finish()
                return
            c = self.host()
            name, share, turn, face_yaw, times = self.shot_plan[self.shot_i]
            loc = c.get_actor_location()
            c.set_actor_location(unreal.Vector(CENTER[0], CENTER[1], loc.z), False, False)
            if face_yaw is not None:
                pc = c.get_controller()
                if pc:
                    pc.set_control_rotation(unreal.Rotator(0.0, 0.0, face_yaw))
            yaw = c.get_actor_rotation().yaw if face_yaw is None else face_yaw
            self.turn_yaw0 = yaw
            c.call_method("DebugRoboViewFrom", (40.0, -240.0, 25.0))
            if share != (0.0, 0.0):
                r = math.radians(yaw)
                r90 = math.radians(yaw + 90.0)
                dx = share[0] * math.cos(r) + share[1] * math.cos(r90)
                dy = share[0] * math.sin(r) + share[1] * math.sin(r90)
                n = max((dx * dx + dy * dy) ** 0.5, 1e-6)
                c.call_method("DebugRoboWalk", (dx / n, dy / n, 1.6))
            self.shot_times = list(times)
            self.shot_name = name
            self.shot_turn = turn
            self.advance("shots_run")
        elif s == "shots_run":
            c = self.host()
            e = self.elapsed()
            if self.shot_turn > 0.0 and e < 1.6:
                pc = c.get_controller()
                if pc:
                    pc.set_control_rotation(unreal.Rotator(0.0, 0.0, self.turn_yaw0 + self.shot_turn * e))
            server = get_world("UEDPIE_0")
            if self.shot_times and e >= self.shot_times[0]:
                self.shot_times.pop(0)
                unreal.SystemLibrary.execute_console_command(
                    server, f"HighResShot 1280x720 filename=gait_{self.shot_name}_{len(self.shot_times)}")
            if not self.shot_times and e > 2.4:
                self.shot_i += 1
                self.advance("shots_next")

    def eval_contracts(self):
        n = len(self.samples)
        if n < 60:
            self.check("c2_feet_grounded", False, f"samples={n} (too few — walk never ran?)")
            return
        rest_foot = max(self.rest.get("footLz", 8.0), self.rest.get("footRz", 8.0))
        all_d = [d for _, d in self.samples]
        max_foot = max(max(x["footLz"], x["footRz"]) for x in all_d)
        self.check("c2_feet_grounded", max_foot <= rest_foot + 3.0,
                   f"maxFootZ={max_foot:.1f} rest={rest_foot:.1f} samples={n}")
        # c3 撐地腳釘住：只評前走段（雙軌守恆式在主軸成立；橫移段守恆部分讓位側帶鉗位）
        fwd = [d for m, d in self.samples if m == "fwd" and (d["footLspd"] > 0 or d["footRspd"] > 0)]
        good = sum(1 for x in fwd if min(x["footLspd"], x["footRspd"]) < 0.5 * x["speed"])
        frac = good / max(len(fwd), 1)
        self.check("c3_support_pinned", len(fwd) >= 10 and frac >= 0.7,
                   f"pinnedFrac={frac:.2f} fwdSamples={len(fwd)}")
        rest_hip = self.rest.get("hipsH", 0.0)
        walk_hip = sum(x["hipsH"] for x in all_d) / n
        self.check("c4_stance_drop", walk_hip <= rest_hip - 5.0,
                   f"restHip={rest_hip:.1f} walkHip={walk_hip:.1f}")
        max_belly = max(x["jBelly"] for x in all_d)
        self.check("c5_jiggle_excited", max_belly >= 0.4, f"maxBelly={max_belly:.2f}")
        sat = sum(1 for x in all_d if x["jBelly"] >= 7.99)
        self.check("c7_not_saturated", sat / n < 0.5, f"satFrac={sat / n:.2f}")
        # c8 雙腳不越側帶（全部走法全部樣本；帶下限 20−寫入容差 2）
        min_lx = min(x["footLx"] for x in all_d)
        max_rx = max(x["footRx"] for x in all_d)
        self.check("c8_no_cross", min_lx >= 17.0 and max_rx <= -17.0,
                   f"minFootLx={min_lx:.1f} maxFootRx={max_rx:.1f} (band>=20-2tol)")
        # c9 膝恆外開（rest 膝 X≈±58.7；馬步外弓角保留的量測面）
        min_klx = min(x["kneeLx"] for x in all_d)
        max_krx = max(x["kneeRx"] for x in all_d)
        self.check("c9_knees_out", min_klx >= 40.0 and max_krx <= -40.0,
                   f"minKneeLx={min_klx:.1f} maxKneeRx={max_krx:.1f} (rest≈±58.7)")
        # c10 大腿骨段間距（穿膜骨級代理；rest≈67）
        min_gap = min(x["thighGap"] for x in all_d)
        self.check("c10_thigh_gap", min_gap >= 30.0, f"minThighGap={min_gap:.1f} rest≈67")

    def finish(self):
        n_pass = sum(1 for x in self.checks if x)
        n_fail = len(self.checks) - n_pass
        log(f"RESULT {n_pass}/{n_fail} " + ("DONE-PASS" if n_fail == 0 else "DONE-FAIL"))
        try:
            unreal.unregister_slate_post_tick_callback(self.handle)
        except Exception:
            pass


Probe()
