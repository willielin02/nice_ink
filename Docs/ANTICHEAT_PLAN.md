# 防作弊＋TikTok 重置制 — 設計規格與工作帳本

> 2026-08-31 定案（user 於本日對話逐輪裁決；前文逐字稿＝專案根「新增 文字文件 (4).txt」）。
> 本文件是這條戰役的**帳本與規格單一出處**。SPEC.md 的回寫項見文末「SPEC 待回寫」。
> 狀態標記：`[定案]`=user 裁決；`[推薦]`=我方推薦、user 未否決即採用；`[待user]`=需要 user 提供或裁決。

## 0. 需求（user 原話語意）

1. **絕對防作弊**：受害者不得知作者身分、身體刺青無法自行消除（含現金不可竄改）。
2. **TikTok 影片自動化重置**：玩家上傳「以人體彩繪還原自己角色刺青」的影片，自動獲得一次
   刺青重置——全自動、零人力審核。
3. **體驗零摩擦** `[定案]`：玩家從 Steam 直接進遊戲、零額外登入、Steam 名字代入預設顯示名。

## 1. 誠實天花板（先寫死，避免日後被當成缺陷回報）

- **語音串通**（Discord 上直接講「是我畫的」）：任何架構治不了。
- **host 即時偷看**（host 機器上有全部世界狀態）：治不了「即時」，治得了「持久結果」（見 §4）。
- **絕對永久性的物理上限＝遊戲售價**（新 Steam 帳號重買＝全新皮膚）。推論：重置券黑市價
  恆 ≤ 遊戲售價，經濟衝擊有界，「一帳號一次」即足夠。
- 遊戲核心樂趣本來就是「從畫風推理作者」；要消滅的是**免費且確定的答案**（ID 直送），
  不是推理本身。

## 2. 架構分層 `[定案：兩邊都用，各管一層]`

| 層 | 用誰 | 理由 |
|---|---|---|
| 商店/上架 | Steam | 上架平台 |
| 身分登入 | **Steam 票證 → EOS Connect 換 PUID**（B5 終態） | 零彈窗零 Epic 帳號；`GetEffectiveDisplayName()` 的「平台帳號名」換供應商即代入 Steam 名 |
| 傳輸/NAT 中繼/lobby/語音 | EOS | 08-05 已 SHIPPED 實測；重寫＝純風險支出 |
| persona 存檔儲存 | EOS PlayerDataStorage | **簽章制後儲存層不再承重**（權威在簽章，桶子只是桶子） |
| 重置券 | Steam Inventory Service | EOS 無等價物；Valve 伺服器端記帳 |
| 防作弊權威＋TikTok | **自建薄後端（公證服務）** | 兩家都不提供這層 |

薄後端＝serverless/VPS 無狀態 HTTPS＋KV 儲存，持有四樣客戶端永遠不能碰的東西：
簽章私鑰、Steam publisher key、TikTok client secret、AI API key。
**不跑遊戲、不管房間、無 tick。** 後端對玩家隱形：身分驗證搭 Steam 票證便車
（客戶端附票證、後端走 Steam Web API `AuthenticateUserTicket` 驗票），玩家零感知。

連帶：B6（EAS 品牌驗證）服務的是 Epic 帳號登入彈窗，B5 終態玩家不見 Epic UI ⇒ B6 大概率
整項消失（動工 B5 時確認）；但**自有網域＋隱私政策不能省**——TikTok app 審核要它
（等於從 B6 搬家到 TikTok 前置）。

## 3. Phase 0 — 純遊戲內三條（零後端，先做）

### P0-1 作者身分 slot 化
- 病灶：`MulticastPaintBegin(AuthorId, …)`（NiceInkCharacter.cpp:6576）NetMulticast 含受害者
  ＝作者身分即時直送，改裝受害者每回合全對。
- 修法：伺服器每回合為每位作畫者配一個**洗牌過的不透明 WorkSlotId**；所有落墨多播只帶
  SlotId（它只承擔「分組鍵」職責）；slot→author 對照留在伺服器，**指認判定之後**才對
  該作品下發真作者供揭曉演出。作畫者本人的客戶端天然知道自己的作品（自己畫的），無妨。
