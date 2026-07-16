# EOS 接線指南（你要做的部分）

程式碼與設定已接好；缺的只有你的 Epic Dev Portal 憑證。**沒填之前遊戲照常可玩**：
`NiHost` / `NiJoin` 走 NULL subsystem＝同一區網（LAN）直接連。

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
   - 解開 EOS 兩段註解，把 `<EOS_*>` 佔位符換成你的憑證
   - `EncryptionKey` 換成你自己隨機的 64 字元 hex
4. `NiHost`／`NiJoin` 的程式碼呼叫把 `bLan` 改為 false（[NiceInkCharacter.cpp](../Source/NiceInk/Private/NiceInkCharacter.cpp) 的 `NiHost`/`NiJoin`），或日後做主選單時暴露成選項。
5. 打包（或 standalone）測試——EOS P2P 不支援 PIE 內多客戶端。

## 語音（憑證填好後的接線步驟，B2 文件化 2026-07-17）

Session 已設 `bUseLobbiesIfAvailable`（EOS lobby）。SPEC 語音規格（**無方位、
全房廣播、內容可說謊**）＝ EOS Lobby RTC 的預設行為，不需要任何空間化程式。

1. `NiceInk.uproject` 加 plugin：`{"Name": "EOSVoiceChat", "Enabled": true}`
   （隨 OnlineSubsystemEOS 出貨，不用另裝）。
2. Dev Portal 的 Client Policy 要含 **Voice** 權限（RTC）。
3. `DefaultEngine.ini` EOS 段加：
   ```ini
   [EOSVoiceChat]
   bEnabled=true
   ```
   並在 `+Artifacts=(...)` 同段確認 EncryptionKey 已換自己的值。
4. 建房參數改一行（[NiceInkSessionSubsystem.cpp](../Source/NiceInk/Private/NiceInkSessionSubsystem.cpp)
   `HostSession`）：`Settings.bUseLobbiesVoiceChatIfAvailable = !bLan;` ——
   lobby 建立時自動開 RTC room，成員進房即入語音。
5. 入房後每個 client 用 `IVoiceChat::Get()` / `EOSVoiceChatUser` 確認
   `JoinChannel` 由 lobby 自動觸發（OnlineSubsystemEOS 的 lobby RTC 整合會代辦；
   若沒有，手動在 PostLogin 後 `JoinChannel(LobbyRTCRoomName, ...)`）。
6. 音量／靜音 UI：之後掛在 ESC 選單（`IVoiceChatUser::SetPlayersListenVolume`）。
7. **驗收清單**：兩台真機（EOS P2P 不支援 PIE）互說話；確認「沉睡者聽得到全房」
   （沉睡端不做任何 voice mute——遮蔽的是視覺不是聽覺）；確認無 3D 衰減。

## 已知邊界

- 存檔鍵目前＝玩家名＋席位；接上 EOS 後應改用 ProductUserId（見
  `NiceInkGameMode::SaveSlotFor` 的註記）。
- 6 人真機驗證（延遲下的筆劃流暢度、投射物公平性）只能真人測。
