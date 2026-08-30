#include "NiceInkFaceShare.h"

#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

namespace
{
	constexpr uint32 FaceBlobMagic = 0x3146494E;   // 'NIF1' little-endian（舊版：三段原始 PNG）
	constexpr uint32 FaceBlobMagic2 = 0x3246494E;  // 'NIF2'（08-14 壓縮版：JPEG RGB＋alpha PNG 分載）
	constexpr int32 FaceBlobMaxPart = 4 * 1024 * 1024; // 單段瘋值上限
	constexpr int32 FaceJpegQuality = 90; // 實測 2048² 臉：640KB PNG→~120KB，質差不可感

	// --- ImageWrapper 小工具（NIF2 打包/解包共用） ---

	bool DecodeToBgra(const TArray<uint8>& FileBytes, TArray64<uint8>& OutBgra, int32& OutW, int32& OutH)
	{
		IImageWrapperModule& Mod = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const EImageFormat Fmt = Mod.DetectImageFormat(FileBytes.GetData(), FileBytes.Num());
		TSharedPtr<IImageWrapper> Wrap = Fmt != EImageFormat::Invalid ? Mod.CreateImageWrapper(Fmt) : nullptr;
		if (!Wrap.IsValid() || !Wrap->SetCompressed(FileBytes.GetData(), FileBytes.Num()))
		{
			return false;
		}
		OutW = Wrap->GetWidth();
		OutH = Wrap->GetHeight();
		return Wrap->GetRaw(ERGBFormat::BGRA, 8, OutBgra);
	}

	bool DecodeToGray(const TArray<uint8>& FileBytes, TArray64<uint8>& OutGray, int32& OutW, int32& OutH)
	{
		IImageWrapperModule& Mod = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const EImageFormat Fmt = Mod.DetectImageFormat(FileBytes.GetData(), FileBytes.Num());
		TSharedPtr<IImageWrapper> Wrap = Fmt != EImageFormat::Invalid ? Mod.CreateImageWrapper(Fmt) : nullptr;
		if (!Wrap.IsValid() || !Wrap->SetCompressed(FileBytes.GetData(), FileBytes.Num()))
		{
			return false;
		}
		OutW = Wrap->GetWidth();
		OutH = Wrap->GetHeight();
		return Wrap->GetRaw(ERGBFormat::Gray, 8, OutGray);
	}

	bool EncodeBgraPair(const TArray64<uint8>& Bgra, int32 W, int32 H,
		TArray64<uint8>& OutJpeg, TArray64<uint8>& OutAlphaPng)
	{
		IImageWrapperModule& Mod = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TSharedPtr<IImageWrapper> Jpeg = Mod.CreateImageWrapper(EImageFormat::JPEG);
		if (!Jpeg.IsValid() || !Jpeg->SetRaw(Bgra.GetData(), Bgra.Num(), W, H, ERGBFormat::BGRA, 8))
		{
			return false;
		}
		OutJpeg = Jpeg->GetCompressed(FaceJpegQuality);

		TArray64<uint8> Alpha;
		Alpha.SetNumUninitialized(static_cast<int64>(W) * H);
		for (int64 i = 0; i < Alpha.Num(); ++i)
		{
			Alpha[i] = Bgra[i * 4 + 3];
		}
		TSharedPtr<IImageWrapper> Png = Mod.CreateImageWrapper(EImageFormat::PNG);
		if (!Png.IsValid() || !Png->SetRaw(Alpha.GetData(), Alpha.Num(), W, H, ERGBFormat::Gray, 8))
		{
			return false;
		}
		OutAlphaPng = Png->GetCompressed();
		return OutJpeg.Num() > 0 && OutAlphaPng.Num() > 0;
	}

