# L_Dojo 燈光截圖（編輯器 viewport，非 PIE）：三個機位拍完自動關編輯器
import unreal, time, os, traceback

OUT = r"C:\Users\willi\AppData\Local\Temp\claude\c--games-Unreal-Engine-nice-ink\b9e4a3cf-70ad-4c05-9233-e84425b9517b\scratchpad\dojo_light_shots_result.txt"
LINES = []

def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[LIGHTSHOT] " + str(msg))
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(LINES))

SHOTS = [
    ("dojolights_hall", unreal.Vector(-260.0, 40.0, 170.0), unreal.Rotator(0.0, 12.0, 0.0)),
    ("dojolights_seats", unreal.Vector(950.0, 400.0, 220.0), unreal.Rotator(0.0, -8.0, -160.0)),
    ("dojolights_ceiling", unreal.Vector(435.0, -178.0, 130.0), unreal.Rotator(0.0, 55.0, 90.0)),
]

class Harness:
    def __init__(self):
        self.t0 = time.monotonic()
        self.idx = -1
        self.task = None
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    def tick(self, dt):
        try:
            self.step()
        except Exception:
            log("EXC:\n" + traceback.format_exc())
            self.finish(quit_now=True)

    def step(self):
        t = time.monotonic() - self.t0
        if t < 15.0 + (self.idx + 1) * 5.0:
            return
        self.idx += 1
        if self.idx >= len(SHOTS):
            log("DONE")
            self.finish(quit_now=True)
            return
        name, loc, rot = SHOTS[self.idx]
        unreal.UnrealEditorSubsystem().set_level_viewport_camera_info(loc, rot)
        unreal.AutomationLibrary.take_high_res_screenshot(1280, 720, name + ".png")
        log("shot %d requested: %s" % (self.idx, name))

    def finish(self, quit_now=False):
        if self.handle:
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.handle = None
        with open(OUT, "w", encoding="utf-8") as f:
            f.write("\n".join(LINES))
        if quit_now:
            unreal.SystemLibrary.quit_editor()

_h = Harness()
log("light-shot harness registered")
