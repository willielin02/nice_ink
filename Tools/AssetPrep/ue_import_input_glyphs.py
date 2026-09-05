# 輸入 glyph 貼圖匯入（2026-09-04）：Kenney Input Prompts 1.5（CC0）的滑鼠圖示
# → /Game/UI/Input/T_InMouse*。調色盤（Paper／Red）已在 PNG 端烘好
# （Tools/UiCheck 的 bake 步驟）⇒ 引擎端只要 UI 設定，畫的時候用白色 tint。
# UI 貼圖鐵則：sRGB=True、TC_EditorIcon（不壓縮＝小圖不糊）、無 mip、Clamp。
# Run: UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript=<this> -unattended -nosplash
import unreal

tools = unreal.AssetToolsHelpers.get_asset_tools()
DEST = "/Game/UI/Input"
SRC = r"c:\games\Unreal Engine\nice_ink\SourceAssets\InputPrompts\png"
NAMES = ["T_InMouseLeft", "T_InMouseRight", "T_InMouseScroll", "T_InMouseMove"]

tasks = []
for n in NAMES:
    t = unreal.AssetImportTask()
    t.filename = SRC + "\\" + n + ".png"
    t.destination_path = DEST
    t.destination_name = n
    t.automated = True
    t.save = True
    t.replace_existing = True
    tasks.append(t)
tools.import_asset_tasks(tasks)

ok = True
for n in NAMES:
    a = unreal.load_asset(DEST + "/" + n)
    if not a:
        unreal.log_error("IMPORT FAILED: " + n)
        ok = False
        continue
    a.set_editor_property("srgb", True)
    a.set_editor_property("compression_settings",
                          unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    a.set_editor_property("mip_gen_settings",
                          unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    a.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    a.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
    a.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
    unreal.EditorAssetLibrary.save_loaded_asset(a)
    unreal.log("GLYPH OK: " + n + "  " + str(a.blueprint_get_size_x()) + "x" + str(a.blueprint_get_size_y()))
unreal.log("INPUT GLYPH IMPORT " + ("DONE" if ok else "FAILED"))