	UTexture2D* CreateFaceTexFromJpegAlpha(const TArray<uint8>& Jpeg, const TArray<uint8>& AlphaPng)
	{
		TArray64<uint8> Bgra, Gray;
		int32 W = 0, H = 0, Wa = 0, Ha = 0;
		if (!DecodeToBgra(Jpeg, Bgra, W, H) || !DecodeToGray(AlphaPng, Gray, Wa, Ha) ||
			W != Wa || H != Ha || W <= 0 || H <= 0)
		{
			return nullptr;
		}
		for (int64 i = 0; i < Gray.Num(); ++i)
		{
			Bgra[i * 4 + 3] = Gray[i];
		}
		UTexture2D* Tex = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
		if (!Tex)
		{
			return nullptr;
		}
		void* Mip = Tex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Mip, Bgra.GetData(), Bgra.Num());
		Tex->GetPlatformData()->Mips[0].BulkData.Unlock();
		Tex->SRGB = true;
		Tex->UpdateResource();
		return Tex;
	}

	void AppendInt(TArray<uint8>& Out, uint32 V)
	{
		Out.Append(reinterpret_cast<const uint8*>(&V), sizeof(V));
	}
	void AppendFloat(TArray<uint8>& Out, float V)
	{
		Out.Append(reinterpret_cast<const uint8*>(&V), sizeof(V));
	}
	bool ReadInt(const TArray<uint8>& In, int32& Cursor, uint32& V)
	{
		if (Cursor + static_cast<int32>(sizeof(V)) > In.Num())
		{
			return false;
		}
		FMemory::Memcpy(&V, In.GetData() + Cursor, sizeof(V));
		Cursor += sizeof(V);
		return true;
	}
	bool ReadFloat(const TArray<uint8>& In, int32& Cursor, float& V)
	{
		if (Cursor + static_cast<int32>(sizeof(V)) > In.Num())
		{
			return false;
		}
		FMemory::Memcpy(&V, In.GetData() + Cursor, sizeof(V));
		Cursor += sizeof(V);
		return true;
	}
}

UNiceInkFaceShare* UNiceInkFaceShare::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UNiceInkFaceShare>() : nullptr;
}

