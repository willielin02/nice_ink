# 皮膚「斑」的定罪 A/B（2026-08-24，user 圈選兩點＋「背腰部很多斑」）
# 離線已證：SkinDetailAmount=0／SkinNormalStrength=0／T_BodyChroma 是 32x32 純色
# ⇒ 材質對皮膚沒有任何空間變化，斑只能來自 N·V（法線＝幾何）。本檔在引擎裡驗這句話。
#   v0def      ＝出貨組態
#   v1flat     ＝HeadlightFloor=1 / HeadlightPower=0（假光全關＝純平色）＝猛藥對照組
#                斑消失 ⇒ 定罪「著色＝法線」；不消失 ⇒ 另有兇手（貼圖/墨/後製）
#   v2nochroma ＝ChromaStrength=0（貼圖已是純色 ⇒ 必須零變化，這是儀器自檢）
# 機位：躺著的受害者（user 截圖同款）＋站立者的胸前/上背（離線量測說斑在那裡）
# 前置：play_mode_robo.ps1 ＋ ini 掛 +StartupScripts=<本檔>；測完移除
import unreal
import time
import ctypes
import traceback
import os
import glob

OUT = r"C:\games\Unreal Engine\nice_ink\Saved\robo_skin_spots_result.txt"
SHOTDIR = r"C:\games\Unreal Engine\nice_ink\Saved\Screenshots\WindowsEditor"
LINES = []


def log(m):
    LINES.append(str(m))
    unreal.log_warning("[SPOT] " + str(m))
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


def V(x, y, z):
    return unreal.Vector(x, y, z)


def add(*vs):
    r = unreal.Vector(0, 0, 0)
    for v in vs:
        r = unreal.Vector(r.x + v.x, r.y + v.y, r.z + v.z)
    return r


def mul(v, s):
    return unreal.Vector(v.x * s, v.y * s, v.z * s)


