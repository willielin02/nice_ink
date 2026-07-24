# 刺青機貼圖版匯入（2026-07-22）：貼圖 PNG×3 → T_TattooMachine_*；
# sumo_tattoo_machine/needle.fbx → SM_TattooMachine/Needle（幾何+UV、槽名 TattooTex）；
# 腳本生成 M_TattooMachine（BaseColor×邊緣暗化 + MR(G=粗糙,B=金屬) + Normal）
# 並直接綁進兩顆 SM 資產＝C++ 零材質碼（偵測 TattooTex 槽名即不蓋 MID）。
# glTF normal=+Y(OpenGL)、UE 期望 -Y(DirectX) ⇒ flip_green_channel=True。
# Run: UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript=<this> -unattended -nosplash
import unreal

tools = unreal.AssetToolsHelpers.get_asset_tools()
DEST = "/Game/Characters"

# --- 貼圖 ---
def tex_task(filename, dest_name):
    t = unreal.AssetImportTask()
    t.filename = filename
    t.destination_path = DEST
    t.destination_name = dest_name
    t.automated = True
    t.save = True
    t.replace_existing = True
    return t

TEXDIR = r"c:\games\Unreal Engine\nice_ink\SourceAssets\TattooMachineTex"
tex_tasks = [
    tex_task(TEXDIR + r"\T_TattooMachine_base_color.png", "T_TattooMachine_BaseColor"),
    tex_task(TEXDIR + r"\T_TattooMachine_metallic_roughness.png", "T_TattooMachine_MR"),
    tex_task(TEXDIR + r"\T_TattooMachine_normal.png", "T_TattooMachine_Normal"),
]
tools.import_asset_tasks(tex_tasks)

t_base = unreal.load_asset(f"{DEST}/T_TattooMachine_BaseColor")
t_mr = unreal.load_asset(f"{DEST}/T_TattooMachine_MR")
t_nrm = unreal.load_asset(f"{DEST}/T_TattooMachine_Normal")
assert t_base and t_mr and t_nrm, "texture import failed"
t_mr.set_editor_property("srgb", False)
t_mr.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
t_nrm.set_editor_property("srgb", False)
t_nrm.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
t_nrm.set_editor_property("flip_green_channel", True)
for t in (t_base, t_mr, t_nrm):
    unreal.EditorAssetLibrary.save_loaded_asset(t)

# --- 材質 M_TattooMachine ---
MAT_FULL = f"{DEST}/M_TattooMachine"
if unreal.EditorAssetLibrary.does_asset_exist(MAT_FULL):
    unreal.EditorAssetLibrary.delete_asset(MAT_FULL)
mat = tools.create_asset("M_TattooMachine", DEST, unreal.Material, unreal.MaterialFactoryNew())
MEL = unreal.MaterialEditingLibrary

s_base = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -700, -200)
s_base.set_editor_property("texture", t_base)
s_base.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)

# 邊緣暗化（同色前後零件的結構分離；fullbright 場景的假光語言）：
# BaseColor × (1 - EdgeDarken × Fresnel)
fres = MEL.create_material_expression(mat, unreal.MaterialExpressionFresnel, -700, 120)
fres.set_editor_property("exponent", 3.0)
fres.set_editor_property("base_reflect_fraction", 0.02)
p_edge = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -700, 260)
p_edge.set_editor_property("parameter_name", "EdgeDarken")
p_edge.set_editor_property("default_value", 0.35)
mul_ef = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -520, 180)
MEL.connect_material_expressions(fres, "", mul_ef, "A")
MEL.connect_material_expressions(p_edge, "", mul_ef, "B")
om = MEL.create_material_expression(mat, unreal.MaterialExpressionOneMinus, -400, 180)
MEL.connect_material_expressions(mul_ef, "", om, "")
mul_bc = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -260, -80)
MEL.connect_material_expressions(s_base, "RGB", mul_bc, "A")
MEL.connect_material_expressions(om, "", mul_bc, "B")
MEL.connect_material_property(mul_bc, "", unreal.MaterialProperty.MP_BASE_COLOR)

