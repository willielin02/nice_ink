# Noto Sans 一族 → /Game/UI/Fonts/FF_NotoSans* FontFace 資產（2026-09-05 中性 UI 制）。
# 一次性 headless 匯入：
#   UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript=<本檔>
# 來源＝SourceAssets/Fonts/NotoSans/（OFL 1.1；Latin/Greek/Cyrillic＋Arabic 走 TTF、
# CJK 走 SubsetOTF——每語各自的子集，Regular+Bold 共 12 面 ~43MB）。
# 只碰引擎類別（AssetImportTask／FontFace factory）⇒ headless 看不到遊戲模組也無妨。
import os
import unreal

SRC = "C:/games/Unreal Engine/nice_ink/SourceAssets/Fonts/NotoSans"
DEST = "/Game/UI/Fonts"

FACES = [
    ("NotoSans-Regular.ttf",        "FF_NotoSans_Regular"),
    ("NotoSans-Bold.ttf",           "FF_NotoSans_Bold"),
    ("NotoSansArabic-Regular.ttf",  "FF_NotoSansArabic_Regular"),
    ("NotoSansArabic-Bold.ttf",     "FF_NotoSansArabic_Bold"),
    ("NotoSansJP-Regular.otf",      "FF_NotoSansJP_Regular"),
    ("NotoSansJP-Bold.otf",         "FF_NotoSansJP_Bold"),
    ("NotoSansTC-Regular.otf",      "FF_NotoSansTC_Regular"),
    ("NotoSansTC-Bold.otf",         "FF_NotoSansTC_Bold"),
    ("NotoSansSC-Regular.otf",      "FF_NotoSansSC_Regular"),
    ("NotoSansSC-Bold.otf",         "FF_NotoSansSC_Bold"),
    ("NotoSansKR-Regular.otf",      "FF_NotoSansKR_Regular"),
    ("NotoSansKR-Bold.otf",         "FF_NotoSansKR_Bold"),
]

tasks = []
for fname, asset in FACES:
    path = os.path.join(SRC, fname)
    if not os.path.exists(path):
        unreal.log_error("[NotoImport] missing source: %s" % path)
        continue
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", path)
    t.set_editor_property("destination_path", DEST)
    t.set_editor_property("destination_name", asset)
    t.set_editor_property("automated", True)
    t.set_editor_property("save", True)
    t.set_editor_property("replace_existing", True)
    tasks.append(t)

unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

ok = 0
for _, asset in FACES:
    p = "%s/%s.%s" % (DEST, asset, asset)
    if unreal.EditorAssetLibrary.does_asset_exist(p):
        obj = unreal.load_asset(p)
        unreal.log_warning("[NotoImport] OK %s (%s)" % (asset, type(obj).__name__))
        ok += 1
    else:
        unreal.log_error("[NotoImport] FAILED %s" % asset)
unreal.log_warning("[NotoImport] DONE %d/%d" % (ok, len(FACES)))
