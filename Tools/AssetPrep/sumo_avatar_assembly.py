# 正式樣張：setup_skin_material（patch 到 SumoRetopo、保留 user FaceMask）＋渲染
import bpy, math
from pathlib import Path

MASTER = r"C:\games\Unreal Engine\nice_ink\SourceAssets\sumo_character_master.blend"
PIPE = Path(r"C:\games\Unreal Engine\nice_ink\Tools\FacePipeline")
OUT = r"C:\Users\willi\AppData\Local\Temp\claude\c--games-Unreal-Engine-nice-ink\00d214a0-ef56-4e54-9786-cb494e647707\scratchpad"
AVATAR = OUT + r"\sumo_avatar_preview.blend"

bpy.ops.wm.open_mainfile(filepath=MASTER)
if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')

# exec 官方 setup 腳本（同 make_avatar_blend 的 patch 手法）：
#  - MESH_NAME 指向 SumoRetopo
#  - build_face_mask 中性化（FaceMask 已由 user 標記欽定）
#  - save_mainfile 中性化（master 不動，另存 avatar 預覽）
src = (PIPE / "setup_skin_material_blender.py").read_text(encoding="utf-8")
src = src.replace('MESH_NAME = "PlusSize_Male_Body_01"', 'MESH_NAME = "SumoRetopo"')
src = src.replace("PIPE = Path(__file__).resolve().parent",
                  'PIPE = Path(r"C:\\games\\Unreal Engine\\nice_ink\\Tools\\FacePipeline")')
src = src.replace("\n    build_face_mask(obj)", "\n    print('FaceMask kept (user-marked)')")
src = src.replace("bpy.ops.wm.save_mainfile(", "(lambda **kw: None)(")
exec(compile(src, "setup_skin_material_blender.py", "exec"))

