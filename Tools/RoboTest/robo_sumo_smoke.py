# Sumo 身體冒煙測試（Python StartupScript 掛載；tick 驅動狀態機）
# 驗證：PIE 出生網格=SM_Sumo、睡姿翻轉朝向（鼻/臀 z 探針）、地標解析距離、
#       畫墨全鏈（UV 解析→筆劃→跨客戶端複寫→RT 匯出）、BowBody=SK_Sumo、
#       左右手軸向實測探針（LeftHand/RightHand/HandProp 世界座標）。
# 產出：scratchpad/robo_sumo_smoke_result.txt + Saved/Screenshots 連拍
import unreal, time, os, traceback

OUT = r"C:\games\Unreal Engine\nice_ink\Saved\robo_sumo_smoke_result.txt"
os.makedirs(os.path.dirname(OUT), exist_ok=True)
LINES = []


def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[SUMOSMOKE] " + str(msg))
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(LINES))


def get_world(tag):
    for w in unreal.ObjectIterator(unreal.World):
        if tag in w.get_path_name():
            return w
    return None


def gs_of(w):
    return unreal.GameplayStatics.get_game_state(w)


def chars_of(w):
    return list(unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NiceInkCharacter))


def find_char(w, pid):
    for c in chars_of(w):
        ps = c.get_editor_property("player_state")
        if ps and ps.get_editor_property("player_id") == pid:
            return c
    return None


def vec(x, y, z):
    return unreal.Vector(x, y, z)


def qpoint(p):
    q = unreal.Vector_NetQuantize()
    q.set_editor_property("x", p.x)
    q.set_editor_property("y", p.y)
    q.set_editor_property("z", p.z)
    return q


def qnormal(x, y, z):
    q = unreal.Vector_NetQuantizeNormal()
    q.set_editor_property("x", x)
    q.set_editor_property("y", y)
    q.set_editor_property("z", z)
    return q


# 本地地標（Blender 量測 y 取負後的 UE 本地值；NiceInkCharacter.cpp 同源）
LM_NOSE = vec(0.0, 22.0, 151.0)
LM_CROTCH = vec(0.0, 44.0, 75.0)
LM_BUTT = vec(0.0, -52.0, 66.0)
LM_BELLY = vec(0.0, 40.0, 103.0)


