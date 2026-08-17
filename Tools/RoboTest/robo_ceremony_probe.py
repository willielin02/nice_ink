# 開場儀式場地探針（2026-08-16；施工前量測——陷阱年鑑「換位置先跑地板探針」）
# 目的：決定圍圈的圈心與半徑，不猜。
#   - 對候選圈心逐環（120~220cm）每 15° 打地板射線＋真膠囊（42×92）淨空測試
#   - 取樣各席位 → 圈上角位的直線路徑（會不會掉出世界／撞死）
# python 讀不到 5.7 的 HitResult 反射 ⇒ 探測本體在 C++（GameMode::DebugCeremonyProbe）
# 產出：Saved/robo_ceremony_probe.txt
import unreal, time, traceback

OUT = r"C:\games\Unreal Engine\nice_ink\Saved\robo_ceremony_probe.txt"
LINES = []

# 候選圈心：躺位（首選＝崩塌零位移修正）＋三個房間中心候選
CENTERS = [
    ("lie",      0.0,  75.0),
    ("mid",      0.0,   0.0),
    ("gaitctr", -50.0, -25.0),
    ("west",   -60.0,  25.0),
]


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


class Probe:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    def advance(self, stage):
        self.stage = stage
        self.stage_t = time.monotonic()
        log("STAGE -> " + stage)

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
                    log("throttle disabled")
                unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
                self.advance("wait_pie")
        elif s == "wait_pie":
            w = get_world("UEDPIE_0")
            if w and unreal.GameplayStatics.get_game_mode(w):
                self.advance("settle")
            elif self.elapsed() > 90:
                log("FAIL: no PIE server world")
                self.finish()
        elif s == "settle":
            if self.elapsed() > 3.0:
                self.advance("probe")
        elif s == "probe":
            w = get_world("UEDPIE_0")
            gm = unreal.GameplayStatics.get_game_mode(w)
            # 席位表（探針輸出也含，這裡先印一次供對照）
            try:
                seats = gm.get_editor_property("SeatSpots")
                log("SEATS " + ", ".join(f"({v.x:.0f},{v.y:.0f})" for v in seats))
            except Exception as e:
                log("seats read failed: " + str(e))
            # 房間中心（user 定案「整個遊戲都在房間的正中央進行」）＝先量最大內接淨空圓
            try:
                res = gm.debug_room_center_probe()
            except Exception:
                res = gm.call_method("DebugRoomCenterProbe", ())
            for line in str(res).splitlines():
                log(line)
            # 直接問關卡：所有靜態部件的名字與包圍盒（射線/overlap 都會被場景結構誤導，
            # 部件包圍盒是唯一不會騙人的來源）
            log("===== DOJO PARTS (name / center / extent) =====")
            rows = []
            for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.StaticMeshActor):
                try:
                    org, ext = a.get_actor_bounds(False)
                    comp = a.static_mesh_component
                    mesh = comp.static_mesh if comp else None
                    rows.append((a.get_name(), str(mesh.get_name()) if mesh else "?",
                                 org.x, org.y, org.z, ext.x, ext.y, ext.z))
                except Exception as e:
                    log("bounds fail %s: %s" % (a.get_name(), e))
            for r in sorted(rows, key=lambda t: t[0]):
                log("PART %-22s mesh=%-28s c=(%7.0f,%7.0f,%7.0f) e=(%6.0f,%6.0f,%6.0f)" % r)
            for name, cx, cy in CENTERS:
                log(f"===== CENTER {name} ({cx:.0f},{cy:.0f}) =====")
                try:
                    res = gm.debug_ceremony_probe(cx, cy)
                except Exception:
                    res = gm.call_method("DebugCeremonyProbe", (cx, cy))
                for line in str(res).split("\n"):
                    log(line)
            log("DONE")
            self.finish()

    def finish(self):
        try:
            unreal.unregister_slate_post_tick_callback(self.handle)
        except Exception:
            pass


Probe()
