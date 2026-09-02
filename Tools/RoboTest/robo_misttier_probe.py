# 灰洗分檔＋墨光學（2026-09-02 CPU 光柵器戰役）的契約探針
#
# 受測物＝打霧層的 max 合成語義與墨光學 LUT：
#   t1 同檔（淡）交疊＝不變深（跨筆劃冪等＝分檔均勻性的構造保證）
#   t2 深壓淺＝生效（實檔蓋過淡檔）
#   t3 淺壓深＝無事（淡檔蓋不掉實檔＝真實墨的物理）
#   t4 流量淡出：Flow 遞減的筆劃尾端密度單調下降（收筆淡出的沉積側）
#   t5 色相曲線：淡檔黑＝偏冷（B>R）、實檔黑＝回到調色盤色
#   t6 重建等價：WashStencil 觸發整層重播後，霧層與 live 逐位相同
#
# 直接驅動畫布 API（BeginStroke 第 7 參數＝Tier）；判讀在
# robo_misttier_check.py（離線 numpy、3 秒一輪）。
# 產出：Saved/robo_misttier_result.txt ＋ Saved/robo_tier_*.png
import os
import time
import traceback

import unreal

OUT = "C:/games/Unreal Engine/nice_ink/Saved/robo_misttier_result.txt"
os.makedirs(os.path.dirname(OUT), exist_ok=True)
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[MISTTIER] " + str(msg))
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(LINES))


def get_world(tag):
    for w in unreal.ObjectIterator(unreal.World):
        if tag in w.get_path_name():
            return w
    return None


