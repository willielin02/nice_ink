# 稿線擋墨（2026-09-01；user 定案「稿線變成擋墨的牆」）的契約
#
# 規則：填色（Shader）掃到**自己畫的**稿線就停；別人的稿線不擋（無圍城騷擾）；
# 而且裁切結果必須存進筆劃 ⇒ **稿線被洗掉之後重建，填色不可以膨脹回去**
#（稿線在 EnterTour 全洗，那正是巡禮＝遊戲高潮那一刻）。
#
# 這支不跑遊戲流程，直接驅動畫布 API（BeginStroke/AddStrokePoint 都是
# BlueprintCallable）＝快、決定性、與相位機無關。
#
# 契約：
#   c1 沒有稿線時，填色跨得過中線（基準：確認掃描本身有覆蓋到兩側）
#   c2 有**自己的**稿線時，稿線另一側的墨顯著減少（牆生效）
#   c3 稿線是**別人**畫的時候不擋（與 c1 同量級）
#   c4 洗掉稿線並重建之後，c2 的裁切**維持住**（牆已存進筆劃）
#   c5 割線（Liner）不受牆影響（牆只管 Shader）
#
# 產出：Saved/robo_stencilwall_result.txt
import os
import time
import traceback

import unreal

OUT = "C:/games/Unreal Engine/nice_ink/Saved/robo_stencilwall_result.txt"
os.makedirs(os.path.dirname(OUT), exist_ok=True)
LINES = []
PASS_N = [0]
FAIL_N = [0]


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[STENCILWALL] " + str(msg))
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(LINES))


def check(name, cond, detail=""):
    if cond:
        PASS_N[0] += 1
        log("PASS %s | %s" % (name, detail))
    else:
        FAIL_N[0] += 1
        log("FAIL %s | %s" % (name, detail))


def get_world(tag):
    for w in unreal.ObjectIterator(unreal.World):
        if tag in w.get_path_name():
            return w
    return None