- 邊界：伺服器判定用的作者恆取 server-side（`GetInkAuthorId()` 於 server 呼叫），栽贓面不變。

### P0-2 沉睡期複製過濾
- 病灶：黑屏是受害者 HUD 自己畫的（NiceInkHUD.cpp:656 一帶），世界照常複製
  ——其他人的位置/aim 全在受害者機器上，「誰站在我身邊」＝作者推理直送。
- 修法：**網路關聯性過濾**——受害者 `bAsleep && !bEyesOpen` 期間，其他角色對其連線
  不 relevant（`IsNetRelevantFor` 或等價機制）；睜眼（偷看）即恢復。
- 感官規格對齊（不可破壞）：沉睡＝遊戲音效全域靜音本來就是既有規格（NiceInkAudio），
  **BGM/RTC 語音不經此層照播**——所以過濾不傷「只能聽」的設計。搖晃攻擊「受害者顯名」
  是明文設計，該條資訊照送。
- 已知風險記帳：關聯性掉線＝客戶端 actor 銷毀，睜眼偷看瞬間 channel 重開有 1~2 個
  net update 的重生延遲＋視覺 pop（jiggle/姿勢暖機）——實測驗收，必要時 SettleJiggleNow
  慣例補位。落墨多播打在受害者自己的 actor 上（owner 恆 relevant）照樣抵達——
  slot 化之後這條不再洩作者，可接受（誠實客戶端黑屏本來就不畫）。
- **揭曉時序（2026-08-31 實作定案，比原設想更乾淨）**：不存在任何「全量對照下發」——
  巡禮/指認全程只有 slot；唯一的身分揭示＝指認判定後的 `GS->RevealedAuthorId`（單幅、
  單值、複製屬性）。判定本身用 server 畫布的真名（`PickedWork.AuthorId`），零改動。
- **偷看恢復速度（已實作）**：睜眼/醒來（`ServerOpenEyesNow`/`ServerSetAsleep(false)`）
  當幀對全角色 `ForceNetUpdate()`——凍結恢復不等 relevancy 自然回流（引擎預設
  RelevantTimeout 5s）。

### P0-3 描圖判定伺服器校驗
- 病灶：`ServerTraceComplete`（NiceInkCharacter.cpp:3450）只驗發話者/相位——改裝受害者
  入睡當幀直呼＝秒醒，連帶搖晃攻擊經濟作廢。
- 修法：伺服器在下發 `ClientStartTrace` 時記起始時間戳；收到 Complete 時要求
  `經過時間 ≥ 保守下限`（下限＝路徑總長 ÷ 游標速度上限 × 安全係數，全部 server 可算；
  搖晃造成的重來只會讓真實時間更長，下限不必計入＝恆保守）。違規＝忽略＋log 記名。
- Phase 2 加碼：甦醒耗時寫進 quorum digest（異常快的甦醒＝全員可見、有簽名的案底）。
- robo 相容：robo_trace_test 若走捷徑完成，需確認走的是 GameMode Debug hook（可豁免）
  或真模擬（自然過下限）；實作時對賬，**不得為了 robo 在正式路徑上開洞**。

## 4. Phase 1–2 — 薄後端（公證服務）

### 4.1 簽章制（persona 不可自行竄改）`[定案]`
- 現行「私人保險箱不可代寫」＝玩家對自己資產有唯一寫入權，與「不可自行消除」**邏輯矛盾**
  ——修法不是加驗證，是**反轉信任方向**。
- blob 照存 EOS PDS（儲存層零遷移），旁邊多後端私鑰簽章；進房全員只認驗得過簽的 blob；
  host 的 `ApplyUploadedPersona` 從「語意照單全收」改為驗簽（NiceInkGameMode.cpp:625 記帳債清掉）。
- **防回滾（不做＝全部白搭）**：簽章內容含**單調遞增序號**，後端 KV 存每 SteamID 最新序號；
  進房驗「簽章有效 ∧ 序號＝後端最新」。舊 blob（輸掉終局前的備份）從此是廢紙。
