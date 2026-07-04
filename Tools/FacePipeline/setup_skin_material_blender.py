"""
Build the layered tattoo-canvas skin material on the player character.

Run:  blender --background <character.blend> --python setup_skin_material_blender.py

Layers (design in PIPELINE_SPEC.md):
  base   = SkinColor (from skin_color.json, linear) x albedo detail tile (UVMap, tiled)
  face   = face_texture.png alpha-blended on top (FaceUV), gated by the FaceMask
           vertex attribute so body islands overlapping the face oval never
           receive face content
  pores  = detail normal tile (UVMap), low strength
  rough  = roughness breakup tile around a base value
  SSS    = subsurface on, skin-ish radius

The node tree of the body material is rebuilt from scratch on every run
(idempotent). Slot 1 (scalp, previously empty) gets the same material.
"""
import bpy
import json
import numpy as np
from pathlib import Path

PIPE = Path(__file__).resolve().parent
DATA = PIPE / "data"    # pipeline constants (FaceUV masks, skin tiles)
OUT = PIPE / "out"      # per-player pipeline outputs
FACE_TEXTURE = OUT / "face_texture.png"
SKIN_JSON = OUT / "skin_color.json"
TILE_ALBEDO = DATA / "skin_tile_albedo_detail.png"
ZONE_TINT = DATA / "skin_zone_tint.png"
TILE_NORMAL = DATA / "skin_tile_normal.png"
TILE_ROUGH = DATA / "skin_tile_rough.png"

MESH_NAME = "PlusSize_Male_Body_01"
# Frequency plan: zone tint 1x (20-50cm), mottle 8x (2-8cm, the band that is
# visible at gameplay distance), pores 40x (1-3mm, closeup only). The normal
# is sampled at BOTH scales so mid-distance still shows surface undulation,
# and the two incommensurate frequencies break tiling repetition.
PORE_SCALE = 40.0
MOTTLE_SCALE = 8.0
DETAIL_AMOUNT = 0.7      # stored +-8% -> effective +-5.6% chromatic mottle
ZONE_AMOUNT = 0.4        # low-freq body zone tint strength (the "alive" layer)
                         # (x ZONE_CLAMP 0.35 -> effective ~+-14%, vascular-zone realism)
NORMAL_STRENGTH = 0.4    # applied to the pore+mid normal mix
NORMAL_MID_MIX = 0.35    # share of the 8x mid undulation in the normal mix
                         # (higher = more visible-at-distance lumps, risks skin-disease look)
ROUGH_BASE = 0.55        # body skin is dry; 0.6 was slightly chalky
ROUGH_VARIATION = 1.2    # breakup amplitude multiplier on the rough tile
SPECULAR_LEVEL = 0.3     # skin reflectance ~2.8% (IOR 1.4), default 0.5 is too hot
SSS_WEIGHT = 0.12
SSS_RADIUS = (1.0, 0.2, 0.1)
SSS_SCALE = 0.05


def load_image(path, non_color=False):
    img = bpy.data.images.load(str(path), check_existing=True)
    img.colorspace_settings.name = 'Non-Color' if non_color else 'sRGB'
    if not non_color:
        img.alpha_mode = 'STRAIGHT'
    img.reload()
    return img


def clear_custom_normals(obj):
    """FBX-imported custom split normals override shade smooth entirely and
    bake in faceting; clear them so POINT-domain smooth shading takes over."""
    me = obj.data
    if me.has_custom_normals:
        bpy.context.view_layer.objects.active = obj
        bpy.ops.mesh.customdata_custom_splitnormals_clear()
        print(f"cleared custom split normals (domain now {me.normals_domain})")