# --- 髮色遮罩＋髮流接進材質（hair over face/skin，Tint Chain 之前）---
import os
# 貼圖一律讀 SourceAssets 常設路徑（scratchpad 隨 session 消失；各 bake 腳本已自動同步副本過來）
S_DIR = r"C:\games\Unreal Engine\nice_ink\SourceAssets"
if os.path.exists(S_DIR + r"\hair_mask.png"):
    _obj = bpy.data.objects["SumoRetopo"]
    _mat = _obj.material_slots[0].material
    _nt = _mat.node_tree
    base_mix = _nt.nodes["Base Color"]
    tint_chain = _nt.nodes["Tint Chain"]
    uv_body = _nt.nodes["UV Body"]
    t_mask = _nt.nodes.new("ShaderNodeTexImage")
    t_mask.name = t_mask.label = "Hair Mask"
    t_mask.image = bpy.data.images.load(S_DIR + r"\hair_mask.png")
    t_mask.image.colorspace_settings.name = 'Non-Color'

    # AO 深溝陰影：v23 回滾停用（2026-07-10 user 定案：整套陰影/細紋修復放棄）
    if False and os.path.exists(S_DIR + r"\body_ao_shadow.png"):
        t_ao = _nt.nodes.new("ShaderNodeTexImage")
        t_ao.name = t_ao.label = "AO Shadow"
        t_ao.image = bpy.data.images.load(S_DIR + r"\body_ao_shadow.png")
        t_ao.image.colorspace_settings.name = 'Non-Color'
        _nt.links.new(uv_body.outputs["UV"], t_ao.inputs["Vector"])
        ao_mult = _nt.nodes.new("ShaderNodeMix")
        ao_mult.name = ao_mult.label = "AO Multiply"
        ao_mult.data_type = 'RGBA'
        ao_mult.blend_type = 'MULTIPLY'
        ao_mult.inputs["Factor"].default_value = 1.0
        _bsdf0 = _nt.nodes["Skin BSDF"]
        _nt.links.new(tint_chain.outputs["Result"], ao_mult.inputs[6])   # A (RGBA)
        _nt.links.new(t_ao.outputs["Color"], ao_mult.inputs[7])          # B (RGBA)
        _nt.links.new(ao_mult.outputs[2], _bsdf0.inputs["Base Color"])   # Result (RGBA)
        print("AO_PATCHED")

    # 臉罩：平滑貼圖版取代二值頂點色（頂點色在邊界面內插=面級鋸齒）
    if os.path.exists(S_DIR + r"\face_mask.png"):
        f_mask = _nt.nodes.new("ShaderNodeTexImage")
        f_mask.name = f_mask.label = "Face Mask Tex"
        f_mask.image = bpy.data.images.load(S_DIR + r"\face_mask.png")
        f_mask.image.colorspace_settings.name = 'Non-Color'
        _nt.links.new(uv_body.outputs["UV"], f_mask.inputs["Vector"])
        _gate = _nt.nodes["Alpha x Mask"]
        for _l in list(_nt.links):
            if _l.to_node == _gate and _l.to_socket == _gate.inputs[1]:
                _nt.links.remove(_l)
        _nt.links.new(f_mask.outputs["Color"], _gate.inputs[1])
        print("FACE_MASK_TEX")
    t_tint = _nt.nodes.new("ShaderNodeTexImage")
    t_tint.name = t_tint.label = "Hair Tint"
    # HairUV=島狀＋逐紋素烘焙（面內場值精確；島縫一階外插 gutter）
    t_tint.image = bpy.data.images.load(S_DIR + r"\hair_tint_hd.png")
    _nt.links.new(uv_body.outputs["UV"], t_mask.inputs["Vector"])
    uv_hair = _nt.nodes.new("ShaderNodeUVMap")
    uv_hair.name = uv_hair.label = "UV Hair"
    uv_hair.uv_map = "HairUV"
    _nt.links.new(uv_hair.outputs["UV"], t_tint.inputs["Vector"])
    print("HAIR_HD_UV")
    mix_hair = _nt.nodes.new("ShaderNodeMix")
    mix_hair.name = mix_hair.label = "Hair Over"
    mix_hair.data_type = 'RGBA'
    _nt.links.new(base_mix.outputs["Result"], mix_hair.inputs["A"])
    _nt.links.new(t_tint.outputs["Color"], mix_hair.inputs["B"])
    _nt.links.new(t_mask.outputs["Color"], mix_hair.inputs["Factor"])
    _nt.links.new(mix_hair.outputs["Result"], tint_chain.inputs["A"])

    # 髮區材質：霧面油頭粗糙度（隨絲理變化）＋髮絲線性凹凸法線
    _bsdf = _nt.nodes["Skin BSDF"]
    rough_base = _nt.nodes["Rough Base"]
    t_hrough = _nt.nodes.new("ShaderNodeTexImage")
    t_hrough.name = t_hrough.label = "Hair Rough Tex"
    t_hrough.image = bpy.data.images.load(S_DIR + r"\hair_rough_hd.png")
    t_hrough.image.colorspace_settings.name = 'Non-Color'
    _nt.links.new(uv_hair.outputs["UV"], t_hrough.inputs["Vector"])
    mix_rough = _nt.nodes.new("ShaderNodeMix")
    mix_rough.name = mix_rough.label = "Hair Rough Over"
    mix_rough.data_type = 'FLOAT'
    _nt.links.new(t_mask.outputs["Color"], mix_rough.inputs["Factor"])
    _nt.links.new(rough_base.outputs[0], mix_rough.inputs["A"])
    _nt.links.new(t_hrough.outputs["Color"], mix_rough.inputs["B"])
    _nt.links.new(mix_rough.outputs["Result"], _bsdf.inputs["Roughness"])
    # 髮區法線：**必須有自己的 Normal Map 節點（tangent=HairUV）**——
    # 共用 UVMap 切線會讓凹凸方向隨島旋轉逐島亂轉（面級板塊病）。
    # 皮膚/髮各自解到向量後，用遮罩在向量層級混合。
    t_hnorm = _nt.nodes.new("ShaderNodeTexImage")
    t_hnorm.name = t_hnorm.label = "Hair Normal Tex"
    t_hnorm.image = bpy.data.images.load(S_DIR + r"\hair_normal_hd.png")
    t_hnorm.image.colorspace_settings.name = 'Non-Color'
    _nt.links.new(uv_hair.outputs["UV"], t_hnorm.inputs["Vector"])
    norm_mix = _nt.nodes["Normal Mix"]
    nmap = _nt.nodes["Normal Map"]
    if os.path.exists(S_DIR + r"\body_normal_scan.png"):
        # 高模烘焙法線（肥肉堆疊本體）⊕ 毛孔細節（UDN 疊加）；nmap 全強度，
        # 毛孔先向平面縮到 0.4 等效（原 NORMAL_STRENGTH）
        t_bn = _nt.nodes.new("ShaderNodeTexImage")
        t_bn.name = t_bn.label = "Body Normal Scan"
        t_bn.image = bpy.data.images.load(S_DIR + r"\body_normal_scan.png")
        t_bn.image.colorspace_settings.name = 'Non-Color'
        _nt.links.new(uv_body.outputs["UV"], t_bn.inputs["Vector"])
        pore_scale = _nt.nodes.new("ShaderNodeMix")
        pore_scale.name = pore_scale.label = "Pore Scale"
        pore_scale.data_type = 'RGBA'
        pore_scale.inputs["Factor"].default_value = 0.4
        pore_scale.inputs[6].default_value = (0.5, 0.5, 1.0, 1.0)
        _nt.links.new(norm_mix.outputs["Result"], pore_scale.inputs[7])
        pore_zb = _nt.nodes.new("ShaderNodeMix")   # 毛孔藍通道歸零（UDN：z 只用高模的）
        pore_zb.name = pore_zb.label = "Pore Zero B"
        pore_zb.data_type = 'RGBA'
        pore_zb.blend_type = 'MULTIPLY'
        pore_zb.inputs["Factor"].default_value = 1.0
        _nt.links.new(pore_scale.outputs[2], pore_zb.inputs[6])
        pore_zb.inputs[7].default_value = (1.0, 1.0, 0.0, 1.0)
        udn_add = _nt.nodes.new("ShaderNodeMix")
        udn_add.name = udn_add.label = "UDN Add"
        udn_add.data_type = 'RGBA'
        udn_add.blend_type = 'ADD'
        udn_add.inputs["Factor"].default_value = 1.0
        _nt.links.new(t_bn.outputs["Color"], udn_add.inputs[6])
        _nt.links.new(pore_zb.outputs[2], udn_add.inputs[7])
        udn_sub = _nt.nodes.new("ShaderNodeMix")
        udn_sub.name = udn_sub.label = "UDN Sub"
        udn_sub.data_type = 'RGBA'
        udn_sub.blend_type = 'SUBTRACT'
        udn_sub.inputs["Factor"].default_value = 1.0
        udn_sub.clamp_result = True
        _nt.links.new(udn_add.outputs[2], udn_sub.inputs[6])
        udn_sub.inputs[7].default_value = (0.5, 0.5, 0.0, 1.0)
        _nt.links.new(udn_sub.outputs[2], nmap.inputs["Color"])
        nmap.inputs["Strength"].default_value = 1.0
        print("BODY_NORMAL_PATCHED")
    else:
        _nt.links.new(norm_mix.outputs["Result"], nmap.inputs["Color"])  # 皮膚鏈原樣
    nmap_h = _nt.nodes.new("ShaderNodeNormalMap")
    nmap_h.name = nmap_h.label = "Hair Normal Map"
    nmap_h.uv_map = "HairUV"
    _nt.links.new(t_hnorm.outputs["Color"], nmap_h.inputs["Color"])
    mixn = _nt.nodes.new("ShaderNodeMix")
    mixn.name = mixn.label = "Hair Normal Blend"
    mixn.data_type = 'VECTOR'
    _nt.links.new(t_mask.outputs["Color"], mixn.inputs["Factor"])
    _nt.links.new(nmap.outputs["Normal"], mixn.inputs[4])   # A (VECTOR)
    _nt.links.new(nmap_h.outputs["Normal"], mixn.inputs[5])  # B (VECTOR)
    vnorm = _nt.nodes.new("ShaderNodeVectorMath")
    vnorm.operation = 'NORMALIZE'
    _nt.links.new(mixn.outputs[1], vnorm.inputs[0])          # Result (VECTOR)
    _nt.links.new(vnorm.outputs["Vector"], _bsdf.inputs["Normal"])
    print("HAIR_PATCHED")

    # --- 褌布料層（CC0 實拍 PBR tile；架構同髮區 override）---
    # 遮罩=fundoshi_mask.png（UV0）；布三貼圖=UV0×TILE 平鋪（織距 sub-texel 進不了圖集，
    # 同皮膚毛孔 tile 原理）；法線內容在縮放 UV0 上、無旋轉 → 共用 UVMap tangent 安全，
    # 但要自己的 Normal Map 節點（強度/內容獨立）。
    SA_DIR = r"C:\games\Unreal Engine\nice_ink\SourceAssets"   # 布資產常設路徑（scratchpad 會隨 session 消失）
    if os.path.exists(SA_DIR + r"\fundoshi_mask.png") and os.path.exists(SA_DIR + r"\fundoshi_color.jpg"):
        F_TILE = 10.0
        # 遮罩用銳化衍生版（sumo_cloth_edge_bake.py 從手繪正源產出；過渡 ±0.75mm）
        _mask_img = r"\fundoshi_mask_sharp.png" if os.path.exists(SA_DIR + r"\fundoshi_mask_sharp.png") else r"\fundoshi_mask.png"
        t_fmask = _nt.nodes.new("ShaderNodeTexImage")
        t_fmask.name = t_fmask.label = "Fundoshi Mask"
        t_fmask.image = bpy.data.images.load(SA_DIR + _mask_img)
        t_fmask.image.colorspace_settings.name = 'Non-Color'
        t_fmask.interpolation = 'Cubic'   # 近拍邊界：雙線性菱形紋 → cubic 平滑
        _nt.links.new(uv_body.outputs["UV"], t_fmask.inputs["Vector"])
        f_map = _nt.nodes.new("ShaderNodeMapping")
        f_map.name = f_map.label = "Fundoshi Tile"
        f_map.inputs["Scale"].default_value = (F_TILE, F_TILE, 1.0)
        _nt.links.new(uv_body.outputs["UV"], f_map.inputs["Vector"])
        t_fcol = _nt.nodes.new("ShaderNodeTexImage")
        t_fcol.name = t_fcol.label = "Fundoshi Color"
        t_fcol.image = bpy.data.images.load(SA_DIR + r"\fundoshi_color.jpg")
        _nt.links.new(f_map.outputs["Vector"], t_fcol.inputs["Vector"])
        t_frough = _nt.nodes.new("ShaderNodeTexImage")
        t_frough.name = t_frough.label = "Fundoshi Rough"
        t_frough.image = bpy.data.images.load(SA_DIR + r"\fundoshi_rough.jpg")
        t_frough.image.colorspace_settings.name = 'Non-Color'
        _nt.links.new(f_map.outputs["Vector"], t_frough.inputs["Vector"])
        t_fnorm = _nt.nodes.new("ShaderNodeTexImage")
        t_fnorm.name = t_fnorm.label = "Fundoshi Normal"
        t_fnorm.image = bpy.data.images.load(SA_DIR + r"\fundoshi_normal.jpg")
        t_fnorm.image.colorspace_settings.name = 'Non-Color'
        _nt.links.new(f_map.outputs["Vector"], t_fnorm.inputs["Vector"])
        # Base Color：tint_chain 之後蓋布（布不吃膚色調變）
        mix_fc = _nt.nodes.new("ShaderNodeMix")
        mix_fc.name = mix_fc.label = "Fundoshi Over"
        mix_fc.data_type = 'RGBA'
        _nt.links.new(t_fmask.outputs["Color"], mix_fc.inputs["Factor"])
        _nt.links.new(tint_chain.outputs["Result"], mix_fc.inputs[6])
        _nt.links.new(t_fcol.outputs["Color"], mix_fc.inputs[7])
        _nt.links.new(mix_fc.outputs[2], _bsdf.inputs["Base Color"])
        # Roughness / Normal：接在現行鏈之後（自動找當前來源）
        cur_r = next(l.from_socket for l in _nt.links
                     if l.to_node == _bsdf and l.to_socket.name == "Roughness")
        mix_fr = _nt.nodes.new("ShaderNodeMix")
        mix_fr.name = mix_fr.label = "Fundoshi Rough Over"
        mix_fr.data_type = 'FLOAT'
        _nt.links.new(t_fmask.outputs["Color"], mix_fr.inputs["Factor"])
        _nt.links.new(cur_r, mix_fr.inputs[2])
        _nt.links.new(t_frough.outputs["Color"], mix_fr.inputs[3])
        _nt.links.new(mix_fr.outputs[0], _bsdf.inputs["Roughness"])
        cur_n = next(l.from_socket for l in _nt.links
                     if l.to_node == _bsdf and l.to_socket.name == "Normal")
        nmap_f = _nt.nodes.new("ShaderNodeNormalMap")
        nmap_f.name = nmap_f.label = "Fundoshi Normal Map"
        _nt.links.new(t_fnorm.outputs["Color"], nmap_f.inputs["Color"])
        mixnf = _nt.nodes.new("ShaderNodeMix")
        mixnf.name = mixnf.label = "Fundoshi Normal Blend"
        mixnf.data_type = 'VECTOR'
        _nt.links.new(t_fmask.outputs["Color"], mixnf.inputs["Factor"])
        _nt.links.new(cur_n, mixnf.inputs[4])
        _nt.links.new(nmap_f.outputs["Normal"], mixnf.inputs[5])
        vnorm_f = _nt.nodes.new("ShaderNodeVectorMath")
        vnorm_f.operation = 'NORMALIZE'
        _nt.links.new(mixnf.outputs[1], vnorm_f.inputs[0])
        _nt.links.new(vnorm_f.outputs["Vector"], _bsdf.inputs["Normal"])
        print("FUNDOSHI_PATCHED")

    # --- 布緣接觸陰影（sumo_cloth_edge_bake 衍生；布側捲邊線+皮側接觸影=厚度感）---
    # 全身皮膚陰影已依 user 決定移除（會干擾刺青呈現）；此環僅 ~2mm 貼著布緣
    if os.path.exists(SA_DIR + r"\fundoshi_edge_shadow.png"):
        t_es = _nt.nodes.new("ShaderNodeTexImage")
        t_es.name = t_es.label = "Edge Shadow"
        t_es.image = bpy.data.images.load(SA_DIR + r"\fundoshi_edge_shadow.png")
        t_es.image.colorspace_settings.name = 'Non-Color'
        _nt.links.new(uv_body.outputs["UV"], t_es.inputs["Vector"])
        cur_c = next(l.from_socket for l in _nt.links
                     if l.to_node == _bsdf and l.to_socket.name == "Base Color")
        mix_es = _nt.nodes.new("ShaderNodeMix")
        mix_es.name = mix_es.label = "Edge Shadow Multiply"
        mix_es.data_type = 'RGBA'
        mix_es.blend_type = 'MULTIPLY'
        mix_es.inputs["Factor"].default_value = 1.0
        _nt.links.new(cur_c, mix_es.inputs[6])
        _nt.links.new(t_es.outputs["Color"], mix_es.inputs[7])
        _nt.links.new(mix_es.outputs[2], _bsdf.inputs["Base Color"])
        print("EDGE_SHADOW_PATCHED")
    # 立體感：布緣浮凸（遮罩羽化=斜坡，自動）＋ body_height.png 手繪高度，
    # 都走 Bump 節點=viewport 即時回饋；UE 匯出時再離線烘成法線貼圖
    if os.path.exists(SA_DIR + r"\body_height.png"):
        cur_n2 = next(l.from_socket for l in _nt.links
                      if l.to_node == _bsdf and l.to_socket.name == "Normal")
        bump_e = _nt.nodes.new("ShaderNodeBump")
        bump_e.name = bump_e.label = "Cloth Edge Bump"
        bump_e.inputs["Strength"].default_value = 1.0
        bump_e.inputs["Distance"].default_value = 0.003    # 布厚 ~3mm（銳化遮罩=陡坡，台階感實）
        _nt.links.new(t_fmask.outputs["Color"], bump_e.inputs["Height"])
        _nt.links.new(cur_n2, bump_e.inputs["Normal"])
        t_hp = _nt.nodes.new("ShaderNodeTexImage")
        t_hp.name = t_hp.label = "Height Paint"
        t_hp.image = bpy.data.images.load(SA_DIR + r"\body_height.png")
        t_hp.image.colorspace_settings.name = 'Non-Color'
        _nt.links.new(uv_body.outputs["UV"], t_hp.inputs["Vector"])
        bump_p = _nt.nodes.new("ShaderNodeBump")
        bump_p.name = bump_p.label = "Height Paint Bump"
        bump_p.inputs["Strength"].default_value = 0.7
        bump_p.inputs["Distance"].default_value = 0.004    # 手繪皺摺幅度 ~4mm
        _nt.links.new(t_hp.outputs["Color"], bump_p.inputs["Height"])
        _nt.links.new(bump_e.outputs["Normal"], bump_p.inputs["Normal"])
        _nt.links.new(bump_p.outputs["Normal"], _bsdf.inputs["Normal"])
        print("HEIGHT_PAINT_PATCHED")

