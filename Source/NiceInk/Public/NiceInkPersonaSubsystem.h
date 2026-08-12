#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "HAL/PlatformProcess.h"
#include "HAL/ThreadSafeBool.h"
#include "Interfaces/OnlineUserCloudInterface.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "NiceInkPersonaSubsystem.generated.h"

// 雲端隨身層（EOS PlayerDataStorage，2026-08-06 B3）：
// 玩家的跨場資產（現金＋碳黑/永久刺青）與選單偏好跟著帳號走——進誰的房間都一樣。
//
// 身分鍵一律用 Connect 層 ProductUserId（EOS NetId 字串「EAS|PUID」的後半）。
// 之後 B5 切 Steam 票證登入（ConnectLoginNoEAS）時 EAS 半空、PUID 照在，
// 這條雲端路徑零改動可用；永不依賴 EpicAccountId。
// （記帳：Epic 帳號登入與 Steam 登入產生的 PUID 不同——切換登入方式＝開發期
// 資產不搬家，屬預期非 bug。）
//
// 未登入（LAN／PIE／robo）＝全部 no-op：構造保證不干擾既有測試與離線遊玩。
UCLASS()
class NICEINK_API UNiceInkPersonaSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// 資產檔拉取狀態（進房上行要等它 Done 才知道有沒有東西可上）
	enum class EPullState : uint8 { NotStarted, InFlight, Done };

	static UNiceInkPersonaSubsystem* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// SessionSubsystem 登入成功後呼叫：拉 settings＋assets 兩檔（冪等）
	void HandleLoginSuccess();

	// --- 自訂臉（SPEC #52 v4.0e：runtime 自拍上傳）---
	// 本機正本＝Saved/PlayerFace/（face_open/face_closed/eye_mask_ink/skin_color）；
	// 啟動時載入、上傳自拍跑完管線後更新。FaceRevision 遞增＝舞台/角色重綁訊號。

	enum class EFaceIntakeState : uint8 { Idle, Running, Done, Failed };

	bool HasCustomFace() const { return FaceOpenTex != nullptr && FaceClosedTex != nullptr; }
	UTexture2D* GetFaceOpen() const { return FaceOpenTex; }
	UTexture2D* GetFaceClosed() const { return FaceClosedTex; }
	UTexture2D* GetEyeMaskInk() const { return EyeMaskInkTex; }
	FLinearColor GetCustomSkinTone() const { return CustomSkinTone; }
	int32 GetFaceRevision() const { return FaceRevision; }

	// 啟動自拍→臉管線（背景行程：venv python intake_selfie.py）；Running 中重入＝忽略
	void BeginSelfieIntake(const FString& SelfiePath);
	EFaceIntakeState GetIntakeState() const { return IntakeState; }
	double GetIntakeElapsedS() const; // Running 中已耗秒數（狀態列顯示）

	// --- 臉庫（2026-08-07：上傳過的臉全保存＝Saved/PlayerFace/library/<id>/，
	// active.txt 指目前穿的那張；點選即換、換臉=FaceRevision 遞增）---
	TArray<FString> ListLibraryFaceIds() const;  // 新→舊
	const FString& GetActiveFaceId() const { return ActiveFaceId; }
	FString GetActiveFaceDir() const;            // library/<active>；無臉＝空字串（房內分發打包用）
	bool ActivateFace(const FString& Id);        // 匯入 library/<id>＋寫 active 指針
	// 臉庫縮圖＝頭像亭 3D 肖像（2026-08-12 取代 thumb.png 貼圖裁切）；
	// 亭未就緒＝暫回 nullptr 不記快取（呼叫端稍後重試）、工件缺席＝記 nullptr
	UTexture* GetFaceThumb(const FString& Id);

	// --- 作者臉（2026-08-10 user 定案）：選單舞台的預設力士＝作者本人。
	// 工件＝Content/AuthorFace/（NonUFS 原樣入包、runtime 匯入）；只給選單舞台
	// 穿——永不進臉庫、永不是玩家可選項（上傳自拍後即被自己的臉取代）。
	bool GetAuthorFace(UTexture2D*& OutOpen, UTexture2D*& OutClosed,
		UTexture2D*& OutMask, FLinearColor& OutSkin);

	// 雲端資產唯讀視圖（個人檔案頁現金／選單舞台穿刺青用；無資產＝nullptr）
	class UNiceInkSaveGame* GetCloudSaveView();

	EPullState GetAssetsPullState() const { return AssetsPull; }
	bool HasCloudAssets() const { return bHasCloudAssets; }
	const TArray<uint8>& GetCachedAssets() const { return CachedAssets; }

	// 結算下行終點：快取＋寫雲端（未登入＝只快取；bytes＝UNiceInkSaveGame 序列化）
	void StoreAssets(const TArray<uint8>& Bytes);

	// 偏好推雲端（GameInstance::SaveSettings 呼叫；套用雲端偏好期間抑制回推）
	void PushSettings();
	bool IsApplyingCloudSettings() const { return bApplyingCloudSettings; }

	// 本機玩家的 PUID（未登入＝空字串）
	FString GetLocalPuid() const;

	// 從 NetId 字串抽 PUID：「EAS|PUID」取後半；無分隔符或後半非十六進位＝空
	//（NULL/LAN 的 id 無分隔符＝天然落空＝走舊制）
	static FString PuidFromNetIdString(const FString& NetIdStr);

