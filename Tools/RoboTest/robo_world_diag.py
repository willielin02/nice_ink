# 診斷用：PIE 開起來之後把每個 PIE 世界的 PlayerController／NiceInkCharacter 列出來
#（哪個世界是 server、哪個 pawn 是本地控制），給 robo_ui_shots_all 的 local_twin 對賬。
import time
import traceback

import unreal

OUT = "C:/games/Unreal Engine/nice_ink/Saved/robo_world_diag.txt"
LINES = []


def log(m):
    LINES.append(str(m))
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(LINES))


class D:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.h = unreal.register_slate_post_tick_callback(self.tick)

    def tick(self, dt):
        try:
            self.step()
        except Exception:
            log("EXC:\n" + traceback.format_exc())
            self.stage = "done"

    def step(self):
        t = time.monotonic() - self.t0
        if self.stage == "boot" and t > 8:
            les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
            if les:
                les.editor_request_begin_play()
                self.stage = "wait"
        elif self.stage == "wait" and t > 40:
            for w in unreal.ObjectIterator(unreal.World):
                p = w.get_path_name()
                if "UEDPIE" not in p:
                    continue
                gm = unreal.GameplayStatics.get_game_mode(w)
                log("WORLD %s gamemode=%s" % (p, "yes" if gm else "no"))
                for pc in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.PlayerController):
                    try:
                        pawn = pc.get_editor_property("pawn")
                        log("  PC %s local=%s pawn=%s" % (pc.get_name(), pc.is_local_player_controller(),
                                                        pawn.get_name() if pawn else None))
                    except Exception as e:
                        log("  PC %s err %s" % (pc.get_name(), e))
                for c in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NiceInkCharacter):
                    ps = c.get_editor_property("player_state")
                    pid = ps.get_editor_property("player_id") if ps else None
                    try:
                        loc = c.is_locally_controlled()
                    except Exception as e:
                        loc = "err %s" % e
                    log("  CHAR %s pid=%s local=%s" % (c.get_name(), pid, loc))
            for i in range(3):
                w = None
                for ww in unreal.ObjectIterator(unreal.World):
                    if "UEDPIE_%d" % i in ww.get_path_name():
                        w = ww
                        break
                if w:
                    pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
                    log("get_player_pawn(UEDPIE_%d, 0) = %s" % (i, pawn.get_name() if pawn else None))
            self.pawns = []
            for w in unreal.ObjectIterator(unreal.World):
                if "UEDPIE" in w.get_path_name():
                    for c in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NiceInkCharacter):
                        if c.is_locally_controlled():
                            self.pawns.append(c)
            for c in self.pawns:
                try:
                    c.call_method("DebugRoboSystemMenu", (True,))
                    log("menu opened on %s (%s)" % (c.get_name(), c.get_world().get_path_name()))
                except Exception as e:
                    log("menu open failed %s" % e)
            self.stage = "shot1"
        elif self.stage == "shot1" and t > 44:
            for w in unreal.ObjectIterator(unreal.World):
                if "UEDPIE_0" in w.get_path_name():
                    unreal.SystemLibrary.execute_console_command(w, "HighResShot 1920x1080 filename=diag_esc_hires")
                    log("hires shot issued")
            self.stage = "shot2"
        elif self.stage == "shot2" and t > 47:
            for w in unreal.ObjectIterator(unreal.World):
                if "UEDPIE_0" in w.get_path_name():
                    unreal.SystemLibrary.execute_console_command(w, "Shot showui filename=diag_esc_shot")
                    log("shot issued")
            self.stage = "shot3"
        elif self.stage == "shot3" and t > 50:
            log("DONE")
            self.stage = "done"
            unreal.unregister_slate_post_tick_callback(self.h)


_d = D()
log("diag registered")
