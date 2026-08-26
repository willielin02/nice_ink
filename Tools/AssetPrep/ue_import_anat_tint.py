# T_BodyChroma ← body_chroma_anat.png（解剖色調；headless -ExecutePythonScript）
# 匯入設定照 08-22 中和圖那次：非 sRGB＋SimpleAverage mip＋TEXTUREGROUP_World，
# 但壓縮改 **BC7**：原本的 TC_VectorDisplacementmap 是未壓縮 RGBA8，中和圖只有
# 32×32 所以無所謂，2048 就是 16MB+mips≈21MB 常駐（08-25 OOM 的帳要記）。
# 內容是平滑低頻乘法場，BC7 視覺無損、4MB+mips≈5.3MB。
import unreal

SRC = r"C:\games\Unreal Engine\nice_ink\SourceAssets\body_chroma_anat.png"
PKG = "/Game/Characters"
NAME = "T_BodyChroma"

task = unreal.AssetImportTask()
task.filename = SRC
task.destination_path = PKG
task.destination_name = NAME
task.automated = True
task.save = True
task.replace_existing = True
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
paths = list(task.get_editor_property("imported_object_paths") or [])
unreal.log(f"ANAT_IMPORT paths={paths}")
assert paths, "import FAILED"

tex = unreal.load_asset(f"{PKG}/{NAME}")
tex.set_editor_property("srgb", False)
tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_BC7)
tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_SIMPLE_AVERAGE)
tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
unreal.EditorAssetLibrary.save_asset(f"{PKG}/{NAME}")
unreal.log(f"ANAT_IMPORT size={tex.blueprint_get_size_x()}x{tex.blueprint_get_size_y()} "
           f"srgb={tex.get_editor_property('srgb')} "
           f"comp={tex.get_editor_property('compression_settings')}")

# 材質參數現值（ChromaStrength 就是這批色調的強度旋鈕）
mat = unreal.load_asset("/Game/Characters/M_InkBodyChar")
mel = unreal.MaterialEditingLibrary
for p in ("ChromaStrength", "SkinBrightness", "SkinDesat"):
    unreal.log(f"ANAT_PARAM {p} = {mel.get_material_default_scalar_parameter_value(mat, p)}")
unreal.log("ANAT_IMPORT_DONE")
