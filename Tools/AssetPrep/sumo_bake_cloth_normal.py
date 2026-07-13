# 布緣/手繪高度 → 切線空間法線貼圖（UE 用；Blender 端 Bump 鏈的離線等價）
# H = sharp_mask×3mm（布厚台階）+ height_paint×4mm（手繪皺摺）
# n = normalize(-gx, +gy, 1)（OpenGL +Y；影像 y 向下故 gy 正號——沿 hair v17 慣例）
# UE 匯入時 TC_Normalmap + flip_green_channel=True（Blender OpenGL → UE DirectX）
import cv2
import numpy as np

SA = r"C:\games\Unreal Engine\nice_ink\SourceAssets"
SIZE = 4096
PXMM = 0.617 * SIZE / 2048   # px per mm
EDGE_MM = 3.0
PAINT_MM = 4.0

sharp = cv2.imread(SA + r"\fundoshi_mask_sharp.png", cv2.IMREAD_GRAYSCALE).astype(np.float32) / 255.0
hp = cv2.imread(SA + r"\body_height.png", cv2.IMREAD_GRAYSCALE)
h01 = cv2.resize(hp, (SIZE, SIZE), interpolation=cv2.INTER_LINEAR).astype(np.float32) / 255.0

H = sharp * EDGE_MM + h01 * PAINT_MM          # mm
gx = cv2.Sobel(H, cv2.CV_32F, 1, 0, ksize=3) / 8.0 * PXMM   # dH(mm)/dx(mm)
gy = cv2.Sobel(H, cv2.CV_32F, 0, 1, ksize=3) / 8.0 * PXMM

n = np.dstack([-gx, gy, np.ones_like(gx)])
n /= np.linalg.norm(n, axis=2, keepdims=True)
img = ((n * 0.5 + 0.5) * 255).astype(np.uint8)[:, :, ::-1]   # RGB->BGR for imwrite
cv2.imwrite(SA + r"\body_cloth_normal.png", img)
slope = np.sqrt(gx**2 + gy**2)
print(f"CLOTH_NORMAL baked 4096: slope p99 {np.percentile(slope, 99):.3f} max {slope.max():.3f}")
