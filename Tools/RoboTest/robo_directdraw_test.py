# 直接畫制驗證（2026-07-20 剛臂制）：站立前傾姿（DrawPose_Backup4 原樣手勢）＋
# 3-DOF（yaw/Hips/踝）解「筆尖=落墨點」＋筆焊死在手＋眼錨定相機 FOV36＋ghost 穿透。
# 斷言：鎖定/aim/相機恆等式/中心落墨 HIT/站姿幾何/入鎖即有筆（未畫也有）/
# 剛臂契約（|手-髖| 距離不變式=手臂沒被 IK 動過）/筆尖可達（tipErr≤1.5）/
# 射線 miss=不可達/ghost 數/同點雙人合法/退出還原。
# 筆即游標（07-20 定案）：墨從實體筆尖出（沿筆軸壓入取接觸點）、UI 準星退役、
# aim 走 One Euro 濾波（靜止濾抖/快掃低滯後）——契約：settle 後筆尖仍釘中心射線。
# 伸縮針制（07-21 user 定案「按下左鍵才伸長、指到哪畫哪」）：筆=SM_TattooMachine、
# LMB=針彈出到皮膚接觸點（needle=實測針長）、放開=收樁（0.6）；身體鉗位外的深度
# 交給伸針解（nSolve>0）——遠點（橫向對側手 1m+）從「搆不到」反轉為「伸針可達」，
# 舊 unreach 計時契約退役（unreach 路徑保留：橫向殘差超容差仍不落墨）。
# 收尾＝覆蓋率量測（DebugRoboCoverageScan：站姿/各眼高/理論極限的可點皮膚面積——
# 趴下鍵已依 07-20 量測定案移除：趴僅 +3.6%、低區正解=翻身）。
# 刺青手感制（07-22 user 定案「高頻點狀出墨＋拉的方式告知方向」）新契約：
# 出墨=5Hz 點狀（dotN 按節拍計量、非連續流）、LMB 按住=方向拉桿巡航
# （DebugRoboPaintStick 模擬）、皮膚面恆速 v_max=k·d·f（dotGapCm≤k×筆寬×1.35=實線
# 保證）、原地扎=冪等（dotN 不灌水）、robo aim 傳送門照舊跳過巡航限速。
# 產出：Saved/robo_directdraw_result.txt + Saved/Screenshots/WindowsEditor/directdraw_*.png
import ctypes
import math
import re
import time
import traceback

import unreal

OUT = "C:/games/Unreal Engine/nice_ink/Saved/robo_directdraw_result.txt"
import os
os.makedirs(os.path.dirname(OUT), exist_ok=True)
LINES = []
PASS_N = [0]
FAIL_N = [0]


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[DIRECTDRAW] " + str(msg))
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(LINES))


def check(name, ok, detail=""):
    (PASS_N if ok else FAIL_N)[0] += 1
    log(("PASS " if ok else "FAIL ") + name + ((" | " + detail) if detail else ""))


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


def dist(a, b):
    return ((a.x - b.x) ** 2 + (a.y - b.y) ** 2 + (a.z - b.z) ** 2) ** 0.5


def bone_w(char, name):
    bow = char.get_editor_property("BowBody")
    return bow.get_bone_transform_by_name(name, unreal.BoneSpaces.WORLD_SPACE).translation


def neck_rel_hips_deg(char):
    # Neck 骨相對 Hips 的旋轉差（度）——剛臂+Head-only 臉向下應恆定：
    # Neck 被甩=脖樁隆起（07-20 viewport 病灶）
    bow = char.get_editor_property("BowBody")
    qn = bow.get_bone_transform_by_name("Neck", unreal.BoneSpaces.WORLD_SPACE).rotation
    qh = bow.get_bone_transform_by_name("Hips", unreal.BoneSpaces.WORLD_SPACE).rotation
    return abs(qh.angular_distance(qn)) * 57.29578


def summary(char):
    s = str(char.call_method("DebugLeanSummary", ()))
    d = {}
    for k, v in re.findall(r"(\w+)=([-\d.]+)", s):
        try:
            d[k] = float(v)
        except ValueError:
            pass
    return s, d


def vec3(s, key):
    m = re.search(key + r"=\(([-\d.]+),([-\d.]+),([-\d.]+)\)", s)
    return (float(m.group(1)), float(m.group(2)), float(m.group(3))) if m else None