def build_face_mask(obj):
    """Vertex color attribute: 1 exactly on the FACE UV island.

    Candidate polys are those whose FaceUV lands inside the FaceUV oval; they
    are grouped into mesh-connected components and only the component that
    contains the actual face (topmost near-axis poly) is kept. Body/hand/
    shoulder islands that merely OVERLAP the oval in UV space are separate
    components and are excluded exactly — no height/width thresholds that can
    break on stocky proportions."""
    me = obj.data

    # FaceUV region: the expanded coverage mask when present, else the legacy oval
    cov = DATA / "faceuv_mask_coverage.png"
    mask_path = cov if cov.exists() else DATA / "faceuv_mask.png"
    mimg = bpy.data.images.load(str(mask_path), check_existing=True)
    mimg.reload()
    mw, mh = mimg.size
    mpx = np.array(mimg.pixels[:], dtype=np.float32).reshape(mh, mw, mimg.channels)
    oval = mpx[..., 0] > 0.5   # bpy rows are bottom-up == v axis, no flip needed

    n_loops = len(me.loops)
    buf = np.zeros(n_loops * 2, dtype=np.float32)
    me.attributes['FaceUV'].data.foreach_get('vector', buf)
    uvs = buf.reshape(-1, 2)
    corner_vert = np.zeros(n_loops, dtype=np.int64)
    me.attributes['.corner_vert'].data.foreach_get('value', corner_vert)

    polys = me.polygons
    centers = np.zeros(len(polys) * 3, dtype=np.float32)
    polys.foreach_get('center', centers)
    c = centers.reshape(-1, 3)

    candidate = np.zeros(len(polys), dtype=bool)
    poly_verts = []
    poly_loops = []
    for p in polys:
        loops = list(range(p.loop_start, p.loop_start + p.loop_total))
        uv = uvs[loops].mean(axis=0)
        xi = min(int(uv[0] * mw), mw - 1)
        yi = min(int(uv[1] * mh), mh - 1)
        candidate[p.index] = bool(oval[yi, xi])
        poly_verts.append([corner_vert[li] for li in loops])
        poly_loops.append(loops)

    # connected components among candidate polys (shared mesh vertices)
    vert_to_polys = {}
    for pi in np.where(candidate)[0]:
        for v in poly_verts[pi]:
            vert_to_polys.setdefault(v, []).append(pi)
    comp = {}
    for seed_pi in np.where(candidate)[0]:
        if seed_pi in comp:
            continue
        cid = seed_pi
        stack = [seed_pi]
        comp[seed_pi] = cid
        while stack:
            pi = stack.pop()
            for v in poly_verts[pi]:
                for qi in vert_to_polys[v]:
                    if qi not in comp:
                        comp[qi] = cid
                        stack.append(qi)

    # the real face = component containing the topmost near-axis candidate
    near_axis = candidate & (np.abs(c[:, 0]) < 0.15)
    face_seed = int(np.where(near_axis)[0][np.argmax(c[near_axis, 1])])
    face_cid = comp[face_seed]
    face_polys = [pi for pi, cid in comp.items() if cid == face_cid]

    # CORNER domain: per-face-corner values never interpolate across polygon
    # boundaries, so non-island polys (ears, neck, shoulders) get exactly 0 --
    # a POINT-domain mask bled 0..1 onto boundary polys, which then sampled
    # the face texture at their unrelated UVMap-copy coordinates (blocky
    # garbage on ears/neck)
    mask = np.zeros(n_loops, dtype=np.float32)
    for pi in face_polys:
        for li in poly_loops[pi]:
            mask[li] = 1.0

    existing = me.color_attributes.get("FaceMask")
    if existing:
        me.color_attributes.remove(existing)
    attr = me.color_attributes.new(name="FaceMask", type='FLOAT_COLOR', domain='CORNER')
    colors = np.repeat(mask, 4).astype(np.float32)
    colors[3::4] = 1.0
    attr.data.foreach_set('color', colors)
    n_comp = len(set(comp.values()))
    print(f"FaceMask: {len(face_polys)} face-island polys "
          f"(of {int(candidate.sum())} oval-overlapping in {n_comp} components), "
          f"{int(mask.sum())}/{n_loops} corners")