for img in bpy.data.images:
    if img.packed_file or img.source != 'FILE':
        continue
    try:
        img.pack()
    except Exception as e:
        print(f"PACK_SKIP {img.name}: {e}")

bpy.context.scene.render.engine = 'BLENDER_EEVEE'   # 存檔前引擎歸位
bpy.ops.wm.save_as_mainfile(filepath=AVATAR, compress=True)
print("AVATAR_SAVED", AVATAR)

# --- 渲染 ---
scene = bpy.context.scene
scene.render.engine = 'BLENDER_EEVEE'
scene.render.resolution_x = 1000
scene.render.resolution_y = 1000
ld = bpy.data.lights.new("key", 'SUN'); ld.energy = 4.0
lo = bpy.data.objects.new("key", ld); bpy.context.collection.objects.link(lo)
lo.rotation_euler = (math.radians(65), 0, math.radians(-25))
ld2 = bpy.data.lights.new("fill", 'SUN'); ld2.energy = 1.2
lo2 = bpy.data.objects.new("fill", ld2); bpy.context.collection.objects.link(lo2)
lo2.rotation_euler = (math.radians(100), 0, math.radians(160))

cd = bpy.data.cameras.new("cam"); cd.type = 'ORTHO'
cam = bpy.data.objects.new("cam", cd); bpy.context.collection.objects.link(cam)
scene.camera = cam

