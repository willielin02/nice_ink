# chrome 貼圖 PNG → /Game/UI（2026-09-09 立體鍵帽；2026-09-11 十修加「一顆鍵一張」）。
#   SourceAssets/UI/chrome/keycap.png → /Game/UI/T_UI_Keycap（9-slice 保底）
#   SourceAssets/UI/chrome/keys/<KEY>.png → /Game/UI/Keys/T_Key_<KEY>（不可按態＝同圖乘透明度，十一修起不另烘）
# 貼圖設定：UI group、TC_EDITOR_ICON（不壓縮＝細線與字禁得起）、never_stream。
# 9-slice **無 mip**（角在縮放時不可以被 mip 糊掉）；一顆鍵一張的**有 mip**（整張圖被當一個物件
# 縮放，128 高畫到 24~36px＝4~5 倍縮小，沒有 mip 就是閃爍的鋸齒；寬度已補到 2 的冪）。
# **資產＝來源資料夾的鏡像**：keys/ 裡沒有的 T_Key_* 會被刪（與 ue_import_icons.py 同一條規矩）。
# headless：UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript=<本檔的無空白路徑副本>
import os
import unreal

SRC = "C:/games/Unreal Engine/nice_ink/SourceAssets/UI/chrome"
DEST = "/Game/UI"
KEYS_DEST = "/Game/UI/Keys"
NAMES = {"keycap.png": "T_UI_Keycap"}

jobs = []   # (src_file, dest_dir, asset_name, with_mips)
for f, name in NAMES.items():
    jobs.append((os.path.join(SRC, f), DEST, name, False))

keys_dir = os.path.join(SRC, "keys")
wanted = set()
for f in sorted(os.listdir(keys_dir)):
    if not f.lower().endswith(".png"):
        continue
    name = "T_Key_%s" % f[:-4]
    wanted.add(name)
    jobs.append((os.path.join(keys_dir, f), KEYS_DEST, name, True))

tasks = []
for src, dest, name, _ in jobs:
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", src)
    t.set_editor_property("destination_path", dest)
    t.set_editor_property("destination_name", name)
    t.set_editor_property("automated", True)
    t.set_editor_property("save", True)
    t.set_editor_property("replace_existing", True)
    tasks.append(t)

unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
ok = 0
for _, dest, name, with_mips in jobs:
    p = "%s/%s.%s" % (dest, name, name)
    if unreal.EditorAssetLibrary.does_asset_exist(p):
        tex = unreal.load_asset(p)
        tex.set_editor_property("mip_gen_settings",
                                unreal.TextureMipGenSettings.TMGS_SHARPEN4 if with_mips
                                else unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        tex.set_editor_property("never_stream", True)
        unreal.EditorAssetLibrary.save_asset(p)
        ok += 1
    else:
        unreal.log_error("[ChromeImport] FAILED %s" % name)

# 鏡像：來源沒有的 T_Key_* 刪掉
removed = 0
if unreal.EditorAssetLibrary.does_directory_exist(KEYS_DEST):
    for ap in unreal.EditorAssetLibrary.list_assets(KEYS_DEST, recursive=False, include_folder=False):
        an = ap.split("/")[-1].split(".")[0]
        if an.startswith("T_Key_") and an not in wanted:
            unreal.EditorAssetLibrary.delete_asset(ap)
            removed += 1

stale_dim = "%s/T_UI_KeycapDim.T_UI_KeycapDim" % DEST
if unreal.EditorAssetLibrary.does_asset_exist(stale_dim):
    unreal.EditorAssetLibrary.delete_asset(stale_dim)
    removed += 1
unreal.log_warning("[ChromeImport] DONE %d/%d (removed %d stale)" % (ok, len(jobs), removed))
