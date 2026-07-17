# 匯出蹲踞作畫基底姿勢 → Source/NiceInk/Private/DrawPoseData.h
#
# 資料源＝sumo_retopo_base16.blend 的骨架 REST 矩陣（bone.matrix_local，armature 空間）。
# 這個檔案被 user 存成「rest 姿勢＝蹲踞作畫姿」（pose_position=REST、DrawPose_Backup action
# 只是套用前的備份）——所以蹲姿的絕對骨骼配置就住在 rest 裡，直接匯出。
#
# 執行期用法（2026-07-17 r2）：**只用位置 T**——ApplyDrawBasePose 以「關節位置重定向」
# 重建姿勢（每骨最小 swing＋Hips 雙約束）。旋轉 Q 僅存參考：絕對旋轉跨不過 FBX 匯入的
# 每骨軸向重映射（r1 直寫旋轉＝蒙皮攤成煎餅、關節位置全對），位置才是可信的跨界資料。
#
# 座標轉換（與 FBX 匯入實測慣例一致：UE 本地臉朝 +Y、Blender 臉朝 -Y）：
#   位置 (x,y,z)m → (100x, -100y, 100z)cm
#
# 用法（headless）：
#   & "C:\Program Files\Blender Foundation\Blender 5.1\blender.exe" --background ^
#     "SourceAssets\sumo_retopo_base16.blend" --python Tools\AssetPrep\export_draw_pose.py
import bpy
import math

OUT_H = bpy.path.abspath("//../Source/NiceInk/Private/DrawPoseData.h")

arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
assert arm.data.pose_position == "REST", "本腳本讀 rest＝蹲姿；檔案狀態變了要重新確認"

def to_ue_pos(v):
    return (v.x * 100.0, -v.y * 100.0, v.z * 100.0)

def to_ue_quat(q):
    return (-q.x, q.y, -q.z, q.w)

rows = []
for b in arm.data.bones:
    if b.name.startswith("Jiggle") or b.name == "Root":
        continue
    m = b.matrix_local  # rest, armature space ＝ 蹲姿絕對配置
    q = to_ue_quat(m.to_quaternion())
    t = to_ue_pos(m.to_translation())
    rows.append((b.name, q, t))

# 手部軸向資料已退役（r2）：手指/拇指方向改由執行期骨骼位置現量（同樣的骨軸慣例地雷）。

lines = []
lines.append("// 由 Tools/AssetPrep/export_draw_pose.py 生成——手改無效，重跑腳本。")
lines.append("// 蹲踞作畫基底姿勢（SourceAssets/sumo_retopo_base16.blend 的 rest＝user 手擺蹲姿）。")
lines.append("// 每骨＝絕對 ComponentSpace transform（UE 空間、cm）；執行期依序（root→leaf）寫入")
lines.append("// BowBody 即得蹲姿。絕對目標＝與骨骼局部軸慣例無關。")
lines.append("#pragma once")
lines.append("")
lines.append("#include \"CoreMinimal.h\"")
lines.append("")
lines.append("struct FDrawPoseBoneCS")
lines.append("{")
lines.append("\tconst TCHAR* BoneName;")
lines.append("\tFQuat Q;    // CS 旋轉")
lines.append("\tFVector T;  // CS 位置（cm）")
lines.append("};")
lines.append("")
lines.append("inline const FDrawPoseBoneCS GDrawPoseCS[] = {")
for name, q, t in rows:
    lines.append(
        f"\t{{ TEXT(\"{name}\"), FQuat({q[0]:.6f}f, {q[1]:.6f}f, {q[2]:.6f}f, {q[3]:.6f}f), "
        f"FVector({t[0]:.3f}f, {t[1]:.3f}f, {t[2]:.3f}f) }},")
lines.append("};")
lines.append("")

with open(OUT_H, "w", encoding="utf-8", newline="\n") as f:
    f.write("\n".join(lines))
print("WROTE", OUT_H, f"({len(rows)} bones)")