import os
scene.render.image_settings.file_format = 'PNG'

def shot(name, loc, rot, ortho):
    cd.ortho_scale = ortho
    cam.location = loc
    cam.rotation_euler = rot
    scene.render.filepath = f"{OUT}\\{name}.png"
    bpy.ops.render.render(write_still=True)
    print("SHOT", name, "exists=", os.path.exists(f"{OUT}\\{name}.png"),
          "filepath=", scene.render.filepath)

shot("av_face", (0, -4, 1.42), (math.radians(90), 0, 0), 0.7)
shot("av_face34", (-2.6, -3.0, 1.5), (math.radians(84), 0, math.radians(-40)), 0.9)
shot("av_back", (0, 4, 1.45), (math.radians(90), 0, math.radians(180)), 0.8)
shot("av_full", (0, -4, 0.9), (math.radians(90), 0, 0), 2.2)

# 閉眼變體（眼睛鏈預覽：換臉貼圖即可，FaceUV 無關 UV0）
CLOSED = r"C:\games\Unreal Engine\nice_ink\Tools\FacePipeline\out\face_texture_eyes_closed.png"
if os.path.exists(CLOSED):
    ft = bpy.data.objects["SumoRetopo"].material_slots[0].material.node_tree.nodes["Face Texture"]
    ft.image = bpy.data.images.load(CLOSED)
    shot("av_face_closed", (0, -4, 1.42), (math.radians(90), 0, 0), 0.7)
print("DONE")