bool UNiceInkFaceShare::BuildBlobFromDir(const FString& Dir, TArray<uint8>& OutBlob)
{
	// 打包快取（省每次進房 ~300ms 轉碼）。
	// **2026-08-31：原本的前提「library 資料夾按時間戳不可變」已經不成立。**
	// 解剖島 UV0 搬家時要重寫每一份 eye_mask_ink.png（追記92），而這支只驗 magic、
	// 不驗新舊 ⇒ 08-14 烤的 blob 一路蓋過 08-31 修好的遮罩，**四輪修全部隱形**
	// （user 連看四張一模一樣的截圖）。凡「衍生快取」都要能自己發現上游變了：
	// 任何來源檔比 blob 新就重算。
	const FString CachePath = Dir / TEXT("blob_nif2.bin");
	IFileManager& FM = IFileManager::Get();
	const FDateTime CacheTime = FM.GetTimeStamp(*CachePath);
	bool bCacheStale = false;
	for (const TCHAR* Src : { TEXT("face_open.png"), TEXT("face_closed.png"),
		TEXT("eye_mask_ink.png"), TEXT("skin_color.json") })
	{
		const FDateTime SrcTime = FM.GetTimeStamp(*(Dir / Src));
		if (SrcTime != FDateTime::MinValue() && SrcTime > CacheTime)
		{
			bCacheStale = true;
			break;
		}
	}
	if (!bCacheStale && FFileHelper::LoadFileToArray(OutBlob, *CachePath) && OutBlob.Num() > 32)
	{
		uint32 Magic = 0;
		FMemory::Memcpy(&Magic, OutBlob.GetData(), sizeof(Magic));
		if (Magic == FaceBlobMagic2)
		{
			return true;
		}
	}
	if (bCacheStale)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("NiFaceShare: blob cache stale (source newer) -> rebuild: %s"), *CachePath);
	}
	OutBlob.Reset();

	TArray<uint8> Open, Closed, Mask;
	if (!FFileHelper::LoadFileToArray(Open, *(Dir / TEXT("face_open.png"))) ||
		!FFileHelper::LoadFileToArray(Closed, *(Dir / TEXT("face_closed.png"))) ||
		!FFileHelper::LoadFileToArray(Mask, *(Dir / TEXT("eye_mask_ink.png"))))
	{
		return false;
	}

	// 膚色沿用 Persona 的 json 解析太重——這裡直接讀檔內 linear_rgb 三數
	FLinearColor Tone(0.4f, 0.22f, 0.13f);
	FString Json;
	if (FFileHelper::LoadFileToString(Json, *(Dir / TEXT("skin_color.json"))))
	{
		// 輕量抽取（格式固定由自家管線產出）："linear_rgb": [r, g, b]
		int32 KeyIdx = Json.Find(TEXT("linear_rgb"));
		if (KeyIdx != INDEX_NONE)
		{
			int32 L = Json.Find(TEXT("["), ESearchCase::IgnoreCase, ESearchDir::FromStart, KeyIdx);
			int32 R = Json.Find(TEXT("]"), ESearchCase::IgnoreCase, ESearchDir::FromStart, L);
			if (L != INDEX_NONE && R != INDEX_NONE && R > L)
			{
				TArray<FString> Parts;
				Json.Mid(L + 1, R - L - 1).ParseIntoArray(Parts, TEXT(","));
				if (Parts.Num() >= 3)
				{
					Tone = FLinearColor(FCString::Atof(*Parts[0]),
						FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2]));
				}
			}
		}
	}

	// NIF2 轉碼：RGB→JPEG（q90）、alpha→無損灰階 PNG；失敗退回 NIF1 原始 PNG 打包
	TArray64<uint8> OpenBgra, ClosedBgra;
	int32 Wo = 0, Ho = 0, Wc = 0, Hc = 0;
	TArray64<uint8> OpenJpeg, OpenA, ClosedJpeg, ClosedA;
	const bool bNif2 =
		DecodeToBgra(Open, OpenBgra, Wo, Ho) && DecodeToBgra(Closed, ClosedBgra, Wc, Hc) &&
		EncodeBgraPair(OpenBgra, Wo, Ho, OpenJpeg, OpenA) &&
		EncodeBgraPair(ClosedBgra, Wc, Hc, ClosedJpeg, ClosedA) &&
		OpenJpeg.Num() < FaceBlobMaxPart && OpenA.Num() < FaceBlobMaxPart &&
		ClosedJpeg.Num() < FaceBlobMaxPart && ClosedA.Num() < FaceBlobMaxPart;

	if (bNif2)
	{
		OutBlob.Reset(40 + static_cast<int32>(OpenJpeg.Num() + OpenA.Num() + ClosedJpeg.Num() + ClosedA.Num()) + Mask.Num());
		AppendInt(OutBlob, FaceBlobMagic2);
		AppendFloat(OutBlob, Tone.R);
		AppendFloat(OutBlob, Tone.G);
		AppendFloat(OutBlob, Tone.B);
		AppendInt(OutBlob, static_cast<uint32>(OpenJpeg.Num()));
		AppendInt(OutBlob, static_cast<uint32>(OpenA.Num()));
		AppendInt(OutBlob, static_cast<uint32>(ClosedJpeg.Num()));
		AppendInt(OutBlob, static_cast<uint32>(ClosedA.Num()));
		AppendInt(OutBlob, static_cast<uint32>(Mask.Num()));
		OutBlob.Append(OpenJpeg.GetData(), static_cast<int32>(OpenJpeg.Num()));
		OutBlob.Append(OpenA.GetData(), static_cast<int32>(OpenA.Num()));
		OutBlob.Append(ClosedJpeg.GetData(), static_cast<int32>(ClosedJpeg.Num()));
		OutBlob.Append(ClosedA.GetData(), static_cast<int32>(ClosedA.Num()));
		OutBlob.Append(Mask);
		FFileHelper::SaveArrayToFile(OutBlob, *CachePath);
		UE_LOG(LogTemp, Log, TEXT("NiFaceShare: NIF2 blob built (%d bytes, was %d raw png)"),
			OutBlob.Num(), 32 + Open.Num() + Closed.Num() + Mask.Num());
		return true;
	}

	UE_LOG(LogTemp, Warning, TEXT("NiFaceShare: NIF2 encode failed - falling back to NIF1 raw png blob"));
	OutBlob.Reset(32 + Open.Num() + Closed.Num() + Mask.Num());
	AppendInt(OutBlob, FaceBlobMagic);
	AppendFloat(OutBlob, Tone.R);
	AppendFloat(OutBlob, Tone.G);
	AppendFloat(OutBlob, Tone.B);
	AppendInt(OutBlob, static_cast<uint32>(Open.Num()));
	AppendInt(OutBlob, static_cast<uint32>(Closed.Num()));
	AppendInt(OutBlob, static_cast<uint32>(Mask.Num()));
	OutBlob.Append(Open);
	OutBlob.Append(Closed);
	OutBlob.Append(Mask);
	return true;
}

