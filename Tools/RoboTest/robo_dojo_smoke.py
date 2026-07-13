# 道場場景冒煙測試（L_Dojo 替換 L_Sauna 後的幾何驗證）
# 驗證：PIE 地圖=L_Dojo、回合推進到 Drawing（出生/選受害/入座全走通）、
#       全角色站位 z 合理（沒掉出地板、沒卡天花板）、席位落在道場包圍盒內。
# 產出：scratchpad/robo_dojo_smoke_result.txt + Saved/Screenshots 連拍
import unreal, time, os, traceback

OUT = r"C:\Users\willi\AppData\Local\Temp\claude\c--games-Unreal-Engine-nice-ink\b9e4a3cf-70ad-4c05-9233-e84425b9517b\scratchpad\robo_dojo_smoke_result.txt"
os.makedirs(os.path.dirname(OUT), exist_ok=True)
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[DOJOSMOKE] " + str(msg))
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(LINES))


def get_world(tag):
    for w in unreal.ObjectIterator(unreal.World):
        if tag in w.get_path_name():
            return w
    return None


def chars_of(w):
    return list(unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NiceInkCharacter))


def vec(x, y, z):
    return unreal.Vector(x, y, z)


class Test:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.shot_count = 0
        self.victim_pid = None
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
                unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
                self.advance("wait_pie")
        elif s == "wait_pie":
            server = get_world("UEDPIE_0")
            if server and unreal.GameplayStatics.get_game_mode(server):
                mapname = server.get_name()
                log(f"PIE map = {mapname} " + ("PASS" if "L_Dojo" in mapname else "FAIL: wrong map"))
                self.advance("wait_drawing")
            elif self.elapsed() > 90.0:
                log("FAIL: PIE never started")
                self.finish()
        elif s == "wait_drawing":
            server = get_world("UEDPIE_0")
            gs = unreal.GameplayStatics.get_game_state(server) if server else None
            if gs and str(gs.get_editor_property("CurrentPhase")).endswith("DRAWING: 3>"):
                self.victim_pid = gs.get_editor_property("VictimPlayerId")
                log(f"DRAWING phase reached, victim={self.victim_pid} PASS")
                self.advance("position_check")
            elif self.elapsed() > 90.0:
                log("FAIL: drawing phase never came (spawn/seating broken in dojo?)")
                self.finish()
        elif s == "position_check":
            if self.elapsed() < 1.5:
                return
            server = get_world("UEDPIE_0")
            ok = True
            for c in chars_of(server):
                pid = c.get_editor_property("player_state").get_editor_property("player_id")
                loc = c.get_actor_location()
                # 地板在 z=0；站姿 root 約 90–120、躺姿貼地也不會低於 -20 或高於 300
                good = -20.0 < loc.z < 300.0
                # 道場包圍盒（世界座標，含 20cm 裕度）
                good = good and (-1943.0 < loc.x < 1732.0) and (-787.0 < loc.y < 868.0)
                ok = ok and good
                log(f"pid={pid} loc=({loc.x:.1f},{loc.y:.1f},{loc.z:.1f})" + ("" if good else " <-- OUT OF DOJO"))
            log("POSITION CHECK: " + ("PASS" if ok else "FAIL"))
            self.advance("photos")
        elif s == "photos":
            if self.elapsed() < 2.0 + self.shot_count * 4.0:
                return
            server = get_world("UEDPIE_0")
            host = unreal.GameplayStatics.get_player_pawn(server, 0)
            pc = unreal.GameplayStatics.get_player_controller(server, 0)
            if host and pc:
                # 從房間四角俯瞰場地中心（桑拿舊中心=席位座標系原點附近）
                spots = ((700.0, 500.0, 250.0), (-900.0, -500.0, 250.0), (0.0, 700.0, 200.0), (-105.0, 41.0, 330.0))
                off = spots[self.shot_count % len(spots)]
                cam = vec(off[0], off[1], off[2])
                host.set_actor_location(cam, False, True)
                look = unreal.MathLibrary.find_look_at_rotation(
                    vec(cam.x, cam.y, cam.z + 62.0), vec(-105.0, 41.0, 60.0))
                pc.set_control_rotation(look)
                unreal.SystemLibrary.execute_console_command(
                    server, f"HighResShot 1280x720 filename=dojosmoke_{self.shot_count}")
            self.shot_count += 1
            if self.shot_count >= 4:
                log("photo burst done")
                log("DONE")
                self.finish()

    def finish(self):
        if self.handle:
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.handle = None
        with open(OUT, "w", encoding="utf-8") as f:
            f.write("\n".join(LINES))


_t = Test()
log("harness registered")