- LAN/離線房：未簽章 persona＝不帶入正式房、不落雲端（明文定義，防靜默漂移）。
- **過渡期staging（重要）**：`/sign-persona` 不能無條件替客戶端簽（否則客戶端自己送
  「清空版」照樣拿到真簽章＝自行消除復活）。Phase 1 過渡＝簽發必附**host 見證**的結算
  單（擋住一般玩家單方自改；host 作弊暫不擋）；Phase 2 升級成 quorum 見證（host 也擋）。
  兩階段的請求格式同構（見證欄位從 1 份簽名變 N 份），升級零遷移。

**Phase 1 接線實作定案（2026-08-31 施工中裁定，全部已落碼）**：
- **信封制 NIP1**：`'NIP1'|seq|siglen|sig|payloadlen|payload`——信封只存在於 PDS 檔與
  網路線上；`CachedAssets` 語意恆＝裸 payload（兩處直引它的消費者零改動）。簽的是
  payload 的 sha256＝零自我指涉、避開 USaveGame delta-vs-CDO 序列化漂移。非 NIP1 magic
  ＝舊裸 blob 遷移路（seq=0）。
- **上行掛 Begin**：`ServerPersonaBegin(TotalBytes, SigSeq, SigHex)`——Begin 本來就是
  拒收判定點，壞簽章格式不必收完 4MB 列車。
- **驗證點**＝`ApplyUploadedPersona`：Ed25519 同步驗（引擎 OpenSSL 1.1.1t、~µs）＋
  `latest-seq` 非同步防回滾。**所有失敗路徑一律「封口＋乾淨新身」，絕不 fallback
  主機本機槽**（那是回滾後門）；逾時同理。
- **`bPersonaVerified` 閘（把原判「Phase 2 才能關」的洗白洞提前關掉）**：未經驗證
  進房的玩家**本場一律不落雲**＝金庫原封。新玩家走 `ServerPersonaNone` 宣稱空身、
  host 對帳本驗「seq=0 才是真新人」；帳本 seq>0 卻宣稱空身＝「假裝雲端故障洗白」
  ＝unverified。結果：洗白攻擊打完整場也改不動自己的雲端。
- **結算單**：`HandleAccusation` 判定當幀 `/attest`（host 單見證 roster=1）、token 存
  GameMode；1.2s 上墨儀式窗天然吸收 HTTP 往返。**中途消費點（雷射/搖晃）與 Logout
  無單＝只寫主機本機槽、上雲延後到下一結算點——記帳待 user 裁**：斷線會丟「上一結算
  點之後」的雷射/搖晃現金變動（併入 digest 是 Phase 2 選項）。
- **fail-open 政策**（後端無回應時）：簽章已驗過的 blob 照套用（只損失該次回滾防護）、
  空身宣稱視為新人——可用性優先，全部留 `NiAnticheat:` log。後端正常時無此路。
- **host 本人**與遠端同構：同樣憑單 `/sign-persona` 換簽章再落雲（`PersistCharacter`）。
- **儀器**＝`NiNotaryTest`（GameInstance Exec）：信封往返／SHA256 標準向量／簽發／
  C++ 驗簽／篡改必敗／無單拒簽／latest-seq／attest／憑單簽發 seq 遞增，對本地後端
  真 HTTP 跑（用法寫在標頭註解）。
- **⚠ B5 身分軸未爆彈**：簽章訊息的 puid＝EOS Connect PUID；後端 dev 模式收自報＝
  自洽。B5 換 Steam 票證後 `AuthenticateUserTicket` 回的是 SteamID64——要嘛後端維護
  SteamID↔PUID 對照、要嘛訊息換軸（舊簽章全失效＝要遷移）。**接 B5 前必先裁。**

### 4.2 escrow（作者保密連 host 都防）
- 作畫者鎖定受害者開畫時，直接 HTTPS 告訴後端「房 R 回合 T 的 slot X＝我」，**不經 host**；
  指認提交後後端才釋出對照供揭曉。全房（含改裝 host、host 自己是受害者）整局只見不透明 slot。
- **時序鐵則**：對照釋出嚴格在指認提交之後；quorum digest 承諾在釋出之前。

