# 沉睡外觀 A/B（2026-08-18）：儀式路徑 vs 舊傳送路徑，睡著後的**完整顯示狀態**逐項對賬。
# user 質問「沒有動為什麼我看到的是這樣」——這支就是去查的。
# 把 CEREMONY 改成 False 再跑一次，兩份輸出逐行 diff。
# 產出：Saved/robo_sleeppose_<on|off>.txt + Screenshots/WindowsEditor/sp_<on|off>_*.png
import unreal, time, ctypes, traceback

CEREMONY = True
TAG = "on" if CEREMONY else "off"
OUT = r"C:\games\Unreal Engine\nice_ink\Saved\robo_sleeppose_%s.txt" % TAG
LINES = []
CAM_LOC = (215.0, -195.0, 195.0)
CAM_LOOK = (430.0, 40.0, 55.0)
BONES = ["Hips", "Spine", "Spine1", "Spine2", "Neck", "Head",
         "LeftShoulder", "LeftArm", "LeftForeArm", "LeftHand",
         "RightShoulder", "RightArm", "RightForeArm", "RightHand",
         "LeftUpLeg", "LeftLeg", "LeftFoot", "RightUpLeg", "RightLeg", "RightFoot",
         "Jiggle_Belly", "Jiggle_Chest_L", "Jiggle_Chest_R", "Jiggle_Butt_L", "Jiggle_Butt_R"]


def log(m):
    LINES.append(str(m))
    unreal.log_warning("[SPAB] " + str(m))
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


class Probe:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    def adv(self, s):
        self.stage = s
        self.stage_t = time.monotonic()
        log("STAGE -> " + s)

    def el(self):
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

    def step(self):
        s = self.stage
        if s == "boot":
            if time.monotonic() - self.t0 > 8.0:
                eps = unreal.find_object(None, "/Script/UnrealEd.Default__EditorPerformanceSettings")
                if eps:
                    eps.set_editor_property("bThrottleCPUWhenNotForeground", False)
                unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
                self.adv("wait_pie")
        elif s == "wait_pie":
            w = get_world("UEDPIE_0")
            gm = unreal.GameplayStatics.get_game_mode(w) if w else None
            if gm:
                gm.set_editor_property("DebugForcedVictimSeat", 0)
                gm.set_editor_property("ceremony_enabled", CEREMONY)  # bool 去掉開頭的 b
                log("CEREMONY=%s" % CEREMONY)
                focus()
                self.adv("wait_sleep")
            elif self.el() > 90:
                log("FAIL no GM")
                self.finish()
        elif s == "wait_sleep":
            w = get_world("UEDPIE_0")
            v = self.victim(w) if w else None
            if v and v.get_editor_property("asleep"):
                self.adv("settle")
            elif self.el() > 120:
                log("FAIL never asleep")
                self.finish()
        elif s == "settle":
            w = get_world("UEDPIE_0")
            hc = self.host(w)
            if hc:
                hc.call_method("DebugRoboViewAt", CAM_LOC + CAM_LOOK)
            if self.el() > 4.0:
                self.dump(w)
                self.finish()

    def dump(self, w):
        v = self.victim(w)
        if not v:
            log("FAIL no victim")
            return
        a = v.get_actor_location()
        r = v.get_actor_rotation()
        log("ACTOR loc=(%.2f,%.2f,%.2f) rot=(p%.2f,y%.2f,r%.2f)" % (a.x, a.y, a.z, r.pitch, r.yaw, r.roll))
        for cname in ["body", "bow_body"]:
            c = v.get_editor_property(cname)
            if not c:
                log("%s: none" % cname)
                continue
            rl = c.get_editor_property("relative_location")
            rr = c.get_editor_property("relative_rotation")
            log("%s relLoc=(%.2f,%.2f,%.2f) relRot=(p%.2f,y%.2f,r%.2f) vis=%s" % (
                cname, rl.x, rl.y, rl.z, rr.pitch, rr.yaw, rr.roll, c.is_visible()))
        bow = v.get_editor_property("bow_body")
        if bow:
            for b in BONES:
                try:
                    t = bow.get_bone_transform_by_name(b, unreal.BoneSpaces.COMPONENT_SPACE)
                    p = t.translation
                    q = t.rotation.rotator()
                    log("BONE %-16s cs=(%8.2f,%8.2f,%8.2f) rot=(p%7.2f,y%7.2f,r%7.2f)" % (
                        b, p.x, p.y, p.z, q.pitch, q.yaw, q.roll))
                except Exception as e:
                    log("BONE %-16s ERR %s" % (b, e))
        log("GAIT " + str(v.call_method("DebugRoboGaitStats", ())))
        for i, name in enumerate(["top", "side"]):
            if name == "side":
                hc = self.host(w)
                if hc:
                    hc.call_method("DebugRoboViewAt", (430.0, -160.0, 60.0, 430.0, 40.0, 30.0))
            unreal.SystemLibrary.execute_console_command(
                w, "HighResShot 1280x720 filename=sp_%s_%s" % (TAG, name))
            log("SHOT sp_%s_%s" % (TAG, name))

    def finish(self):
        log("DONE")
        try:
            unreal.unregister_slate_post_tick_callback(self.handle)
        except Exception:
            pass


Probe()
