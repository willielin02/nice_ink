# ESC 選單三頁截圖（2026-09-16；user：「這裡在顯示什麼？按鍵、內容等等的都看不清楚」）。
# 開 PIE → 等 Drawing（host＝作畫者，站著）→ 開 ESC 選單 → 首頁／設定／怎麼玩各拍一張
#（Shot showui＝含 Slate；HighResShot 不含）。頁碼走 DebugRoboSystemMenuPage 鉤子＝HUD 的
# SysMenuPage 單一來源（此前 Slate 自己另存一份 ⇒ 鉤子設了頁畫面不動）。
# 產出：Saved/robo_escmenu_result.txt + Saved/Screenshots/WindowsEditor/hbesc_*.png
import time, traceback
import unreal

OUT = r"C:\games\Unreal Engine\nice_ink\Saved\robo_escmenu_result.txt"
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[ESCMENU] " + str(msg))
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(LINES))


def get_world(tag):
    for w in unreal.ObjectIterator(unreal.World):
        if tag in w.get_path_name():
            return w
    return None


class Shots:
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

    def server(self):
        return get_world("UEDPIE_0")

    def local_pawns(self):
        out = []
        for w in unreal.ObjectIterator(unreal.World):
            if "UEDPIE_" not in w.get_path_name():
                continue
            for c in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NiceInkCharacter):
                if c.is_locally_controlled():
                    out.append(c)
        return out

    def call_local(self, name, args=()):
        for c in self.local_pawns():
            try:
                c.call_method(name, args)
            except Exception as e:
                log("%s failed: %s" % (name, e))

    def shot(self, name):
        pc = unreal.GameplayStatics.get_player_controller(self.server(), 0)
        unreal.SystemLibrary.execute_console_command(self.server(), "Shot showui filename=hbesc_%s" % name, pc)
        log("SHOT hbesc_%s" % name)

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
                unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
                self.advance("wait_pie")
        elif s == "wait_pie":
            server = self.server()
            gm = unreal.GameplayStatics.get_game_mode(server) if server else None
            if gm:
                gm.set_editor_property("DebugForcedVictimSeat", 1)
                self.advance("wait_drawing")
            elif self.elapsed() > 90.0:
                log("FAIL: no PIE")
                self.finish()
        elif s == "wait_drawing":
            gs = unreal.GameplayStatics.get_game_state(self.server())
            if gs and str(gs.get_editor_property("CurrentPhase")).endswith("DRAWING: 3>"):
                self.advance("open")
            elif self.elapsed() > 60.0:
                log("FAIL: drawing phase never came")
                self.finish()
        elif s == "open":
            if self.elapsed() > 2.0:
                self.call_local("DebugRoboSystemMenu", (True,))
                self.advance("shot_root")
        elif s == "shot_root":
            if self.elapsed() > 2.5:
                self.shot("root")
                self.advance("page_settings")
        elif s == "page_settings":
            if self.elapsed() > 0.8:
                self.call_local("DebugRoboSystemMenuPage", (2,))
                self.advance("shot_settings")
        elif s == "shot_settings":
            if self.elapsed() > 1.5:
                self.shot("settings")
                self.advance("page_howto")
        elif s == "page_howto":
            if self.elapsed() > 0.8:
                self.call_local("DebugRoboSystemMenuPage", (1,))
                self.advance("shot_howto")
        elif s == "shot_howto":
            if self.elapsed() > 1.5:
                self.shot("howto")
                self.advance("close")
        elif s == "close":
            if self.elapsed() > 0.8:
                self.call_local("DebugRoboSystemMenuPage", (0,))
                self.call_local("DebugRoboSystemMenu", (False,))
                log("RESULT DONE")
                self.finish()

    def finish(self):
        try:
            unreal.unregister_slate_post_tick_callback(self.handle)
        except Exception:
            pass


Shots()