# 幾何：遠離縫區的乾淨平面帶（稿線牆戰役實測 x<0.50 全平面路徑）
CX, CY = 0.47, 0.55
STEP = 0.0012
NSTEP = 18
TIER_L, TIER_M, TIER_F = 77, 153, 255  # 與 ShaderTierAlphaFor 對賬（中檔＝剛好 60.0%）
BLACK = unreal.LinearColor(0.0168, 0.0168, 0.0168, 1.0)


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
                    self.stage_t = time.monotonic()
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

    # --- 畫布操作 ---

    def hstroke(self, canvas, author, y, tier, flows=None):
        """水平打霧掃（左→右）；flows=逐點流量列（None=全滿）"""
        x0 = CX - STEP * NSTEP
        f0 = flows[0] if flows else 255
        canvas.call_method("BeginStroke",
                           (author, BLACK, unreal.Vector2D(x0, y), True,
                            unreal.InkNeedle.SHADER, f0, tier))
        n = 2 * NSTEP
        for i in range(1, n + 1):
            f = flows[min(i, len(flows) - 1)] if flows else 255
            canvas.call_method("AddStrokePoint",
                               (author, unreal.Vector2D(x0 + i * STEP, y), f))
        canvas.call_method("EndStroke", (author,))

    def vstroke(self, canvas, author, x, tier):
        """垂直打霧掃（跨過水平帶＝製造交疊區）"""
        y0 = CY - STEP * NSTEP
        canvas.call_method("BeginStroke",
                           (author, BLACK, unreal.Vector2D(x, y0), True,
                            unreal.InkNeedle.SHADER, 255, tier))
        for i in range(1, 2 * NSTEP + 1):
            canvas.call_method("AddStrokePoint",
                               (author, unreal.Vector2D(x, y0 + i * STEP), 255))
        canvas.call_method("EndStroke", (author,))

    def run(self):
        w = get_world("UEDPIE_0")
        chars = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NiceInkCharacter)
        log("chars=%d" % len(chars))
        if not chars:
            log("FAIL no characters")
            return
        canvas = chars[0].get_editor_property("InkCanvas")
        base = "C:/games/Unreal Engine/nice_ink/Saved/robo_tier"

        # A. 同檔（淡）交疊：水平淡 ＋ 垂直淡 ⇒ 交疊區不得變深
        canvas.call_method("WashAllMarker", ())
        self.hstroke(canvas, 101, CY, TIER_L)
        self.vstroke(canvas, 101, CX, TIER_L)
        canvas.call_method("ExportLayersToPng", (base + "_a",))

        # B. 深壓淺：水平淡、垂直實 ⇒ 交疊區＝實
        canvas.call_method("WashAllMarker", ())
        self.hstroke(canvas, 101, CY, TIER_L)
        self.vstroke(canvas, 101, CX, TIER_F)
        canvas.call_method("ExportLayersToPng", (base + "_b",))

        # C. 淺壓深：水平實、垂直淡 ⇒ 交疊區仍＝實
        canvas.call_method("WashAllMarker", ())
        self.hstroke(canvas, 101, CY, TIER_F)
        self.vstroke(canvas, 101, CX, TIER_L)
        canvas.call_method("ExportLayersToPng", (base + "_c",))

        # D. 流量淡出：實檔、Flow 沿路 255→18 遞減
        canvas.call_method("WashAllMarker", ())
        n = 2 * NSTEP
        flows = [max(18, int(255 - (255 - 18) * i / n)) for i in range(n + 1)]
        self.hstroke(canvas, 101, CY, TIER_F, flows)
        canvas.call_method("ExportLayersToPng", (base + "_d",))

        # E. 中檔單帶（色相曲線的中點樣本）
        canvas.call_method("WashAllMarker", ())
        self.hstroke(canvas, 101, CY, TIER_M)
        canvas.call_method("ExportLayersToPng", (base + "_e",))

        # G. 罩染（09-02 glazing；user 以真實刺青流程定案）：滿檔黑帶＋淡檔**紅**
        #    垂直罩過去（畫兩次＝罩染冪等一併受測）⇒ 交疊區＝紅罩黑（壓暗的紅調）、
        #    不再是「無事」
        RED = unreal.LinearColor(0.855, 0.0144, 0.0722, 1.0)
        canvas.call_method("WashAllMarker", ())
        self.hstroke(canvas, 101, CY, TIER_F)
        for _ in range(2):
            y0 = CY - STEP * NSTEP
            canvas.call_method("BeginStroke",
                               (101, RED, unreal.Vector2D(CX, y0), True,
                                unreal.InkNeedle.SHADER, 255, TIER_L))
            for i in range(1, 2 * NSTEP + 1):
                canvas.call_method("AddStrokePoint",
                                   (101, unreal.Vector2D(CX, y0 + i * STEP), 255))
            canvas.call_method("EndStroke", (101,))
        canvas.call_method("ExportLayersToPng", (base + "_g",))

        # F. 重建等價：混合圖樣（含罩染）→ dump pre → WashStencil（無稿可洗、但恆
        #    觸發 RebuildRenderTargets＝整層從筆劃重播）→ dump post ⇒ 逐位相同
        canvas.call_method("WashAllMarker", ())
        self.hstroke(canvas, 101, CY, TIER_L)
        self.vstroke(canvas, 101, CX, TIER_F)
        self.hstroke(canvas, 101, CY + 0.004, TIER_F)
        canvas.call_method("BeginStroke",
                           (101, RED, unreal.Vector2D(CX + 0.006, CY - 0.01), True,
                            unreal.InkNeedle.SHADER, 255, TIER_L))
        for i in range(1, 20):
            canvas.call_method("AddStrokePoint",
                               (101, unreal.Vector2D(CX + 0.006, CY - 0.01 + i * STEP), 255))
        canvas.call_method("EndStroke", (101,))
        canvas.call_method("ExportLayersToPng", (base + "_f_pre",))
        canvas.call_method("WashStencil", ())
        canvas.call_method("ExportLayersToPng", (base + "_f_post",))

        canvas.call_method("WashAllMarker", ())
        log("EXPORTED CX=%.4f CY=%.4f STEP=%.5f NSTEP=%d TIERS=%d/%d/%d"
            % (CX, CY, STEP, NSTEP, TIER_L, TIER_M, TIER_F))
        log("DONE")

    def finish(self):
        log("HARNESS END")
        if self.handle:
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.handle = None


_p = Probe()
log("harness registered")
