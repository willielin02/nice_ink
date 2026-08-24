# tri-cache 規模量測（2026-08-25 臨時儀器）：veil 採樣每點 ~1ms 的歸因——
# ResolveUVToWorldWithNormal 是 CachedTris 線性全掃，掃的到底是幾個三角形？
# 順便量 world→UV（DebugResolveBodyUV，同樣全掃）的單次牆鐘。
# 產出：Saved/robo_tricount_result.txt
import os
import time
import traceback

import unreal

OUT = "C:/games/Unreal Engine/nice_ink/Saved/robo_tricount_result.txt"
os.makedirs(os.path.dirname(OUT), exist_ok=True)
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[TRICOUNT] " + str(msg))
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
            w = get_world("UEDPIE_0")
            chars = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NiceInkCharacter)
            log(f"chars={len(chars)}")
            c = chars[0]
            body = c.get_editor_property("Body")
            bt = body.get_world_transform()
            p = bt.transform_location(unreal.Vector(0.0, 26.0, 95.0))
            # 首呼 = 建快取（含建置成本），之後才是穩態單次
            log("first(build): " + str(body.call_method("DebugResolveBodyUV", (p,))))
            N = 20
            t0 = time.monotonic()
            for _ in range(N):
                body.call_method("DebugResolveBodyUV", (p,))
            dt_ms = (time.monotonic() - t0) * 1000.0 / N
            log(f"world->UV full-scan: {dt_ms:.3f} ms/call (N={N}, 含 python 呼叫開銷)")
            log("steady: " + str(body.call_method("DebugResolveBodyUV", (p,))))
            log("DONE")
            self.finish()

    def finish(self):
        log("HARNESS END")
        if self.handle:
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.handle = None


_p = Probe()
log("harness registered")
