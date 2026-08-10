#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "NiceInkFaceShare.generated.h"

// 自訂臉房內分發的登記簿（2026-08-10；SPEC #52 強制上傳制的最後一哩）。
// 每個端（含 listen 主機）各養一本：席位 → 解碼好的臉貼圖三件組＋膚色。
// 資料流：owner client 把本機臉工件打包成 blob → 分塊上行（角色 ServerFace* 列車，
// 沿用 B3 位元組列車格式）→ server 存原始 blob（晚到者補發）→ 分塊下行給每個
// viewer 的角色（ClientFace*）→ 各端解碼進本登記簿 → EnsureAvatarApplied／
// DrawFaceTok 輪詢套用。LAN 與 EOS 同路（臉不走雲端、走房內 P2P）。
USTRUCT()
struct FNiFaceEntry
{
	GENERATED_BODY()

	// runtime 匯入的貼圖無資產登記——UPROPERTY＝GC 錨
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> Open;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> Closed;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> Mask;

	FLinearColor Tone = FLinearColor(0.4f, 0.22f, 0.13f);
	int32 Revision = 0; // 1 起算；變動＝角色端重套訊號
};

UCLASS()
class NICEINK_API UNiceInkFaceShare : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UNiceInkFaceShare* Get(const UObject* WorldContext);

	// blob 格式（版本 'NIF1'）：magic + tone RGB + 三段長度 + face_open.png
	// + face_closed.png + eye_mask_ink.png 原始位元組。
	// 從臉工件資料夾（Saved/PlayerFace/library/<id>）打包；缺件＝false
	static bool BuildBlobFromDir(const FString& Dir, TArray<uint8>& OutBlob);

	// 解碼 blob → 貼圖入登記簿（收件端；壞資料＝false 不入簿）
	bool StoreBlob(int32 Seat, const TArray<uint8>& Blob);

	// 本人端零延遲路：直接用 Persona 已在記憶體的貼圖入簿（不繞網路）
	void StoreTextures(int32 Seat, UTexture2D* Open, UTexture2D* Closed,
		UTexture2D* Mask, FLinearColor Tone);

	int32 GetRevision(int32 Seat) const;
	UTexture2D* GetOpen(int32 Seat) const;
	UTexture2D* GetClosed(int32 Seat) const;
	UTexture2D* GetMask(int32 Seat) const;
	FLinearColor GetTone(int32 Seat) const;

private:
	UPROPERTY(Transient)
	TMap<int32, FNiFaceEntry> Entries;
};
