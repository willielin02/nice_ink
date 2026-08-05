# MarkerPen_Audition — 稿筆（麥克筆）音源候選（全 CC0）

來源＝freesound.org、授權全部 **CC0**（公有領域、可商用免致謝）。
檔案＝官方公開 HQ 試聽檔（128kbps MP3）；CC0 下直接用試聽檔合法，但**選定後
建議用免費帳號下載原始 WAV**（頁面連結如下）再進加工鏈。

`norm/` ＝響度對齊 -16 LUFS 的試聽版（比音色不比音量）。

## 四強（量測初篩後）

| 檔 | 時長 | 訊號品質 | 內容 | 頁面 |
|---|---|---|---|---|
| 383872 | 53.6s | 峰值 -0.3dB（飽） | Permanent Marker Writing＝素材量最大 | freesound.org/people/deleted_user_7146007/sounds/383872/ |
| 751055 | 6.0s | 峰值 -0.8dB、RMS 最高 | marker pen writes on paper＝訊號最密 | freesound.org/people/SSkiba88/sounds/751055/ |
| 351145 | 4.5s | 峰值 -9.6dB | fast and slow strokes＝快慢筆觸都有（手機錄、無後製） | freesound.org/people/MBPiM/sounds/351145/ |
| 118621 | 76.9s | 峰值 -15.6dB（需增益） | writing with marker＝最長素材庫 | freesound.org/people/krb21/sounds/118621/ |

## 淘汰（量測出局）

- 119926（峰值 -35dB）、46632（峰值 -39.9dB）：錄得太小聲，拉增益底噪一起上來。
- 481076（峰值 -27.8dB）：同病較輕，仍出局。
- 335956：whiteboard squeak＝白板面不是紙/皮膚面，質感錯棚（留檔備查）。

## 選定與產出（2026-08-05 user 定案＝351145）

user 耳測選定 **351145_MBPiM**（「聲音最好」）。切段診斷：4 段筆觸
（A=0~1.25s／B、C≈0.35s 短快筆／D=2.34~4.53s 持續段、內部零斷點、RMS 穩定
-20~-22dB）。產出（皆 48k/16-bit/mono、-6dBFS 峰值）：

- `marker_loop.wav`（1.35s）：D 段穩定區 2.85~4.45s＋0.25s 等功率尾頭交叉淡接
  ＝無縫摩擦床 → /Game/Audio/marker_loop（looping=True）。
- `marker_dab.wav`（0.353s）：B 段筆觸＋邊緣淡化＝落筆觸感音
  → /Game/Audio/marker_dab（ENiSound::MarkerDab）。

運行時模型（NiceInkCharacter::UpdateMarkerSfx）：落筆=dab（InkCanvas 按針型
分流、全端重放）；摩擦 loop=本人專屬、音量=√(筆尖速度/RefSpeed)×MarkerSfxVolume
×MasterVolume、速度 EMA τ0.06s——**筆沒動就沒聲音（LMB 按住也一樣）、停頓自然
歸零、收筆即停**；音高 0.94~1.06 隨速度。旋鈕=MarkerSfxRefSpeedCmS(8)/
MarkerSfxVolume(0.7)。

沉睡靜音規則＝滅別人的、**自己夢中刺青機聲照有**（user 2026-08-05 定案；
刺青機 buzz=下一批、程序合成＋伸針調變）。
