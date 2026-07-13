# 深溝陰影合成（venv python）：cavity 摺線 ∪ AO 深穴 → body_ao_shadow.png
# 教訓：低模的淺摺線 AO 測不到（乳下遮蔽中位僅 0.026）——摺線靠曲率（cavity），
#       AO 只負責深穴（腋下/胯下）。兩者各自過曲線後取 max。
# 旋鈕：AO 曲線 T0/T1/γ；cavity 曲線 C0/C1（乳下摺線峰值實測 ~0.094）；溝底色 TARGET
# 鐵律：禁止 UV 空間 blur（跨島滲色）
import cv2, numpy as np
from pathlib import Path
SA = Path(__file__).resolve().parent.parent.parent / "SourceAssets"
T0, T1, GAMMA = 0.03, 0.22, 0.85
C0, C1 = 0.025, 0.09
TARGET = np.array([0.10, 0.11, 0.18], np.float32)   # BGR 深暖黑
STRENGTH = 0.5   # 深度總開關：1.0=v27 全深、0.5=v28 半深
ao = cv2.imread(str(SA / "body_ao.png"), cv2.IMREAD_GRAYSCALE).astype(np.float32) / 255.0
cav = np.load(SA / "body_cavity.npy")
occ = 1.0 - ao
cr_ao = np.clip((occ - T0) / (T1 - T0), 0, 1) ** GAMMA
cr_cav = np.clip((cav - C0) / (C1 - C0), 0, 1)
crease = np.maximum(cr_ao, cr_cav) * STRENGTH
# 排除遮罩（user MARK_Dirty 標記＋胸口拓樸瑕疵區，見 SourceAssets/shadow_exclude.png）
_ex = cv2.imread(str(SA / "shadow_exclude.png"), cv2.IMREAD_GRAYSCALE)
if _ex is not None:
    crease *= (1.0 - _ex.astype(np.float32) / 255.0)
shadow = 1.0 - crease[..., None] * (1.0 - TARGET[None, None, :])
cv2.imwrite(str(SA / "body_ao_shadow.png"), (shadow * 255).astype(np.uint8))
print(f"SHADOW dark%={(shadow.mean(axis=2)<0.85).mean()*100:.1f}")