bool UNiceInkFaceShare::PeekTone(const TArray<uint8>& Blob, FLinearColor& OutTone)
{
	int32 Cursor = 0;
	uint32 Magic = 0;
	FLinearColor Tone(0.4f, 0.22f, 0.13f);
	if (!ReadInt(Blob, Cursor, Magic) || (Magic != FaceBlobMagic && Magic != FaceBlobMagic2) ||
		!ReadFloat(Blob, Cursor, Tone.R) || !ReadFloat(Blob, Cursor, Tone.G) ||
		!ReadFloat(Blob, Cursor, Tone.B))
	{
		return false;
	}
	OutTone = Tone;
	return true;
}

bool UNiceInkFaceShare::StoreBlob(int32 Seat, const TArray<uint8>& Blob)
{
	int32 Cursor = 0;
	uint32 Magic = 0;
	FLinearColor Tone(0.4f, 0.22f, 0.13f);
	if (!ReadInt(Blob, Cursor, Magic) || (Magic != FaceBlobMagic && Magic != FaceBlobMagic2) ||
		!ReadFloat(Blob, Cursor, Tone.R) || !ReadFloat(Blob, Cursor, Tone.G) ||
		!ReadFloat(Blob, Cursor, Tone.B))
	{
		return false;
	}

	auto SlicePart = [&Blob, &Cursor](uint32 Len) -> TArray<uint8>
	{
		TArray<uint8> Bytes(Blob.GetData() + Cursor, static_cast<int32>(Len));
		Cursor += Len;
		return Bytes;
	};

	UTexture2D* Open = nullptr;
	UTexture2D* Closed = nullptr;
	UTexture2D* Mask = nullptr;
	if (Magic == FaceBlobMagic2)
	{
		// NIF2：JPEG RGB＋alpha 灰 PNG 分載合體；眼罩原始 PNG
		uint32 LenOpenRgb = 0, LenOpenA = 0, LenClosedRgb = 0, LenClosedA = 0, LenMask = 0;
		if (!ReadInt(Blob, Cursor, LenOpenRgb) || !ReadInt(Blob, Cursor, LenOpenA) ||
			!ReadInt(Blob, Cursor, LenClosedRgb) || !ReadInt(Blob, Cursor, LenClosedA) ||
			!ReadInt(Blob, Cursor, LenMask))
		{
			return false;
		}
		const uint32 Lens[5] = { LenOpenRgb, LenOpenA, LenClosedRgb, LenClosedA, LenMask };
		uint64 Sum = 0;
		for (uint32 L : Lens)
		{
			if (L == 0 || L > static_cast<uint32>(FaceBlobMaxPart))
			{
				return false;
			}
			Sum += L;
		}
		if (static_cast<uint64>(Cursor) + Sum != static_cast<uint64>(Blob.Num()))
		{
			return false;
		}
		const TArray<uint8> OpenRgb = SlicePart(LenOpenRgb);
		const TArray<uint8> OpenA = SlicePart(LenOpenA);
		const TArray<uint8> ClosedRgb = SlicePart(LenClosedRgb);
		const TArray<uint8> ClosedA = SlicePart(LenClosedA);
		TArray<uint8> MaskPng = SlicePart(LenMask);
		Open = CreateFaceTexFromJpegAlpha(OpenRgb, OpenA);
		Closed = CreateFaceTexFromJpegAlpha(ClosedRgb, ClosedA);
		Mask = FImageUtils::ImportBufferAsTexture2D(MaskPng);
	}
	else
	{
		// NIF1（舊版相容）：三段原始 PNG
		uint32 LenOpen = 0, LenClosed = 0, LenMask = 0;
		if (!ReadInt(Blob, Cursor, LenOpen) || !ReadInt(Blob, Cursor, LenClosed) ||
			!ReadInt(Blob, Cursor, LenMask))
		{
			return false;
		}
		if (LenOpen == 0 || LenOpen > FaceBlobMaxPart || LenClosed == 0 || LenClosed > FaceBlobMaxPart ||
			LenMask == 0 || LenMask > FaceBlobMaxPart ||
			Cursor + static_cast<int32>(LenOpen + LenClosed + LenMask) != Blob.Num())
		{
			return false;
		}
		TArray<uint8> OpenPng = SlicePart(LenOpen);
		TArray<uint8> ClosedPng = SlicePart(LenClosed);
		TArray<uint8> MaskPng = SlicePart(LenMask);
		Open = FImageUtils::ImportBufferAsTexture2D(OpenPng);
		Closed = FImageUtils::ImportBufferAsTexture2D(ClosedPng);
		Mask = FImageUtils::ImportBufferAsTexture2D(MaskPng);
	}
	if (!Open || !Closed || !Mask)
	{
		UE_LOG(LogTemp, Warning, TEXT("NiFaceShare: blob decode failed (seat %d)"), Seat);
		return false;
	}
	// 眼罩＝資料圖（UV0 乘進墨層）——關 sRGB（與 Persona 匯入同規）
	Mask->SRGB = false;
	Mask->UpdateResource();

	StoreTextures(Seat, Open, Closed, Mask, Tone);
	UE_LOG(LogTemp, Log, TEXT("NiFaceShare: face stored for seat %d (%d bytes)"), Seat, Blob.Num());
	return true;
}

