# 脖子縫合版截圖套件（2026-08-15；SK_Sumo_Whole 站立/俯仰/作畫姿的脖子外觀自查）
# host 當攝影機、model（client m2）當被拍者：①站立平視 ②抬頭 ③低頭（LookPitch）
# ④lean-lock 埋頭 ⑤偷瞄——各拍正面 110cm 近景＋側面。robo 截圖=幾何自查；質感由 user viewport。
# 產出：Saved/robo_neckwhole_result.txt + Saved/Screenshots/WindowsEditor/neckwhole_*.png
import ctypes, math, time, traceback, os
import unreal
OUT = "C:/games/Unreal Engine/nice_ink/Saved/robo_neckwhole_result.txt"
LINES = []
def log(m):
    LINES.append(str(m)); unreal.log_warning("[NECKW] " + str(m))
    open(OUT, "w", encoding="utf-8").write("\n".join(LINES))
def get_world(tag):
    for w in unreal.ObjectIterator(unreal.World):
        if tag in w.get_path_name(): return w
    return None
def find_char(w, pid):
    for c in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NiceInkCharacter):
        ps = c.get_editor_property("player_state")
        if ps and ps.get_editor_property("player_id") == pid: return c
    return None
def focus_main_window():
    user32 = ctypes.windll.user32
    def cb(h, _):
        buf = ctypes.create_unicode_buffer(256); user32.GetWindowTextW(h, buf, 256)
        if "NiceInk" in buf.value and "Unreal Editor" in buf.value:
            user32.SetForegroundWindow(h); return False
        return True
    P = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p); user32.EnumWindows(P(cb), 0)

class Probe:
    def __init__(self):
        self.t0 = time.monotonic(); self.stage = "boot"; self.stage_t = self.t0
        self.victim_pid = self.host_pid = self.model_pid = None
        self.plan = []; self.i = 0
        self.handle = unreal.register_slate_post_tick_callback(self.tick)
    def advance(self, s): self.stage = s; self.stage_t = time.monotonic(); log("STAGE -> " + s)
    def elapsed(self): return time.monotonic() - self.stage_t
    def tick(self, dt):
        try: self.step()
        except Exception:
            log("EXC:\n" + traceback.format_exc()); self.finish()
    def server(self): return get_world("UEDPIE_0")
    def host(self): return unreal.GameplayStatics.get_player_pawn(self.server(), 0)
    def model_client(self): return find_char(get_world("UEDPIE_2"), self.model_pid)
    def model_server(self): return find_char(self.server(), self.model_pid)
    def model_pc(self):
        return unreal.GameplayStatics.get_player_controller(get_world("UEDPIE_2"), 0)

    def shoot(self, name, side=False):
        server = self.server(); m = self.model_server(); h = self.host()
        pc = unreal.GameplayStatics.get_player_controller(server, 0)
        mloc = m.get_actor_location()
        # 網格前向＝actor -X（陷阱年鑑：面向鏡頭=yaw 180）——「正面」機位站在 -forward 側
        fv = m.get_actor_forward_vector(); yaw = math.degrees(math.atan2(-fv.y, -fv.x))
        ang = math.radians(yaw + (90.0 if side else 0.0))
        # 頭高≈mloc.z+60（膠囊中心 z 到頭 ~+60）
        cam = unreal.Vector(mloc.x + 130.0 * math.cos(ang), mloc.y + 130.0 * math.sin(ang), mloc.z + 55.0)
        h.set_actor_location(cam, False, True)
        look = unreal.MathLibrary.find_look_at_rotation(
            unreal.Vector(cam.x, cam.y, cam.z + 62.0), unreal.Vector(mloc.x, mloc.y, mloc.z + 55.0))
        pc.set_control_rotation(look)
        unreal.SystemLibrary.execute_console_command(server, f"HighResShot 1280x720 filename=neckwhole_{name}")
        log(f"shot {name}")

    def step(self):
        s = self.stage
        if s == "boot":
            if time.monotonic() - self.t0 > 8.0:
                eps = unreal.find_object(None, "/Script/UnrealEd.Default__EditorPerformanceSettings")
                if eps: eps.set_editor_property("bThrottleCPUWhenNotForeground", False)
                unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play(); self.advance("wait_pie")
        elif s == "wait_pie":
            w = self.server()
            if w and unreal.GameplayStatics.get_game_mode(w):
                unreal.GameplayStatics.get_game_mode(w).set_editor_property("DebugForcedVictimSeat", 1); self.advance("wait_drawing")
        elif s == "wait_drawing":
            w = self.server(); gs = unreal.GameplayStatics.get_game_state(w) if w else None
            if gs and str(gs.get_editor_property("CurrentPhase")).endswith("DRAWING: 3>"):
                self.victim_pid = gs.get_editor_property("VictimPlayerId")
                self.host_pid = self.host().get_editor_property("player_state").get_editor_property("player_id")
                for c in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NiceInkCharacter):
                    pid = c.get_editor_property("player_state").get_editor_property("player_id")
                    if pid != self.victim_pid and pid != self.host_pid: self.model_pid = pid; break
                log(f"victim={self.victim_pid} host={self.host_pid} model={self.model_pid}")
                focus_main_window()
                # 計畫：(名稱, 動作)
                self.plan = [
                    ("stand_front", lambda: self.model_pc().set_control_rotation(unreal.Rotator(0.0, 0.0, self.myaw)), False),
                    ("stand_side", None, True),
                    ("up_front", lambda: self.model_pc().set_control_rotation(unreal.Rotator(0.0, 60.0, self.myaw)), False),
                    ("up_side", None, True),
                    ("down_front", lambda: self.model_pc().set_control_rotation(unreal.Rotator(0.0, -60.0, self.myaw)), False),
                    ("down_side", None, True),
                    ("lean_side", lambda: (self.model_pc().set_control_rotation(unreal.Rotator(0.0, 0.0, self.myaw)), self.enter_lean()), True),
                    ("lean_front", None, False),
                ]
                self.myaw = self.model_pc().get_control_rotation().yaw
                self.i = 0; self.advance("plan")
            elif self.elapsed() > 40: log("FAIL: no drawing"); self.finish()
        elif s == "plan":
            if self.i >= len(self.plan): log("DONE"); self.finish(); return
            name, act, side = self.plan[self.i]
            if not getattr(self, "acted", False):
                self.acted = True
                if act: act()
                self.stage_t = time.monotonic()
                return
            if self.elapsed() < 2.5: return
            self.shoot(name, side)
            self.i += 1; self.acted = False; self.stage_t = time.monotonic()

    def enter_lean(self):
        m = self.model_client(); victim = find_char(get_world("UEDPIE_2"), self.victim_pid)  # 同世界（client）
        bt = victim.get_editor_property("Body").get_world_transform()
        p = bt.transform_location(unreal.Vector(0.0, 26.0, 95.0)); n = bt.transform_direction(unreal.Vector(0.0, 1.0, 0.0))
        ok = m.call_method("DebugRoboEnterLean", (victim, p, n)); log(f"enter lean -> {ok}")

    def finish(self):
        try: unreal.unregister_slate_post_tick_callback(self.handle)
        except Exception: pass

Probe()
