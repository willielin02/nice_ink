# 腮紅/黑眼圈/舊刺青疊色的端到端實證（2026-09-02；user 連問兩次＝要證據不要理論）
# 場景：
#   R1 素皮＝淡檔紅亂掃（腮紅）
#   R2 素皮＝淡檔黑短弧（黑眼圈）
#   R3 碳黑舊刺青（畫黑→ConvertWorkToCarbon）＋另一作者在其上：
#      左半＝實檔紅（該蓋得住）、右半＝淡檔紅（該幾乎看不見）
# 產出：Saved/robo_blush_{marker,mist,tattoo}.png ＋ robo_blush_result.txt
import os
import time
import traceback

import unreal

OUT = "C:/games/Unreal Engine/nice_ink/Saved/robo_blush_result.txt"
os.makedirs(os.path.dirname(OUT), exist_ok=True)
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[BLUSH] " + str(msg))
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(LINES))


def get_world(tag):
    for w in unreal.ObjectIterator(unreal.World):
        if tag in w.get_path_name():
            return w
    return None


RED = unreal.LinearColor(0.855, 0.0144, 0.0722, 1.0)   # 調色盤紅（sRGB 238,32,77）
BLACK = unreal.LinearColor(0.0168, 0.0168, 0.0168, 1.0)
TIER_L, TIER_F = 77, 255
STEP = 0.0012


class Probe:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        perf = unreal.find_object(None, "/Script/UnrealEd.Default__EditorPerformanceSettings")
        for prop in ("throttle_cpu_when_not_foreground", "bThrottleCPUWhenNotForeground"):
            try:
                perf.set_editor_property(prop, False)
                break
            except Exception:
                pass
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    def tick(self, dt):
        try:
            self.step()
        except Exception:
            log("EXC:\n" + traceback.format_exc())
            self.finish()

    def step(self):
        s = self.stage
        if s == "boot":
            if time.monotonic() - self.t0 > 10.0:
                sub = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
                if sub:
                    sub.editor_request_begin_play()
                    self.stage = "wait_pie"
        elif s == "wait_pie":
            w = get_world("UEDPIE_0")
            if w and unreal.GameplayStatics.get_game_mode(w):
                self.stage = "run"
                self.stage_t = time.monotonic()
        elif s == "run":
            if time.monotonic() - self.stage_t < 3.0:
                return
            self.run()
            self.finish()

    def scribble(self, canvas, author, cx, cy, color, tier, n=40, rx=0.006, ry=0.004):
        """小範圍來回亂掃（模擬玩家塗腮紅）"""
        import math
        pts = []
        for i in range(n + 1):
            t = i / float(n)
            pts.append(unreal.Vector2D(cx + math.cos(t * math.pi * 7.0) * rx,
                                       cy + (t - 0.5) * 2.0 * ry))
        canvas.call_method("BeginStroke",
                           (author, color, pts[0], True, unreal.InkNeedle.SHADER, 255, tier))
        for p in pts[1:]:
            canvas.call_method("AddStrokePoint", (author, p, 255))
        canvas.call_method("EndStroke", (author,))

    def hstroke(self, canvas, author, x0, x1, y, color, tier):
        n = max(4, int(abs(x1 - x0) / STEP))
        canvas.call_method("BeginStroke",
                           (author, color, unreal.Vector2D(x0, y), True,
                            unreal.InkNeedle.SHADER, 255, tier))
        for i in range(1, n + 1):
            canvas.call_method("AddStrokePoint",
                               (author, unreal.Vector2D(x0 + (x1 - x0) * i / n, y), 255))
        canvas.call_method("EndStroke", (author,))

    def run(self):
        w = get_world("UEDPIE_0")
        chars = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NiceInkCharacter)
        log("chars=%d" % len(chars))
        if not chars:
            log("FAIL no characters")
            return
        canvas = chars[0].get_editor_property("InkCanvas")
        canvas.call_method("WashAllMarker", ())

        # R1 素皮腮紅：淡檔紅（x~0.435）
        self.scribble(canvas, 101, 0.435, 0.55, RED, TIER_L)
        # R2 素皮黑眼圈：淡檔黑（x~0.455）
        self.scribble(canvas, 101, 0.455, 0.55, BLACK, TIER_L, n=24, rx=0.004, ry=0.002)

        # R3 碳黑舊刺青：author 101 畫實黑塊 → 轉碳黑
        for k in range(7):
            self.hstroke(canvas, 101, 0.470, 0.492, 0.545 + k * 0.0018, BLACK, TIER_F)
        wid = canvas.call_method("GetActiveWorkId", (101,))
        log("carbon work id=%s" % wid)
        ok = canvas.call_method("ConvertWorkToCarbon", (wid,))
        log("ConvertWorkToCarbon=%s" % ok)

        # 另一作者在舊刺青上：左半實檔紅、右半淡檔紅（各三道，垂直跨過碳黑帶）
        for k in range(3):
            x = 0.4735 + k * 0.0022
            canvas.call_method("BeginStroke",
                               (202, RED, unreal.Vector2D(x, 0.541), True,
                                unreal.InkNeedle.SHADER, 255, TIER_F))
            for i in range(1, 13):
                canvas.call_method("AddStrokePoint",
                                   (202, unreal.Vector2D(x, 0.541 + i * 0.0015), 255))
            canvas.call_method("EndStroke", (202,))
        for k in range(3):
            x = 0.4835 + k * 0.0022
            canvas.call_method("BeginStroke",
                               (202, RED, unreal.Vector2D(x, 0.541), True,
                                unreal.InkNeedle.SHADER, 255, TIER_L))
            for i in range(1, 13):
                canvas.call_method("AddStrokePoint",
                                   (202, unreal.Vector2D(x, 0.541 + i * 0.0015), 255))
            canvas.call_method("EndStroke", (202,))

        canvas.call_method("ExportLayersToPng",
                           ("C:/games/Unreal Engine/nice_ink/Saved/robo_blush",))
        log("EXPORTED")
        log("DONE")

    def finish(self):
        log("HARNESS END")
        if self.handle:
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.handle = None


_p = Probe()
log("harness registered")
