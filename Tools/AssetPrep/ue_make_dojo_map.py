import unreal

# 建立 L_Dojo：載入 L_Sauna（磁碟不動）→ 記憶體中把 SM_SaunaRoom 換成 SM_Dojo
# → save_map 另存 /Game/Maps/L_Dojo。
# 陷阱備忘：level duplicate_asset 後同 session load_level 會 fatal（world GC leak）——
# 所以用另存而不是複製。結果寫結果檔（headless stdout 不可靠）。

RESULT = r"C:\Users\willi\AppData\Local\Temp\claude\c--games-Unreal-Engine-nice-ink\b9e4a3cf-70ad-4c05-9233-e84425b9517b\scratchpad\dojo_map_result.txt"
lines = []
def out(s):
    lines.append(str(s))
    unreal.log(s)

try:
    sm = unreal.load_asset("/Game/Dojo/SM_Dojo")
    assert sm, "SM_Dojo missing"
    bs = sm.get_editor_property("body_setup")
    flag = bs.get_editor_property("collision_trace_flag")
    if flag != unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE:
        bs.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        unreal.EditorAssetLibrary.save_asset("/Game/Dojo/SM_Dojo")
        out("COLLISION: was %s -> set complex-as-simple" % flag)
    else:
        out("COLLISION: already complex-as-simple")
    db = sm.get_bounding_box()
    ds = db.max - db.min
    out("DOJO BOUNDS min=(%.1f,%.1f,%.1f) max=(%.1f,%.1f,%.1f) size=(%.1f,%.1f,%.1f)"
        % (db.min.x, db.min.y, db.min.z, db.max.x, db.max.y, db.max.z, ds.x, ds.y, ds.z))
    out("DOJO MATERIAL SLOTS: %d" % len(sm.static_materials))

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    les.load_level("/Game/Maps/L_Sauna")

    actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    swapped = 0
    for a in actor_sub.get_all_level_actors():
        if not isinstance(a, unreal.StaticMeshActor):
            continue
        comp = a.static_mesh_component
        mesh = comp.static_mesh
        if mesh and mesh.get_name() == "SM_SaunaRoom":
            old_loc = a.get_actor_location()
            old_scale = a.get_actor_scale3d()
            sb = a.get_actor_bounds(False)  # (origin, extent) 世界空間
            s_origin, s_extent = sb[0], sb[1]
            out("SAUNA ACTOR loc=(%.1f,%.1f,%.1f) scale=(%.2f,%.2f,%.2f) worldbox center=(%.1f,%.1f,%.1f) extent=(%.1f,%.1f,%.1f)"
                % (old_loc.x, old_loc.y, old_loc.z, old_scale.x, old_scale.y, old_scale.z,
                   s_origin.x, s_origin.y, s_origin.z, s_extent.x, s_extent.y, s_extent.z))
            floor_z = s_origin.z - s_extent.z
            comp.set_editor_property("static_mesh", sm)
            a.set_actor_scale3d(unreal.Vector(1.0, 1.0, 1.0))
            d_center = (db.min + db.max) * 0.5
            new_loc = unreal.Vector(
                s_origin.x - d_center.x,
                s_origin.y - d_center.y,
                floor_z - db.min.z)
            a.set_actor_location(new_loc, False, False)
            a.set_actor_label("SM_Dojo")
            out("NEW ACTOR loc=(%.1f,%.1f,%.1f) -> dojo floor at z=%.1f" % (new_loc.x, new_loc.y, new_loc.z, floor_z))
            swapped += 1
    out("SWAPPED: %d" % swapped)

    world = unreal.EditorLevelLibrary.get_editor_world()
    ok = unreal.EditorLoadingAndSavingUtils.save_map(world, "/Game/Maps/L_Dojo")
    out("SAVE_AS L_Dojo: %s" % bool(ok))
    out("MAP SAVE DONE")
except Exception as e:
    import traceback
    out("EXC: %s" % traceback.format_exc())

with open(RESULT, "w", encoding="utf-8") as f:
    f.write("\n".join(lines) + "\n")