# MR：glTF 慣例 G=Roughness、B=Metallic；金屬度乘旋鈕（fullbright 均勻光下全金屬會悶灰）
s_mr = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -700, 420)
s_mr.set_editor_property("texture", t_mr)
s_mr.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
mk_r = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -500, 400)
mk_r.set_editor_property("r", False)
mk_r.set_editor_property("g", True)
mk_r.set_editor_property("b", False)
mk_r.set_editor_property("a", False)
MEL.connect_material_expressions(s_mr, "RGBA", mk_r, "")
MEL.connect_material_property(mk_r, "", unreal.MaterialProperty.MP_ROUGHNESS)
mk_m = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -500, 520)
mk_m.set_editor_property("r", False)
mk_m.set_editor_property("g", False)
mk_m.set_editor_property("b", True)
mk_m.set_editor_property("a", False)
MEL.connect_material_expressions(s_mr, "RGBA", mk_m, "")
p_met = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -500, 640)
p_met.set_editor_property("parameter_name", "MetallicStrength")
p_met.set_editor_property("default_value", 0.35)
mul_m = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -320, 560)
MEL.connect_material_expressions(mk_m, "", mul_m, "A")
MEL.connect_material_expressions(p_met, "", mul_m, "B")
MEL.connect_material_property(mul_m, "", unreal.MaterialProperty.MP_METALLIC)

s_n = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -700, 760)
s_n.set_editor_property("texture", t_nrm)
s_n.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
MEL.connect_material_property(s_n, "", unreal.MaterialProperty.MP_NORMAL)

MEL.recompile_material(mat)
unreal.EditorAssetLibrary.save_asset(MAT_FULL)
print("MAT_DONE", MAT_FULL)

# --- 網格（幾何+UV、不匯材質→槽名 TattooTex 保留）＋綁材質 ---
def fbx_task(filename, dest_name):
    task = unreal.AssetImportTask()
    task.filename = filename
    task.destination_path = DEST
    task.destination_name = dest_name
    task.automated = True
    task.save = True
    task.replace_existing = True
    ui = unreal.FbxImportUI()
    ui.import_mesh = True
    ui.import_as_skeletal = False
    ui.import_animations = False
    ui.import_materials = False
    ui.import_textures = False
    smd = ui.static_mesh_import_data
    smd.set_editor_property("combine_meshes", True)
    smd.set_editor_property("generate_lightmap_u_vs", False)
    smd.set_editor_property("auto_generate_collision", False)
    task.options = ui
    return task

mesh_tasks = [
    fbx_task(r"c:\games\Unreal Engine\nice_ink\SourceAssets\sumo_tattoo_machine.fbx", "SM_TattooMachine"),
    fbx_task(r"c:\games\Unreal Engine\nice_ink\SourceAssets\sumo_tattoo_grip.fbx", "SM_TattooGrip"),
    fbx_task(r"c:\games\Unreal Engine\nice_ink\SourceAssets\sumo_tattoo_needle.fbx", "SM_TattooNeedle"),
]
tools.import_asset_tasks(mesh_tasks)

for p in ["SM_TattooMachine", "SM_TattooGrip", "SM_TattooNeedle"]:
    sm = unreal.load_asset(f"{DEST}/{p}")
    assert sm, f"missing {p}"
    for i, m in enumerate(sm.static_materials):
        sm.set_material(i, mat)
    unreal.EditorAssetLibrary.save_loaded_asset(sm)
    names = [str(m.material_slot_name) for m in sm.static_materials]
    b = sm.get_bounding_box()
    print(f"SLOTS {p}: {names} mat_bound={[str(m.material_interface.get_name()) if m.material_interface else None for m in sm.static_materials]} bounds z=[{b.min.z:.2f},{b.max.z:.2f}]")

print("IMPORT SCRIPT DONE")
