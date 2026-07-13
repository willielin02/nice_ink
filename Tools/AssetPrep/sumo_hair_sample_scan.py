# 掃描髮貼圖轉印——影像端：cv2.remap 雙線性取樣＋島外 gutter 外擴
import numpy as np
import cv2
from pathlib import Path

S = Path(r"C:\Users\willi\AppData\Local\Temp\claude\c--games-Unreal-Engine-nice-ink\00d214a0-ef56-4e54-9786-cb494e647707\scratchpad")
SIZE = 2048
uv_map = np.load(S / "hair_scanuv.npy")     # (H,W,2) scan UV
cover = np.load(S / "hair_cover.npy")       # (H,W) bool
scan = cv2.imread(str(S / "scan_Image_0.png"))  # BGR uint8（sRGB 原值直傳）

map_x = (uv_map[..., 0] * scan.shape[1] - 0.5).astype(np.float32)
map_y = ((1.0 - uv_map[..., 1]) * scan.shape[0] - 0.5).astype(np.float32)
tint = cv2.remap(scan, map_x, map_y, cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)
tint[~cover] = 0

# 島外 gutter：把髮色向空白外擴（羽化帶/雙線性/mip 取樣用）
k3 = np.ones((3, 3), np.uint8)
filled = tint.copy()
for it in range(12):
    grown = cv2.dilate(filled, k3)
    empty = (filled.sum(axis=2) == 0)
    filled[empty] = grown[empty]

cv2.imwrite(str(S / "hair_tint.png"), filled)
import shutil
shutil.copy(S / "hair_tint.png", r"C:\games\Unreal Engine\nice_ink\SourceAssets\hair_tint.png")
print(f"SAMPLED texels={int(cover.sum())} nonzero={(filled.sum(axis=2)>0).sum()}")
