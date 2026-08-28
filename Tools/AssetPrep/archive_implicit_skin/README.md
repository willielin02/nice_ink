# Implicit Skinning 肥肉引擎（2026-08-27 封存）

從論文（Vaillant et al. SIGGRAPH 2013）自寫的離線實作。**從未進入遊戲**（Source/Config/Content 零變更）。
封存原因＝user 裁決：開場動畫走蹲踞＋剪接路線，不需要接觸形變引擎。

留下的資產：
- pose_domain_metrics.py（共用量尺，仍在上層被掃描器使用）
- g0 基線手法（吸收靜止重疊）——褌 cross 指標同病可用
- 實測結論：膝免費、髖是褌瓶頸、同塊自摺佔破圖 42% 且此法原理上治不了

若要重跑：腳本 import 上層目錄的 pose_domain_*，需把檔案移回上層或補 sys.path。