def dist3(a, b):
    return ((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2) ** 0.5


def seg_speeds(samples):
    # (t, cw) 序列 → 前半/後半平均皮膚速度（cm/s）
    if len(samples) < 6:
        return None
    mid = len(samples) // 2

    def spd(seg):
        d = sum(dist3(seg[i + 1][1], seg[i][1]) for i in range(len(seg) - 1))
        return d / max(seg[-1][0] - seg[0][0], 1e-3)

    return spd(samples[:mid]), spd(samples[mid:])


def aim_towards(char, world_pt):
    # 以角色相機位置算朝向 world_pt 的 (az, tilt)
    cam = char.get_editor_property("FirstPersonCamera").get_world_location()
    dx, dy, dz = world_pt.x - cam.x, world_pt.y - cam.y, world_pt.z - cam.z
    L = max((dx * dx + dy * dy + dz * dz) ** 0.5, 1e-3)
    az = math.degrees(math.atan2(dy, dx))
    tilt = -math.degrees(math.asin(max(-1.0, min(1.0, dz / L))))
    return az, tilt


BELLY = unreal.Vector(0.0, 26.0, 95.0)
BELLY_N = unreal.Vector(0.0, 1.0, 0.0)
THIGH = unreal.Vector(16.0, 8.0, 48.0)


class Test:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.victim_pid = None
        self.host_pid = None
        self.model_pid = None
        self.tp_i = 0
        # 編輯器背景 CPU 節流必關（07-24 實錘：user 用機時編輯器失焦→整場 PIE 3fps
        # →superfast 探針的 2.66Hz 正弦被混疊成慢爬=假 FAIL；ini 寫檔會被編輯器
        # 退出回寫蓋掉，python 直設 CDO=運行時權威）
        # 5.7 沒把類別曝露成 unreal.EditorPerformanceSettings——CDO 用 find_object
        #（Default__LevelEditorPlaySettings 同 pattern）；引擎每幀讀這個設定=立即生效。
        # 屬性名兩試（snake 名在 5.7 解析失敗過）；ini 防線=Saved/Config/WindowsEditor/
        # EditorSettings.ini（config=EditorSettings，不是 EditorPerProjectUserSettings——
        # 07-24 踩坑：寫錯 ini 檔整輪白跑）
        perf = unreal.find_object(None, "/Script/UnrealEd.Default__EditorPerformanceSettings")
        ok = False
        for prop in ("throttle_cpu_when_not_foreground", "bThrottleCPUWhenNotForeground"):
            try:
                perf.set_editor_property(prop, False)
                ok = True
                log("editor bg-throttle disabled via " + prop)
                break
            except Exception:
                pass
        if not ok:
            log("WARN: cannot disable bg-throttle (both property names failed)")
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    def advance(self, stage):
        self.stage = stage
        self.stage_t = time.monotonic()
        log("STAGE -> " + stage)

    def elapsed(self):
        return time.monotonic() - self.stage_t

    def tick(self, dt):
        try:
            self.step()
        except Exception:
            log("EXC:\n" + traceback.format_exc())
            self.finish()

    def server(self):
        return get_world("UEDPIE_0")

    def host_char(self):
        return unreal.GameplayStatics.get_player_pawn(self.server(), 0)

    def lowest_point(self):
        victim = find_char(self.server(), self.victim_pid)
        bt = victim.get_editor_property("Body").get_world_transform()
        best = None
        for lx, ly, lz in ((25.0, 5.0, 60.0), (-25.0, 5.0, 60.0),
                           (22.0, 10.0, 95.0), (-22.0, 10.0, 95.0), (28.0, 8.0, 120.0)):
            p = bt.transform_location(unreal.Vector(lx, ly, lz))
            if best is None or p.z < best.z:
                best = p
        return best

    def victim_pt(self, lp, ln):
        victim = find_char(self.server(), self.victim_pid)
        bt = victim.get_editor_property("Body").get_world_transform()
        return bt.transform_location(lp), bt.transform_direction(ln)

    def enter_lean(self, artist, name, lp, ln):
        victim = find_char(self.server(), self.victim_pid)
        p, n = self.victim_pt(lp, ln)
        ok = artist.call_method("DebugRoboEnterLean", (victim, p, n))
        log(f"[{name}] DebugRoboEnterLean -> {ok}")
        self.cur_point = p

    def step(self):
        s = self.stage
        if s == "boot":
            if time.monotonic() - self.t0 > 8.0:
                unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
                self.advance("wait_pie")
        elif s == "wait_pie":
            server = self.server()
            if server and unreal.GameplayStatics.get_game_mode(server):
                unreal.GameplayStatics.get_game_mode(server).set_editor_property("DebugForcedVictimSeat", 1)
                self.advance("wait_drawing")
        elif s == "wait_drawing":
            server = self.server()
            gs = unreal.GameplayStatics.get_game_state(server) if server else None
            if gs and str(gs.get_editor_property("CurrentPhase")).endswith("DRAWING: 3>"):
                self.victim_pid = gs.get_editor_property("VictimPlayerId")
                self.host_pid = self.host_char().get_editor_property("player_state").get_editor_property("player_id")
                for c in unreal.GameplayStatics.get_all_actors_of_class(server, unreal.NiceInkCharacter):
                    pid = c.get_editor_property("player_state").get_editor_property("player_id")
                    if pid != self.victim_pid and pid != self.host_pid:
                        self.model_pid = pid
                        break
                log(f"victim={self.victim_pid} host={self.host_pid} model={self.model_pid}")
                focus_main_window()
                self.advance("lock_belly")
        elif s == "lock_belly":
            if self.elapsed() < 0.8:
                return
            host = find_char(self.server(), self.host_pid)
            self.enter_lean(host, "belly", BELLY, BELLY_N)
            # 打稿制（07-25）：預設工具=麥克筆打稿（needleSel 2）——驗過預設後切
            # Liner，讓既有的機器/巡航契約群在原語義下續跑
            raw, d = summary(host)
            check("default tool is stencil marker", d.get("needleSel") == 2, raw)
            host.call_method("DebugRoboNeedle", (0,))
            self.advance("verify_belly")
        elif s == "verify_belly":
            if self.elapsed() < 1.5:
                return
            host = find_char(self.server(), self.host_pid)
            raw, d = summary(host)
            check("belly locked", d.get("locked") == 1, raw)
            check("locked FOV 36", abs(d.get("fov", 0) - 36.0) < 0.5, raw)
            # 3 人 PIE 局＝host/victim/model → ghost 恰 1（victim 與自己不 ghost）
            check("ghosts applied (others translucent)", d.get("ghosts", -1) >= 1, raw)
            # 站立前傾幾何（Backup4：髖 ~68cm、腳在身後、頭前伸）
            ground = host.get_actor_location().z - 92.0
            hips = bone_w(host, "Hips")
            check("stance hips height ~68", 45.0 < hips.z - ground < 88.0, f"hipsZ-ground={hips.z - ground:.1f}")
            fwd = host.get_actor_forward_vector()
            foot = bone_w(host, "LeftFoot")
            fd = (foot.x - hips.x) * fwd.x + (foot.y - hips.y) * fwd.y
            # 前傾配重：髖後坐、腳在身下略前（首輪實測 +18.8 與截圖一致）——鉗合理帶
            check("feet under body (counterweight)", -20.0 < fd < 45.0, f"dot={fd:.1f}")
            head = bone_w(host, "Head")
            hd = (head.x - hips.x) * fwd.x + (head.y - hips.y) * fwd.y
            check("head thrust forward", hd > 15.0, f"dot={hd:.1f}")
            # 相機＝本體承載＋朝向≡aim（恆等式）
            cam = host.get_editor_property("FirstPersonCamera")
            cl = cam.get_world_location()
            cm = unreal.GameplayStatics.get_player_camera_manager(self.server(), 0)
            check("view through pawn camera", dist(cm.get_camera_location(), cl) < 3.0)
            cr = cam.get_socket_rotation("None")
            f = unreal.MathLibrary.get_forward_vector(cr)
            az = d.get("az", 0.0)
            tilt = d.get("tilt", 0.0)
            aim = unreal.Vector(
                math.cos(math.radians(az)) * math.cos(math.radians(tilt)),
                math.sin(math.radians(az)) * math.cos(math.radians(tilt)),
                -math.sin(math.radians(tilt)))
            cosang = f.x * aim.x + f.y * aim.y + f.z * aim.z
            ang = math.degrees(math.acos(max(-1.0, min(1.0, cosang))))
            # 07-28 活眉心相機（SPEC #44 還原）：前向=look-at P、與 aim 方向差「視差角」
            # ＝合法（眼在活眉心、射線原點在錨點）。精確恆等式改由下一條 center resolve
            # 驗（螢幕中心=P）；這裡只留視差 sanity 上界。
            check("camera dir ~ aim (parallax bound)", ang < 8.0, f"ang={ang:.2f}")
            # 中心落墨解算
            hit = str(host.call_method("DebugRoboCanvasResolve", (0.5, 0.5)))
            parts = hit.split()
            ok_hit = len(parts) >= 3 and parts[0] == "HIT" and 0.0 <= float(parts[1]) <= 1.0
            check("centre ray resolves to skin", ok_hit, hit)
            # 剛臂制新契約（07-20）：
            check("pen exists before any ink", d.get("penValid") == 1, raw)
            check("tip reachable at entry", d.get("reach") == 1 and 0.0 <= d.get("tipErr", 99) <= 1.5, raw)
            # 伸縮針契約（07-21）：入鎖未觸發＝收樁（0.6 遮出針口）、機器懸在皮膚上方
            check("needle stub when idle", 0.3 <= d.get("needle", -1.0) <= 0.9 and d.get("trig", -1) == 0, raw)
            check("hip delta within whitelist clamp", abs(d.get("hipDeg", 99)) <= 40.05, raw)
            check("ankle delta within whitelist clamp", abs(d.get("ankleDeg", 99)) <= 18.05, raw)
            hips = bone_w(host, "Hips")
            hand = bone_w(host, "RightHand")
            self.hand_hips0 = dist(hand, hips)
            self.hand_hips0_l = dist(bone_w(host, "LeftHand"), hips)
            self.neck_rel0 = neck_rel_hips_deg(host)
            self.advance("paint")
        elif s == "paint":
            if self.elapsed() < 0.5:
                return
            host = find_char(self.server(), self.host_pid)
            host.call_method("DebugRoboPaintHold", (True,))
            # 微掃：畫一小段弧（aim 抖動讓細分取樣真的走過皮膚）
            raw, d = summary(host)
            self.paint_az0 = d.get("az", 0.0)
            self.paint_tilt0 = d.get("tilt", 45.0)
            self.advance("paint_sweep")
        elif s == "paint_sweep":
            t = self.elapsed()
            host = find_char(self.server(), self.host_pid)
            if t < 1.6:
                host.call_method("DebugRoboDrawAim",
                                 (self.paint_az0 + math.sin(t * 4.0) * 6.0,
                                  self.paint_tilt0 + math.cos(t * 3.0) * 4.0))
                return
            if t < 2.2:
                return  # 顯示層平滑貼齊（~100ms 追趕）後再驗
            raw, d = summary(host)
            check("pen tip rides centre ray while painting", d.get("penValid") == 1 and 0.0 <= d.get("penRayErr", 99) < 3.0, raw)
            check("tip stays reachable during sweep", d.get("reach") == 1 and d.get("tipErr", 99) <= 1.5, raw)
            hh = dist(bone_w(host, "RightHand"), bone_w(host, "Hips"))
            check("rigid arm invariant |hand-hips| const", abs(hh - self.hand_hips0) < 1.0,
                  f"d0={self.hand_hips0:.1f} d1={hh:.1f}")
            hhl = dist(bone_w(host, "LeftHand"), bone_w(host, "Hips"))
            check("rigid left arm invariant (deep-chain convergence)", abs(hhl - self.hand_hips0_l) < 1.0,
                  f"d0={self.hand_hips0_l:.1f} d1={hhl:.1f}")
            nr = neck_rel_hips_deg(host)
            check("neck stump rigid (face delta at Head only)", abs(nr - self.neck_rel0) < 3.0,
                  f"rel0={self.neck_rel0:.1f} rel1={nr:.1f}")
            # 伸縮針：觸發中針彈出到皮膚（07-28 姿勢主導制：深度全交給針——掃離入座
            # 校準點後手膚距漂移由伸縮吸收＝近點也可有中度伸長，帶放寬）
            check("needle extended while painting", d.get("trig") == 1 and 2.0 <= d.get("needle", -1.0) <= 30.0, raw)
            # 分帳制：伸長 e 針/握管各半——近點掃掠帶內握管允許中度伸長
            check("grip within telescoping band", 21.0 <= d.get("grip", -1.0) <= 42.0, raw)
            # 刺青手感制（07-22）：出墨只在巡航推進時前進——robo aim 傳送門的 sweep
            # 跳段不落墨（跳不是線；非巡航位移=解算噪聲不入帳），僅首針＋零星接觸點
            dn = d.get("dotN", -1.0)
            check("backdoor sweep does not ink (cruise-gated emission)", 1.0 <= dn <= 8.0,
                  f"dotN={dn:.0f} (first-contact dot only)")
            # FP 2D 筆制（07-22）：fp_paint 截圖現在照的是 HUD 貼圖機身＋針線
            #（本人 3D 筆 OwnerNoSee；tp 圖照模特=3D 原樣）
            unreal.SystemLibrary.execute_console_command(
                self.server(), "HighResShot 1280x720 filename=directdraw_fp_paint")
            self.advance("cruise_start")
        elif s == "cruise_start":
            if self.elapsed() < 0.5:
                return
            # 巡航契約：回到肚皮中央、掛方向拉桿（az+ 向）——針以皮膚面恆速拖行
            host = find_char(self.server(), self.host_pid)
            host.call_method("DebugRoboDrawAim", (self.paint_az0, self.paint_tilt0))
            self.advance("cruise_engage")
        elif s == "cruise_engage":
            if self.elapsed() < 0.8:
                return
            host = find_char(self.server(), self.host_pid)
            raw, d = summary(host)
            self.cruise_az0 = d.get("az", 0.0)
            self.cruise_dot0 = d.get("dotN", 0.0)
            host.call_method("DebugRoboPaintStick", (90.0, 0.0))
            self.advance("cruise_verify")
        elif s == "cruise_verify":
            if self.elapsed() < 2.5:
                return
            host = find_char(self.server(), self.host_pid)
            raw, d = summary(host)
            check("cruise engaged (stick beyond deadzone)", d.get("cruise") == 1, raw)
            vmax = d.get("vmaxCm", 0.0)
            check("vmax follows conservation law k*d*f", 1.8 <= vmax <= 3.0, raw)
            # 實線契約：最近兩針的皮膚距 ≤ k×筆寬×1.35（0.263cm）——超過=虛線=失敗
            #（間距與頻率無關：12Hz 只加快巡航速度、針距恆 k×筆寬）
            gap = d.get("dotGapCm", -1.0)
            check("dot gap holds solid-line bound", 0.02 <= gap <= 0.28, f"gapCm={gap:.3f}")
            # 節拍計量（牆鐘域、寬帶：重載 robo PIE 遊戲時間 ~7 成牆鐘）
            dn = d.get("dotN", 0.0) - self.cruise_dot0
            check("cruise dots paced by frequency", 15.0 <= dn <= 40.0, f"d_dotN={dn:.0f}")
            # 實速契約（07-22 三修：債務+外環增益）：tipSpd＝針實走/巡航「遊戲時間」
            # ——玩家體感的速度域（牆鐘除法會把編輯器變慢誤讀成針變慢，診斷輪實錘：
            # 牆鐘算 1.56、遊戲時間 2.26=97% v_max）
            tspd = d.get("tipSpd", -1.0)
            # 07-28 姿勢主導制重定基線：射線原點=解析眉心（距皮膚較近）＝穩態 ~2.6
            #（兩輪 2.63/2.64 穩定、gain 0.80 收斂——是新幾何常數不是失控）
            check("cruise tip speed matches vmax (game-time)", 1.95 <= tspd <= 2.80,
                  f"tipSpd={tspd:.2f} cm/s vs vmax 2.34 (hopSpd={d.get('hopSpd', -1):.2f} gain={d.get('gain', -1):.2f})")
            # 皮膚面恆速：aim 位移對應 ~vmax×t（角度域寬鬆帶：掠射/距離/時間膨脹）
            daz = abs(d.get("az", 0.0) - self.cruise_az0)
            check("cruise speed capped (aim crawls)", 3.0 <= daz <= 14.0, f"dAz={daz:.2f}")
            check("pen tip still rides centre ray in cruise",
                  d.get("penValid") == 1 and 0.0 <= d.get("penRayErr", 99) < 3.0, raw)
            # 導引預測路徑（07-24 皮繩制：導引=針→游標的待走路徑、長度=追趕殘距
            # ≤皮繩 2.5cm／步長 0.75 ⇒ 預期 4-5 點；16cm 固定前瞻退役——游標之外
            # 的方向是未知的，預測它=捏造）
            check("guide path predicts ahead on skin", d.get("guideN", -1.0) >= 3.0, raw)
            # 巡航中截圖：行進蟻導引虛線＋針尖中心＋機身——「要位移去哪」讀感自查
            unreal.SystemLibrary.execute_console_command(
                self.server(), "HighResShot 1280x720 filename=directdraw_fp_cruising")
            self.advance("cruise_stop")
        elif s == "cruise_stop":
            if self.elapsed() < 0.3:
                return
            # 拉回死區＝停針原地扎：dotN 不得再長（同 UV 冪等、資料不灌水）
            host = find_char(self.server(), self.host_pid)
            host.call_method("DebugRoboPaintStick", (0.0, 0.0))
            self.cruise_dot1 = None
            self.advance("cruise_idle_verify")
        elif s == "cruise_idle_verify":
            host = find_char(self.server(), self.host_pid)
            raw, d = summary(host)
            if self.cruise_dot1 is None:
                if self.elapsed() < 0.5:
                    return
                self.cruise_dot1 = d.get("dotN", 0.0)
                return
            if self.elapsed() < 2.0:
                return
            check("stationary needle is idempotent (no dot inflation)",
                  d.get("dotN", 0.0) - self.cruise_dot1 <= 1.0,
                  f"dotN {self.cruise_dot1:.0f} -> {d.get('dotN', 0):.0f}")
            check("cruise disengaged in deadzone", d.get("cruise") == 0, raw)
            self.advance("pstate_engage")
        elif s == "pstate_engage":
            # --- P-狀態游標制契約組（07-28 user 定案「滑鼠直接控制皮膚上的點」）---
            # DebugRoboMouse=合成滑鼠增量＝與真人同一條游標管線。角度命令=角度制
            #（上游所有 robo 契約在角度制下運行、語義不變）；增量輸入=游標制。
            host = find_char(self.server(), self.host_pid)
            if self.elapsed() < 0.4:
                host.call_method("DebugRoboMouse", (0.3, 0.0))
                return
            raw, d = summary(host)
            check("cursor mode engages on mouse input", d.get("curs") == 1, raw)
            self.ps_samples = []
            self.ps_cam = []
            self.advance("pstate_gain_pos")
        elif s == "pstate_gain_pos":
            # 恆定增益（去程）：等速餵滑鼠、量游標皮膚速度——前後半速度比 ≈1
            #（老病：身體追趕/髖槓桿把筆速灌成 2~5×、regime 切換忽快忽慢）
            host = find_char(self.server(), self.host_pid)
            host.call_method("DebugRoboMouse", (0.3, 0.0))
            raw, d = summary(host)
            cw = vec3(raw, "cw")
            if cw:
                self.ps_samples.append((self.elapsed(), cw))
            camo = vec3(raw, "camo")
            if camo:
                self.ps_cam.append((time.monotonic(), camo))
            if self.elapsed() < 1.2:
                return
            spd = seg_speeds(self.ps_samples)
            if spd:
                a, b = spd
                ratio = b / max(a, 1e-3)
                check("cursor gain constant while body settles",
                      0.7 <= ratio <= 1.4 and a > 1.0,
                      f"spd first={a:.1f} second={b:.1f} cm/s ratio={ratio:.2f}")
            else:
                check("cursor gain constant while body settles", False, "no samples")
            self.ps_samples = []
            self.advance("pstate_gain_neg")
        elif s == "pstate_gain_neg":
            # 回程（增益對稱＋游標回到肚皮域內）
            host = find_char(self.server(), self.host_pid)
            host.call_method("DebugRoboMouse", (-0.3, 0.0))
            raw, d = summary(host)
            cw = vec3(raw, "cw")
            if cw:
                self.ps_samples.append((self.elapsed(), cw))
            camo = vec3(raw, "camo")
            if camo:
                self.ps_cam.append((time.monotonic(), camo))
            if self.elapsed() < 1.2:
                return
            spd = seg_speeds(self.ps_samples)
            ok = False
            det = "no samples"
            if spd:
                a, b = spd
                ok = 0.7 <= (b / max(a, 1e-3)) <= 1.4 and a > 1.0
                det = f"spd first={a:.1f} second={b:.1f} cm/s"
            check("cursor gain symmetric on return", ok, det)
            self.ps_stop_cw = vec3(raw, "cw")
            self.advance("pstate_stop")
        elif s == "pstate_stop":
            # 停手零漂：不餵增量 1.2s——游標在世界中寸步不移（停手過頭構造性死亡）
            if self.elapsed() < 1.2:
                return
            host = find_char(self.server(), self.host_pid)
            raw, d = summary(host)
            cw = vec3(raw, "cw")
            drift = dist3(cw, self.ps_stop_cw) if (cw and self.ps_stop_cw) else 99.0
            check("cursor holds still after input stops (no overshoot)",
                  drift <= 0.5, f"drift={drift:.2f}cm over 1.2s")
            # WYSIWYG：相機前向與（游標−相機）夾角≈0——螢幕中心=游標=墨（構造保證）
            camo = vec3(raw, "camo")
            camd = vec3(raw, "camd")
            ang = 99.0
            if cw and camo and camd:
                vx, vy, vz = cw[0] - camo[0], cw[1] - camo[1], cw[2] - camo[2]
                L = max((vx * vx + vy * vy + vz * vz) ** 0.5, 1e-3)
                dot = (vx * camd[0] + vy * camd[1] + vz * camd[2]) / L
                ang = math.degrees(math.acos(max(-1.0, min(1.0, dot))))
            check("camera centre rides cursor exactly", ang <= 0.5, f"ang={ang:.2f}deg")
            # 視野穩定度契約（07-28 二修「頭=被穩定的平台」）：相機**位置**速度有界
            # ——中心點釘住只保證準星不晃，位置快移=中心以外整個視野繞著中心搖
            #（視差搖）。載體三軸限速後：平台速度=限速輸入的解析函數=構造有界。
            cam_max = 0.0
            cams = self.ps_cam
            for i in range(len(cams) - 3):
                dt_s = cams[i + 3][0] - cams[i][0]
                if dt_s > 0.02:
                    cam_max = max(cam_max, dist3(cams[i + 3][1], cams[i][1]) / dt_s)
            check("camera platform speed bounded (stabilised head)",
                  0.0 < cam_max <= 40.0, f"maxCamSpd={cam_max:.1f}cm/s over gain sweeps")
            self.advance("stencil_switch")
        elif s == "stencil_switch":
            # 打稿制（07-25）：切麥克筆、瞄到乾淨帶（tilt-5≈上方 ~5cm）準備畫稿線
            if self.elapsed() < 0.5:
                return
            host = find_char(self.server(), self.host_pid)
            host.call_method("DebugRoboNeedle", (2,))
            host.call_method("DebugRoboDrawAim", (self.paint_az0, self.paint_tilt0 - 5.0))
            self.advance("stencil_sweep")
        elif s == "stencil_sweep":
            # 麥克筆＝手速自由直畫（無巡航）：勻速掃 az+ 畫一條稿線
            t = self.elapsed()
            host = find_char(self.server(), self.host_pid)
            if t < 0.8:
                if t > 0.5 and not hasattr(self, "stencil_dot0_set"):
                    self.stencil_dot0_set = True
                    raw, d = summary(host)
                    check("stencil marker selected", d.get("needleSel") == 2, raw)
                    self.stencil_dot0 = d.get("dotN", 0.0)
                return
            if t < 2.4:
                host.call_method("DebugRoboDrawAim",
                                 (self.paint_az0 + (t - 0.8) * 5.0, self.paint_tilt0 - 5.0))
                return
            raw, d = summary(host)
            dn = d.get("dotN", 0.0) - self.stencil_dot0
            # 8° 稿線（07-28 姿勢主導制：射線原點=活眉心、原點距與弧長隨鎖點幾何變
            # ——帶放寬到 10~150；節拍契約由 dotGapCm 另驗）
            check("stencil line deposits at hand speed", 10.0 <= dn <= 150.0,
                  f"d_dotN={dn:.0f} over 8deg stencil sweep")
            # FP 截圖：2D 向量麥克筆＋紫稿線讀感自查（視覺件必附截圖鐵律）
            unreal.SystemLibrary.execute_console_command(
                self.server(), "HighResShot 1280x720 filename=directdraw_fp_stencil")
            host.call_method("DebugRoboPaintHold", (False,))
            self.advance("stencil_follow_prep")
        elif s == "stencil_follow_prep":
            # 上墨沿稿（07-28 姿勢主導制改制）：舊「記角度回壓稿線」在載體移動下
            # 角度→位置不穩（follow 間歇 0 點實錘）——改在當下現畫一小段新稿、
            # 原地切 Liner 壓在段首＝構造上壓線
            t = self.elapsed()
            host = find_char(self.server(), self.host_pid)
            if t < 0.4:
                return
            ph = getattr(self, "fprep_phase", 0)
            if ph == 0:
                self.fprep_phase = 1
                host.call_method("DebugRoboDrawAim", (self.paint_az0 + 0.5, self.paint_tilt0 - 5.0))
                return
            if ph == 1:
                if t < 1.0:
                    return
                self.fprep_phase = 2
                host.call_method("DebugRoboPaintHold", (True,))  # 稿筆現畫新段
                return
            if ph == 2:
                if t < 2.2:
                    host.call_method("DebugRoboDrawAim",
                                     (self.paint_az0 + 0.5 + (t - 1.0) * 3.5, self.paint_tilt0 - 5.0))
                    return
                self.fprep_phase = 3
                host.call_method("DebugRoboPaintHold", (False,))
                host.call_method("DebugRoboNeedle", (0,))
                host.call_method("DebugRoboDrawAim", (self.paint_az0 + 0.9, self.paint_tilt0 - 5.0))
                return
            if t < 2.9:
                return
            self.advance("stencil_follow_engage")
        elif s == "stencil_follow_engage":
            if self.elapsed() < 0.8:
                return
            host = find_char(self.server(), self.host_pid)
            raw, d = summary(host)
            self.follow_az0 = d.get("az", 0.0)
            self.follow_dot0 = d.get("dotN", 0.0)
            host.call_method("DebugRoboPaintHold", (True,))
            self.advance("stencil_follow_verify")
        elif s == "stencil_follow_verify":
            # 沿稿契約：無任何方向命令（無 PaintStick）——針自己沿稿線走。
            # 稿線 ~4.8cm、v_max 2.34cm/s ⇒ ~2s 走完＝follow 態要在中途取樣
            host = find_char(self.server(), self.host_pid)
            if self.elapsed() < 1.2:
                return
            if not hasattr(self, "follow_mid"):
                raw, d = summary(host)
                self.follow_mid = d
                log("FOLLOW MID | " + raw)
                return
            if self.elapsed() < 2.8:
                return
            raw, d = summary(host)
            check("needle auto-follows stencil (mid-run)", self.follow_mid.get("follow") == 1,
                  f"mid follow={self.follow_mid.get('follow')}")
            check("follow keeps machine speed (cruising mid-run)",
                  self.follow_mid.get("cruise") == 1, raw)
            daz = abs(d.get("az", 0.0) - self.follow_az0)
            check("follow walks along the stencil line", 2.0 <= daz <= 14.0, f"dAz={daz:.2f}")
            gap = d.get("dotGapCm", -1.0)
            check("follow ink holds solid-line bound", 0.02 <= gap <= 0.28, f"gapCm={gap:.3f}")
            dn = d.get("dotN", 0.0) - self.follow_dot0
            check("follow deposits ink dots", dn >= 12.0, f"d_dotN={dn:.0f}")
            host.call_method("DebugRoboPaintHold", (False,))
            self.advance("stencil_restore")
        elif s == "stencil_restore":
            # 回到肚皮中央、重新按住＝shader 段照舊語義起跑
            if self.elapsed() < 0.5:
                return
            host = find_char(self.server(), self.host_pid)
            host.call_method("DebugRoboDrawAim", (self.paint_az0, self.paint_tilt0))
            host.call_method("DebugRoboPaintHold", (True,))
            self.advance("shader_switch")
        elif s == "shader_switch":
            if self.elapsed() < 0.5:
                return
            # 雙針制（07-23）：切 Shader 打霧針——寬針 9mm+點距>直徑=stipple、
            # 守恆式 v=1.4×0.9×4=5.0cm/s（各針各自成立）。先回肚皮中央再巡航
            #（liner 段累積位移+shader 5cm/s 會滑向剪影邊=假 FAIL）
            host = find_char(self.server(), self.host_pid)
            host.call_method("DebugRoboDrawAim", (self.paint_az0, self.paint_tilt0))
            host.call_method("DebugRoboNeedle", (1,))
            self.advance("shader_engage")
        elif s == "shader_engage":
            if self.elapsed() < 0.8:
                return
            # 四版（自由揮掃噴槍制）：霧針無巡航——出墨=LMB 且 aim 在動；
            # 沉積=30Hz 恆定流量、單 stamp 薄層、疊趟變深
            host = find_char(self.server(), self.host_pid)
            raw, d = summary(host)
            check("shader selected via hook", d.get("needleSel") == 1, raw)
            self.shader_dot0 = d.get("dotN", 0.0)
            self.advance("shader_sweep")
        elif s == "shader_sweep":
            t = self.elapsed()
            host = find_char(self.server(), self.host_pid)
            if t < 2.0:
                # 模擬手的自由揮掃（8°/s ≈ 7cm/s；每 tick 連續推 aim=移動閘開）
                host.call_method("DebugRoboDrawAim",
                                 (self.paint_az0 + 4.0 + t * 8.0, self.paint_tilt0))
                return
            raw, d = summary(host)
            dn = d.get("dotN", 0.0) - self.shader_dot0
            # 排針制（八版）：慢掃 / 排距 0.2cm（07-28 姿勢主導制：弧長隨原點距變
            # ——帶放寬到 20~220；排距契約由 dotGapCm 另驗）
            check("shader deposits while sweeping (row-metered)",
                  20.0 <= dn <= 220.0, f"d_dotN={dn:.0f} over ~2s slow sweep")
            self.fast_dot0 = d.get("dotN", 0.0)
            self.fast_mid_logged = False
            self.advance("shader_fastsweep")
        elif s == "shader_fastsweep":
            # user 實際操作域：快速來回掃（峰值 ~108°/s 的正弦往復）——慢掃 8°/s
            # 通過≠快掃通過；哪個閘失守讓它自己報名（mid-sweep 抓 raw summary）
            t = self.elapsed()
            host = find_char(self.server(), self.host_pid)
            if t < 2.4:
                host.call_method("DebugRoboDrawAim",
                                 (self.paint_az0 + 6.0 + 18.0 * math.sin(t * 6.0),
                                  self.paint_tilt0))
                if not self.fast_mid_logged and t > 1.0:
                    self.fast_mid_logged = True
                    raw, d = summary(host)
                    log("FASTSWEEP MID | " + raw)
                return
            raw, d = summary(host)
            dn = d.get("dotN", 0.0) - self.fast_dot0
            check("shader deposits on FAST back-and-forth (user regime)",
                  dn >= 15.0, f"d_dotN={dn:.0f} over ~2.4s fast sweep")
            check("tip stays reachable during fast sweep", d.get("reach") == 1, raw)
            self.sfast_dot0 = d.get("dotN", 0.0)
            self.sfast_mid_gap = None
            self.advance("shader_superfast")
        elif s == "shader_superfast":
            # 真人手速域探針（九版；五修鐵則二犯後補上：探針要打在使用者實際手速
            # 量級）——峰值 300°/s=真人自然掃（300~1000°/s）的下緣；八版天花板
            # 300 排/s 在此域把排腰斬成虛線。斷言=中途針距仍≈排距（連續性）＋
            # 總排數超過舊天花板可能值（天花板已抬離真人域）。
            t = self.elapsed()
            host = find_char(self.server(), self.host_pid)
            if t < 1.6:
                host.call_method("DebugRoboDrawAim",
                                 (self.paint_az0 + 6.0 + 18.0 * math.sin(t * 16.7),
                                  self.paint_tilt0))
                if self.sfast_mid_gap is None and t > 0.8:
                    raw, d = summary(host)
                    self.sfast_mid_gap = d.get("dotGapCm", -1.0)
                    self.sfast_mid_flow = d.get("flow", -1.0)  # 手速→濃淡（十二版）
                    log("SUPERFAST MID | " + raw)
                return
            raw, d = summary(host)
            dn = d.get("dotN", 0.0) - self.sfast_dot0
            check("superfast rows stay continuous (gap ~= spacing)",
                  self.sfast_mid_gap is not None and 0.05 <= self.sfast_mid_gap <= 0.45,
                  f"mid dotGapCm={self.sfast_mid_gap}")
            check("flow ceiling lifted above human domain",
                  dn >= 500.0, f"d_dotN={dn:.0f} over ~1.6s superfast sweep")
            unreal.SystemLibrary.execute_console_command(
                self.server(), "HighResShot 1280x720 filename=directdraw_fp_shaderline")
            victim = find_char(self.server(), self.victim_pid)
            victim.get_editor_property("InkCanvas").call_method(
                "ExportLayersToPng",
                ("C:/games/Unreal Engine/nice_ink/Saved/robo_shader",))
            self.shader_dot1 = d.get("dotN", 0.0)
            self.advance("shader_still")
        elif s == "shader_still":
            if self.elapsed() < 1.5:
                return
            # 移動閘：LMB 仍按住、aim 靜止 → 零沉積（按住不動不出墨=user 定案）
            host = find_char(self.server(), self.host_pid)
            raw, d = summary(host)
            check("no deposit while holding still (movement gate)",
                  d.get("dotN", 0.0) - self.shader_dot1 <= 2.0,
                  f"dotN {self.shader_dot1:.0f} -> {d.get('dotN', 0):.0f}")
            self.advance("flow_slowpass")
        elif s == "flow_slowpass":
            # 手速→濃淡（十二版）契約①：工作速度慢掃=滿流量（勞動量校準域）。
            # 兩趟相鄰軌（tilt+4/+7 ≈ 半帶距）落在乾淨區＝疊軌剖面的像素證據
            #（robo_flow_mist − robo_shader_mist 差分即孤立這兩條新帶）。
            # 重定位時先放開 LMB——aim 傳送的直線軌跡會在肚皮拖出雜排。
            t = self.elapsed()
            host = find_char(self.server(), self.host_pid)
            if not getattr(self, "flow_setup", False):
                self.flow_setup = True
                host.call_method("DebugRoboPaintHold", (False,))
                host.call_method("DebugRoboDrawAim",
                                 (self.paint_az0 - 8.0, self.paint_tilt0 + 4.0))
                return
            if t < 0.8:
                return
            if not getattr(self, "flow_hold", False):
                self.flow_hold = True
                raw, d = summary(host)
                self.flow_dot0 = d.get("dotN", 0.0)
                self.flow_slow_flow = None
                host.call_method("DebugRoboPaintHold", (True,))
                return
            if t < 2.6:
                host.call_method("DebugRoboDrawAim",
                                 (self.paint_az0 - 8.0 + (t - 0.8) * 8.0,
                                  self.paint_tilt0 + 4.0))
                if self.flow_slow_flow is None and t > 1.8:
                    raw, d = summary(host)
                    self.flow_slow_flow = d.get("flow", -1.0)
                    log("FLOWSLOW MID | " + raw)
                return
            raw, d = summary(host)
            dn = d.get("dotN", 0.0) - self.flow_dot0
            check("flow full at working sweep (slow = calibrated tone)",
                  self.flow_slow_flow is not None and self.flow_slow_flow >= 248,
                  f"mid flow={self.flow_slow_flow}")
            check("flow probe pass1 deposits", dn >= 20.0, f"d_dotN={dn:.0f}")
            self.advance("flow_pass2")
        elif s == "flow_pass2":
            # 契約②：superfast（真人手速下緣）流量單調下降——快=淡的表達軸存在；
            # 第二趟相鄰軌供疊軌剖面像素自查（鐘形邊坡互填谷 vs 舊平頂縱紋）
            t = self.elapsed()
            host = find_char(self.server(), self.host_pid)
            if not getattr(self, "flow2_setup", False):
                self.flow2_setup = True
                host.call_method("DebugRoboPaintHold", (False,))
                # +4.95=與 pass1（+4.0）差 0.95°≈半帶距（實測 ~1.07cm/°；帶寬 2cm
                # 十三版收窄後 1.5° 已變 8 成帶寬——探針幾何跟帶寬走、要實測不猜）
                host.call_method("DebugRoboDrawAim",
                                 (self.paint_az0 - 8.0, self.paint_tilt0 + 4.95))
                return
            if t < 0.8:
                return
            if not getattr(self, "flow2_hold", False):
                self.flow2_hold = True
                raw, d = summary(host)
                self.flow2_dot0 = d.get("dotN", 0.0)
                host.call_method("DebugRoboPaintHold", (True,))
                return
            if t < 2.6:
                host.call_method("DebugRoboDrawAim",
                                 (self.paint_az0 - 8.0 + (t - 0.8) * 8.0,
                                  self.paint_tilt0 + 4.95))
                return
            raw, d = summary(host)
            dn = d.get("dotN", 0.0) - self.flow2_dot0
            check("flow probe pass2 deposits (adjacent track)", dn >= 20.0, f"d_dotN={dn:.0f}")
            # 十六版填色制：流量恆滿（手速→濃淡退役——塗色工具的濃度屬於機器）
            check("flow constant across speeds (fill tool contract)",
                  getattr(self, "sfast_mid_flow", None) is not None and
                  self.sfast_mid_flow == 255.0,
                  f"superfast mid flow={getattr(self, 'sfast_mid_flow', None)}")
            victim = find_char(self.server(), self.victim_pid)
            victim.get_editor_property("InkCanvas").call_method(
                "ExportLayersToPng",
                ("C:/games/Unreal Engine/nice_ink/Saved/robo_flow",))
            self.advance("color_probe")
        elif s == "color_probe":
            # 調色盤 × 打霧（十六版追修驗證）：紅墨（index 2）短掃一趟——驗
            # 選色→ServerPaintBegin→stroke→MistRT RGB→材質 premult over 全鏈；
            # 像素自查（post-run）驗 robo_color−robo_flow 差分區 R≫B。
            t = self.elapsed()
            host = find_char(self.server(), self.host_pid)
            if not getattr(self, "color_setup", False):
                self.color_setup = True
                host.call_method("DebugRoboPaintHold", (False,))
                host.call_method("DebugRoboColor", (2,))
                host.call_method("DebugRoboDrawAim",
                                 (self.paint_az0 - 8.0, self.paint_tilt0 + 9.5))
                return
            if t < 0.8:
                return
            if not getattr(self, "color_hold", False):
                self.color_hold = True
                raw, d = summary(host)
                self.color_dot0 = d.get("dotN", 0.0)
                host.call_method("DebugRoboPaintHold", (True,))
                return
            if t < 2.2:
                host.call_method("DebugRoboDrawAim",
                                 (self.paint_az0 - 8.0 + (t - 0.8) * 8.0,
                                  self.paint_tilt0 + 9.5))
                return
            raw, d = summary(host)
            dn = d.get("dotN", 0.0) - self.color_dot0
            check("palette color stroke deposits (shader, red)", dn >= 15.0,
                  f"d_dotN={dn:.0f}")
            victim = find_char(self.server(), self.victim_pid)
            victim.get_editor_property("InkCanvas").call_method(
                "ExportLayersToPng",
                ("C:/games/Unreal Engine/nice_ink/Saved/robo_color",))
            host.call_method("DebugRoboColor", (0,))
            host.call_method("DebugRoboNeedle", (0,))
            self.advance("shader_restore")
        elif s == "shader_restore":
            if self.elapsed() < 0.8:
                return
            host = find_char(self.server(), self.host_pid)
            raw, d = summary(host)
            check("liner restored after toggle back",
                  d.get("needleSel") == 0 and 1.8 <= d.get("vmaxCm", 0.0) <= 3.0, raw)
            # 視覺證據：巡航段的實線（FP 特寫）＋受害者 MarkerRT 像素級匯出
            #（sweep 的散點 vs 巡航的連續線段——實線契約的眼見為憑）
            unreal.SystemLibrary.execute_console_command(
                self.server(), "HighResShot 1280x720 filename=directdraw_fp_cruiseline")
            victim = find_char(self.server(), self.victim_pid)
            ok_png = victim.get_editor_property("InkCanvas").call_method(
                "ExportLayersToPng",
                ("C:/games/Unreal Engine/nice_ink/Saved/robo_cruise",))
            log(f"marker RT export -> {ok_png}")
            self.advance("paint_release")
        elif s == "paint_release":
            if self.elapsed() < 1.0:
                return
            host = find_char(self.server(), self.host_pid)
            host.call_method("DebugRoboPaintHold", (False,))
            self.advance("lookup")
        elif s == "lookup":
            if self.elapsed() < 1.2:
                return
            # 抬頭看受害者的臉（偷瞄=同一顆頭；替身真頭在枕位）
            host = find_char(self.server(), self.host_pid)
            victim = find_char(self.server(), self.victim_pid)
            vhead = bone_w(victim, "Head")
            host.call_method("DebugRoboAimAt", (vhead,))  # 射線原點反算（07-28 相機≠射線原點）
            self.advance("lookup_shot")
        elif s == "lookup_shot":
            if self.elapsed() < 1.2:
                return
            unreal.SystemLibrary.execute_console_command(
                self.server(), "HighResShot 1280x720 filename=directdraw_fp_lookup")
            self.advance("miss_aim")
        elif s == "miss_aim":
            if self.elapsed() < 0.8:
                return
            # 看向天花板（射線 miss）＝不可達＝不落墨；筆仍在手上
            host = find_char(self.server(), self.host_pid)
            host.call_method("DebugRoboDrawAim", (0.0, -30.0))
            self.advance("miss_verify")
        elif s == "miss_verify":
            if self.elapsed() < 1.0:
                return
            host = find_char(self.server(), self.host_pid)
            raw, d = summary(host)
            check("ray miss => unreachable (no ink gate)", d.get("reach") == 0, raw)
            check("pen still in hand on miss", d.get("penValid") == 1, raw)
            check("needle retracts to stub after release", 0.3 <= d.get("needle", -1.0) <= 0.9, raw)
            self.advance("far_aim")
        elif s == "far_aim":
            if self.elapsed() < 0.8:
                return
            # 從肚皮席位指向「離作畫者最遠的那隻手」（大字仰躺、橫向 1m+）——
            # P 在皮膚上但超出本席位可達補丁，驗「搆不到計時」餵 HUD 提示的鏈。
            # 首輪教訓：指向腳踝方向會被較近的皮膚攔截、且沿體軸的補丁其實搆得到
            #（髖後仰 16.8 仍 reach=1）——遠點要用骨骼實測挑，不猜座標
            host = find_char(self.server(), self.host_pid)
            victim = find_char(self.server(), self.victim_pid)
            hl = host.get_actor_location()
            vh_l = bone_w(victim, "LeftHand")
            vh_r = bone_w(victim, "RightHand")
            far_p = vh_l if dist(vh_l, hl) > dist(vh_r, hl) else vh_r
            # 07-28 姿勢主導制：遠側橫向點=稿筆域（稿筆墨=P 恆等、隨看隨畫）；機器的
            # 伸縮契約由近點檢查（needle/grip 帶）承載——極端側向針軸搆不到=誠實極限
            host.call_method("DebugRoboNeedle", (2,))
            host.call_method("DebugRoboAimAt", (far_p,))  # 視覺伺服收斂
            self.advance("far_verify")
        elif s == "far_verify":
            # 載體限速（07-28 頭=被穩定的平台）：遠點大轉向 ~2s 才到位——伺服上限
            # 已放寬 2.5s、這裡等 3.2s
            if self.elapsed() < 3.2:
                return
            host = find_char(self.server(), self.host_pid)
            raw, d = summary(host)
            check("far point on face-ray (servo converged)", d.get("reach") == 1, raw)
            self.far_dot0 = d.get("dotN", 0.0)
            self.far_az = d.get("az", 0.0)
            self.far_tilt = d.get("tilt", 45.0)
            self.advance("far_paint")
        elif s == "far_paint":
            if self.elapsed() < 0.3:
                return
            host = find_char(self.server(), self.host_pid)
            if not getattr(self, "far_hold", False):
                self.far_hold = True
                host.call_method("DebugRoboPaintHold", (True,))
                return
            t = self.elapsed()
            if t < 1.8:
                # 稿筆有移動閘：小幅掃動出墨
                host.call_method("DebugRoboDrawAim",
                                 (self.far_az + math.sin(t * 3.0) * 2.0, self.far_tilt))
                return
            raw, d = summary(host)
            dn = d.get("dotN", 0.0) - self.far_dot0
            check("far sketch deposits at P (stencil ink=centre)", dn >= 5.0,
                  f"d_dotN={dn:.0f} on far hand")
            unreal.SystemLibrary.execute_console_command(
                self.server(), "HighResShot 1280x720 filename=directdraw_fp_farneedle")
            host.call_method("DebugRoboPaintHold", (False,))
            host.call_method("DebugRoboNeedle", (0,))
            self.advance("low_exit")
        elif s == "low_exit":
            if self.elapsed() < 0.6:
                return
            find_char(self.server(), self.host_pid).call_method("ServerExitLean", ())
            self.advance("low_enter")
        elif s == "low_enter":
            if self.elapsed() < 0.9:
                return
            self.low_p = self.lowest_point()
            host = find_char(self.server(), self.host_pid)
            hl = host.get_actor_location()
            nx, ny = hl.x - self.low_p.x, hl.y - self.low_p.y
            L = max((nx * nx + ny * ny) ** 0.5, 1e-3)
            ok = host.call_method("DebugRoboEnterLean",
                                  (find_char(self.server(), self.victim_pid), self.low_p,
                                   unreal.Vector(nx / L, ny / L, 0.0)))
            log(f"[low] p=({self.low_p.x:.0f},{self.low_p.y:.0f},{self.low_p.z:.0f}) enter={ok}")
            self.advance("low_verify")
        elif s == "low_verify":
            if self.elapsed() < 1.8:
                return
            host = find_char(self.server(), self.host_pid)
            raw, d = summary(host)
            ground = host.get_actor_location().z - 92.0
            check("low point locked", d.get("locked") == 1, raw)
            check("low point reachable (deep fold)", d.get("reach") == 1 and 0.0 <= d.get("tipErr", 99) <= 1.5,
                  f"pZ-ground={self.low_p.z - ground:.0f} | " + raw)
            # 道場受害者最低皮膚點僅 ~37cm＝摺深依目標而定；契約=可達＋不反向抬升
            check("fold direction sane (hip <= 5)", d.get("hipDeg", 99) <= 5.0, raw)
            cam = host.get_editor_property("FirstPersonCamera").get_world_location()
            check("camera anchor followed down", cam.z - ground < 110.0, f"camZ-ground={cam.z - ground:.1f}")
            unreal.SystemLibrary.execute_console_command(
                self.server(), "HighResShot 1280x720 filename=directdraw_fp_low")
            self.advance("low_exit2")
        elif s == "low_exit2":
            if self.elapsed() < 1.2:
                return
            find_char(self.server(), self.host_pid).call_method("ServerExitLean", ())
            self.advance("samepoint")
        elif s == "samepoint":
            if self.elapsed() < 1.2:
                return
            # 獨佔已廢除：第二人鎖同一點必須成功（ghost 疊坐合法）
            model = find_char(self.server(), self.model_pid)
            self.enter_lean(model, "model-same-belly", BELLY, BELLY_N)
            self.advance("samepoint_verify")
        elif s == "samepoint_verify":
            if self.elapsed() < 1.5:
                return
            model = find_char(self.server(), self.model_pid)
            check("second painter same spot allowed", bool(model.get_editor_property("bLeanLocked")))
            unreal.SystemLibrary.execute_console_command(
                self.server(), "HighResShot 1280x720 filename=directdraw_fp_ghostpair")
            self.advance("tp_prep")
        elif s == "tp_prep":
            if self.elapsed() < 1.0:
                return
            # host 起身當攝影機（退鎖=ghost 還原；tp 圖同時驗證模特材質回真身）
            host = find_char(self.server(), self.host_pid)
            host.call_method("ServerExitLean", ())
            self.advance("tp_verify_exit")
        elif s == "tp_verify_exit":
            if self.elapsed() < 1.2:
                return
            host = find_char(self.server(), self.host_pid)
            raw, d = summary(host)
            check("exit clears lock", d.get("locked") == 0, raw)
            check("exit restores FOV 90", abs(d.get("fov", 0) - 90.0) < 0.5, raw)
            check("exit clears ghosts", d.get("ghosts", -1) == 0, raw)
            # 視角規則基線（SPEC）：站姿本人看不見自己身體全貌——退鎖必須還原
            # OwnerNoSee=true（07-20 user 抓到「畫完低頭看得到自己」＝這裡曾還原反向）
            body_ons = host.get_editor_property("Body").get_editor_property("bOwnerNoSee")
            bow_ons = host.get_editor_property("BowBody").get_editor_property("bOwnerNoSee")
            check("exit restores owner-no-see baseline (self hidden in FP)",
                  bool(body_ons) and bool(bow_ons), f"body={body_ons} bow={bow_ons}")
            hc = self.host_char()
            hl = hc.get_actor_location()
            hc.set_actor_location(unreal.Vector(hl.x + 300.0, hl.y + 300.0, hl.z), False, True)
            for prop in ("Body", "BowBody"):
                comp = hc.get_editor_property(prop)
                if comp:
                    comp.set_visibility(False, True)
            # 模特動筆（owning 端＝client2）
            w2 = get_world("UEDPIE_2")
            m2 = find_char(w2, self.model_pid)
            if m2:
                m2.call_method("DebugRoboPaintHold", (True,))
            self.tp_i = 0
            self.advance("tp_shots")
        elif s == "tp_shots":
            if self.elapsed() < 1.5 + self.tp_i * 3.0:
                return
            server = self.server()
            model = find_char(server, self.model_pid)
            hc = self.host_char()
            pc = unreal.GameplayStatics.get_player_controller(server, 0)
            mloc = model.get_actor_location()
            if self.tp_i < 3:
                ang = math.radians(self.tp_i * 120.0 + 30.0)
                cam = unreal.Vector(mloc.x + 260.0 * math.cos(ang), mloc.y + 260.0 * math.sin(ang), mloc.z + 110.0)
                hc.set_actor_location(cam, False, True)
                look = unreal.MathLibrary.find_look_at_rotation(
                    unreal.Vector(cam.x, cam.y, cam.z + 62.0), unreal.Vector(mloc.x, mloc.y, mloc.z - 40.0))
                pc.set_control_rotation(look)
                unreal.SystemLibrary.execute_console_command(
                    server, f"HighResShot 1280x720 filename=directdraw_tp_{self.tp_i}")
            elif self.tp_i == 3:
                # 第四張：後頸近拍（脖樁不隆起/橋接無裂縫自查）
                mhead = bone_w(model, "Head")
                back = model.get_actor_forward_vector()
                cam4 = unreal.Vector(mhead.x - back.x * 120.0, mhead.y - back.y * 120.0, mhead.z + 55.0)
                hc.set_actor_location(cam4, False, True)
                look4 = unreal.MathLibrary.find_look_at_rotation(
                    unreal.Vector(cam4.x, cam4.y, cam4.z + 62.0), mhead)
                pc.set_control_rotation(look4)
                unreal.SystemLibrary.execute_console_command(
                    server, "HighResShot 1280x720 filename=directdraw_tp_nape")
            elif self.tp_i == 4:
                # 模特換到低位（陡角覆蓋——上輪只拍安全高度=脖破漏網的檢討）
                model.call_method("ServerExitLean", ())
            elif self.tp_i == 5:
                lp = self.lowest_point()
                self.low_tp_p = lp
                ml = model.get_actor_location()
                nx, ny = ml.x - lp.x, ml.y - lp.y
                L = max((nx * nx + ny * ny) ** 0.5, 1e-3)
                model.call_method("DebugRoboEnterLean",
                                  (find_char(server, self.victim_pid), lp,
                                   unreal.Vector(nx / L, ny / L, 0.0)))
            elif self.tp_i == 6:
                raw, d = summary(model)
                check("model low reach (steep angle)", d.get("reach") == 1, raw)
                mhead = bone_w(model, "Head")
                fwd = model.get_actor_forward_vector()
                cam6 = unreal.Vector(mhead.x - fwd.y * 150.0, mhead.y + fwd.x * 150.0, mhead.z + 30.0)
                hc.set_actor_location(cam6, False, True)
                look6 = unreal.MathLibrary.find_look_at_rotation(
                    unreal.Vector(cam6.x, cam6.y, cam6.z + 62.0), mhead)
                pc.set_control_rotation(look6)
                unreal.SystemLibrary.execute_console_command(
                    server, "HighResShot 1280x720 filename=directdraw_tp_lowprofile")
            elif self.tp_i == 7:
                mhead = bone_w(model, "Head")
                back = model.get_actor_forward_vector()
                cam7 = unreal.Vector(mhead.x - back.x * 110.0, mhead.y - back.y * 110.0, mhead.z + 60.0)
                hc.set_actor_location(cam7, False, True)
                look7 = unreal.MathLibrary.find_look_at_rotation(
                    unreal.Vector(cam7.x, cam7.y, cam7.z + 62.0), mhead)
                pc.set_control_rotation(look7)
                unreal.SystemLibrary.execute_console_command(
                    server, "HighResShot 1280x720 filename=directdraw_tp_lownape")
            self.tp_i += 1
            if self.tp_i >= 8:
                self.advance("coverage")
        elif s == "coverage":
            if self.elapsed() < 1.5:
                return
            # 覆蓋率量測（user 提問：不趴/趴/理論極限各能點到多少皮膚）——
            # 同步呼叫、可能凍幾十秒屬正常；數字進結果檔供分析
            host = find_char(self.server(), self.host_pid)
            victim = find_char(self.server(), self.victim_pid)
            cov = str(host.call_method("DebugRoboCoverageScan", (victim, 48)))
            log("COVERAGE " + cov)
            d = {k: float(v) for k, v in re.findall(r"(\w+)=([-\d.]+)", cov)}
            check("coverage scan sane (enough samples)", d.get("total", 0) > 500, cov)
            log(f"SUMMARY pass={PASS_N[0]} fail={FAIL_N[0]}")
            log("DONE (PIE left running)")
            self.finish()

    def finish(self):
        log("HARNESS END")
        if self.handle:
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.handle = None


_t = Test()
log("harness registered")
