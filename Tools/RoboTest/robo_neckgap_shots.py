# 站立/作畫脖子縫隙 A/B 截圖＋量測（2026-08-16；ni.NeckRingFromMesh 1=真權重 vs 0=舊烘焙表）
# host 當攝影機、model（client m2）當被拍者：站立平視/抬頭/低頭/lean 埋頭，各拍正面近景＋側面，
# 每張同時記 NeckStretch GetDebugSummary（tableErr=舊表 vs 真權重的環點差 cm）。
# 產出：Saved/robo_neckgap_result.txt + Saved/Screenshots/WindowsEditor/neckgap_{new,old}_*.png
import ctypes, math, time, traceback, os
import unreal
OUT = "C:/games/Unreal Engine/nice_ink/Saved/robo_neckgap_result.txt"
LINES = []
def log(m):
    LINES.append(str(m)); unreal.log_warning("[NECKGAP] " + str(m))
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
        self.plan = []; self.i = 0; self.mode = "new"
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

    def shoot(self, name, side=0):
        # side: 0=正面 1=側面 2=背面；相機＝host 眼睛，站地板上、離脖子 75cm、看 Neck 骨
        server = self.server(); m = self.model_server(); h = self.host()
        pc = unreal.GameplayStatics.get_player_controller(server, 0)
        bow = m.get_editor_property("BowBody")
        neck = bow.get_socket_location("Neck")
        fv = m.get_actor_forward_vector(); yaw = math.degrees(math.atan2(-fv.y, -fv.x))
        ang = math.radians(yaw + 90.0 * side)
        mloc = m.get_actor_location()
        cam = unreal.Vector(neck.x + 75.0 * math.cos(ang), neck.y + 75.0 * math.sin(ang), mloc.z)
        h.set_actor_location(cam, False, True)
        eye = h.get_editor_property("FirstPersonCamera").get_world_location()
        pc.set_control_rotation(unreal.MathLibrary.find_look_at_rotation(eye, neck))
        unreal.SystemLibrary.execute_console_command(server, f"HighResShot 1920x1080 filename=neckgap_{self.mode}_{name}")
        ns = m.get_editor_property("neck_stretch")
        log(f"shot {self.mode}_{name}: eyeZ={eye.z:.0f} neckZ={neck.z:.0f} " + str(ns.call_method("GetDebugSummary", ())))
    def set_mode(self, mode):
        self.mode = mode
        unreal.SystemLibrary.execute_console_command(self.server(), f"ni.NeckRingFromMesh {0 if mode == 'old' else 1}")
        unreal.SystemLibrary.execute_console_command(self.server(), f"ni.NeckStretchOff {1 if mode == 'off' else 0}")
        log(f"mode -> {mode}")

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
                rot = lambda p: (lambda: self.model_pc().set_control_rotation(unreal.Rotator(0.0, p, self.myaw)))
                self.plan = []
                for mode in ("new",):
                    self.plan += [(f"m{mode}", (lambda md=mode: self.set_mode(md)), None)]
                    for pose, pit in (("stand", 0.0), ("up8", 8.0), ("down8", -8.0), ("up", 60.0), ("down", -60.0)):
                        self.plan += [(f"{pose}_front", rot(pit), 0), (f"{pose}_side", None, 1), (f"{pose}_back", None, 2)]
                # 對照：靜態未切網格（Body SM）同機位——分辨鋸齒是「切縫」還是「網格自身下顎摺痕交叉」
                self.plan += [("smon", lambda: self.toggle_sm(True), None),
                              ("sm_stand_front", rot(0.0), 0), ("sm_stand_side", None, 1), ("sm_stand_back", None, 2),
                              ("smoff", lambda: self.toggle_sm(False), None)]
                self.plan += [("mnew", lambda: self.set_mode("new"), None),
                              ("lean_side", lambda: (self.model_pc().set_control_rotation(unreal.Rotator(0.0, 0.0, self.myaw)), self.enter_lean()), 1),
                              ("lean_front", None, 0), ("lean_back", None, 2),
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
            if side is None:
                self.i += 1; self.acted = False; self.stage_t = time.monotonic(); return
            if self.elapsed() < 2.5: return
            self.shoot(name, side)
            self.i += 1; self.acted = False; self.stage_t = time.monotonic()

    def toggle_sm(self, on):
        m = self.model_server()
        m.set_editor_property("bSkeletalStandEnabled", not on)  # 關骨骼站姿＝tick 自己切回靜態未切 Body
        log(f"static body visible -> {on}")
    def enter_lean(self):
        # 在 server 世界對 model 的權威 actor 呼叫（python guard 把 RPC 壓成本地＝在 server 執行才是真的）
        m = self.model_server(); victim = find_char(self.server(), self.victim_pid)
        bt = victim.get_editor_property("Body").get_world_transform()
        p = bt.transform_location(unreal.Vector(0.0, 26.0, 95.0)); n = bt.transform_direction(unreal.Vector(0.0, 1.0, 0.0))
        ok = m.call_method("DebugRoboEnterLean", (victim, p, n)); log(f"enter lean (server) -> {ok}")

    def finish(self):
        try: unreal.unregister_slate_post_tick_callback(self.handle)
        except Exception: pass

Probe()