def build_material(mat, skin_linear):
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    nodes, links = nt.nodes, nt.links

    def node(kind, name, x, y, **props):
        nd = nodes.new(kind)
        nd.name = nd.label = name
        nd.location = (x, y)
        for k, v in props.items():
            setattr(nd, k, v)
        return nd

    out = node('ShaderNodeOutputMaterial', 'Output', 900, 0)
    bsdf = node('ShaderNodeBsdfPrincipled', 'Skin BSDF', 600, 0)
    bsdf.inputs['Subsurface Weight'].default_value = SSS_WEIGHT
    bsdf.inputs['Subsurface Radius'].default_value = SSS_RADIUS
    if 'Subsurface Scale' in bsdf.inputs:
        bsdf.inputs['Subsurface Scale'].default_value = SSS_SCALE
    if 'Specular IOR Level' in bsdf.inputs:
        bsdf.inputs['Specular IOR Level'].default_value = SPECULAR_LEVEL
    links.new(bsdf.outputs['BSDF'], out.inputs['Surface'])

    def ratio_decode(tex_out, amount, x, y, tag):
        """16-bit ratio texture -> 1 + (value*2 - 1) * amount, per channel."""
        v1 = node('ShaderNodeVectorMath', f'{tag} x2', x, y, operation='SCALE')
        v1.inputs['Scale'].default_value = 2.0
        links.new(tex_out, v1.inputs[0])
        v2 = node('ShaderNodeVectorMath', f'{tag} -1', x + 150, y, operation='SUBTRACT')
        v2.inputs[1].default_value = (1.0, 1.0, 1.0)
        links.new(v1.outputs[0], v2.inputs[0])
        v3 = node('ShaderNodeVectorMath', f'{tag} amt', x + 300, y, operation='SCALE')
        v3.inputs['Scale'].default_value = amount
        links.new(v2.outputs[0], v3.inputs[0])
        v4 = node('ShaderNodeVectorMath', f'{tag} +1', x + 450, y, operation='ADD')
        v4.inputs[1].default_value = (1.0, 1.0, 1.0)
        links.new(v3.outputs[0], v4.inputs[0])
        return v4.outputs[0]

    # --- body base: SkinColor x chromatic detail x low-freq zone tint -------
    uv_body = node('ShaderNodeUVMap', 'UV Body', -1100, 200, uv_map='UVMap')
    map_pore = node('ShaderNodeMapping', 'Pore Scale', -900, 350)
    map_pore.inputs['Scale'].default_value = (PORE_SCALE, PORE_SCALE, 1.0)
    links.new(uv_body.outputs['UV'], map_pore.inputs['Vector'])
    map_mottle = node('ShaderNodeMapping', 'Mottle Scale', -900, 150)
    map_mottle.inputs['Scale'].default_value = (MOTTLE_SCALE, MOTTLE_SCALE, 1.0)
    links.new(uv_body.outputs['UV'], map_mottle.inputs['Vector'])

    tex_alb = node('ShaderNodeTexImage', 'Albedo Detail', -700, 500)
    tex_alb.image = load_image(TILE_ALBEDO, non_color=True)
    links.new(map_mottle.outputs['Vector'], tex_alb.inputs['Vector'])
    detail_ratio = ratio_decode(tex_alb.outputs['Color'], DETAIL_AMOUNT, -400, 500, 'Detail')

    # zone tint is sampled in GENERATED (rest-pose local, [0,1]) coordinates,
    # not UV: the UV atlas splits adjacent surface into distant islands, so a
    # UV-sampled low-freq map produces visible color steps along every seam.
    # A planar projection is seam-free by construction and fine for soft zones.
    texco = node('ShaderNodeTexCoord', 'TexCoord', -900, -50)
    tex_zone = node('ShaderNodeTexImage', 'Zone Tint', -700, 250)
    tex_zone.image = load_image(ZONE_TINT, non_color=True)
    links.new(texco.outputs['Generated'], tex_zone.inputs['Vector'])
    zone_ratio = ratio_decode(tex_zone.outputs['Color'], ZONE_AMOUNT, -400, 250, 'Zone')

    variation = node('ShaderNodeVectorMath', 'Detail x Zone', 100, 380,
                     operation='MULTIPLY')
    links.new(detail_ratio, variation.inputs[0])
    links.new(zone_ratio, variation.inputs[1])

    skin_rgb = node('ShaderNodeRGB', 'SkinColor', 100, 200)
    skin_rgb.outputs[0].default_value = (*skin_linear, 1.0)

    # --- face layer: alpha blend gated by FaceMask --------------------------
    uv_face = node('ShaderNodeUVMap', 'UV Face', -900, -50, uv_map='FaceUV')
    tex_face = node('ShaderNodeTexImage', 'Face Texture', -700, -50)
    tex_face.image = load_image(FACE_TEXTURE)
    links.new(uv_face.outputs['UV'], tex_face.inputs['Vector'])

    vc = node('ShaderNodeVertexColor', 'FaceMask', -700, -300, layer_name='FaceMask')
    gate = node('ShaderNodeMath', 'Alpha x Mask', -300, -150, operation='MULTIPLY')
    links.new(tex_face.outputs['Alpha'], gate.inputs[0])
    links.new(vc.outputs['Color'], gate.inputs[1])

    # v4.5 order: mix the face over the RAW skin color FIRST, then multiply
    # the detail/zone tint chain over BOTH sides. The old order tinted only
    # the body side, so at the island edge the un-tinted face band met the
    # tinted body with a ~34 dE step and the pore detail stopped dead at the
    # face boundary (visible flat halo ring).
    base = node('ShaderNodeMix', 'Base Color', 450, 100,
                data_type='RGBA', blend_type='MIX')
    links.new(gate.outputs[0], base.inputs['Factor'])
    links.new(skin_rgb.outputs[0], base.inputs['A'])
    links.new(tex_face.outputs['Color'], base.inputs['B'])

    final_col = node('ShaderNodeMix', 'Tint Chain', 650, 200,
                     data_type='RGBA', blend_type='MULTIPLY')
    final_col.inputs['Factor'].default_value = 1.0
    links.new(base.outputs['Result'], final_col.inputs['A'])
    links.new(variation.outputs[0], final_col.inputs['B'])
    links.new(final_col.outputs['Result'], bsdf.inputs['Base Color'])

    # --- dual-scale normal: pores (closeup) + mid undulation (distance) -----
    norm_img = load_image(TILE_NORMAL, non_color=True)
    tex_np = node('ShaderNodeTexImage', 'Normal Pore', -500, -400)
    tex_np.image = norm_img
    links.new(map_pore.outputs['Vector'], tex_np.inputs['Vector'])
    tex_nm = node('ShaderNodeTexImage', 'Normal Mid', -500, -650)
    tex_nm.image = norm_img
    links.new(map_mottle.outputs['Vector'], tex_nm.inputs['Vector'])

    mix_n = node('ShaderNodeMix', 'Normal Mix', -200, -520,
                 data_type='RGBA', blend_type='MIX')
    mix_n.inputs['Factor'].default_value = NORMAL_MID_MIX
    links.new(tex_np.outputs['Color'], mix_n.inputs['A'])
    links.new(tex_nm.outputs['Color'], mix_n.inputs['B'])

    nmap = node('ShaderNodeNormalMap', 'Normal Map', 0, -520)
    nmap.inputs['Strength'].default_value = NORMAL_STRENGTH
    links.new(mix_n.outputs['Result'], nmap.inputs['Color'])
    links.new(nmap.outputs['Normal'], bsdf.inputs['Normal'])

    # --- roughness breakup at mottle scale (visible at distance) ------------
    tex_r = node('ShaderNodeTexImage', 'Rough Tile', -500, -900)
    tex_r.image = load_image(TILE_ROUGH, non_color=True)
    links.new(map_mottle.outputs['Vector'], tex_r.inputs['Vector'])
    r1 = node('ShaderNodeMath', 'Rough Center', -300, -900, operation='SUBTRACT')
    r1.inputs[1].default_value = 0.5
    links.new(tex_r.outputs['Color'], r1.inputs[0])
    r2 = node('ShaderNodeMath', 'Rough Var', -150, -900, operation='MULTIPLY')
    r2.inputs[1].default_value = ROUGH_VARIATION
    links.new(r1.outputs[0], r2.inputs[0])
    r3 = node('ShaderNodeMath', 'Rough Base', 0, -900, operation='ADD')
    r3.inputs[1].default_value = ROUGH_BASE
    links.new(r2.outputs[0], r3.inputs[0])
    links.new(r3.outputs[0], bsdf.inputs['Roughness'])


def main():
    with open(SKIN_JSON, encoding='utf-8') as f:
        skin = json.load(f)
    skin_linear = skin['linear_rgb']
    print(f"skin color: {skin['srgb_hex']} linear={skin_linear}")

    obj = bpy.data.objects[MESH_NAME]
    clear_custom_normals(obj)
    build_face_mask(obj)

    mat = obj.material_slots[0].material
    build_material(mat, skin_linear)

    # scalp slot was empty -> give it the same skin material
    for slot in obj.material_slots:
        if slot.material is None:
            slot.material = mat
            print("assigned skin material to empty slot")

    bpy.ops.wm.save_mainfile()
    print(f"Saved: {bpy.data.filepath}")


if __name__ == "__main__":
    main()
