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
	COUNT
};

namespace NiLoc
{
	constexpr int32 NumLangs = 13;

	// 目前語言下的字串（Ctx＝任何能摸到 GameInstance 的 UObject；null＝英文）
	NICEINK_API FString T(const UObject* Ctx, ENiLocKey Key);

	// 帶一個 {0} 參數的字串
	NICEINK_API FString TFmt(const UObject* Ctx, ENiLocKey Key, const FString& Arg0);

	// 語言原生名（設定列顯示用）／culture 代碼（字體分流＋SetCurrentCulture）
	NICEINK_API FString LangNativeName(int32 LangIndex);
	NICEINK_API const TCHAR* LangCultureCode(int32 LangIndex);

	// 從 OS 預設文化推初始語言索引
	NICEINK_API int32 DetectDefaultLang();

	// 從任意文化字串比對語言索引（-culture= 覆寫路徑用）
	NICEINK_API int32 MatchLangFromCulture(const FString& CultureName);
}
