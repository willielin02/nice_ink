# Oswald（展示字體，OFL 1.1）→ /Game/UI/Fonts/FF_Oswald_* ＋ 標誌字貼圖 /Game/UI/T_UI_Logo
# （2026-09-06 UI 第一批：字體兩種人格——展示體＝粗壓縮大寫，內文體＝Noto Sans）。
# 一次性 headless 匯入：
#   UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript=<本檔>
# 只碰引擎類別（AssetImportTask）⇒ headless 看不到遊戲模組也無妨。
import os
import unreal

ROOT = "C:/games/Unreal Engine/nice_ink"
ITEMS = [
    (ROOT + "/SourceAssets/Fonts/Oswald/Oswald-Bold.ttf",   "/Game/UI/Fonts", "FF_Oswald_Bold"),
    (ROOT + "/SourceAssets/Fonts/Oswald/Oswald-Medium.ttf", "/Game/UI/Fonts", "FF_Oswald_Medium"),
    (ROOT + "/SourceAssets/UI/logo_nice_ink.png",           "/Game/UI",       "T_UI_Logo"),
]

tasks = []
for path, dest, name in ITEMS:
    if not os.path.exists(path):
        unreal.log_error("[OswaldImport] missing source: %s" % path)
        continue
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", path)
    t.set_editor_property("destination_path", dest)
    t.set_editor_property("destination_name", name)
    t.set_editor_property("automated", True)
    t.set_editor_property("save", True)
    t.set_editor_property("replace_existing", True)
    tasks.append(t)

unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

ok = 0
for _, dest, name in ITEMS:
    p = "%s/%s.%s" % (dest, name, name)
    if unreal.EditorAssetLibrary.does_asset_exist(p):
        obj = unreal.load_asset(p)
        if isinstance(obj, unreal.Texture2D):
            # UI 貼圖：無 mip、UI 壓縮群、sRGB
            obj.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
            obj.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
            obj.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
            obj.set_editor_property("never_stream", True)
            unreal.EditorAssetLibrary.save_asset(p)
        unreal.log_warning("[OswaldImport] OK %s (%s)" % (name, type(obj).__name__))
        ok += 1
    else:
        unreal.log_error("[OswaldImport] FAILED %s" % name)
unreal.log_warning("[OswaldImport] DONE %d/%d" % (ok, len(ITEMS)))