### 4.3 quorum 見證（host 對持久結果的作弊關閉）
- 回合結束各客戶端把結果摘要（誰的哪幅成碳黑/永久、現金增減、指認結果、**甦醒耗時**、
  **各自作品雜湊的自我見證**）簽名直送後端；N-of-M 一致才簽發新 blob。
- 擋掉：host 偽造結果、栽贓（自我見證對不上即破局）、改任何人現金。host 能做的最多＝
  拒絕結算（這局白玩，自損非偽造）。
- 後端結算時**驗語意界限**（單局現金增減 ≤ 規則上限、單位時間局數上限）⇒ 小號共謀
  只能以「正常遊玩最大速率」刷，不能瞬間造富。
- 受害者抽選＝**commit-reveal**（各端交亂數承諾→揭示→hash 合成選人）⇒ host 不能指定受害者。
- 殘留記帳：多數共謀（3+ 小號自嗨房）＝用真規則刷，限速不可消滅；後端私鑰外洩＝信任歸零
  （營運安全：金鑰不進 repo 不進客戶端、定期輪替）。

### 4.4 端點一覽
| 端點 | 職責 |
|---|---|
| `POST /sign-persona` | 結算通過/重置銷券後簽發新 blob（序號 +1） |
| `POST /escrow` | slot→author 登記與指認後釋出 |
| `POST /attest` | 收 round digest、N-of-M 對賬、語意驗界 |
| `GET /latest-seq/<id>` | 進房驗序號 |
| `POST /redeem-reset` | 銷券＋簽發清空 blob（§5.5） |

## 5. Phase 3 — TikTok 自動重置管線

### 5.1 綁定（一次性）
遊戲內「綁定 TikTok」→ TikTok Login Kit OAuth（後端持 secret）→ 記 `SteamID ↔ open_id`。
**一個 TikTok 帳號只綁一個 SteamID**（open_id 唯一鍵）——擋一片餵多帳。

### 5.2 提交審核（玩家發片後主動按「送審」）
API 現實：TikTok Display API **不提供影片檔下載**（只有 id/描述/時間/封面圖）⇒ 雙軌對賬：
1. 玩家送審時把**原始影片檔一併上傳後端**；
2. 後端拉 `video.list` 驗：公開影片 ∧ 描述含活動 hashtag ∧ `create_time` 在活動期 ∧
   影片 id 未用過（一次性）；
3. **封面比對**：`cover_image_url` vs 上傳影片影格相似度 ⇒ 證明「TikTok 那支＝交審這支」，
   擋調包（TikTok 掛牆壁三秒、審核交另一支）。
零爬蟲、零 ToS 風險。

### 5.3 AI 內容審核（全自動）
抽影格（每 2s 一張、上限 ~20）＋**該玩家簽章 blob 裡的刺青貼圖（ground truth 在後端手上）**
→ 多模態模型封閉題：「影格中是否有真人皮膚上以人體彩繪重現此參考圖案」→ 信心分數三段：
高＝自動過／中＝人工抽查佇列（量小）／低＝拒＋遊戲內回原因可重交。
成本每次數美分。事後防線：隨機抽驗，抽到假＝Steam 收回券＋標記帳號。

### 5.4 發券
後端 publisher key 呼 `AddPromoItem`；itemdef `tradable:false / marketable:false / game_only:true`。
**一人一次由 Valve 記帳**（同 itemdef 同 SteamID 只授一次）＋後端 KV 雙保險。

### 5.5 消耗＝清刺青（接回簽章管線）
`/redeem-reset`：伺服器端 `ConsumeItem` 銷券 → 成功才簽發**刺青清空、序號 +1** 的新 blob
（現金不動）→ 客戶端存回雲端。原子性：銷券成功但簽發未達＝後端記 pending、重連補發，不吞券。

### 5.6 平台解耦
「TikTok 存在性驗證」做成 adapter；審核/發券/消耗不動即可加 YouTube Shorts/Reels
（TikTok 監管風險對沖）。

