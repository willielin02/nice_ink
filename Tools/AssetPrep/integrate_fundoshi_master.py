"""Integrate the accepted v105 fundoshi shell into the canonical master blend.

- Backs up master to masters/sumo_character_master_v15_prefundoshi.blend first.
- Appends FundoshiSurface_Rebuild from v105, renames it "Fundoshi".
- Transfers armature weights from SumoRetopo (nearest-face interp, limit 8,
  normalized), parents to the armature with an Armature modifier.
- Smart-UV unwrap into "UVMap" (cloth texture is tileable; this UV is never
  consumed by the ink system — C++ tri-cache skips the fundoshi section).
- Black "FaceMask" color attribute (band is not a face).
- Single material slot "M_Fundoshi" (name-only; UE material assigned at import).

Run: blender --background --python integrate_fundoshi_master.py
"""
import bpy, json, math, os, shutil, time, traceback

ROOT = r"C:\games\Unreal Engine\nice_ink"
MASTER = os.path.join(ROOT, "SourceAssets", "sumo_character_master.blend")
BACKUP = os.path.join(ROOT, "SourceAssets", "masters", "sumo_character_master_v15_prefundoshi.blend")
V105 = os.path.join(ROOT, "SourceAssets", "previews", "sumo_avatar_v105_pairpants_solid.blend")
PROG = os.path.join(ROOT, "Saved", "FundoshiIntegrate", "progress.jsonl")

os.makedirs(os.path.dirname(PROG), exist_ok=True)
T0 = time.time()

def log(stage, **kw):
    rec = {"t": round(time.time() - T0, 1), "stage": stage}
    rec.update(kw)
    with open(PROG, "a") as f:
        f.write(json.dumps(rec) + "\n")

def main():
    assert not os.path.exists(BACKUP), f"backup already exists: {BACKUP}"
    shutil.copy2(MASTER, BACKUP)
    log("backup", path=BACKUP)

    bpy.ops.wm.open_mainfile(filepath=MASTER)
    if bpy.context.object and bpy.context.object.mode != 'OBJECT':
        bpy.ops.object.mode_set(mode='OBJECT')

    meshes = [o.name for o in bpy.data.objects if o.type == 'MESH']
    assert meshes == ["SumoRetopo"], f"unexpected meshes in master: {meshes}"
    body = bpy.data.objects["SumoRetopo"]
    arm = bpy.data.objects["Skeleton_Plus-size"]

    # --- append the accepted band ---
    with bpy.data.libraries.load(V105) as (src, dst):
        assert "FundoshiSurface_Rebuild" in src.objects
        dst.objects = ["FundoshiSurface_Rebuild"]
    band = dst.objects[0]
    bpy.context.scene.collection.objects.link(band)
    band.name = "Fundoshi"
    band.data.name = "Fundoshi"
    # sanity: identity transform + expected size
    assert max(abs(v) for row in band.matrix_world - __import__("mathutils").Matrix.Identity(4)
               for v in row) < 1e-6, "band transform not identity"
    assert len(band.data.vertices) == 7134, f"unexpected band verts {len(band.data.vertices)}"
    for m in list(band.modifiers):
        band.modifiers.remove(m)
    band.data.materials.clear()
    log("appended", verts=len(band.data.vertices))

    bpy.context.view_layer.objects.active = band
    for o in bpy.context.view_layer.objects:
        o.select_set(o is band)

    # --- weights: transfer from body, limit 8, normalize ---
    band.vertex_groups.clear()
    dt = band.modifiers.new("DT", "DATA_TRANSFER")
    dt.object = body
    dt.use_vert_data = True
    dt.data_types_verts = {'VGROUP_WEIGHTS'}
    dt.vert_mapping = 'POLYINTERP_NEAREST'
    dt.layers_vgroup_select_src = 'ALL'
    dt.layers_vgroup_select_dst = 'NAME'
    bpy.ops.object.datalayout_transfer(modifier="DT")
    bpy.ops.object.modifier_apply(modifier="DT")
    bpy.ops.object.vertex_group_limit_total(group_select_mode='ALL', limit=8)
    bpy.ops.object.vertex_group_normalize_all(group_select_mode='ALL', lock_active=False)
    nonzero = sum(1 for v in band.data.vertices if v.groups)
    assert nonzero == len(band.data.vertices), f"verts without weights: {len(band.data.vertices) - nonzero}"
    log("weights", groups=len(band.vertex_groups), verts_weighted=nonzero)

    # --- parenting + armature modifier ---
    band.parent = arm
    band.matrix_parent_inverse = arm.matrix_world.inverted()
    am = band.modifiers.new("Armature", "ARMATURE")
    am.object = arm

    # --- UV unwrap (cloth tiling UV; ink system never reads this) ---
    while band.data.uv_layers:
        band.data.uv_layers.remove(band.data.uv_layers[0])
    band.data.uv_layers.new(name="UVMap")
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(66.0), island_margin=0.02)
    bpy.ops.object.mode_set(mode='OBJECT')
    log("uv", layers=[l.name for l in band.data.uv_layers])

    # --- FaceMask vertex color (black: not a face) ---
    attr = band.data.color_attributes.new("FaceMask", "FLOAT_COLOR", "POINT")
    black = [0.0, 0.0, 0.0, 1.0] * len(band.data.vertices)
    attr.data.foreach_set("color", black)

    # --- material slot (name is the contract with the UE import script) ---
    mat = bpy.data.materials.get("M_Fundoshi") or bpy.data.materials.new("M_Fundoshi")
    band.data.materials.append(mat)

    bpy.ops.wm.save_mainfile()
    log("saved", blend=MASTER)
    log("done", status="OK")

try:
    main()
except Exception:
    log("exception", trace=traceback.format_exc()[-1500:])
    raise
