# 站立視野俯仰上身探針（2026-08-15）：本人相機抬頭/低頭 → 他端(server world 旁觀)
# 與本人端 Neck→Head 骨向量的 Z 分量同號變化；左右轉頭 = 頭身零相對 yaw。
# 契約：c1 抬頭 dz>+閾；c2 低頭 dz<-閾；c3 兩端同向且量級接近（追趕收斂後）；
#       c4 抬頭時 yaw 改變不產生頭骨相對身體的偏航（Neck→Head 水平投影對齊身體前向）。
# 產出：Saved/robo_lookpitch_result.txt
import unreal, time, math, traceback

OUT = r"C:\games\Unreal Engine\nice_ink\Saved\robo_lookpitch_result.txt"
LINES = []


def log(m):
    LINES.append(str(m)); unreal.log_warning("[LOOK] " + str(m))
    open(OUT, "w", encoding="utf-8").write("\n".join(LINES))


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


def bone(c, name):
    return c.get_editor_property("bow_body").get_bone_location_by_name(name, unreal.BoneSpaces.WORLD_SPACE)


def head_vec(c):
    n = bone(c, "Neck"); h = bone(c, "Head")
    v = (h.x - n.x, h.y - n.y, h.z - n.z)
    l = max((v[0]**2 + v[1]**2 + v[2]**2) ** 0.5, 1e-6)
    return (v[0]/l, v[1]/l, v[2]/l)


