# 亮度量測：PIE 開場（Bottle Spin 前後皆可，趁沒人變成受害者、畫面乾淨）
# 把主機 pawn 移到室內機位往場中看 → HighResShot（檔名帶批次號）→ DONE
import unreal, time, os, traceback

OUT = r"C:\Users\willi\AppData\Local\Temp\claude\c--games-Unreal-Engine-nice-ink\b9e4a3cf-70ad-4c05-9233-e84425b9517b\scratchpad\robo_bright_result.txt"
BATCH_FILE = r"C:\Users\willi\AppData\Local\Temp\claude\c--games-Unreal-Engine-nice-ink\b9e4a3cf-70ad-4c05-9233-e84425b9517b\scratchpad\bright_batch.txt"
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[BRIGHT] " + str(msg))
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(LINES))


def get_world(tag):
    for w in unreal.ObjectIterator(unreal.World):
        if tag in w.get_path_name():
            return w
    return None


with open(BATCH_FILE) as f:
    BATCH = f.read().strip()


class Test:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.shot_t = None
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
            if server and unreal.GameplayStatics.get_player_pawn(server, 0):
                self.advance("settle")
            elif self.elapsed() > 90.0:
                log("FAIL: PIE never started")
                self.finish()
        elif s == "settle":
            if self.elapsed() < 3.0:
                return
            server = get_world("UEDPIE_0")
            host = unreal.GameplayStatics.get_player_pawn(server, 0)
            pc = unreal.GameplayStatics.get_player_controller(server, 0)
            cam = unreal.Vector(300.0, 400.0, 170.0)
            host.set_actor_location(cam, False, True)
            look = unreal.MathLibrary.find_look_at_rotation(
                unreal.Vector(cam.x, cam.y, cam.z + 62.0), unreal.Vector(-105.0, 41.0, 80.0))
            pc.set_control_rotation(look)
            unreal.SystemLibrary.execute_console_command(
                server, "HighResShot 1280x720 filename=bright_%s" % BATCH)
            log("shot requested: bright_%s" % BATCH)
            self.shot_t = time.monotonic()
            self.advance("wait_shot")
        elif s == "wait_shot":
            if self.elapsed() < 5.0:
                return
            log("DONE")
            self.finish()

    def finish(self):
        if self.handle:
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.handle = None
        with open(OUT, "w", encoding="utf-8") as f:
            f.write("\n".join(LINES))


_t = Test()
log("bright harness registered, batch=" + BATCH)
