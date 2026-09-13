#pragma once

#include "CoreMinimal.h"
#include "DreamMaze.h"
#include "DreamTrace.h"
#include "GameFramework/GameMode.h"
#include "NiceInkTypes.h"
#include "NiceInkGameMode.generated.h"

class ANiceInkCharacter;
class ANiceInkGameState;
class ANiceInkPlayerState;

// SPEC v3.0 回合狀態機（server 權威）。
//
// Lobby → BottleSpin → ┌ Seating → Drawing → Tour → Accusation → Resolution ┐
//                      └───────────（猜對換人／猜錯罰酒再畫）←──────────────┘
//                                                    └ 第三杯 → Finale → PostGame
//
// 作畫階段沒有計時器：受害者按 WASD 現身即收束（定案 #17）。
UCLASS()
class NICEINK_API ANiceInkGameMode : public AGameMode
{
	GENERATED_BODY()

public:
	ANiceInkGameMode();

	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId,
		FString& ErrorMessage) override;
	virtual void Logout(AController* Exiting) override;
	// 2026-09-13 user 定案：「位置完全等於排序，有人離開其他人遞補，他要重新排隊」⇒ 關掉引擎的重連保留
	//（AGameMode 預設把離場者的 PlayerState 留五分鐘、同帳號回來換回去）。大廳外不准中途加入，大廳裡沒有任何
	// 值得保留的狀態（現金在雲端、罰酒每局歸零、臉會重傳）⇒ 回來的人一律是新玩家、拿新席位、排最後。
	virtual void AddInactivePlayer(APlayerState* PlayerState, APlayerController* PC) override {}
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId,
		const FString& Options, const FString& Portal = TEXT("")) override;

	// 房主踢人（2026-08-13：ESC 選單房主段呼叫——listen server 上 HUD 就在
	// 伺服器行程、免 RPC）；斷線＋記入本場拒再入名單
	void HostKickPlayer(class ANiceInkPlayerState* PS);

	// session 狀態鏡射遊戲真相（2026-08-14）：真開局=StartSession（session 層
	// 擋中途加入）、回大廳/場間=EndSession（重新可搜可加入）。引擎 AGameMode
	// 的 match 開場即 StartSession＝大廳期間 LAN beacon 無聲拒答的根因——
	// GameSession 已換 no-op 版（NiceInkGameSession），狀態只從這裡走
	void SetSessionInProgress(bool bInProgress);
	virtual APawn* SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot) override;

	// --- 場地配置（座標系沿用桑拿房實測；L_Dojo 道場已以地板探針驗證全席位落在開放地板，
	//     道場 actor 基準點為此西移 250cm——見 CLAUDE.md 陷阱年鑑「地板探針」條） ---

	// 六個席位（2D；z 由地板探測決定）。
	// **2026-08-16 全部搬到房間正中央**（user 定案「我希望整個遊戲都在房間的正中央進行」）：
	// 關卡實測（部件包圍盒，不是射線猜的）——floor_Shape 中心 (403,27)、跨
	// x −309…1115 / y −352…406＝14.2×7.6m 長廳；拉門在**東西兩端**（x≈−306 / x≈1110）；
	// 三塊榻榻米中心 (431,41)、跨 x 130…732＝道場的天然舞台（也正是 CLAUDE.md 記的
	// 部件基準點 430.7,40.9）。舊席位擠在 x −300…155＝長廳**最西端、緊貼西側門口**，
	// 席位 3/4 幾乎貼牆（射線量到淨空只有 72cm / 6cm）。
	// 現制＝繞舞台中心 (430,40) 半徑 230 的六等分環（儀式圍圈 R=160 在其內側，
	// Gather 那一拍還是有「往中間靠攏」的動作）。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Stage")
	TArray<FVector2D> SeatSpots = {
		FVector2D(660.0f, 40.0f),    // 東（0°）
		FVector2D(545.0f, 239.0f),   // 東北（60°）
		FVector2D(315.0f, 239.0f),   // 西北（120°）
		FVector2D(200.0f, 40.0f),    // 西（180°）
		FVector2D(315.0f, -159.0f),  // 西南（240°）
		FVector2D(545.0f, -159.0f),  // 東南（300°）
	};

	// 受害者仰躺位置＝舞台中心（榻榻米中心；亦為儀式圍圈圈心與崩塌終點）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Stage")
	FVector2D VictimLieSpot = FVector2D(430.0f, 40.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Stage")
	float VictimLieYaw = 0.0f;

	// --- 流程參數 ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Flow")
	bool bAutoStart = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Flow", meta = (ClampMin = "2", ClampMax = "6"))
	int32 MinPlayersToStart = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Flow")
	float AutoStartDelay = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Flow")
	float BottleSpinSeconds = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Flow")
	float SeatingSeconds = 3.0f;

	// --- 入睡儀式（2026-08-16；全部旋鈕、bCeremonyEnabled=false 一鍵退回舊制傳送）---
	// 開場（BottleSpin 相位）＝Gather＋Spin；每回合入睡（Seating 相位）＝
	// Approach＋PickUp＋Drink＋Collapse。**零硬切**：全程無 teleport，
	// 崩塌終點 ≡ GetVictimLieTransform()。

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Ceremony")
	bool bCeremonyEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Ceremony")
	float CeremonyGatherSeconds = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Ceremony")
	float CeremonySpinSeconds = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Ceremony")
	float CeremonyApproachSeconds = 1.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Ceremony")
	float CeremonyPickupSeconds = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Ceremony")
	float CeremonyDrinkSeconds = 1.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Ceremony")
	float CeremonyCollapseSeconds = 1.1f;

	// 圍圈半徑（探針定值 160：躺位為圈心時 6 個 60° 角位全部淨空、間距 167cm）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Ceremony")
	float CeremonyCircleRadiusCm = 160.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Ceremony")
	int32 CeremonySpinTurnsMin = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Ceremony")
	int32 CeremonySpinTurnsMax = 5;

	// --- 開場動畫（2026-08-27；user 定案敘事：榻榻米喝酒看電視→極道刺青→關電視→
	// 房主提議→開局）。**只在本房第一場播**（續攤直接 Gather）；PIE 一律跳過＝robo
	// 契約零干擾。入座＝相位切換當幀 teleport——與導演鏡頭的硬切同幀，玩家看不見
	// 自己的 pawn 被搬（同一招＝「載入淡入蓋掉起始態」的既有前例）。---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Intro")
	bool bOpeningIntroEnabled = true;

	// robo 自驗專用：預設 false＝PIE 永遠跳過開場（契約零擾動）；
	// 截圖腳本用 python 翻成 true 才能在 PIE 拍開場五拍
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Intro")
	bool bOpeningIntroForceInPIE = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Intro")
	float IntroSitSeconds = 4.0f;      // 全景：六人盤腿看電視

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Intro")
	// 電視特寫：**節目五分鏡を頭から全部流す長さ**（2026-08-28 user 指定
	// 「請把時間延長讓玩家可以在電視前完整看完你準備的影片片段」）。2.6→10.4。
	// 分鏡の割りは NiceInkTvFilm::ResolveShot の比率表が持つので、**長さはこの一つの
	// 旋鈕だけ**で決まる（比率は自動で等比伸縮、動画の物理速度は別経路で保たれる）。
	// 開場全長への影響＝BeginOpeningIntro の Total が自動で吸収（相位計時器も伸びる）。
	// 08-29 user 要求 +50%（10.4→15.6）。分鏡の割りは比率表なので自動で等比に伸び、
	// 動画の物理速度（太鼓の拍・提燈の揺れ・煙）は Seconds 側が持つので変わらない
	// ——**尺はこの一つの旋鈕だけ**、というのはこのために作った構造。
	// 分鏡の秒は**この値の従属変数**（比率表 ResolveShot::Lo/Hi で決まる）。下の内訳は
	// 15.6s のときの値＝**この行の数字を変えたら内訳も書き直すこと**（さもなくば腐る）。
	float IntroNoticeSeconds = 15.6f;  // ＠15.6s：夜祭2.18/太鼓3.12/神輿3.28/登場3.74/振り返り3.28

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Intro")
	float IntroTvOffSeconds = 1.5f;    // 白線收掉

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Intro")
	float IntroProposeSeconds = 3.0f;  // 房主舉機、馬達聲

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Intro")
	float IntroRiseSeconds = 1.2f;     // 剪回全景：全員已站立

	// 巡禮每幅約 20 秒（SPEC；playtest 調整）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Flow")
	float TourSecondsPerWork = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Flow")
	float ResolutionSeconds = 6.0f;

	// 終局羞辱時間長度
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Flow")
	float FinaleSeconds = 30.0f;

	// 罰酒三杯制（SPEC 定案 #12；與人數無關）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Flow")
	int32 PenaltyCupsToFinale = 3;

	// --- 醉夢圓形迷宮（SPEC v3.3 甦醒小遊戲；待定 #2 全部旋鈕在 FDreamMazeParams） ---

	// 每杯一組難度檔（索引＝受害者當前罰酒杯數——酒越深夢越深）。
	// Config 可由 DefaultGame.ini 覆寫；ini 陣列語意＝先 !MazeParamsPerCup=ClearArray
	// 再逐條 +MazeParamsPerCup=(...)，否則會疊在 ctor 預設之後。
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Maze")
	TArray<FDreamMazeParams> MazeParamsPerCup;

	// SPEC 定案 #31 常數：兇手轉盤 5 秒（非難度旋鈕，改它＝改 SPEC）
	static constexpr float TrapDialSeconds = 5.0f;

	// --- 醉夢描圖（SPEC v4.0 定案 #49/#50；迷宮退役） ---

	// 每杯一組難度檔（索引＝受害者當前罰酒杯數——酒越深夢越深）。
	// Config 覆寫語意同 MazeParamsPerCup（先 !Clear 再逐條 +）。
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Trace")
	TArray<FDreamTraceParams> TraceParamsPerCup;

	// 搖晃攻擊（定案 #50；費用佔位 500＝金額錨定連動 SPEC 待定 #6/#18）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Economy")
	int32 ShakeAttackCost = 500;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Trace")
	float ShakeAttackSeconds = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Trace")
	float ShakeAttackAmpCm = 1.2f;

	// 每攻擊者冷卻（防機關槍連砸；金錢是主限流、冷卻是節拍保底）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Trace")
	float ShakeAttackCooldownSec = 4.0f;

	// 搖晃攻擊路由（Character 的 Server RPC 轉進來）：驗相位/身分/現金/冷卻
	// →扣款→受害者端套用（攻擊者顯名）；回執只給攻擊者
	void HandleShakeAttack(class ANiceInkCharacter* Attacker);

	// --- 迷宮事件路由（Character 的 Server RPC 轉進來） ---

	// 受害者踩中陷阱：驗證後只通知兇手開轉盤＋掛失效保險（逾時/掉線＝0 度）
	void HandleMazeTrapHit(class ANiceInkCharacter* Victim, int32 KillerPlayerId);
	void HandleTrapDialSubmit(class ANiceInkCharacter* Killer, float AngleDeg);

	// --- 玩家角色的入口 ---

	void RequestStartMatch();
	void HandleEmergeRequest(ANiceInkCharacter* Requester, bool bForce = false);
	void HandleAccusation(ANiceInkCharacter* Accuser, int32 WorkId, int32 AccusedPlayerId);

	// 場間大廳：雷射自己最舊的碳黑一級（自費；三級清除；永久無效）
	void HandleLaserRequest(ANiceInkCharacter* Requester);

	// 翻身提案（2026-07-15 user 定案）：作畫者之一提出、「其餘的人」＝
	// 全體非受害者玩家全數同意後翻身；一次一案、逾時作廢；非 ragdoll。
	void HandleFlipPropose(ANiceInkCharacter* Proposer);
	void HandleFlipAgree(ANiceInkCharacter* Agreer);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Economy")
	int32 LaserCostPerPass = 2000;

	// 作畫許可（server 權威）：作畫階段畫受害者；終局羞辱時間畫輸家。
	bool CanPaintOn(const ANiceInkCharacter* Painter, const ANiceInkCharacter* Target) const;

	// --- Robo-test 鉤子（python 驅動的自動驗證用）---
	// python 執行期間 FEditorScriptExecutionGuard 會把 RPC 全部壓成本地執行，
	// multicast 出不了網；這些鉤子把動作排進 timer，回呼在遊戲 tick 裡跑（guard 外），
	// RPC 走正常網路路徑。也是後續里程碑的 headless 測試工具。

	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugRoboStroke(FVector2D FromUV, FVector2D ToUV, int32 ColorIndex);

	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugRoboEmerge();

	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugRoboAccuse(bool bCorrect);

	// robo：從任何相位直接進結局（拍 Finale／PostGame 的畫面用）。兩人房猜錯需要第三人，
	// 所以三杯路徑在 PIE 走不到——這裡只拍畫面不驗規則（規則由 fullloop 守）。
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugRoboFinale();

	// robo：把受害者身上第一幅會消失的刺青當眾轉碳黑（拍場間雷射標籤用；兩人房走不到猜錯）
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugRoboCarbonize();

	// 大廳（2026-09-07 二版）：在場的人依席位排成鏡頭對面的弧（置中、24° 一席），面向鏡頭
	void PlaceLobbyArc();
	FTimerHandle LobbyArcTimer;

	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugRoboSpray(float AimYawWorld, uint8 OriginType);

	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugRoboKick(float AimYawWorld);

	// 迷宮：代兇手送轉盤度數（timer-deferred；robo 驗證受害者端旋轉用）
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugRoboMazeDial(float AngleDeg);

	// 翻身：跳過表決直接執行（timer-deferred；robo 驗證背面姿勢/翻面後作畫用）
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugRoboFlip();

	// 迷宮生成統計（純計算、無 RPC——python 可直呼）；報表字串回傳＋進 log
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	FString DebugMazeStats(int32 NumSeeds, int32 Cup);

	// 描圖生成統計（v4.0 調參儀器；純計算可直呼）
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	FString DebugTraceStats(int32 NumSeeds, int32 Cup);

	// 搖晃攻擊（timer-deferred；以第一位非受害者玩家為攻擊者走真實 Handle 路徑）
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugRoboShake();

	// 開場儀式場地探針（2026-08-16；施工前量測——陷阱年鑑「換位置先跑地板探針」）：
	// 對圈心周圍多組半徑逐 15° 打地板射線＋膠囊淨空測試，並取樣各席位→角位的直線路徑。
	// python 讀不到 5.7 的 HitResult 反射 ⇒ 探測必須在 C++ 側做、回機讀字串。
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	FString DebugCeremonyProbe(float CenterX, float CenterY) const;

	// 房間中心探針（2026-08-16；user 定案「整個遊戲都在房間的正中央進行」）：
	// 掃網格找可站立地板 → 回報可走域包圍盒與形心 → 對候選中心求「最大內接淨空圓」
	// （所有環角都可站立的最大半徑），排序後回報前幾名。**「正中央」用量的不用猜。**
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	FString DebugRoomCenterProbe() const;

	// robo 測試：指定開場受害者的席位（-1＝隨機，正式行為）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Debug")
	int32 DebugForcedVictimSeat = -1;

	// robo 測試：指定描圖種子（0＝隨機，正式行為）——圖案池由種子選圖，
	// 固定種子＝固定圖案＝契約零 flake
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Debug")
	int32 DebugForcedTraceSeed = 0;

	// --- 防作弊 P0（2026-08-31；帳本=Docs/ANTICHEAT_PLAN.md §3）---

	// P0-1 作者槽位：每回合洗牌的不透明分組鍵——落墨多播線上只走 slot，
	// slot→真名對照只活在 server（受害者的改裝客戶端讀不到作者）。
	// 惰性指派（首針當下）＋回合換代即重洗；同時把 slot 寫進角色的 DrawSlotId
	//（COND_OwnerOnly 複製給本人＝本地預測分組鍵同源）。
	int32 GetOrAssignDrawSlot(class ANiceInkCharacter* Artist);
	// 未知 id（負值證據鍵、跨場 RestoreWork 真名）原值回還
	int32 ResolveDrawSlot(int32 WireId) const;

	// P0-3 甦醒時間下限：發夢當下算好「最早合法甦醒時刻」（線長/針速上限×係數）；
	// 早於它的 ServerTraceComplete/ServerMazeExited＝改裝客戶端，拒收。
	bool CanVictimWakeNow() const;

	// 下限係數（誠實最短≈線長/v_max；0.5＝失敗重來/搖晃/網路抖動全不誤殺的保守裕量）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Trace")
	float TraceWakeFloorFactor = 0.5f;

	// robo 專用喚醒（timer-deferred 同其他 hooks）：server 端直設睜眼＝合法繞過下限
	//（下限管的是客戶端宣稱；server 自己決定不受限）。取代舊的 DreamTrace
	// DebugForceComplete 捷徑——那條現在會被 P0-3 的閘擋掉。
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugRoboWake();

	// P0-1 對照表（server 秘密；round 換代即重洗）
	TMap<int32, int32> DrawSlotByAuthor;
	TMap<int32, int32> AuthorByDrawSlot;
	int32 DrawSlotRound = INDEX_NONE;

	// P0-3：最早合法甦醒時刻（world seconds；0＝無夢進行中）
	float TraceWakeEarliestTime = 0.0f;

	// 雲端隨身上行終點（Character::ServerPersonaEnd 驗收後轉入；B3）：
	// bytes＝UNiceInkSaveGame 序列化；驗證→套用（錢包＋逐幅 MulticastRestoreWork）。
	// P1（Docs/ANTICHEAT_PLAN.md §4.1）：配置了公證後端時先驗 Ed25519 簽章（同步）＋
	// 對後端 latest-seq 防回滾（非同步；後端無回應＝fail-open 套用並記 log）。
	// 驗不過＝乾淨新身（**不得** fallback 主機本機槽——那是回滾後門）。
	void ApplyUploadedPersona(ANiceInkCharacter* Character, const TArray<uint8>& Bytes,
		int32 SigSeq = 0, const FString& SigHex = FString());

	// 舊本體（反序列化＋鉗位＋套用＋熱備）：未配置後端時行為與 P1 之前逐位相同
	void ApplyPersonaBytesNow(ANiceInkCharacter* Character, const TArray<uint8>& Bytes);

	// P2 結算見證（quorum）：指認判定當幀 host 以自己的 digest 上報 /attest；
	// 其餘見證人由各 client 的 MaybeNotarizeTick 自行上報（不經 host）——
	// ⅔ 一致才結算。host 的 token 可能落在別人的回應裡＝輪詢 /settlement 補拿。
	// 1.2s 上墨儀式窗天然吸收 HTTP 往返。
	void RequestRoundAttest();
	void PollSettlementToken(int32 TriesLeft);

	// P0-3/P2：本回合入睡起點（world seconds）——甦醒耗時＝睜眼當幀寫進
	// GameState.LastWakeSeconds 進 quorum digest（秒醒案底全員可見）
	float TraceSleepStartTime = 0.0f;

	// P1：玩家宣稱「我沒有雲端資產」（新玩家/空身）——對後端帳本驗：seq=0＝真新人
	//（verified 乾淨開局）；seq>0＝有簽發史卻宣稱沒有＝洗白攻擊（unverified＝本場不落雲）
	void ResolveNoPersonaClaim(ANiceInkCharacter* Character);
	FString NotaryRoomId;          // attest 房鍵（首次結算生成，一房一鍵）
	FString RoundSettlementToken;  // 最近一次結算的 token（空＝本輪拿不到＝不上雲）

	// --- 自訂臉房內分發（2026-08-10；server 端集散地）---
	// 上行驗收終點：存原始 blob（晚到者補發用）＋入主機登記簿＋廣播給已報到 viewer
	void OnFaceBlobReceived(ANiceInkCharacter* From, const TArray<uint8>& Blob);
	// 現身閘 ack（08-14 三修）：觀看者回報「席位 Seat 的臉已入我的登記簿」——
	// 該席全部在册觀看者 ack 齊＝FaceGateShowNow（單一權威制）
	void OnViewerGotFace(ANiceInkCharacter* Viewer, int32 Seat);
	// 遠端 viewer 報到（Character::ServerFaceHello）：補發所有已知臉（跳過本人席位）
	void RegisterFaceViewer(ANiceInkCharacter* Viewer);

private:
	// 自訂臉分發內部（節奏發送＝防 reliable 緩衝溢位；一次一 job 順序出貨）
	TMap<int32, TSharedPtr<TArray<uint8>>> FaceBlobs;      // seat → 原始 blob
	TArray<TWeakObjectPtr<ANiceInkCharacter>> FaceViewers; // 已報到的遠端收件角色
	struct FNiFaceSendJob
	{
		TWeakObjectPtr<ANiceInkCharacter> Target;
		int32 Seat = -1;
		TSharedPtr<TArray<uint8>> Blob;
		int32 NextOff = 0;
		bool bBegun = false;
	};
	TArray<FNiFaceSendJob> FaceSendQueue;
	// 現身閘記帳：seat → 尚未 ack 的觀看者；seat → 該席角色（show 用）
	TMap<int32, TArray<TWeakObjectPtr<ANiceInkCharacter>>> FacePendingAcks;
	TMap<int32, TWeakObjectPtr<ANiceInkCharacter>> FaceSeatChar;
	FTimerHandle FaceSendTimer;
	void EnqueueFaceJob(ANiceInkCharacter* Target, int32 Seat, const TSharedPtr<TArray<uint8>>& Blob);
	void TickFaceSend();
	FTimerHandle PhaseTimerHandle;
	FTimerHandle AutoStartTimerHandle;

	int32 NextSeatIndex = 0;
	TArray<int32> TourWorkIds;
	int32 TourCursor = 0;

	// 被踢玩家的 net id（本場拒再入；LAN NULL id 無效時只斷線不記名——記帳）
	TSet<FString> KickedNetIds;

	// session 是否已標記 InProgress（SetSessionInProgress 冪等用）
	bool bSessionInProgress = false;

	// Resolution 演出後要接的分支
	int32 PendingNextVictimId = INDEX_NONE;
	bool bPendingFinale = false;

	// 搖晃攻擊冷卻表（server-only；每回合 EnterSeating 清空）
	TMap<int32, float> LastShakeTimeByPlayer;

	// 兇手轉盤 pending（一次一件；受害者死亡序列中不會再踩）
	int32 PendingDialKillerId = INDEX_NONE;
	TWeakObjectPtr<ANiceInkCharacter> PendingDialVictim;
	FTimerHandle DialFailsafeHandle;
	void ResolveTrapDial(float AngleDeg);

	// 翻身表決（server-only；顯示位在 GameState）
	TSet<int32> FlipAgreedIds;
	FTimerHandle FlipTimeoutHandle;
	void MaybeExecuteFlip();
	void ClearFlipProposal();

	ANiceInkGameState* NIState() const;
	ANiceInkCharacter* GetVictimCharacter() const;
	ANiceInkPlayerState* FindNIPlayerState(int32 PlayerId) const;

	FTransform GetSeatTransform(int32 SeatIndex) const;
	FTransform GetVictimLieTransform() const;
	float ProbeFloorZ(const FVector& At) const;

	// avatar 派發：優先玩家意向（DesiredAvatarIndex），被佔用則從席位起輪派空位
	int32 PickAvatarFor(const class ANiceInkPlayerState* PS) const;

	void MaybeScheduleAutoStart();
	void EnterBottleSpin();
	void OnBottleSpinDone();
	void EnterSeating(int32 VictimPlayerId);
	void OnSeatingDone();

	// --- 入睡儀式（server 權威；客戶端只是這些複製參數的純函式）---
	// 舞台幾何 → GameState（單一來源；PostLogin 與儀式起手各叫一次）
	void EnsureStageGeometry();
	void SetCeremonyStep(ENiCeremonyStep Step, float Duration);
	void OnCeremonyStepDone();
	// 真正的入睡（傳送/DreamTrace/回合索引）——儀式版由 Collapse 結束呼叫、
	// 舊制版由 EnterSeating 立即呼叫
	void BeginVictimSleep(bool bAlreadyLying);
	// 圍圈角位旋轉偏移：掃描 72 個候選（5°），取「全角位淨空且總角位移最小」者
	float ComputeCeremonySlotOffset() const;
	bool IsCeremonySpotClear(const FVector& At) const;
	class ANiceInkBottle* GetOrSpawnBottle();

	// 開場動畫：電視道具（開場才生、之後留在場上＝道場家具）＋入座/起身
	class ANiceInkTvSet* GetOrSpawnTvSet();
	void BeginOpeningIntro();
	void SeatPlayersForIntro();
	void ReleasePlayersFromIntro();
	bool bOpeningIntroPlayed = false;   // 本房第一場已播（GameMode 生命週期＝本房）
	TWeakObjectPtr<class ANiceInkTvSet> IntroTvSet;

	// 抽中但尚未揭曉的受害者（轉瓶結束才寫進 GameState——HUD 不提前劇透）
	int32 PendingVictimId = INDEX_NONE;
	TWeakObjectPtr<class ANiceInkBottle> CeremonyBottle;
	void EnterTour();
	void AdvanceTour();
	void EnterAccusation();
	void OnResolutionDone();
	void EnterFinale();
	void OnFinaleDone();

	// 回合結算清場：全員洗麥克筆與證據標記、解除致盲（SPEC：指認結算時一同洗掉）
	void RoundCleanupAllCharacters();

	// 斷線兜底：受害者中離／人數不足＝本回合作廢（洗掉、清 pending），
	// 人夠→重新轉瓶續攤；不夠→回大廳等人。
	void AbortRound(bool bEnoughPlayers);

	// 相位切換時全員強制起身（貼臉鎖定不跨相位）
	void ForceExitAllLeans();

	// 跨場持久化（錢包＋刺青）。存檔鍵＝EOS ProductUserId（B3，跨房/改名/席位
	// 恆定；Steam 票證登入同為 Connect 層 PUID＝同鍵路徑）；無 PUID（LAN/PIE/robo）
	// fallback＝舊制玩家名（去 PIE 尾碼）＋席位。
	FString SaveSlotFor(const class ANiceInkPlayerState* PS) const;
	void PersistCharacter(ANiceInkCharacter* Character);
	void RestoreCharacter(ANiceInkCharacter* Character);

	// 跨場資產還原編排（PostLogin 起跳、1s 輪詢至多 TicksLeft 次）：
	// 無 PUID＝立刻走本機槽（LAN/PIE 原路）；EOS 主機本人＝等雲端拉取；
	// EOS 遠端＝等上行列車；逾時＝本機 PUID 槽 fallback
	void TryRestoreTick(TWeakObjectPtr<ANiceInkCharacter> WeakChar, int32 TicksLeft);

	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void PersistAllCharacters();

	void SetPhaseTimer(float Seconds, void (ANiceInkGameMode::*Handler)());
};