void UNiceInkFaceShare::StoreTextures(int32 Seat, UTexture2D* Open, UTexture2D* Closed,
	UTexture2D* Mask, FLinearColor Tone)
{
	if (Seat < 0 || !Open || !Closed)
	{
		return;
	}
	FNiFaceEntry& E = Entries.FindOrAdd(Seat);
	E.Open = Open;
	E.Closed = Closed;
	E.Mask = Mask;
	E.Tone = Tone;
	++E.Revision;
}

void UNiceInkFaceShare::StoreToneEarly(int32 Seat, FLinearColor Tone)
{
	if (Seat < 0)
	{
		return;
	}
	FNiFaceEntry& E = Entries.FindOrAdd(Seat);
	E.Tone = Tone;
	++E.ToneRevision;
	UE_LOG(LogTemp, Log, TEXT("NiFaceShare: tone-early stored (seat %d)"), Seat);
}

int32 UNiceInkFaceShare::GetToneRevision(int32 Seat) const
{
	const FNiFaceEntry* E = Entries.Find(Seat);
	return E ? E->ToneRevision : 0;
}

int32 UNiceInkFaceShare::GetRevision(int32 Seat) const
{
	const FNiFaceEntry* E = Entries.Find(Seat);
	return E ? E->Revision : 0;
}

UTexture2D* UNiceInkFaceShare::GetOpen(int32 Seat) const
{
	const FNiFaceEntry* E = Entries.Find(Seat);
	return E ? E->Open.Get() : nullptr;
}

UTexture2D* UNiceInkFaceShare::GetClosed(int32 Seat) const
{
	const FNiFaceEntry* E = Entries.Find(Seat);
	return E ? E->Closed.Get() : nullptr;
}

UTexture2D* UNiceInkFaceShare::GetMask(int32 Seat) const
{
	const FNiFaceEntry* E = Entries.Find(Seat);
	return E ? E->Mask.Get() : nullptr;
}

FLinearColor UNiceInkFaceShare::GetTone(int32 Seat) const
{
	const FNiFaceEntry* E = Entries.Find(Seat);
	return E ? E->Tone : FLinearColor(0.4f, 0.22f, 0.13f);
}
