# EOS 接線指南

> **2026-08-05 狀態：全鏈已通**——憑證已填、EOS 已啟用、單機實測過（Epic 登入→
> lobby 建房→RTC 語音房自動入房，log `NiVoice: loggedIn=1 channels=1` 為證）。
> 本文件保留為 Portal 設定的重建手冊（換產品/重建憑證時照走）。
>
> **Portal 必要配方（缺一即登入被拒，08-05 三連錯實測）**：
> ① Client Policy（Peer2Peer 型）＝Lobbies/Sessions/Voice/PlayerDataStorage；
> ② Epic 帳戶服務協議接受＋Application 關聯 Client；
> ③ **EAS Application 許可＝Basic Profile＋Friends＋Online Presence 三項全開並按保存**
> ——引擎登入請求恆為 basic_profile+friends_list+presence+offline_access，
> `AuthScopeFlags` 縮不掉（實測無效），只能 Portal 端對齊。
> 品牌設置＝上架前才需要（自有網域 DNS 驗證＋隱私政策頁＋128px logo；
> 草稿模式的「未經驗證」插頁按「繼續使用應用程式」即可測試）。

## 現在就能做的（不需要 EOS）

同一台機器或同一 LAN 上開兩份遊戲（或 PIE 多客戶端）：

- 主機在主控台輸入 `NiHost` → 建 LAN 房、以 listen server 重載桑拿房
- 其他人輸入 `NiJoin` → 搜到第一個房直接進
- 進房後 `NiStart` 開局（或等自動開局）

## 開網路房（EOS）的步驟

1. 到 [Epic Dev Portal](https://dev.epicgames.com/portal) 建立組織與產品（免費）。
2. 產品內：
   - **Product Settings → SDK Credentials**：拿 `ProductId`、`SandboxId`、`DeploymentId`
   - **Clients**：新增一個 Client（Policy 選 `GameClient` 或含 Connect/Sessions/Lobbies 權限），拿 `ClientId`、`ClientSecret`
3. 打開 `Config/DefaultEngine.ini`，找到「連線層」段落：
   - 把 `DefaultPlatformService=NULL` 註解掉
   - 解開 EOS 各段註解（含 `[EOSVoiceChat]`），把 `<EOS_*>` 佔位符換成你的憑證
   - `EncryptionKey` 換成你自己隨機的 64 字元 hex
4. ~~`NiHost`／`NiJoin` 改 bLan~~（2026-08-05 已改自動：所有建房/搜房入口
   跟隨 `IsOnlineServiceConfigured()`——NULL=LAN、EOS=網路房，零手動切換）。
5. 打包（或 standalone）測試——EOS P2P 不支援 PIE 內多客戶端。

## 語音（程式端 2026-08-05 已全接好；剩憑證）

Session 已設 `bUseLobbiesIfAvailable`（EOS lobby）。SPEC 語音規格（**無方位、
全房廣播、內容可說謊**）＝ EOS Lobby RTC 的預設行為，不需要任何空間化程式。

已接好的部分（2026-08-05）：
1. ~~uproject 加 EOSVoiceChat plugin~~ **完成**。
2. ~~建房參數 `bUseLobbiesVoiceChatIfAvailable = !bLan`~~ **完成**
   （[NiceInkSessionSubsystem.cpp](../Source/NiceInk/Private/NiceInkSessionSubsystem.cpp)
   `HostSession`）——lobby 建立時自動開 RTC room，成員進房即入語音。
3. ~~ini `[EOSVoiceChat] bEnabled=true`~~ **完成**（在連線層註解塊裡，解註解即生效）。
4. ~~自動入房驗收儀器~~ **完成**：`NiceInkGameInstance` 的 **NiVoice 探針**——
   EOS 配置下進網路地圖後每 3 秒 log 一次語音登入/頻道狀態，入到頻道即停；
   30 秒沒入＝log 大聲警告（第一嫌疑＝Client Policy 缺 Voice 權限）。
   語音出聲走 EOS SDK 自己的音訊裝置、不經 UE 音訊——沉睡者全域靜音天然
   不會誤殺語音（SPEC：聽覺開放）。

剩下要做的：
5. Dev Portal 的 Client Policy 要含 **Voice** 權限（RTC）——建 Client 時一次勾好。
6. 音量／靜音 UI：之後掛在 ESC 選單（`IVoiceChatUser::SetPlayersListenVolume`）。
7. **驗收清單**：兩台真機（EOS P2P 不支援 PIE）互說話＋看 log 的 `NiVoice:` 行；
   確認「沉睡者聽得到全房」；確認無 3D 衰減。

## 已知邊界

- 存檔鍵目前＝玩家名＋席位；接上 EOS 後應改用 ProductUserId（見
  `NiceInkGameMode::SaveSlotFor` 的註記）。
- 6 人真機驗證（延遲下的筆劃流暢度、投射物公平性）只能真人測。