# 幾何（**牆要橫過墨帶才擋得到**：帶垂直於行進方向）：
#   掃描沿 +X ⇒ 墨帶往 ±Y 延伸（半寬 ShaderRowHalfWidthUv=0.00301）
#   稿線畫成**水平線**（固定 Y、沿 X），落在帶心上方 WALL_DY ⇒ 應該擋住上半邊
CX, CY = 0.50, 0.55
STEP = 0.0012              # 掃描每點 UV 步長（~0.4cm）
NSTEP = 22                 # 左右各掃幾步
WALL_DY = 0.0015           # 稿線離帶心的距離（半寬的一半）
WALL_Y = CY + WALL_DY


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

    def advance(self, stage):
        self.stage = stage
        self.stage_t = time.monotonic()

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
            if time.monotonic() - self.t0 > 10.0:
                sub = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
                if sub:
                    sub.editor_request_begin_play()
                    self.advance("wait_pie")
        elif s == "wait_pie":
            w = get_world("UEDPIE_0")
            if w and unreal.GameplayStatics.get_game_mode(w):
                self.advance("run")
        elif s == "run":
            if self.elapsed() < 3.0:
                return
            self.run()
            self.finish()

    # --- 畫布操作 ---

    def stencil_line(self, canvas, author):
        """水平稿線（點刺，與玩家路徑同構）——橫過墨帶＝真的能當牆"""
        purple = unreal.LinearColor(0.165, 0.034, 0.283, 1.0)
        x0 = CX - STEP * NSTEP * 1.5
        canvas.call_method("BeginStroke",
                           (author, purple, unreal.Vector2D(x0, WALL_Y), True,
                            unreal.InkNeedle.STENCIL, 255))
        for i in range(1, 3 * NSTEP + 1):
            canvas.call_method("AddStrokePoint",
                               (author, unreal.Vector2D(x0 + i * STEP, WALL_Y), 255))
        canvas.call_method("EndStroke", (author,))

    def curvy(self, canvas, author):
        """S 形彎曲掃描（無稿線）——梳齒只在**轉彎**處出現，直線掃測不到。
        章距 0.2cm＝與遊戲的 ShaderStampSpacingCm 同值。"""
        import math
        black = unreal.LinearColor(0.0168, 0.0168, 0.0168, 1.0)
        n = 160
        def pt(i):
            t = i / float(n)
            return unreal.Vector2D(CX - 0.030 + t * 0.060,
                                   CY + math.sin(t * math.pi * 2.2) * 0.012)
        canvas.call_method("BeginStroke",
                           (author, black, pt(0), True, unreal.InkNeedle.SHADER, 255))
        for i in range(1, n + 1):
            canvas.call_method("AddStrokePoint", (author, pt(i), 255))
        canvas.call_method("EndStroke", (author,))

    def comb(self, canvas, author):
        """正面衝牆的填色梳（09-02 v2 契約）：多條垂直短掃、由下往上、停在牆前。
        v1 的削章模型只會把章「橫向夾窄」且只查行進方向 ±0.3R 的窗 ⇒ 正面衝過去
        的圓章前半整枚壓過線＝user 截圖的放射白刺；既有 sweep 與牆**平行**＝
        構造上測不到這個病。這個案例就是它的最小重現。"""
        black = unreal.LinearColor(0.0168, 0.0168, 0.0168, 1.0)
        y0 = CY - 0.012
        y1 = WALL_Y - 0.0004   # 停在牆前（章半徑 0.00301 的圓會想越過去）
        for k in range(9):
            x = CX - 0.008 + k * 0.002
            n = 14
            canvas.call_method("BeginStroke",
                               (author, black, unreal.Vector2D(x, y0), True,
                                unreal.InkNeedle.SHADER, 255))
            for i in range(1, n + 1):
                y = y0 + (y1 - y0) * i / float(n)
                canvas.call_method("AddStrokePoint",
                                   (author, unreal.Vector2D(x, y), 255))
            canvas.call_method("EndStroke", (author,))

    def sweep(self, canvas, author, needle):
        """橫向一趟掃：從左掃到右、跨過 WALL_X"""
        black = unreal.LinearColor(0.0168, 0.0168, 0.0168, 1.0)
        x0 = CX - STEP * NSTEP
        canvas.call_method("BeginStroke",
                           (author, black, unreal.Vector2D(x0, CY), True, needle, 255))
        for i in range(1, 2 * NSTEP + 1):
            canvas.call_method("AddStrokePoint",
                               (author, unreal.Vector2D(x0 + i * STEP, CY), 255))
        canvas.call_method("EndStroke", (author,))

    def side_ink(self, canvas, path):
        """匯出霧層，回傳 (左側墨量, 右側墨量)——以 WALL_X 為界"""
        canvas.call_method("ExportLayersToPng", (path,))
        return path

    def run(self):
        w = get_world("UEDPIE_0")
        chars = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NiceInkCharacter)
        log("chars=%d" % len(chars))
        if not chars:
            check("victim canvas found", False, "no characters")
            return
        canvas = chars[0].get_editor_property("InkCanvas")
        if not canvas:
            check("victim canvas found", False, "no InkCanvas")
            return
        base = "C:/games/Unreal Engine/nice_ink/Saved/robo_wall"

        # A. 無稿線基準
        canvas.call_method("WashAllMarker", ())
        self.sweep(canvas, 101, unreal.InkNeedle.SHADER)
        self.side_ink(canvas, base + "_a")

        # B. 自己的稿線
        canvas.call_method("WashAllMarker", ())
        self.stencil_line(canvas, 101)
        self.sweep(canvas, 101, unreal.InkNeedle.SHADER)
        self.side_ink(canvas, base + "_b")

        # C. 別人的稿線
        canvas.call_method("WashAllMarker", ())
        self.stencil_line(canvas, 202)
        self.sweep(canvas, 101, unreal.InkNeedle.SHADER)
        self.side_ink(canvas, base + "_c")

        # D. 洗掉稿線後重建（模擬 EnterTour）
        canvas.call_method("WashAllMarker", ())
        self.stencil_line(canvas, 101)
        self.sweep(canvas, 101, unreal.InkNeedle.SHADER)
        canvas.call_method("WashStencil", ())
        self.side_ink(canvas, base + "_d")

        # E. 割線不受牆影響
        canvas.call_method("WashAllMarker", ())
        self.stencil_line(canvas, 101)
        self.sweep(canvas, 101, unreal.InkNeedle.LINER)
        self.side_ink(canvas, base + "_e")

        # F. 彎曲掃描（無稿線）＝梳齒契約的受測物
        canvas.call_method("WashAllMarker", ())
        self.curvy(canvas, 101)
        self.side_ink(canvas, base + "_f")

        # G. 正面衝牆（09-02 v2 契約）：g0=無稿線基準、g=有自己的稿線
        canvas.call_method("WashAllMarker", ())
        self.comb(canvas, 101)
        self.side_ink(canvas, base + "_g0")

        canvas.call_method("WashAllMarker", ())
        self.stencil_line(canvas, 101)
        self.comb(canvas, 101)
        self.side_ink(canvas, base + "_g")

        canvas.call_method("WashAllMarker", ())
        log("EXPORTED base=%s (a=無稿線 b=自己稿線 c=別人稿線 d=洗稿後 e=割線)")
        log("WALL_Y=%.4f CX=%.4f CY=%.4f STEP=%.5f NSTEP=%d" % (WALL_Y, CX, CY, STEP, NSTEP))
        log("DONE")

    def finish(self):
        log("PASS=%d FAIL=%d" % (PASS_N[0], FAIL_N[0]))
        log("HARNESS END")
        if self.handle:
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.handle = None


_p = Probe()
log("harness registered")