### 5.7 活動內容紅線 `[已向 user 提出，文案屬 user 裁決域]`
活動文案只要求「以人體彩繪在皮膚上重現你角色的刺青，畫在哪都算」——**不出現任何服裝名詞**、
不要求軀幹位置。理由：官方「要求」與參加者「自選」隔著整個法務世界；未成年人穿常服畫前臂
即可完整參加；類別錨定在彩繪藝術（bodypainting/SFX 類）而非泳裝內容。AI 審核與位置無關，
不受此影響。

## 6. 全鏈防作弊對照表（驗收清單）

| 攻擊 | 擋它的層 |
|---|---|
| 改 blob（現金/刺青） | 簽章驗不過，全房拒收 |
| 出示舊的合法簽章 blob | 序號防回滾 |
| 記憶體改自己局內現金 | 局內權威在 host、結算權威在 quorum |
| 改裝 host 改任何人持久資產 | quorum digest 對不上＝不落盤 |
| host 栽贓筆劃 | 自我作品雜湊見證 |
| host 指定受害者 | commit-reveal |
| 受害者偷知作者 | slot 化＋複製過濾＋escrow |
| 描圖秒醒 | 時間下限＋quorum 記甦醒耗時 |
| 一片多領/調包/掛牆壁 | 影片 id 一次性＋封面對賬＋AI ground truth 比對 |
| 偽造/複製/轉賣重置券 | Steam Inventory 伺服器端＋itemdef 旗標 |
| 跳過券直接清刺青 | 清空 blob 只有後端簽得出來 |
| 小號農場刷錢 | 語意驗界限速（不可消滅，有界） |

## 7. 外部依賴（`[待user]`，可與 Phase 0~1 平行辦）

| 項目 | 內容 | 備註 |
|---|---|---|
| hosting＋網域＋TLS | 後端上線 | 網域與隱私政策 TikTok 審核必須 |
| TikTok developer app | Login Kit＋video.list 送審 | **數週級等待＝全計畫最長外部依賴，越早送越好** |
| Steamworks | 合作夥伴帳號（$100）＋app＋publisher key＋itemdef | Phase 3 硬依賴 |
| AI API key | 審核帳單 | |
| 金鑰保管 | 私鑰/publisher key/secret 存放 | 絕不進 repo |

## 8. 施工順序與狀態

| 階段 | 內容 | 狀態 |
|---|---|---|
| Phase 0 | P0-1 slot 化／P0-2 複製過濾／P0-3 描圖校驗 | **BUILT-自驗 2026-08-31**：編譯綠；robo_trace 21/1（唯一 FAIL＝t3 observer 需第三個 PIE 世界＝2-client 降級後結構性不可跑、非本批）；robo_directdraw 59/6（六失敗名＝ghosts/shader row-metered/palette/far×3＝既有失敗集逐字子集、零新增）；robo_feign 23/0；**新儀器 robo_sleepfreeze_probe 6/6**（server 作畫者走 466.3cm、沉睡受害者世界複本位移 0.0cm＝凍結實錘；睜眼後 d=0.0＝收斂即時）；喚醒鉤子 7 支腳本換 `DebugRoboWake`。**沉睡凍結的視覺（偷看第一眼、醒來 snap）待 user viewport** |
| Phase 1 | 後端骨架＋/sign-persona＋/escrow＋遊戲端接線（本地可跑） | 未動工 |
| Phase 2 | /attest quorum＋commit-reveal＋描圖耗時入 digest | 未動工 |
| Phase 3 | TikTok adapter＋AI 審核＋Steam Inventory | 未動工（外部依賴 §7） |

## 9. SPEC 待回寫（累積，等 user 授權對齊時一次寫入）

1. 防作弊資訊模型：作者身分 slot 化＋沉睡期複製過濾＋描圖伺服器校驗（新章）。
2. 信任架構：簽章制 persona（「私人保險箱」語意修訂：保險箱仍私人，內容物改為後端簽發的本票）。
3. TikTok 重置券制（新章：一帳號一次、Steam Inventory、消耗＝後端簽發清空 blob）。
4. 平台分層定案（Steam 身分/庫存＋EOS 傳輸/儲存＋自建公證後端）；B6 縮編待 B5 確認。