private:
	IOnlineUserCloudPtr GetUserCloud() const;
	FUniqueNetIdPtr GetLoggedInUserId() const;

	void OnReadUserFileComplete(bool bWasSuccessful, const FUniqueNetId& UserId, const FString& FileName);
	void OnWriteUserFileComplete(bool bWasSuccessful, const FUniqueNetId& UserId, const FString& FileName);
	void ApplyCloudSettings(const TArray<uint8>& Bytes);

	// 自訂臉內部
	FString PlayerFaceDir() const;                 // <Saved>/PlayerFace
	FString LibraryDir() const;                    // <Saved>/PlayerFace/library
	void LoadCustomFaceFromDisk();                 // 啟動載入（含舊平鋪檔遷移）
	bool ImportFaceArtifacts(const FString& Dir);  // 三張 png＋skin_color.json → 記憶體
	// 匯入共用核心（自訂臉／作者臉同一套工件格式）
	static bool ImportFaceDirInto(const FString& Dir, TObjectPtr<UTexture2D>& OutOpen,
		TObjectPtr<UTexture2D>& OutClosed, TObjectPtr<UTexture2D>& OutMask, FLinearColor& OutSkin);
	bool TickIntake(float DeltaSeconds);           // 管線行程輪詢（FTSTicker）

	EPullState AssetsPull = EPullState::NotStarted;
	bool bHasCloudAssets = false;
	TArray<uint8> CachedAssets;

	bool bApplyingCloudSettings = false;
	bool bDelegatesBound = false;
	FDelegateHandle ReadHandle;
	FDelegateHandle WriteHandle;

	// 自訂臉狀態（UPROPERTY＝防 GC：runtime 匯入的貼圖無資產登記）
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> FaceOpenTex;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> FaceClosedTex;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> EyeMaskInkTex;
	FLinearColor CustomSkinTone = FLinearColor(0.4f, 0.22f, 0.13f);
	int32 FaceRevision = 0;

	// 作者臉快取（懶載入一次；UPROPERTY＝runtime 匯入貼圖的 GC 錨）
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> AuthorFaceOpenTex;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> AuthorFaceClosedTex;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> AuthorEyeMaskInkTex;
	FLinearColor AuthorSkinTone = FLinearColor(0.4f, 0.22f, 0.13f);
	bool bAuthorFaceLoadTried = false;

	EFaceIntakeState IntakeState = EFaceIntakeState::Idle;
	FProcHandle IntakeProc;
	FTSTicker::FDelegateHandle IntakeTicker;
	double IntakeStartTime = 0.0;
	FString PendingIntakeId; // 本次上傳的 library id（完成後 Activate）
	FString ActiveFaceId;

	// 內建管線（SPEC #52 C1）背景執行結果——worker 寫、game thread 讀；
	// shared 持有＝subsystem 先關閉也不懸掛
	struct FNativeIntakeState
	{
		FThreadSafeBool bDone = false;
		FThreadSafeBool bSuccess = false;
	};
	TSharedPtr<FNativeIntakeState, ESPMode::ThreadSafe> NativeIntake;

	// 縮圖快取（UPROPERTY＝GC 錨——Slate brush 不保 GC；值＝頭像亭肖像 RT）
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UTexture>> ThumbCache;

	// 雲端資產反序列化快取（CachedAssets 變動即失效）
	UPROPERTY(Transient)
	TObjectPtr<class UNiceInkSaveGame> CloudSaveView;
	bool bCloudViewDirty = true;
};