class Probe:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.host_pid = None
        self.step_i = 0
        self.shots = []
        self.orig = {}
        self.white = unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture")
        for p in glob.glob(os.path.join(SHOTDIR, "sp_*.png")):
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
            self.finish()

    def all_skin_mids(self):
        # 所有 PIE 世界、所有角色的皮膚 MID（截圖抓的是聚焦視窗的世界＝08-22 血價）
        mids = []
        for tag in ("UEDPIE_0", "UEDPIE_1", "UEDPIE_2", "UEDPIE_3"):
            w = get_world(tag)
            if not w:
                continue
            for c in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NiceInkCharacter):
                for comp in c.get_components_by_class(unreal.MeshComponent):
                    for mi in range(comp.get_num_materials()):
                        m = comp.get_material(mi)
                        if isinstance(m, unreal.MaterialInstanceDynamic):
                            try:
                                m.get_scalar_parameter_value("SkinBrightness")
                            except Exception:
                                continue
                            if m not in mids:
                                mids.append(m)
        return mids

    def host(self):
        return find_char(get_world("UEDPIE_0"), self.host_pid)

    def plan(self, server):
        gs = unreal.GameplayStatics.get_game_state(server)
        vic = find_char(server, gs.get_editor_property("VictimPlayerId"))
        bow = vic.get_editor_property("BowBody")
        origin, ext, rad = unreal.SystemLibrary.get_component_bounds(bow)
        A = V(1, 0, 0) if ext.x >= ext.y else V(0, 1, 0)
        Lf = V(0, 1, 0) if ext.x >= ext.y else V(-1, 0, 0)
        log("victim origin %s ext %s" % (origin, ext))

        def cam(name, cpos, foc, fov=45):
            self.shots.append((name, foc, cpos, fov))

        # 距離要拉開：躺著的身體長軸半徑就有 ~90cm（ext 實測），機位貼太近會鑽進網格裡
        cam("sp_lie_top", add(origin, mul(A, -110), V(0, 0, 300)), add(origin, V(0, 0, 20)), 55)
        # cam("sp_lie_belly", add(origin, mul(A, -230), V(0, 0, 150)), add(origin, mul(A, 10), V(0, 0, 30)), 45)
        # cam("sp_lie_graze", add(origin, mul(A, -300), V(0, 0, 62)), add(origin, mul(A, 10), V(0, 0, 34)), 40)
        # cam("sp_lie_hip", add(origin, mul(A, 120), mul(Lf, 210), V(0, 0, 150)), add(origin, mul(A, 20), V(0, 0, 25)), 45)
        # cam("sp_lie_hip_graze", add(origin, mul(A, 60), mul(Lf, 260), V(0, 0, 55)), add(origin, mul(A, 10), V(0, 0, 30)), 40)

        # 站立者：2 客戶端 PIE 只有「主機＋受害者」（3 客戶端＝VRAM OOM，已實踩）
        # ⇒ 沒有第三人時就拍主機自己：把他的 BowBody 的 OwnerNoSee 關掉，自由機位就看得到。
        stand = None
        for c in unreal.GameplayStatics.get_all_actors_of_class(server, unreal.NiceInkCharacter):
            pid = c.get_editor_property("player_state").get_editor_property("player_id")
            if pid != gs.get_editor_property("VictimPlayerId") and pid != self.host_pid:
                stand = c
                break
        if stand is None:
            stand = self.host()
            if stand:
                for tag in ("UEDPIE_0", "UEDPIE_1", "UEDPIE_2"):
                    w = get_world(tag)
                    if not w:
                        continue
                    h = find_char(w, self.host_pid)
                    if not h:
                        continue
                    bow = h.get_editor_property("BowBody")
                    bow.set_owner_no_see(False)
                    bow.set_visibility(True)
                log("no third char -> shooting host self (BowBody OwnerNoSee off)")
        if stand:
            sl = stand.get_actor_location()
            f = stand.get_actor_forward_vector()
            r = stand.get_actor_right_vector()
            log("standing char at %s fwd %s" % (sl, f))
            # cam("sp_st_chest", add(sl, mul(f, 210), V(0, 0, 50)), add(sl, V(0, 0, 40)), 40)
            # cam("sp_st_chest_graze", add(sl, mul(f, 90), mul(r, 195), V(0, 0, 45)), add(sl, mul(f, 20), V(0, 0, 35)), 40)
            # cam("sp_st_back", add(sl, mul(f, -210), V(0, 0, 55)), add(sl, V(0, 0, 45)), 40)
            # cam("sp_st_back_graze", add(sl, mul(f, -100), mul(r, 195), V(0, 0, 52)), add(sl, mul(f, -20), V(0, 0, 42)), 40)
            # cam("sp_st_waist", add(sl, mul(f, -190), V(0, 0, 8)), add(sl, V(0, 0, 2)), 40)
            # cam("sp_st_full", add(sl, mul(f, 330), V(0, 0, 60)), add(sl, V(0, 0, 30)), 50)
        else:
            log("WARN: no standing non-host char")
        log("planned %d cameras x 3 variants" % len(self.shots))

    def apply_variant(self, k):
        mids = self.all_skin_mids()
        if not self.orig and mids:
            m = mids[0]
            for p in ("HeadlightFloor", "HeadlightPower", "ChromaStrength"):
                self.orig[p] = m.get_scalar_parameter_value(p)
            log("orig %s  mids=%d" % (self.orig, len(mids)))
        for m in mids:
            if k == 0:
                m.set_scalar_parameter_value("HeadlightFloor", self.orig["HeadlightFloor"])
                m.set_scalar_parameter_value("HeadlightPower", self.orig["HeadlightPower"])
                m.set_scalar_parameter_value("ChromaStrength", self.orig["ChromaStrength"])
            elif k == 1:
                m.set_scalar_parameter_value("HeadlightFloor", 1.0)
                m.set_scalar_parameter_value("HeadlightPower", 0.0)
                m.set_scalar_parameter_value("ChromaStrength", self.orig["ChromaStrength"])
            elif k == 2:
                m.set_scalar_parameter_value("HeadlightFloor", self.orig["HeadlightFloor"])
                m.set_scalar_parameter_value("HeadlightPower", self.orig["HeadlightPower"])
                m.set_scalar_parameter_value("ChromaStrength", 0.0)
            elif k == 3:
                # 假光關＋墨層全白＝把「非著色」的東西再拆一層
                m.set_scalar_parameter_value("HeadlightFloor", 1.0)
                m.set_scalar_parameter_value("HeadlightPower", 0.0)
                for t in ("MarkerRT", "TattooRT", "MistRT"):
                    m.set_texture_parameter_value(t, self.white)
            elif k == 4:
                for t in ("MarkerRT", "TattooRT", "MistRT"):
                    m.set_texture_parameter_value(t, self.white)
                m.set_texture_parameter_value("FaceTex", self.white)

    def step(self):
        s = self.stage
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
            server = get_world("UEDPIE_0")
            if server and unreal.GameplayStatics.get_game_mode(server):
                unreal.GameplayStatics.get_game_mode(server).set_editor_property("DebugForcedVictimSeat", 1)
                self.advance("wait_drawing")
        elif s == "wait_drawing":
            server = get_world("UEDPIE_0")
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
                if self.host_pid is None:
                    log("FAIL: no host artist")
                    self.finish()
                    return
                self.advance("setup")
            elif self.elapsed() > 260.0:
                log("FAIL: no drawing phase")
                self.finish()
        elif s == "setup":
            if self.elapsed() < 3.0:
                return
            server = get_world("UEDPIE_0")
            self.plan(server)
            self.apply_variant(0)
            focus_main_window()
            self.step_i = 0
            self.advance("shots")
        elif s == "shots":
            server = get_world("UEDPIE_0")
            # 7 拍：發截圖指令與換參數**必須分拍**——HighResShot 是下一幀才抓，
            # 同拍改材質會讓檔名與內容錯開一格（08-24 實踩：v0def 檔其實是 flat 畫面）
            k = self.step_i // 11
            sub = self.step_i % 11
            if k >= len(self.shots):
                self.apply_variant(0)
                log("RESULT DONE-PASS")
                self.finish()
                return
            if self.elapsed() < 1.1:
                return
            name, foc, cpos, fov = self.shots[k]
            c = self.host()
            if sub == 0:
                c.call_method("DebugRoboViewAt", (cpos.x, cpos.y, cpos.z, foc.x, foc.y, foc.z))
                unreal.SystemLibrary.execute_console_command(server, "fov %d" % fov)
                self.apply_variant(0)
            elif sub == 1:
                unreal.SystemLibrary.execute_console_command(server, "HighResShot 1600x900 filename=%s_v0def" % name)
            elif sub == 2:
                self.apply_variant(1)
            elif sub == 3:
                unreal.SystemLibrary.execute_console_command(server, "HighResShot 1600x900 filename=%s_v1flat" % name)
            elif sub == 4:
                self.apply_variant(2)
            elif sub == 5:
                unreal.SystemLibrary.execute_console_command(server, "HighResShot 1600x900 filename=%s_v2nochroma" % name)
            elif sub == 6:
                self.apply_variant(3)
            elif sub == 7:
                unreal.SystemLibrary.execute_console_command(server, "HighResShot 1600x900 filename=%s_v3flatnoink" % name)
            elif sub == 8:
                self.apply_variant(4)
            elif sub == 9:
                unreal.SystemLibrary.execute_console_command(server, "HighResShot 1600x900 filename=%s_v4flatnoinkface" % name)
            elif sub == 10:
                for m in self.all_skin_mids():
                    m.set_scalar_parameter_value("HeadlightFloor", self.orig["HeadlightFloor"])
                    m.set_scalar_parameter_value("HeadlightPower", self.orig["HeadlightPower"])
                    m.set_scalar_parameter_value("ChromaStrength", self.orig["ChromaStrength"])
                log("shot set done: %s" % name)
            self.step_i += 1
            self.stage_t = time.monotonic()

    def finish(self):
        try:
            unreal.unregister_slate_post_tick_callback(self.handle)
        except Exception:
            pass


Probe()
