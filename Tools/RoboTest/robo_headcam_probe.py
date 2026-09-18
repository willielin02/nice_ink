# 站姿頭黏相機探針（2026-09-15）：本體相機是否真的每 tick 黏在 Head 骨眉心。
# 契約：c1 站姿相機啟用、c2 靜止時相機≡眉心（camBrowErr<0.05）、c3 靜止不抖（relZ p2p<0.3）、
#      c4 走路中相機≡眉心（黏合不是讀到上一幀）、c5 走路相機下沉（沉腰 10×stance）、
#      c6 走路相機沉浮 p2p≥0.3（步點 0.7×stance＝「真的有動」下限）、c7 走路橫擺 p2p≥3
#      （重心橫移 ±3.5）、c8 停步 3s 回到靜止高度（±0.5）。
# 走法＝先傳送回房中心，朝角色前方走 1.4s（道場 X 域 ~440cm，不准直走 >1.5s）。
# 產出：Saved/robo_headcam_result.txt
import unreal, time, re, traceback

OUT = r"C:\games\Unreal Engine\nice_ink\Saved\robo_headcam_result.txt"
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[HEADCAM] " + str(msg))
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


def stats(char):
    s = str(char.call_method("DebugRoboHeadCam", ()))
    d = {}
    for k, v in re.findall(r"(\w+)=([-\d.]+)", s):
        try:
            d[k] = float(v)
        except ValueError:
            pass
    return s, d


CENTER = (-50.0, -25.0)
LEG_SECS = 1.4


