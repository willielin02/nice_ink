# Lean-lock 自駕測試（Python StartupScript 掛載；tick 驅動狀態機）
# 產出：scratchpad/robo_leanlock_result.txt + Saved/InkQA/leanlock_*.png
import unreal, time, os, traceback

OUT = r"C:\Users\willi\AppData\Local\Temp\claude\c--games-Unreal-Engine-nice-ink\437eeebd-9de3-4c5c-a294-f05d7c315ce1\scratchpad\robo_leanlock_result.txt"
LINES = []

def log(msg):
    LINES.append(str(msg))
    unreal.log_warning("[LEANTEST] " + str(msg))
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

class Test:
    def __init__(self):
        self.t0 = time.monotonic()
        self.stage = "boot"
        self.stage_t = self.t0
        self.artist_pid = None
        self.victim_pid = None
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
                self.advance("enter_lean")
            elif self.elapsed() > 40.0:
                log("FAIL: drawing phase never came")
                self.finish()
        elif s == "enter_lean":
            if self.elapsed() < 1.0:
                return
            server = get_world("UEDPIE_0")
            victim = find_char(server, self.victim_pid)
            artist = find_char(server, self.artist_pid)
            body = victim.get_editor_property("Body")
            point = body.get_world_transform().transform_location(unreal.Vector(0.0, 20.0, 95.0))
            qp = unreal.Vector_NetQuantize()
            qp.set_editor_property("x", point.x); qp.set_editor_property("y", point.y); qp.set_editor_property("z", point.z)
            qn = unreal.Vector_NetQuantizeNormal()
            qn.set_editor_property("x", 0.0); qn.set_editor_property("y", 0.0); qn.set_editor_property("z", 1.0)
            artist.call_method("ServerEnterLean", (victim, qp, qn))
            self.advance("verify_lean")
        elif s == "verify_lean":
            if self.elapsed() < 1.5:
                return
            ok_all = True
            for tag in ("UEDPIE_0", "UEDPIE_1", "UEDPIE_2"):
                w = get_world(tag)
                a = find_char(w, self.artist_pid) if w else None
                locked = a.get_editor_property("bLeanLocked") if a else None
                bow = a.get_editor_property("BowBody") if a else None
                bow_vis = bow.is_visible() if bow else None
                bow_asset = bow.get_skinned_asset() if bow else None
                log(f"{tag} locked={locked} bowVisible={bow_vis} bowAsset={bow_asset.get_name() if bow_asset else None}")
                ok_all = ok_all and bool(locked)
            log("LEAN REPLICATION: " + ("PASS" if ok_all else "FAIL"))
            # 姿勢解算驗收：頭骨要抵達落筆點上方 ~22cm、臉軸對準落筆點
            server0 = get_world("UEDPIE_0")
            a0 = find_char(server0, self.artist_pid)
            bow0 = a0.get_editor_property("BowBody")
            try:
                n = bow0.get_num_bones()
                log(f"bones({n})")
                head = bow0.get_bone_transform_by_name("Head", unreal.BoneSpaces.WORLD_SPACE).translation
                lp = a0.get_editor_property("LeanPoint")
                lp = unreal.Vector(lp.x, lp.y, lp.z)
                d = unreal.Vector(head.x - lp.x, head.y - lp.y, head.z - lp.z)
                dist = (d.x**2 + d.y**2 + d.z**2) ** 0.5
                log(f"head-to-lockpoint: {dist:.1f} cm " + ("PASS(<32)" if dist < 32.0 else "FAIL"))
            except Exception as e:
                log("pose probe err: " + str(e))
            # 細筆畫一條線（timer-deferred hook——guard 外，multicast 走真網路）
            server = get_world("UEDPIE_0")
            gm = unreal.GameplayStatics.get_game_mode(server)
            gm.debug_robo_stroke(unreal.Vector2D(0.44, 0.44), unreal.Vector2D(0.52, 0.46), 2)
            self.advance("verify_stroke")
        elif s == "verify_stroke":
            if self.elapsed() < 2.0:
                return
            w2 = get_world("UEDPIE_2")
            victim2 = find_char(w2, self.victim_pid)
            works = victim2.get_editor_property("InkCanvas").get_works()
            n = len(works)
            log(f"client stroke works={n} " + ("PASS" if n >= 1 else "FAIL"))
            # RT 匯出（細筆視覺證據）
            server = get_world("UEDPIE_0")
            victim = find_char(server, self.victim_pid)
            out = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()) + "/InkQA/leanlock"
            victim.get_editor_property("InkCanvas").export_layers_to_png(out)
            log("RT exported")
            # 偷瞄
            artist = find_char(server, self.artist_pid)
            artist.call_method("ServerSetPeeking", (True,))
            self.advance("verify_peek")
        elif s == "verify_peek":
            if self.elapsed() < 1.5:
                return
            w1 = get_world("UEDPIE_1")
            a1 = find_char(w1, self.artist_pid)
            peek = a1.get_editor_property("bPeeking") if a1 else None
            log(f"peek replicated={peek} " + ("PASS" if peek else "FAIL"))
            server = get_world("UEDPIE_0")
            artist = find_char(server, self.artist_pid)
            artist.call_method("ServerSetPeeking", (False,))
            # 給受害者兩次小遊戲成功 → 踹飛鎖定中的作畫者
            victim = find_char(server, self.victim_pid)
            victim.call_method("ServerMinigameHit", ())
            victim.call_method("ServerMinigameHit", ())
            self.advance("kick_interrupt")
        elif s == "kick_interrupt":
            if self.elapsed() < 1.0:
                return
            server = get_world("UEDPIE_0")
            victim = find_char(server, self.victim_pid)
            artist = find_char(server, self.artist_pid)
            # 朝作畫者方向踹
            vloc = victim.get_editor_property("Body").get_world_transform().transform_location(unreal.Vector(0.0, 0.0, 88.0))
            aloc = artist.get_actor_location()
            import math
            yaw = math.degrees(math.atan2(aloc.y - vloc.y, aloc.x - vloc.x))
            gm = unreal.GameplayStatics.get_game_mode(server)
            gm.debug_robo_kick(yaw)
            self.advance("verify_kick")
        elif s == "verify_kick":
            if self.elapsed() < 1.5:
                return
            server = get_world("UEDPIE_0")
            artist = find_char(server, self.artist_pid)
            locked = artist.get_editor_property("bLeanLocked")
            works = artist.get_editor_property("InkCanvas").get_works()
            bruises = [wk for wk in works if wk.get_editor_property("AuthorId") == -20]
            log(f"after kick: locked={locked} bruise_works={len(bruises)} " +
                ("PASS" if (not locked and len(bruises) >= 1) else "PARTIAL/FAIL"))
            self.advance("pose_for_photo")
        elif s == "pose_for_photo":
            if self.elapsed() < 2.0:
                return
            # 只讓非主機的作畫者湊上去；主機當觀察者對準他拍彎腰
            server = get_world("UEDPIE_0")
            victim = find_char(server, self.victim_pid)
            body = victim.get_editor_property("Body")
            host = unreal.GameplayStatics.get_player_pawn(server, 0)
            host_pid = host.get_editor_property("player_state").get_editor_property("player_id")
            model = None
            for c in chars_of(server):
                pid = c.get_editor_property("player_state").get_editor_property("player_id")
                if pid != self.victim_pid and pid != host_pid:
                    model = c
                    break
            if model:
                p = body.get_world_transform().transform_location(unreal.Vector(0.0, 22.0, 95.0))
                qp = unreal.Vector_NetQuantize()
                qp.set_editor_property("x", p.x); qp.set_editor_property("y", p.y); qp.set_editor_property("z", p.z)
                qn = unreal.Vector_NetQuantizeNormal()
                qn.set_editor_property("x", 0.0); qn.set_editor_property("y", 0.0); qn.set_editor_property("z", 1.0)
                model.call_method("ServerEnterLean", (victim, qp, qn))
                self.model_pid = model.get_editor_property("player_state").get_editor_property("player_id")
            self.shot_count = 0
            self.advance("photo_burst")
        elif s == "photo_burst":
            if self.elapsed() < 3.0 + self.shot_count * 4.0:
                return
            server = get_world("UEDPIE_0")
            model = find_char(server, getattr(self, "model_pid", -1))
            host = unreal.GameplayStatics.get_player_pawn(server, 0)
            pc = unreal.GameplayStatics.get_player_controller(server, 0)
            if model and host and pc:
                mloc = model.get_actor_location()
                cam = unreal.Vector(mloc.x + 150.0, mloc.y + 130.0, mloc.z + 60.0)
                host.set_actor_location(cam, False, True)
                look = unreal.MathLibrary.find_look_at_rotation(
                    unreal.Vector(cam.x, cam.y, cam.z + 62.0), unreal.Vector(mloc.x, mloc.y, mloc.z - 40.0))
                pc.set_control_rotation(look)
                unreal.SystemLibrary.execute_console_command(server, f"HighResShot 1280x720 filename=leanpose_{self.shot_count}")
            self.shot_count += 1
            if self.shot_count >= 8:
                log("photo burst done")
                self.finish()

    def finish(self):
        log("DONE (PIE left running for screenshots)")
        if self.handle:
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.handle = None

_t = Test()
log("harness registered")
