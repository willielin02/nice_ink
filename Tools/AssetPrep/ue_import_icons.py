# 圖示 PNG → /Game/UI/Icons/T_Ico_<name>，**並把不在來源資料夾裡的圖示資產刪掉**
# （2026-09-09：資產＝來源資料夾的鏡像。此前只匯入不刪，於是 Content 裡累積了 44 個
#  沒有任何程式碼引用的圖示——而 /Game/UI 整個目錄在 cook 白名單裡，全部進包）。
# headless：UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript=<本檔的無空白路徑副本>
import os
import unreal

SRC = "C:/games/Unreal Engine/nice_ink/SourceAssets/UI/icons"
DEST = "/Game/UI/Icons"

# 2026-09-09 圖示大減後留下的孤兒（繪製點都已拆除；保留 TattooPen/MarkerPen＝FP viewmodel）
DEAD = [
    "/Game/UI/Icons/T_UI_Spray", "/Game/UI/Icons/T_UI_Kick", "/Game/UI/Icons/T_UI_Marker",
    "/Game/UI/Icons/T_UI_Cash", "/Game/UI/Icons/T_UI_Cup", "/Game/UI/Icons/T_UI_Eye",
    "/Game/UI/Icons/T_UI_Rotate", "/Game/UI/Icons/T_UI_Trap", "/Game/UI/Icons/T_UI_Sleep",
    "/Game/UI/Icons/T_UI_Nose",
    "/Game/UI/Input/T_InMouseLeft", "/Game/UI/Input/T_InMouseRight",
    "/Game/UI/Input/T_InMouseScroll", "/Game/UI/Input/T_InMouseMove",
]

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

# --- 鏡像：來源沒有的 T_Ico_* ＋ 點名的孤兒，一律刪掉 ---
keep = set(names)
killed = 0
for asset in unreal.EditorAssetLibrary.list_assets(DEST, recursive=False, include_folder=False):
    short = asset.split("/")[-1].split(".")[0]
    if short.startswith("T_Ico_") and short not in keep:
        if unreal.EditorAssetLibrary.delete_asset(asset):
            killed += 1
        else:
            unreal.log_error("[IconImport] DELETE FAILED %s" % asset)
for p in DEAD:
    if unreal.EditorAssetLibrary.does_asset_exist(p):
        if unreal.EditorAssetLibrary.delete_asset(p):
            killed += 1
        else:
            unreal.log_error("[IconImport] DELETE FAILED %s" % p)

unreal.log_warning("[IconImport] DONE import %d/%d  deleted %d" % (ok, len(names), killed))