class Test:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.artist_pid = None
        self.victim_pid = None
        self.lean_uv = None
        self.shot_count = 0
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

    def step(self):
        s = self.stage
        if s == "boot":
            if time.monotonic() - self.t0 > 8.0:
                unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
                self.advance("wait_pie")
        elif s == "wait_pie":
            server = get_world("UEDPIE_0")
            if server and unreal.GameplayStatics.get_game_mode(server):
                self.advance("wait_drawing")
            elif self.elapsed() > 60.0:
                log("FAIL: PIE never started")
                self.finish()
        elif s == "wait_drawing":
            server = get_world("UEDPIE_0")
            gs = gs_of(server)
            if gs and str(gs.get_editor_property("CurrentPhase")).endswith("DRAWING: 3>"):
                self.victim_pid = gs.get_editor_property("VictimPlayerId")
                for c in chars_of(server):
                    pid = c.get_editor_property("player_state").get_editor_property("player_id")
                    if pid != self.victim_pid:
                        self.artist_pid = pid
                        break
                log(f"victim={self.victim_pid} artist={self.artist_pid}")
                self.advance("body_check")
            elif self.elapsed() > 60.0:
                log("FAIL: drawing phase never came")
                self.finish()
        elif s == "body_check":
            if self.elapsed() < 1.0:
                return
            # 三個世界全部角色：網格必須是 SM_Sumo、scale=1
            ok = True
            for tag in ("UEDPIE_0", "UEDPIE_1", "UEDPIE_2"):
                w = get_world(tag)
                if not w:
                    ok = False
                    log(f"{tag}: world missing")
                    continue
                for c in chars_of(w):
                    body = c.get_editor_property("Body")
                    mesh = body.get_editor_property("static_mesh")
                    mname = mesh.get_name() if mesh else None
                    sc = c.get_actor_scale3d()
                    pid = c.get_editor_property("player_state").get_editor_property("player_id")
                    good = (mname == "SM_Sumo" and abs(sc.x - 1) < 0.01
                            and abs(sc.y - 1) < 0.01 and abs(sc.z - 1) < 0.01)
                    ok = ok and good
                    log(f"{tag} pid={pid} mesh={mname} scale=({sc.x:.2f},{sc.y:.2f},{sc.z:.2f})"
                        + ("" if good else " <-- BAD"))
            log("BODY MESH CHECK: " + ("PASS" if ok else "FAIL"))
            self.advance("sleep_orientation")
        elif s == "sleep_orientation":
            # 受害者已入睡（Drawing 相位）：驗證睡姿翻轉與朝向
            server = get_world("UEDPIE_0")
            victim = find_char(server, self.victim_pid)
            body = victim.get_editor_property("Body")
            rot = body.get_editor_property("relative_rotation")
            log(f"victim body rel rot: pitch={rot.pitch:.1f} yaw={rot.yaw:.1f} roll={rot.roll:.1f}"
                f" (expect lie 0/90/-90)")
            xf = body.get_world_transform()
            nose_w = xf.transform_location(LM_NOSE)
            butt_w = xf.transform_location(LM_BUTT)
            crotch_w = xf.transform_location(LM_CROTCH)
            face_up = nose_w.z > butt_w.z and crotch_w.z > butt_w.z
            log(f"nose z={nose_w.z:.1f} crotch z={crotch_w.z:.1f} butt z={butt_w.z:.1f}"
                f" -> face-up {'PASS' if face_up else 'FAIL'}")
            eyes = body.are_eyes_closed()
            log(f"victim eyes closed: {eyes} " + ("PASS" if eyes else "FAIL"))
            # 地標解析距離（地標常數是否貼在 sumo 皮膚上）
            for name, lm in (("nose", LM_NOSE), ("crotch", LM_CROTCH),
                             ("butt", LM_BUTT), ("belly", LM_BELLY)):
                w_pos = xf.transform_location(lm)
                diag = body.debug_resolve_body_uv(w_pos)
                log(f"landmark {name}: {diag}")
            self.advance("resolve_stroke")
        elif s == "resolve_stroke":
            if self.elapsed() < 1.0:
                return
            # 肚皮上解析一個 UV → 用 Debug hook 畫一條線（真網路 multicast）
            server = get_world("UEDPIE_0")
            victim = find_char(server, self.victim_pid)
            body = victim.get_editor_property("Body")
            xf = body.get_world_transform()
            belly_w = xf.transform_location(LM_BELLY)
            # UE python 慣例：bool+out 參數的 UFUNCTION 只回傳裸 out 值（同 find_collision_uv）
            uv = body.resolve_body_uv(belly_w, 30.0)
            log(f"belly UV resolve: uv=({uv.x:.4f},{uv.y:.4f})")
            self.lean_uv = uv
            gm = unreal.GameplayStatics.get_game_mode(server)
            gm.debug_robo_stroke(uv, unreal.Vector2D(uv.x + 0.03, uv.y + 0.012), 2)
            self.advance("verify_stroke")
        elif s == "verify_stroke":
            if self.elapsed() < 2.5:
                return
            w2 = get_world("UEDPIE_2")
            victim2 = find_char(w2, self.victim_pid)
            works = victim2.get_editor_property("InkCanvas").get_works()
            n = len(works)
            log(f"client-2 stroke works={n} " + ("PASS" if n >= 1 else "FAIL"))
            # 筆劃 UV 反解回世界（雙向解算煙測）
            server = get_world("UEDPIE_0")
            victim = find_char(server, self.victim_pid)
            body = victim.get_editor_property("Body")
            wpos = body.resolve_uv_to_world(self.lean_uv)
            log(f"UV->world roundtrip: pos=({wpos.x:.1f},{wpos.y:.1f},{wpos.z:.1f})")
            out = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()) + "/InkQA/sumosmoke"
            victim.get_editor_property("InkCanvas").export_layers_to_png(out)
            log("RT exported to Saved/InkQA/sumosmoke_*")
            self.advance("enter_lean")
        elif s == "enter_lean":
            if self.elapsed() < 1.0:
                return
            # 作畫者貼臉鎖定到肚皮（躺姿朝上 → 法線 +Z）
            server = get_world("UEDPIE_0")
            victim = find_char(server, self.victim_pid)
            artist = find_char(server, self.artist_pid)
            body = victim.get_editor_property("Body")
            point = body.get_world_transform().transform_location(LM_BELLY)
            artist.call_method("ServerEnterLean", (victim, qpoint(point), qnormal(0.0, 0.0, 1.0)))
            self.advance("bow_probe")
        elif s == "bow_probe":
            if self.elapsed() < 2.0:
                return
            server = get_world("UEDPIE_0")
            artist = find_char(server, self.artist_pid)
            locked = artist.get_editor_property("bLeanLocked")
            bow = artist.get_editor_property("BowBody")
            asset = bow.get_skinned_asset() if bow else None
            log(f"lean locked={locked} bowAsset={asset.get_name() if asset else None} "
                + ("PASS" if (locked and asset and asset.get_name() == "SK_Sumo") else "FAIL"))
            try:
                n = bow.get_num_bones()
                log(f"bow bones: {n}")
                # 頭到落筆點距離（彎腰解算沿用 char17 常數 → 我-13 前允許 PARTIAL）
                head = bow.get_bone_transform_by_name("Head", unreal.BoneSpaces.WORLD_SPACE).translation
                lp = artist.get_editor_property("LeanPoint")
                d = ((head.x - lp.x) ** 2 + (head.y - lp.y) ** 2 + (head.z - lp.z) ** 2) ** 0.5
                log(f"head-to-lockpoint: {d:.1f} cm " + ("PASS(<32)" if d < 32.0 else "PARTIAL (我-13 彎腰重調前可接受)"))
                # 左右手軸向實測探針：世界座標 + 相對角色右向量的側別
                aloc = artist.get_actor_location()
                right = artist.get_actor_right_vector()
                fwd = artist.get_actor_forward_vector()
                for bone in ("LeftHand", "RightHand", "LeftHandProp", "RightHandProp",
                             "LeftFoot", "RightFoot"):
                    try:
                        bt = bow.get_bone_transform_by_name(bone, unreal.BoneSpaces.WORLD_SPACE)
                        p = bt.translation
                        rel = vec(p.x - aloc.x, p.y - aloc.y, p.z - aloc.z)
                        side = rel.x * right.x + rel.y * right.y + rel.z * right.z
                        front = rel.x * fwd.x + rel.y * fwd.y + rel.z * fwd.z
                        log(f"bone {bone}: world=({p.x:.1f},{p.y:.1f},{p.z:.1f}) "
                            f"side={side:+.1f} (右+/左-) front={front:+.1f} z={rel.z:+.1f}")
                    except Exception as e:
                        log(f"bone {bone}: ERR {e}")
            except Exception as e:
                log("bow probe err: " + str(e))
            self.advance("photo_lie")
        elif s == "photo_lie":
            if self.elapsed() < 2.0 + self.shot_count * 4.0:
                return
            # 主機鏡頭對準躺著的受害者（順便拍到彎腰作畫者）
            server = get_world("UEDPIE_0")
            victim = find_char(server, self.victim_pid)
            host = unreal.GameplayStatics.get_player_pawn(server, 0)
            pc = unreal.GameplayStatics.get_player_controller(server, 0)
            if victim and host and pc:
                vloc = victim.get_actor_location()
                offsets = ((150.0, 130.0, 80.0), (-160.0, 120.0, 90.0), (0.0, -170.0, 100.0), (120.0, 0.0, 150.0))
                off = offsets[self.shot_count % len(offsets)]
                cam = vec(vloc.x + off[0], vloc.y + off[1], vloc.z + off[2])
                host.set_actor_location(cam, False, True)
                look = unreal.MathLibrary.find_look_at_rotation(
                    vec(cam.x, cam.y, cam.z + 62.0), vec(vloc.x, vloc.y, vloc.z - 60.0))
                pc.set_control_rotation(look)
                unreal.SystemLibrary.execute_console_command(
                    server, f"HighResShot 1280x720 filename=sumosmoke_{self.shot_count}")
            self.shot_count += 1
            if self.shot_count >= 4:
                log("photo burst done")
                log("DONE")
                self.finish()

    def finish(self):
        if self.handle:
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.handle = None
        with open(OUT, "w", encoding="utf-8") as f:
            f.write("\n".join(LINES))


_t = Test()
log("harness registered")