class Probe:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.host_pid = None
        self.rest = []
        self.walk = []
        self.checks = []
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
            c = self.host()
            e = self.elapsed()
            if e < 2.0:
                return
            raw, d = stats(c)
            if d.get("speed", 0.0) < 1.0:
                self.rest.append(d)
            if e - self.last_dump >= 0.5:
                self.last_dump = e
                log(f"rest t={e:.1f}: " + raw)
            if e >= 3.5:
                n = len(self.rest)
                if n < 20:
                    self.check("c1_active", False, f"rest samples={n} (too few)")
                    self.finish()
                    return
                act = all(x.get("active") == 1.0 for x in self.rest)
                self.check("c1_active", act, f"active on all {n} rest samples={act}")
                err = max(x["camBrowErr"] for x in self.rest)
                self.check("c2_rest_glued", err < 0.05, f"maxCamBrowErr={err:.3f}cm")
                zs = [x["relZ"] for x in self.rest]
                p2p = max(zs) - min(zs)
                self.check("c3_rest_still", p2p < 0.3, f"relZ p2p={p2p:.3f} mean={sum(zs) / n:.2f} (old capsule cam=64)")
                self.rest_z = sum(zs) / n
                self.rest_y = sum(x["relY"] for x in self.rest) / n
                loc = c.get_actor_location()
                c.set_actor_location(unreal.Vector(CENTER[0], CENTER[1], loc.z), False, False)
                yaw = c.get_actor_rotation().yaw
                import math
                r = math.radians(yaw)
                c.call_method("DebugRoboWalk", (math.cos(r), math.sin(r), LEG_SECS))
                log(f"walk: yaw={yaw:.0f} secs={LEG_SECS}")
                self.last_dump = -1.0
                self.advance("walk")
        elif s == "walk":
            c = self.host()
            e = self.elapsed()
            raw, d = stats(c)
            if d.get("speed", 0.0) > 120.0:
                self.walk.append(d)
            if e - self.last_dump >= 0.25:
                self.last_dump = e
                log(f"walk t={e:.2f}: " + raw)
            if e >= LEG_SECS + 0.1:
                n = len(self.walk)
                if n < 30:
                    self.check("c4_walk_glued", False, f"walk samples={n} (too few — walk never ran?)")
                    self.advance("stop_sample")
                    return
                err = max(x["camBrowErr"] for x in self.walk)
                self.check("c4_walk_glued", err < 0.05, f"maxCamBrowErr={err:.3f}cm samples={n}")
                steady = [x for x in self.walk if x.get("stance", 0.0) >= 0.95]
                zs = [x["relZ"] for x in (steady or self.walk)]
                ys = [x["relY"] for x in (steady or self.walk)]
                mean_z = sum(zs) / len(zs)
                self.check("c5_walk_drop", mean_z <= self.rest_z - 5.0,
                           f"restZ={self.rest_z:.2f} walkZ={mean_z:.2f} steady={len(steady)}")
                zp2p = max(zs) - min(zs)
                self.check("c6_walk_bob", zp2p >= 0.3, f"relZ p2p={zp2p:.3f} (bob 0.7×stance)")
                yp2p = max(ys) - min(ys)
                self.check("c7_walk_sway", yp2p >= 3.0,
                           f"relY p2p={yp2p:.3f} restY={self.rest_y:.2f} (weight shift ±3.5)")
                self.last_dump = -1.0
                self.advance("stop_sample")
        elif s == "stop_sample":
            c = self.host()
            e = self.elapsed()
            if e < 3.0:
                return
            raw, d = stats(c)
            log("post-stop: " + raw)
            self.check("c8_settle", abs(d["relZ"] - self.rest_z) < 0.5 and d["camBrowErr"] < 0.05,
                       f"relZ={d['relZ']:.2f} rest={self.rest_z:.2f} err={d['camBrowErr']:.3f}")
            # 設定開關（2026-09-15）：關＝相機退回膠囊掛點 (0,0,64)、走路不晃；再開＝回眉心。
            # 反向契約：不驗這段，「開關」就只是一個沒人看著的布林。
            gi = unreal.GameplayStatics.get_game_instance(get_world("UEDPIE_0"))
            gi.set_editor_property("bHeadBobEnabled", False)
            log("head bob -> OFF")
            self.off = []
            self.advance("off_rest")
        elif s == "off_rest":
            c = self.host()
            e = self.elapsed()
            if e < 0.5:
                return
            raw, d = stats(c)
            self.off.append(d)
            if e >= 1.5:
                n = len(self.off)
                act = all(x.get("active") == 0.0 for x in self.off)
                self.check("c9_off_inactive", act, f"active==0 on all {n} samples={act}")
                dz = max(abs(x["relZ"] - 64.0) for x in self.off)
                dx = max(abs(x["relX"]) for x in self.off)
                dy = max(abs(x["relY"]) for x in self.off)
                self.check("c10_off_capsule_mount", dz < 0.01 and dx < 0.01 and dy < 0.01,
                           f"max|relZ-64|={dz:.3f} max|relX|={dx:.3f} max|relY|={dy:.3f} (old mount 0,0,64)")
                log("off rest: " + raw)
                loc = c.get_actor_location()
                c.set_actor_location(unreal.Vector(CENTER[0], CENTER[1], loc.z), False, False)
                import math
                r = math.radians(c.get_actor_rotation().yaw)
                c.call_method("DebugRoboWalk", (math.cos(r), math.sin(r), LEG_SECS))
                self.off = []
                self.advance("off_walk")
        elif s == "off_walk":
            c = self.host()
            e = self.elapsed()
            raw, d = stats(c)
            if d.get("speed", 0.0) > 120.0:
                self.off.append(d)
            if e >= LEG_SECS + 0.1:
                n = len(self.off)
                if n < 30:
                    self.check("c11_off_no_bob", False, f"walk samples={n} (too few)")
                else:
                    zs = [x["relZ"] for x in self.off]
                    ys = [x["relY"] for x in self.off]
                    zp2p = max(zs) - min(zs)
                    yp2p = max(ys) - min(ys)
                    self.check("c11_off_no_bob", zp2p < 0.01 and yp2p < 0.01 and abs(sum(zs) / n - 64.0) < 0.01,
                               f"relZ p2p={zp2p:.3f} relY p2p={yp2p:.3f} meanZ={sum(zs) / n:.2f} samples={n}")
                log("off walk: " + raw)
                gi = unreal.GameplayStatics.get_game_instance(get_world("UEDPIE_0"))
                gi.set_editor_property("bHeadBobEnabled", True)
                log("head bob -> ON")
                self.advance("on_again")
        elif s == "on_again":
            c = self.host()
            e = self.elapsed()
            if e < 3.0:
                return
            raw, d = stats(c)
            log("on again: " + raw)
            self.check("c12_on_again", d["active"] == 1.0 and d["camBrowErr"] < 0.05 and abs(d["relZ"] - self.rest_z) < 0.5,
                       f"active={d['active']:.0f} err={d['camBrowErr']:.3f} relZ={d['relZ']:.2f} rest={self.rest_z:.2f}")
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
