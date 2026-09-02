#pragma once

#include "CoreMinimal.h"

// 選單/大廳字串表（2026-08-06 user 定案照抄 Meccha 13 語）。
// 輕量自管表（非 UE LocRes 管線）：鍵×13 語常數表、零資產、robo 可測——
// 與全專案「全程式、無編輯器資產」philosophy 同款。
// 語言順序恆定（存檔索引依賴）：EN JA ZHT ZHS KO ES FR IT DE PTBR RU TR AR。
// 字體矩陣（複合字體 SubTypeface）已按 culture 分流——切語言時要同步
// SetCurrentCulture（繁簡 Han 分流靠它）。

enum class ENiLocKey : uint8
{
	YourName, ClickToType, HostARoom, RoomVisibility, InviteOnly, PublicRoom,
	JoinARoom, SettingsBtn, Quit,
	AskHostCode, JoinWithCode, PublicRooms, NoPublicRooms, Refresh, Back,
	WindowMode, Resolution, MouseSensitivity, MasterVolume, Language,
	Fullscreen, Borderless, Windowed, ApplyDisplay, Licenses,
	StatusCreating, StatusLooking, StatusJoining,
	ErrEnterCode, ErrNoOnline, ErrCreateFailed, ErrSearchFailed, ErrNoRoomsLan,
	ErrRoomGone, ErrRoomFull, ErrJoinFailed, ErrResolve, ErrNoRoomWithCode, ErrSignIn,
	LobbyCodeHint, LobbyStart, LobbyWaiting, LobbyWaitingHost,
	// 個人檔案頁（2026-08-06 SPEC #52 v4.0e：辨識＝名字＋臉 icon）
	Profile, UploadSelfie, CashLabel, FaceProcessing, FaceUpdated, FaceFailed,
	NotSignedIn, BrowHint,
	// 臉庫（2026-08-07：上傳過的臉全保存、點選即換）
	ReuploadSelfie, SavedFaces,
	// 臉制閘門＋隱私如實聲明（2026-08-10 user 定案：一定要上傳照片才能開始）
	FaceGateHint, PrivacyHint,
	// 選單邏輯修（2026-08-10 user 20 條驗收單）：門檻寫在門口、後果可預期、
	// 狀態有出口、破壞性動作二段確認
	CancelBtn,            // 建房/搜房/加入進行中的取消鈕
	FaceGateDoor,         // 主卡：無臉時預告「要先上傳自拍」（按了才彈走＝因果斷裂修）
	FaceProcessingDoor,   // 主卡：自拍處理中預告
	SignInBrowserNote,    // 未登入：預告開房/加入會開瀏覽器登入
	InviteOnlyDesc,       // 邀請制的後果說明
	PublicDesc,           // 公開的後果說明
	ConfirmQuit,          // 離開二段確認
	TypeCodeHint,         // 加入頁：房號用鍵盤打
	NameFallbackNote,     // 名字欄：目前是隨機名
	NameSavedNote,        // 名字已儲存回饋
	InstantNote,          // 設定：此組立即生效
	BorderlessResNote,    // 設定：無邊框固定桌面解析度（解析度旋鈕停用原因）
	UnappliedDiscardWarn, // 設定：未套用就返回的二段警告
	RenderScale,          // 設定：畫質（渲染比例；2026-08-11 取代解析度選單的效能旋鈕）
	CreateYourRikishi,    // 首啟創角頁標題（2026-08-12 首啟導流：無臉=開機直進創角）
	MaxPlayersLabel,      // 建房頁「房間人數」段標（2026-08-14 房間人數制）
	RoomNameLabel,        // 建房頁公開房房名段標（2026-08-14 徵人啟事）
	RoomNameHint,         // 房名輸入框 hint（說明用途：讓別人知道你在找怎樣的玩家）
	OpenPublicShortcut,   // 加入頁空狀態捷徑「自己開一間」（死路變轉化）
	AllLanguages,         // 加入頁語言過濾「全部語言」選項
	// 效能設定（2026-08-25）：引擎預設 FrameRateLimit=0＋VSync off＝顯卡被拉到
	// 滿速去畫每秒 500 張（實測空道場 1280×720 Frame 1.95ms／GPU 67%／93W）。
	// 「沒有上限」不是設計選擇，是沒人踩煞車——補上煞車，並讓玩家自己決定。
	FrameLimit,           // 設定：幀率上限
	VSyncLabel,           // 設定：垂直同步
	Unlimited,            // 幀率上限的「無上限」值
	OptionOn,             // 通用二態：開
	OptionOff,            // 通用二態：關

