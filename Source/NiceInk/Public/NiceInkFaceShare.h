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
	// 08-14 tone 提前制：tone 只有 16 bytes、不該搭 blob 列車尾班車——FaceBegin RPC
	// 先帶到、身體膚色進房瞬間先套（臉貼圖隨後）。獨立版本號＝不觸發貼圖套用路
	int32 ToneRevision = 0;
};

UCLASS()
class NICEINK_API UNiceInkFaceShare : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UNiceInkFaceShare* Get(const UObject* WorldContext);

	// blob 格式（08-14 起 'NIF2'＝壓縮版；B7 結帳）：magic + tone RGB + 五段長度 +
	// face_open JPEG(q90) + face_open alpha PNG(灰) + face_closed JPEG + closed alpha
	// PNG + eye_mask_ink.png。RGB 走 JPEG（照片內容 640KB→~120KB）、alpha 走無損
	// 灰階 PNG（島罩/頸淡出是承重合成通道、JPEG 不帶 alpha）；眼罩照舊 PNG。
	// 1.5MB→~350KB＝320KB/s 列車 ~1 秒送完。打包結果快取在 Dir/blob_nif2.bin
	//（library 資料夾按時間戳不可變＝快取恆有效）；解碼端相容舊 'NIF1'。
	// 從臉工件資料夾（Saved/PlayerFace/library/<id>）打包；缺件＝false
	static bool BuildBlobFromDir(const FString& Dir, TArray<uint8>& OutBlob);

	// 從 blob 頭部讀 tone（NIF1/NIF2 皆可；供 FaceBegin RPC 提前帶 tone）
	static bool PeekTone(const TArray<uint8>& Blob, FLinearColor& OutTone);

	// 解碼 blob → 貼圖入登記簿（收件端；壞資料＝false 不入簿）
	bool StoreBlob(int32 Seat, const TArray<uint8>& Blob);

	// 本人端零延遲路：直接用 Persona 已在記憶體的貼圖入簿（不繞網路）
	void StoreTextures(int32 Seat, UTexture2D* Open, UTexture2D* Closed,
		UTexture2D* Mask, FLinearColor Tone);

	// tone 提前入簿（FaceBegin RPC 到手即呼；blob 到手後 StoreBlob 覆蓋同值）
	void StoreToneEarly(int32 Seat, FLinearColor Tone);
	int32 GetToneRevision(int32 Seat) const;

	int32 GetRevision(int32 Seat) const;
	UTexture2D* GetOpen(int32 Seat) const;
	UTexture2D* GetClosed(int32 Seat) const;
	UTexture2D* GetMask(int32 Seat) const;
	FLinearColor GetTone(int32 Seat) const;

private:
	UPROPERTY(Transient)
	TMap<int32, FNiFaceEntry> Entries;
};
