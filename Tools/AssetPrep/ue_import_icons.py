# Lucide 線圖示 PNG → /Game/UI/Icons/T_Ico_<name>（2026-09-06 UI 第二批）。
# headless：UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript=<本檔的無空白路徑副本>
import os
import unreal

SRC = "C:/games/Unreal Engine/nice_ink/SourceAssets/UI/icons"
DEST = "/Game/UI/Icons"

tasks, names = [], []
for f in sorted(os.listdir(SRC)):
    if not f.startswith("ico_") or not f.endswith(".png"):
        continue
    name = "T_Ico_" + f[4:-4]
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", os.path.join(SRC, f))
    t.set_editor_property("destination_path", DEST)
    t.set_editor_property("destination_name", name)
    t.set_editor_property("automated", True)
    t.set_editor_property("save", True)
    t.set_editor_property("replace_existing", True)
    tasks.append(t)
    names.append(name)

unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
ok = 0
for name in names:
    p = "%s/%s.%s" % (DEST, name, name)
    if unreal.EditorAssetLibrary.does_asset_exist(p):
        tex = unreal.load_asset(p)
        tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        tex.set_editor_property("never_stream", True)
        unreal.EditorAssetLibrary.save_asset(p)
        ok += 1
    else:
        unreal.log_error("[IconImport] FAILED %s" % name)
unreal.log_warning("[IconImport] DONE %d/%d" % (ok, len(names)))
