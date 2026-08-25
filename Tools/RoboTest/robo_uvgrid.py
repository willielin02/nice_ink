# 世界→UV 空間索引的等價性＋加速比量測（2026-08-25 延遲戰役）
#
# 為什麼要這支：`ResolveBodyUV` 掛在**每一個筆劃點**上，舊實作是 204,398 三角形
# 線性全掃（傳 PreferNearUV 的正常作畫路徑掃兩遍）。追記80 §5 把它列為下一刀。
# 本批換成本地空間均勻網格。**效能修的唯一驗收形式＝輸出逐位相同**（追記80 鐵則），
# 所以 DebugResolveBodyUV 被改成同時跑兩條路並自報 match/scanMs/gridMs——
# 這支只是把它撒在整具身體上、把數字收攏。唯讀，不改任何資產。
#
# 產出：Saved/robo_uvgrid_result.txt
import os
import time
import traceback

import unreal

OUT = "C:/games/Unreal Engine/nice_ink/Saved/robo_uvgrid_result.txt"
os.makedirs(os.path.dirname(OUT), exist_ok=True)
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[UVGRID] " + str(msg))
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(LINES))


def get_world(tag):
    for w in unreal.ObjectIterator(unreal.World):
        if tag in w.get_path_name():
            return w
    return None


def parse(s):
    """DebugResolveBodyUV 的字串 → dict（key=val 空白分隔；括號值原樣留字串）"""
    out = {}
    for tok in str(s).split(" "):
        if "=" in tok:
            k, _, v = tok.partition("=")
            out[k] = v
    return out


def pct(vals, q):
    if not vals:
        return -1.0
    v = sorted(vals)
    i = min(len(v) - 1, max(0, int(round((len(v) - 1) * q))))
    return v[i]


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
                self.advance("measure")
        elif s == "measure":
            if self.elapsed() < 3.0:
                return
            self.measure()
            self.finish()

    def measure(self):
        w = get_world("UEDPIE_0")
        chars = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NiceInkCharacter)
        log("chars=%d" % len(chars))
        body = chars[0].get_editor_property("Body")

        # 單點 A/B（含 tri-cache 首建）——保留 robo_tricount 的可比性
        p = body.get_world_transform().transform_location(unreal.Vector(0.0, 26.0, 95.0))
        log("point A/B: " + str(body.call_method("DebugResolveBodyUV", (p,))))

        # 全身 bench：兩條路走同一個 Impl，只差候選來源；量測全在 C++ 內（無 python 開銷）
        for n in (200, 2000):
            r = str(body.call_method("DebugUvGridBench", (n,)))
            log("bench N=%d: %s" % (n, r))
            d = parse(r)
            if d.get("mismatch") == "0":
                log("  VERDICT: PASS 逐位相同 (n=%s hits=%s)" % (d.get("n"), d.get("hits")))
            else:
                log("  VERDICT: FAIL 答案變了 mismatch=%s" % d.get("mismatch"))

        log("DONE")

    def finish(self):
        log("HARNESS END")
        if self.handle:
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.handle = None


_p = Probe()
log("harness registered")
