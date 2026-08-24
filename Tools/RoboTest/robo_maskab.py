# 褌遮罩 T_FundoshiMask 的資產級 A/B（2026-08-24）：user 圈2 的範圍內有一條 0->255 的
# 遮罩硬邊，而皮膚材質有在取樣這張圖（它不是 MID 參數，只能換資產）。
# 問題：這張遮罩在皮膚上到底畫了什麼？換成全黑（遮罩=0）看畫面有沒有變。
# 三連拍：A 原樣 -> B 全黑 -> C 還原（C 必須≈A，否則這次 A/B 不算數）。
# 前置：play_mode_robo.ps1 ＋ ini 掛 +StartupScripts=<本檔>；測完移除
import unreal
import time
import ctypes
import traceback
import os
import glob

OUT = r"C:\games\Unreal Engine\nice_ink\Saved\robo_maskab_result.txt"
SHOTDIR = r"C:\games\Unreal Engine\nice_ink\Saved\Screenshots\WindowsEditor"
SA = r"c:\games\Unreal Engine\nice_ink\SourceAssets"
LINES = []


def log(m):
    LINES.append(str(m))
    unreal.log_warning("[MASK] " + str(m))
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(LINES))


def get_world(tag):
    for w in unreal.ObjectIterator(unreal.World):
        if tag in w.get_path_name():
            return w
    return None


def find_char(w, pid):
    for c in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NiceInkCharacter):
        ps = c.get_editor_property("player_state")
        if ps and ps.get_editor_property("player_id") == pid:
            return c
    return None


def focus_main_window():
    u = ctypes.windll.user32

    def cb(h, _):
        b = ctypes.create_unicode_buffer(256)
        u.GetWindowTextW(h, b, 256)
        if "NiceInk" in b.value and "Unreal Editor" in b.value:
            u.SetForegroundWindow(h)
            return False
        return True
    P = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    u.EnumWindows(P(cb), 0)


def reimport_mask(src_png):
    t = unreal.AssetImportTask()
    t.filename = src_png
    t.destination_path = "/Game/Characters/Cloth"
    t.destination_name = "T_FundoshiMask"
    t.automated = True
    t.save = True
    t.replace_existing = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    tex = unreal.load_asset("/Game/Characters/Cloth/T_FundoshiMask")
    if tex:
        tex.set_editor_property("srgb", False)
    log("  reimported mask <- %s" % os.path.basename(src_png))


class Probe:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.host_pid = None
        self.step_i = 0
        self.shots = []
        for p in glob.glob(os.path.join(SHOTDIR, "mk_*.png")):
            try:
                os.remove(p)
            except OSError:
                pass
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    def advance(self, s):
        self.stage = s
        self.stage_t = time.monotonic()
        log("STAGE -> " + s)

    def elapsed(self):
        return time.monotonic() - self.stage_t

    def tick(self, dt):
        try:
            self.step()
        except Exception:
            log("EXC:\n" + traceback.format_exc())
            reimport_mask(os.path.join(SA, "fundoshi_mask_final.png"))
            self.finish()

    def step(self):
        s = self.stage
        server = get_world("UEDPIE_0")
        if s == "boot":
            if time.monotonic() - self.t0 > 8.0:
                eps = unreal.find_object(None, "/Script/UnrealEd.Default__EditorPerformanceSettings")
                if eps:
                    eps.set_editor_property("bThrottleCPUWhenNotForeground", False)
                les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
                if les is None:
                    return
                les.editor_request_begin_play()
                self.advance("wait_pie")
        elif s == "wait_pie":
            if server and unreal.GameplayStatics.get_game_mode(server):
                unreal.GameplayStatics.get_game_mode(server).set_editor_property("DebugForcedVictimSeat", 1)
                self.advance("wait_drawing")
        elif s == "wait_drawing":
            gs = unreal.GameplayStatics.get_game_state(server) if server else None
            ph = str(gs.get_editor_property("CurrentPhase")) if gs else "NO"
            if gs and ph.endswith("DRAWING: 3>"):
                vpid = gs.get_editor_property("VictimPlayerId")
                for c in unreal.GameplayStatics.get_all_actors_of_class(server, unreal.NiceInkCharacter):
                    pid = c.get_editor_property("player_state").get_editor_property("player_id")
                    if pid != vpid and c.is_player_controlled() and c.get_controller() and \
                       c.get_controller().is_local_player_controller():
                        self.host_pid = pid
                        break
                self.advance("setup")
            elif self.elapsed() > 260.0:
                log("FAIL: no drawing phase")
                self.finish()
        elif s == "setup":
            if self.elapsed() < 3.0:
                return
            # 拍站著的主機自己（關掉 OwnerNoSee）：褌邊在腰/髖，正是圈2 的解剖位置
            h = find_char(server, self.host_pid)
            for tag in ("UEDPIE_0", "UEDPIE_1", "UEDPIE_2"):
                w = get_world(tag)
                if not w:
                    continue
                c = find_char(w, self.host_pid)
                if c:
                    bow = c.get_editor_property("BowBody")
                    bow.set_owner_no_see(False)
                    bow.set_visibility(True)
            sl = h.get_actor_location()
            f = h.get_actor_forward_vector()
            r = h.get_actor_right_vector()

            def cam(name, cpos, foc, fov):
                self.shots.append((name, foc, cpos, fov))
            cam("mk_backhip", unreal.Vector(sl.x - f.x * 130 + r.x * 40, sl.y - f.y * 130 + r.y * 40, sl.z + 5),
                unreal.Vector(sl.x, sl.y, sl.z - 8), 38)
            cam("mk_frontthigh", unreal.Vector(sl.x + f.x * 120 + r.x * 45, sl.y + f.y * 120 + r.y * 45, sl.z - 25),
                unreal.Vector(sl.x + f.x * 8, sl.y + f.y * 8, sl.z - 42), 38)
            focus_main_window()
            self.step_i = 0
            self.advance("shots")
        elif s == "shots":
            if self.elapsed() < 1.3:
                return
            k = self.step_i
            nshot = len(self.shots)
            # 每個機位 3 拍（A 原/B 黑/C 還原），中間夾換圖
            plan = []
            for i in range(nshot):
                plan += [("cam", i), ("shot", "%s_A_orig" % self.shots[i][0])]
            plan += [("mask", "black")]
            for i in range(nshot):
                plan += [("cam", i), ("shot", "%s_B_maskoff" % self.shots[i][0])]
            plan += [("mask", "final")]
            for i in range(nshot):
                plan += [("cam", i), ("shot", "%s_C_restored" % self.shots[i][0])]
            if k >= len(plan):
                log("RESULT DONE-PASS")
                self.finish()
                return
            kind, arg = plan[k]
            if kind == "cam":
                name, foc, cpos, fov = self.shots[arg]
                c = find_char(server, self.host_pid)
                c.call_method("DebugRoboViewAt", (cpos.x, cpos.y, cpos.z, foc.x, foc.y, foc.z))
                unreal.SystemLibrary.execute_console_command(server, "fov %d" % fov)
            elif kind == "shot":
                unreal.SystemLibrary.execute_console_command(
                    server, "HighResShot 1600x900 filename=%s" % arg)
            elif kind == "mask":
                reimport_mask(os.path.join(SA, "mask_black.png" if arg == "black" else "fundoshi_mask_final.png"))
            self.step_i += 1
            self.stage_t = time.monotonic()

    def finish(self):
        try:
            unreal.unregister_slate_post_tick_callback(self.handle)
        except Exception:
            pass


Probe()