	// --- 作畫相位（2026-09-02）：局內 HUD 的第一批進表字串 ---
	// 此前作畫/巡禮/指認/醉夢全是寫死英文（全 HUD 只有 4 條走本地化、且全在大廳），
	// 玩家選了語言只有選單與大廳會變、開局就跳回英文。本批＝作畫相位全清；
	// 其餘相位待排。防再犯的閘門＝Tools/UiCheck/hud_loc_lint.py。
	PhaseDrawing,         // 上方橫幅：相位名
	SubjectAsleep,        // 上方橫幅副標「{0} 睡著了」
	NeedleStencil,        // 筆名：打稿麥克筆
	NeedleLiner,          // 筆名：割線針
	NeedleShader,         // 筆名：打霧針
	TrayRelease,          // 墨杯盤底部提示：放開沾杯／盤外取消
	TraySwitchNeedle,     // 墨杯盤抬頭：Q 換筆
	HudOutOfReach,        // 搆不到（琥珀色；持續 >1s 才出現）
	HudFlipAsk,           // 翻身表決：徵求同意 {0}/{1}
	HudFlipWait,          // 翻身表決：已投票、等其他人 {0}/{1}
	HudShakeBought,       // 搖夢成功回執
	HudShakeRefused,      // 搖夢被拒（現金/冷卻）
	HudStandingDrawing,   // 作畫相位、站著時的提示（F 翻身／G 搖夢）
	// 常駐操作提示的**動詞**（2026-09-02 四修，照 Meccha 實物）：鍵位由鍵帽圖形
	// 表達，文字只講「做什麼」——他們的 UI 是 `鍵帽＋圖示＋2~5 字動詞`，不是句子。
	// 一次性教學卡退役：我先前把「形式」的問題（句子）誤診成「時機」的問題（常駐）。
	ActFlip,
	ActInk,
	ActCups,
	ActWash,
	ActNeedle,
	ActShake,
	ActStand,
	// 作畫相位其餘常駐句（09-02 二補：由 Tools/UiCheck/hud_loc_lint.py 抓出來的，
	// 我自己走查時漏了——這正是閘門存在的理由）
	HudLeanIn,            // 站在受害者旁：RMB 湊近入鎖
	HudFeignSleep,        // 受害者裝睡中
	HudEyesOpen,          // 受害者無聲甦醒中
	COUNT
};

namespace NiLoc
{
	constexpr int32 NumLangs = 13;

	// 目前語言下的字串（Ctx＝任何能摸到 GameInstance 的 UObject；null＝英文）
	NICEINK_API FString T(const UObject* Ctx, ENiLocKey Key);

	// 帶一個 {0} 參數的字串
	NICEINK_API FString TFmt(const UObject* Ctx, ENiLocKey Key, const FString& Arg0);

	// 帶 {0} 與 {1} 兩個參數（翻身表決的 n/N）
	NICEINK_API FString TFmt(const UObject* Ctx, ENiLocKey Key, const FString& Arg0, const FString& Arg1);

	// 語言原生名（設定列顯示用）／culture 代碼（字體分流＋SetCurrentCulture）
	NICEINK_API FString LangNativeName(int32 LangIndex);
	NICEINK_API const TCHAR* LangCultureCode(int32 LangIndex);

	// 從 OS 預設文化推初始語言索引
	NICEINK_API int32 DetectDefaultLang();

	// 從任意文化字串比對語言索引（-culture= 覆寫路徑用）
	NICEINK_API int32 MatchLangFromCulture(const FString& CultureName);
}