class Probe:
    def __init__(self):
        self.t0 = time.monotonic(); self.stage = "boot"; self.stage_t = self.t0
        self.pid = None; self.checks = []; self.rest = None
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    def advance(self, s): self.stage = s; self.stage_t = time.monotonic(); log("STAGE -> " + s)
    def elapsed(self): return time.monotonic() - self.stage_t
    def check(self, n, ok, d): self.checks.append(bool(ok)); log(("PASS " if ok else "FAIL ") + n + " :: " + d)

    def tick(self, dt):
        try: self.step()
        except Exception:
            log("EXC:\n" + traceback.format_exc()); self.finish()

    def pc(self):
        w = get_world("UEDPIE_0"); return unreal.GameplayStatics.get_player_controller(w, 0)

    def both(self):
        return find_char(get_world("UEDPIE_0"), self.pid), find_char(get_world("UEDPIE_1"), self.pid)

    def step(self):
        s = self.stage
        if s == "boot":
            if time.monotonic() - self.t0 > 8.0:
                eps = unreal.find_object(None, "/Script/UnrealEd.Default__EditorPerformanceSettings")
                if eps: eps.set_editor_property("bThrottleCPUWhenNotForeground", False)
                unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
                self.advance("wait_pie")
        elif s == "wait_pie":
            w = get_world("UEDPIE_0")
            if w and unreal.GameplayStatics.get_game_mode(w):
                unreal.GameplayStatics.get_game_mode(w).set_editor_property("DebugForcedVictimSeat", 1)
                self.advance("wait_drawing")
        elif s == "wait_drawing":
            w = get_world("UEDPIE_0"); gs = unreal.GameplayStatics.get_game_state(w)
            if gs and str(gs.get_editor_property("CurrentPhase")).endswith("DRAWING: 3>"):
                vic = gs.get_editor_property("VictimPlayerId")
                for c in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.NiceInkCharacter):
                    pid = c.get_editor_property("player_state").get_editor_property("player_id")
                    if pid != vic and c.is_player_controlled() and c.get_controller() and c.get_controller().is_local_player_controller():
                        self.pid = pid; break
                if self.pid is None: log("FAIL: no host artist"); self.finish(); return
                log(f"host pid={self.pid}"); self.advance("rest")
            elif self.elapsed() > 40: log("FAIL: no drawing phase"); self.finish()
        elif s == "rest":
            if self.elapsed() < 1.5: return
            h, r = self.both()
            self.rest = head_vec(h); self.yaw0 = self.pc().get_control_rotation().yaw
            log(f"rest headvec host={self.rest} remote={head_vec(r) if r else None}")
            self.pc().set_control_rotation(unreal.Rotator(0.0, 40.0, self.yaw0))
            self.advance("up")
        elif s == "up":
            if self.elapsed() < 1.2: return
            h, r = self.both()
            hv, rv = head_vec(h), head_vec(r)
            dzh, dzr = hv[2] - self.rest[2], rv[2] - self.rest[2]
            log(f"up: host dz={dzh:+.3f} remote dz={dzr:+.3f}")
            for tag, c in (("host", h), ("remote", r)):
                ns = c.get_editor_property("neck_stretch")
                log(f"neck@up[{tag}]: " + str(ns.call_method("GetDebugSummary", ())))
            self.check("c1_look_up_host", dzh > 0.02, f"dz={dzh:+.3f} (expect >+0.02; 12deg cap x exp curve)")
            self.check("c1b_look_up_remote", dzr > 0.02, f"dz={dzr:+.3f}")
            self.check("c3_both_agree", abs(dzh - dzr) < 0.02, f"|host-remote|={abs(dzh-dzr):.3f}")
            self.pc().set_control_rotation(unreal.Rotator(0.0, -40.0, self.yaw0))
            self.advance("down")
        elif s == "down":
            if self.elapsed() < 1.2: return
            h, r = self.both()
            hv, rv = head_vec(h), head_vec(r)
            dzh, dzr = hv[2] - self.rest[2], rv[2] - self.rest[2]
            log(f"down: host dz={dzh:+.3f} remote dz={dzr:+.3f}")
            for tag, c in (("host", h), ("remote", r)):
                ns = c.get_editor_property("neck_stretch")
                log(f"neck@down[{tag}]: " + str(ns.call_method("GetDebugSummary", ())))
            self.check("c2_look_down_host", dzh < -0.02, f"dz={dzh:+.3f} (expect <-0.02)")
            self.check("c2b_look_down_remote", dzr < -0.02, f"dz={dzr:+.3f}")
            self.dz40 = dzh
            self.sweep = list(range(-89, 90, 7)); self.sweep_i = 0; self.sweep_rows = []
            self.pc().set_control_rotation(unreal.Rotator(0.0, float(self.sweep[0]), self.yaw0))
            self.advance("sweep")
        elif s == "sweep":
            if self.elapsed() < 0.45: return
            h, r = self.both()
            ns = r.get_editor_property("neck_stretch")
            summ = str(ns.call_method("GetDebugSummary", ()))
            import re as _re
            m = _re.search(r"vis=(\d) chord=([\d.]+)", summ)
            vis, chord = (int(m.group(1)), float(m.group(2))) if m else (-1, -1.0)
            self.sweep_rows.append((self.sweep[self.sweep_i], vis, chord))
            self.sweep_i += 1
            if self.sweep_i < len(self.sweep):
                self.pc().set_control_rotation(unreal.Rotator(0.0, float(self.sweep[self.sweep_i]), self.yaw0))
                self.stage_t = time.monotonic()
                return
            log("sweep(remote) pitch:vis:chord = " + " ".join(f"{p}:{v}:{c:.1f}" for p, v, c in self.sweep_rows))
            # 08-15 終案：站立也用切開版＋程序化伸縮脖（user 定案）→|pitch|≥5 楔縫必開＝伸縮脖必現
            bad = [p for p, v, c in self.sweep_rows if abs(p) >= 5 and v == 0]
            self.check("c6_neck_visible_when_pitched", len(bad) == 0, f"hidden-at-pitch={bad}")
            self.pc().set_control_rotation(unreal.Rotator(0.0, 89.0, self.yaw0))
            self.advance("cap")
        elif s == "cap":
            if self.elapsed() < 1.2: return
            h, r = self.both()
            dz89 = head_vec(h)[2] - self.rest[2]
            log(f"cap: host dz@89={dz89:+.3f} (dz@40 down was {self.dz40:+.3f})")
            # 12° 上限：dz@89 ≈ 骨向量繞 X 轉 12° 的 Z 變化 ~0.19；不得超 0.25（=無鉗）
            self.check("c5_cap_12deg", 0.05 < dz89 < 0.25, f"dz@89={dz89:+.3f} (12deg cap band 0.05~0.25)")
            # 抬頭 + 轉身 90°：頭骨相對身體不得偏航
            self.pc().set_control_rotation(unreal.Rotator(0.0, 40.0, self.yaw0 + 90.0))
            self.advance("yaw")
        elif s == "yaw":
            if self.elapsed() < 1.2: return
            h, r = self.both()
            for tag, c in (("host", h), ("remote", r)):
                fw = c.get_actor_forward_vector(); v = head_vec(c)
                # 水平投影夾角
                hx, hy = v[0], v[1]; l = max((hx*hx + hy*hy) ** 0.5, 1e-6)
                cosang = (hx/l) * fw.x + (hy/l) * fw.y
                ang = math.degrees(math.acos(max(-1.0, min(1.0, cosang))))
                # rest 時的水平偏差當基準（骨鏈本身可能不正對前向）
                rx, ry = self.rest[0], self.rest[1]; rl = max((rx*rx+ry*ry)**0.5, 1e-6)
                self.check(f"c4_no_rel_yaw_{tag}", ang < 25.0, f"head-vs-body horiz angle={ang:.1f}deg (rest tilt included)")
            self.pc().set_control_rotation(unreal.Rotator(0.0, 0.0, self.yaw0))
            self.finish()

    def finish(self):
        n_pass = sum(1 for x in self.checks if x); n_fail = len(self.checks) - n_pass
        log(f"RESULT {n_pass}/{n_fail} " + ("DONE-PASS" if n_fail == 0 else "DONE-FAIL"))
        try: unreal.unregister_slate_post_tick_callback(self.handle)
        except Exception: pass


Probe()
