# 褌布著色 A/B 探針（2026-08-18）：證明「假光讓法線真的上到畫面」。
#
# 背景定罪：L_Dojo 零方向光（只有 SkyLight）＋r.ReflectionMethod=0 ⇒ 均勻天光的
# 漫射與法線無關 ⇒ M_Fundoshi 的 Normal/Roughness 在畫面上恆等於沒接。
#
# 三張同機位圖（只動材質參數，鏡頭/姿勢/光照全不動＝單變因）：
#   fd_black : ClothBrightness=0        -> 布變全黑，用來把布的像素從畫面切出來（遮罩）
#   fd_flat  : LightFloor=1, Sheen=0    -> 位元等價於改動前的外觀（假光全關）
#   fd_lit   : 現行預設                  -> 假光＋掠角 sheen
# 判讀（離線 fundoshi_ab_measure.py）：布遮罩內的**低頻**亮度 std
#   flat≈0（平的照片）而 lit 明顯>0 ⇒ 起伏真的上到畫面了。
# 亮度均值同時輸出，供 ClothBrightness 對齊——A/B 必須單變因，
# 不能讓「變亮了」冒充「變好看了」。
#
# 前置：play_mode_robo.ps1（編輯器關閉時跑）＋ ini 掛 +StartupScripts=<本檔>
# 產出：Saved/robo_fundoshi_result.txt ＋ Saved/Screenshots/WindowsEditor/fd_*.png
import unreal, time, ctypes, traceback, os, glob

OUT = r"C:\games\Unreal Engine\nice_ink\Saved\robo_fundoshi_result.txt"
SHOTDIR = r"C:\games\Unreal Engine\nice_ink\Saved\Screenshots\WindowsEditor"
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[FD] " + str(msg))
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
    user32 = ctypes.windll.user32

    def enum_cb(h, _):
        buf = ctypes.create_unicode_buffer(256)
        user32.GetWindowTextW(h, buf, 256)
        if "NiceInk" in buf.value and "Unreal Editor" in buf.value:
            user32.SetForegroundWindow(h)
            return False
        return True
    WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    user32.EnumWindows(WNDENUMPROC(enum_cb), 0)


class Probe:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.host_pid = None
        self.step_i = 0
        self.mid = None
        # 同名不覆蓋＝會看到舊圖（陷阱年鑑）——先清乾淨
        for p in glob.glob(os.path.join(SHOTDIR, "fd_*.png")):
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

    def host(self):
        w = get_world("UEDPIE_0")
        return find_char(w, self.host_pid) if w else None

    def tick(self, dt):
        try:
            self.step()
        except Exception:
            log("EXC:\n" + traceback.format_exc())
            self.finish()

    def set_cloth(self, floor, sheen, bright):
        self.mid.set_scalar_parameter_value("ClothLightFloor", floor)
        self.mid.set_scalar_parameter_value("ClothSheenStrength", sheen)
        self.mid.set_scalar_parameter_value("ClothBrightness", bright)

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
            server = get_world("UEDPIE_0")
            if server and unreal.GameplayStatics.get_game_mode(server):
                unreal.GameplayStatics.get_game_mode(server).set_editor_property("DebugForcedVictimSeat", 1)
                self.advance("wait_drawing")
        elif s == "wait_drawing":
            server = get_world("UEDPIE_0")
            gs = unreal.GameplayStatics.get_game_state(server)
            # 卡住時要能指認卡在哪一相位（無聲失敗必須先開口）
            ph = str(gs.get_editor_property("CurrentPhase")) if gs else "NO_GAMESTATE"
            if ph != getattr(self, "_last_phase", None):
                self._last_phase = ph
                n = unreal.GameplayStatics.get_game_state(server)
                cnt = len(n.get_editor_property("player_array")) if n else -1
                log(f"  phase={ph} players={cnt} t={self.elapsed():.1f}s")
            if gs and ph.endswith("DRAWING: 3>"):
                victim_pid = gs.get_editor_property("VictimPlayerId")
                for c in unreal.GameplayStatics.get_all_actors_of_class(server, unreal.NiceInkCharacter):
                    pid = c.get_editor_property("player_state").get_editor_property("player_id")
                    if pid != victim_pid and c.is_player_controlled() and c.get_controller() and \
                       c.get_controller().is_local_player_controller():
                        self.host_pid = pid
                        break
                if self.host_pid is None:
                    log("FAIL: no host artist")
                    self.finish()
                    return
                self.advance("setup")
            elif self.elapsed() > 90.0:
                log("FAIL: no drawing phase")
                self.finish()
        elif s == "setup":
            if self.elapsed() < 1.0:
                return
            c = self.host()
            # 褌槽的 MID：按槽名找（SK/SM 槽序相反，寫死 index 會抓錯）
            bow = c.get_editor_property("BowBody")
            sk = None
            for getter in (lambda: bow.get_editor_property("skinned_asset"),
                           lambda: bow.get_skinned_asset()):
                try:
                    sk = getter()
                    if sk:
                        break
                except Exception:
                    continue
            if not sk:
                log("FAIL: BowBody 無 skinned asset")
                self.finish()
                return
            mats = list(sk.get_editor_property("materials"))
            names = [str(m.get_editor_property("material_slot_name")) for m in mats]
            log(f"BowBody slots = {names}")
            idx = next((i for i, n in enumerate(names) if "Fundoshi" in n), -1)
            if idx < 0:
                log("FAIL: 找不到 Fundoshi 槽")
                self.finish()
                return
            self.mid = bow.create_dynamic_material_instance(idx)
            log(f"Fundoshi slot={idx} MID={self.mid.get_name() if self.mid else None}")
            if not self.mid:
                self.finish()
                return
            # 3/4 側前機位、髖部取景（曲率跨度大＝最能讀出明暗梯度）
            c.call_method("DebugRoboViewFrom", (70.0, -110.0, -10.0))
            focus_main_window()
            self.advance("shots")
        elif s == "shots":
            server = get_world("UEDPIE_0")
            # 黑廻し輪（08-18）：mask / 無 sheen / 現行預設——sheen 在黑底上買到什麼
            SEQ = [
                (2.0, "mask",    lambda: self.set_cloth(1.0, 0.0, 0.0)),
                (3.4, "shot",    lambda: unreal.SystemLibrary.execute_console_command(
                    server, "HighResShot 1600x900 filename=fd_black")),
                (4.8, "nosheen", lambda: self.set_cloth(0.45, 0.0, 1.0)),
                (6.2, "shot",    lambda: unreal.SystemLibrary.execute_console_command(
                    server, "HighResShot 1600x900 filename=fd_flat")),
                (7.6, "lit",     lambda: self.set_cloth(0.45, 0.18, 1.0)),
                (9.0, "shot",    lambda: unreal.SystemLibrary.execute_console_command(
                    server, "HighResShot 1600x900 filename=fd_lit")),
            ]
            if self.step_i < len(SEQ):
                t, tag, fn = SEQ[self.step_i]
                if self.elapsed() >= t:
                    fn()
                    log(f"  step {self.step_i} {tag}")
                    self.step_i += 1
            elif self.elapsed() >= 10.4:
                self.set_cloth(0.45, 0.35, 1.0)
                log("RESULT DONE-PASS")
                self.finish()

    def finish(self):
        try:
            unreal.unregister_slate_post_tick_callback(self.handle)
        except Exception:
            pass


Probe()
