# 產生 Source/NiceInk/Private/NeckSeamData.h（generated——引擎脖子生成＋軌道量測的資料地基）
# 輸入：neck_seam_rings.json（sumo_neck_ring_cut.py 產出）＋ master.blend（頭殼點雲）
#       ＋ body_chroma.png（UV0 端色）＋ hair_tint_hd.png / hair_mask.png（髮際端色）
# 座標約定：UE component space = (x_blender, -y_blender, z_blender) × 100（robo 實證）。
# 端色編碼：VertexColor.rgb = linear值/2（材質端 ×2 解碼，同 chroma 貼圖慣例）、.a = hairW。
#   skin 支路：SkinTone × (VC.rgb×2)＝chroma 因子；hair 支路：VC.rgb×2＝絕對髮色 albedo。
# Run: blender --background --python sumo_neck_export_header.py
import bpy
import json
import math

SA = r"C:/games/Unreal Engine/nice_ink/SourceAssets"
MASTER = SA + "/sumo_character_master.blend"
JSON_IN = SA + "/neck_seam_rings.json"
OUT_H = r"C:/games/Unreal Engine/nice_ink/Source/NiceInk/Private/NeckSeamData.h"

with open(JSON_IN, "r", encoding="utf-8") as f:
    data = json.load(f)
assert data["count"] == 84  # cut_seam_head3 @ base16（2026-07-17 二度重標）

# ---------- 貼圖採樣 ----------
def load_img(path):
    img = bpy.data.images.load(path)
    w, h = img.size
    px = list(img.pixels)  # RGBA float, row 0 = bottom（與 Blender UV v 同向）
    return (w, h, px)

def sample(imgt, u, v):
    w, h, px = imgt
    u = min(max(u % 1.0, 0.0), 0.999999)
    v = min(max(v % 1.0, 0.0), 0.999999)
    x = u * (w - 1)
    y = v * (h - 1)
    x0, y0 = int(x), int(y)
    x1, y1 = min(x0 + 1, w - 1), min(y0 + 1, h - 1)
    fx, fy = x - x0, y - y0
    def at(xx, yy):
        i = (yy * w + xx) * 4
        return px[i], px[i + 1], px[i + 2]
    c00, c10, c01, c11 = at(x0, y0), at(x1, y0), at(x0, y1), at(x1, y1)
    return tuple(
        (c00[k] * (1 - fx) + c10[k] * fx) * (1 - fy) + (c01[k] * (1 - fx) + c11[k] * fx) * fy
        for k in range(3))

