# UiCheck — HUD 版面與輸入增益的離線契約

**不開引擎、一輪一秒。** 這裡的腳本把 C++ 端的版面公式與增益鏈用 python 1:1 重現，
守的是「用眼睛看不出來、但玩家會踩到」的那一類缺陷。跑法：

    python -X utf8 Tools/UiCheck/tray_layout_check.py
    python -X utf8 Tools/UiCheck/tray_cursor_check.py

- `tray_layout_check.py`（97 檢查）＝墨杯盤／狀態 cluster 的版面：六種解析度下卡片
  不出界、格縫無死區、輕點 RMB 必沾回同一杯、列標落在 gutter 內、以及
  **透明度棋盤的對比工作區間**（下限 15 階＝低於此讀成摻白的粉彩；上限 70 階＝
  高於此格子壓過顏色）。對應 `FNiInkTrayLayout::Compute` 與 `ANiceInkHUD::DrawInkCup`。
- `tray_cursor_check.py`（15 檢查）＝托盤游標增益：**FOV 無關**、桌面指標對齊、
  跟著 UI 縮放（任何解析度橫越杯陣的手移動量相同）、玩家 `MouseSensitivityScale`
  生效。對應 `ANiceInkCharacter::TrayCursorPixelsPerCount`。

## 鐵則（血價都在 SHIP_PLAN 追記107~110）

- **契約自己也要被查**：一版把文字高度當成＝字級（實際是行框，要 ×1.4），
  而且那個常數忘了乘 UiScale ⇒ 只有 1080p 那一列是誠實的。
- **參數有工作區間時，閘門要兩邊都夾**：只寫下限的閘門會放行「矯枉過正」——
  而矯枉過正正是修好上一個 bug 最常見的副作用。
- **負向測試要打中你以為的那條路**：改預設值時先確認呼叫端沒有顯式傳值蓋掉它
  （本目錄兩支都曾因此假通過）。改完一定要看它**為了正確的理由**失敗。

## 全站 UI 改版的儀器（2026-09-04）

- `ui_token_audit.py` — 掃 C++ 原始碼對賬兩個載體的樣式常數（半徑階梯／面板 alpha／
  按鈕樣式語義／明朝體分布／間距網格）。**掃碼不看截圖**：截圖只證明那一幀長這樣，
  掃碼才知道「有幾種值」。
- `hud_mock.py` — 作畫 HUD 的全畫面離線 mock ＋尺度閘門（所有尺寸是 U=4 的整數倍、
  字級等比階梯、四邊共用邊距）。改 `NiUi` 任何一個常數就重跑。
- `strip_mock.py` / `strip_measure.py` — 操作列 glyph 風格對照與版面一致性對賬
  （單一對齊軸／固定行距／同層等高）。
- `shot_measure.py` — **量真機截圖**（`robo_ui_shots.py` 產出）的版面不變量。
  帶負向測試：把期望邊距／列距改掉，閘門必須開火。

**這一輪五個閘門自己的 bug（都先報過假通過）**：正則在內層逗號就停、`rindex` 當定位、
list 參照當群組、亮度門檻把鍵帽濾掉、位置檢查只驗「有對比」。
**閘門必須被負向測試**，而且要問「什麼樣的壞會讓它照樣亮綠燈」。
