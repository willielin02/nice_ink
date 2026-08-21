# 色度場後處理：掃描 albedo 取樣 → 明度攤平 → 純色度乘法場
# 原理：蠟感病根=albedo 無血色分布；掃描的顏色變化是真的，但亮度裡住著烘死的陰影
#（違反 v52 皮膚零陰影鐵律）→ 只留 rgb/L 的色度、亮度整張丟掉，均值歸一成 delta 場。
# 輸出=body_chroma.png（16-bit，值=factor/2，材質端 ×2 解碼）
import os
import numpy as np
import cv2

S = os.environ.get("CHROMA_S", "")
OUT_SRC = r"C:\games\Unreal Engine\nice_ink\SourceAssets\body_chroma.png"
SIGMA = 6.0        # 512 空間 ≈ 27mm 平滑——殺高頻，留血色斑駁
SIGMA_TAN = 40.0   # ≈ 18cm——掃描模特曬痕（整肢色差）住在這個頻段以上，除掉
# 珊瑚橘教訓：橘=藍分量被壓（藍↓=黃橘↑）；藍下限收緊=珊瑚化封頂
CLIP_LO_B, CLIP_HI_B = 0.85, 1.20
CLIP_LO, CLIP_HI = 0.82, 1.20

uv = np.load(S + r"\body_scanuv.npy")          # (512,512,2) 掃描 UV
cover = np.load(S + r"\body_cover.npy")        # (512,512) bool
scan = cv2.imread(S + r"\scan_Image_0.png", cv2.IMREAD_UNCHANGED)  # BGR(A) uint8
scan = scan[:, :, :3].astype(np.float32) / 255.0
H = scan.shape[0]
N = uv.shape[0]

# --- 取樣：UV -> 掃描像素座標（v 翻轉）---
mapx = (uv[:, :, 0] * H).astype(np.float32)
mapy = ((1.0 - uv[:, :, 1]) * H).astype(np.float32)
raw = cv2.remap(scan, mapx, mapy, cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)

# --- 島安全填充：空紋素=最近覆蓋紋素的值（填滿整張再 blur，跨島滲色鎖死在遠溝）---
inv = (~cover).astype(np.uint8)
_, labels = cv2.distanceTransformWithLabels(inv, cv2.DIST_L2, 5,
                                            labelType=cv2.DIST_LABEL_PIXEL)
idx_cover = np.zeros(labels.max() + 1, np.int64)
ys, xs = np.nonzero(cover)
idx_cover[labels[cover]] = ys * N + xs
near = idx_cover[labels]
filled = raw.reshape(-1, 3)[near.ravel()].reshape(N, N, 3)
filled[cover] = raw[cover]

# --- sRGB -> linear，低通，色度抽取 ---
def srgb_to_lin(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
lin = srgb_to_lin(filled)
lin = cv2.GaussianBlur(lin, (0, 0), SIGMA)
# BGR 順序的 luminance
L = 0.0722 * lin[:, :, 0] + 0.7152 * lin[:, :, 1] + 0.2126 * lin[:, :, 2]
chroma = lin / np.maximum(L, 1e-5)[:, :, None]

# --- 帶通：除掉超低頻（掃描模特的曬痕=整肢色差=他的個人身分，不是血色）---
ultra = cv2.GaussianBlur(chroma, (0, 0), SIGMA_TAN)
chroma = chroma / np.maximum(ultra, 1e-5)

# --- 均值歸一（只在覆蓋區統計）→ 純 delta 場：平均=1，SkinTone 不被整體偏色 ---
mean = chroma[cover].mean(axis=0)
chroma = chroma / mean[None, None, :]
chroma[:, :, 0] = np.clip(chroma[:, :, 0], CLIP_LO_B, CLIP_HI_B)  # B 通道（BGR）
chroma[:, :, 1:] = np.clip(chroma[:, :, 1:], CLIP_LO, CLIP_HI)
# clip 後再歸一一次（單邊裁切會偏移均值）
mean2 = chroma[cover].mean(axis=0)
chroma = chroma / mean2[None, None, :]

cov = chroma[cover]
for name, ch in zip("BGR", range(3)):
    v = cov[:, ch]
    print("%s: mean=%.3f p5=%.3f p95=%.3f min=%.3f max=%.3f" %
          (name, v.mean(), np.percentile(v, 5), np.percentile(v, 95), v.min(), v.max()))

# --- 上採樣 1024、編碼 factor/2 -> 16-bit PNG ---
up = cv2.resize(chroma, (1024, 1024), interpolation=cv2.INTER_CUBIC)
enc = np.clip(up / 2.0, 0.0, 1.0)
png16 = (enc * 65535.0 + 0.5).astype(np.uint16)
assert cv2.imwrite(S + r"\body_chroma.png", png16)
assert cv2.imwrite(OUT_SRC, png16)
print("CHROMA_POST_DONE ->", OUT_SRC)