def srgb_to_lin(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4

chroma_t = load_img(SA + "/body_chroma.png")      # 16-bit linear data（值=factor/2）
hair_t = load_img(SA + "/hair_tint_hd.png")       # 8-bit sRGB albedo
hairm_t = load_img(SA + "/hair_mask.png")         # 8-bit mask

def solid_hair_tint(u0, v0):
    # 只從「實繪核心」（mask>0.9）採髮色——直採縫頂點自己的 hairuv 會咬到髮條的
    # 灰白邊緣像素（2026-07-17 白汙染實錘：mask=0.59 邊緣採出 albedo 0.42 灰）。
    # 由內向外擴窗搜尋核心像素，取其平均；找不到＝這點不算髮（回 None）。
    w, h, _ = hair_t
    for rad_px in (8, 24, 48):
        du = rad_px / w
        dv = rad_px / h
        acc = [0.0, 0.0, 0.0]
        n = 0
        STEPS = 7
        for iy in range(STEPS):
            for ix in range(STEPS):
                uu = u0 + (ix / (STEPS - 1) - 0.5) * 2 * du
                vv = v0 + (iy / (STEPS - 1) - 0.5) * 2 * dv
                if sample(hairm_t, uu, vv)[0] > 0.9:
                    c = sample(hair_t, uu, vv)
                    for k in range(3):
                        acc[k] += srgb_to_lin(c[k])
                    n += 1
        if n:
            return [c / n for c in acc]
    return None

def ring_color(v):
    # 皮膚支路＝中性 0.5（chroma 場均值中性、縫區偏差微小；且臉區的 UV0 圖集位置
    # 不在 chroma 覆蓋內＝採樣是垃圾——robo 截圖實錘灰斑）；只實采髮際的髮色。
    # 髮支路門檻化（2026-07-17 修正）：MARK_Hair 權重域比實繪髮罩大一圈——權重=1
    # 但髮罩≈0 的頂點若照乘積走會產生「權重域端色」的半調污染；未達門檻一律純膚。
    cs = 0.0
    hm = 0.0
    uv_main = None
    n_main = -1
    for e in v["uv"]:
        n = e["n"]
        m = sample(hairm_t, e["hairuv"][0], e["hairuv"][1])[0]
        hm += m * n
        cs += n
        if n > n_main:
            n_main = n
            uv_main = e["hairuv"]
    hm /= cs
    hairw = v["hairW"] * hm  # 髮支路強度＝MARK_Hair × 髮罩實值
    if hairw < 0.5:
        return [0.5, 0.5, 0.5], 0.0          # 未達實繪門檻＝純膚端色
    hr = solid_hair_tint(uv_main[0], uv_main[1])
    if hr is None:
        return [0.5, 0.5, 0.5], 0.0          # 鄰域無實繪核心＝同上
    # rgb 儲存（linear/2 編碼）：skin=0.5、hair=albedo/2——同一欄位按 a 混合語意
    rgb = [0.5 * (1.0 - hairw) + (hr[k] * 0.5) * hairw for k in range(3)]
    return rgb, hairw

# ---------- 骨名表＋權重（top-4 歸一） ----------
bone_names = []
def bone_idx(nm):
    if nm not in bone_names:
        bone_names.append(nm)
    return bone_names.index(nm)

def conv_vert(v):
    p = v["pos"]
    n = v["nrm"]
    pos = (p[0] * 100.0, -p[1] * 100.0, p[2] * 100.0)
    nrm = (n[0], -n[1], n[2])
    w = sorted(v["w"], key=lambda x: -x[1])[:4]
    tot = sum(x[1] for x in w)
    inf = [(bone_idx(x[0]), x[1] / tot) for x in w]
    while len(inf) < 4:
        inf.append((0, 0.0))
    rgb, hairw = ring_color(v)
    return pos, nrm, rgb, hairw, inf

head_conv = [conv_vert(v) for v in data["head_ring"]]
body_conv = [conv_vert(v) for v in data["body_ring"]]

# ---------- 頭殼點雲（master 術後：頭殼＝帶 Head 權重的全部頂點） ----------
bpy.ops.wm.open_mainfile(filepath=MASTER)
body = bpy.data.objects["SumoRetopo"]
me = body.data
gi_head = body.vertex_groups["Head"].index
shell = []
for v in me.vertices:
    for g in v.groups:
        if g.group == gi_head and g.weight > 0.5:
            shell.append((v.co.x * 100.0, -v.co.y * 100.0, v.co.z * 100.0))
            break
assert len(shell) == 1542, f"head shell {len(shell)} != 1542"  # cut_seam_head3 手術實測

# ---------- 寫標頭 ----------
def fmt_vert(c):
    pos, nrm, rgb, hairw, inf = c
    b = ",".join(str(i) for i, _ in inf)
    w = ",".join(f"{x:.6f}f" for _, x in inf)
    return (f"\t{{{pos[0]:.4f}f,{pos[1]:.4f}f,{pos[2]:.4f}f, "
            f"{nrm[0]:.5f}f,{nrm[1]:.5f}f,{nrm[2]:.5f}f, "
            f"{rgb[0]:.5f}f,{rgb[1]:.5f}f,{rgb[2]:.5f}f, {hairw:.4f}f, "
            f"{{{b}}}, {{{w}}}}},")

L = []
L.append("// NeckSeamData.h — GENERATED by Tools/AssetPrep/sumo_neck_export_header.py. DO NOT EDIT.")
L.append(f"// 邊界環＝user 手標切縫（{data['seam_source']}，{data['count']} 頂點）切開後的頭側/身側環（UE component space, cm）。")
L.append("// 端色編碼：Rgb=linear/2（材質×2 解碼）、HairW=MARK_Hair×髮罩；rest 時兩環逐點重合。")
L.append("#pragma once")
L.append("#include \"CoreMinimal.h\"")
L.append("")
L.append("namespace NeckSeamData")
L.append("{")
L.append(f"constexpr int32 RingCount = {data['count']};")
L.append("")
L.append("struct FSeamVert")
L.append("{")
L.append("\tfloat Px, Py, Pz;          // rest 位置（component space cm）")
L.append("\tfloat Nx, Ny, Nz;          // rest 法線（縫區法線移植後＝術前視覺）")
L.append("\tfloat R, G, B;             // 端色（linear/2）")
L.append("\tfloat HairW;               // 髮支路強度")
L.append("\tint32 Bone[4];             // BoneNames 索引")
L.append("\tfloat W[4];                // 歸一化權重")
L.append("};")
L.append("")
L.append("inline const TCHAR* BoneNames[] = {")
for nm in bone_names:
    L.append(f"\tTEXT(\"{nm}\"),")
L.append("};")
L.append(f"constexpr int32 BoneNameCount = {len(bone_names)};")
L.append("")
L.append("// 頭側環（切開後隨 Head 骨剛體）")
L.append("constexpr FSeamVert HeadRing[RingCount] = {")
for c in head_conv:
    L.append(fmt_vert(c))
L.append("};")
L.append("")
L.append("// 身側環（Neck/Head 質量已轉移 Spine1；肩/Jiggle 保留）")
L.append("constexpr FSeamVert BodyRing[RingCount] = {")
for c in body_conv:
    L.append(fmt_vert(c))
L.append("};")
L.append("")
L.append(f"// 頭殼點雲（{len(shell)} 頂點，rest component space cm）——軌道淨空量測用")
L.append(f"constexpr int32 HeadShellCount = {len(shell)};")
L.append("constexpr float HeadShellPts[HeadShellCount][3] = {")
for p in shell:
    L.append(f"\t{{{p[0]:.3f}f,{p[1]:.3f}f,{p[2]:.3f}f}},")
L.append("};")
L.append("}")
L.append("")

with open(OUT_H, "w", encoding="utf-8", newline="\n") as f:
    f.write("\n".join(L))
print(f"HEADER_DONE bones={bone_names} shell={len(shell)}")
