#include "NiceInkFaceShare.h"

#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	constexpr uint32 FaceBlobMagic = 0x3146494E; // 'NIF1' little-endian
	constexpr int32 FaceBlobMaxPart = 4 * 1024 * 1024; // 單張 png 瘋值上限

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

bool UNiceInkFaceShare::StoreBlob(int32 Seat, const TArray<uint8>& Blob)
{
	int32 Cursor = 0;
	uint32 Magic = 0, LenOpen = 0, LenClosed = 0, LenMask = 0;
	FLinearColor Tone(0.4f, 0.22f, 0.13f);
	if (!ReadInt(Blob, Cursor, Magic) || Magic != FaceBlobMagic ||
		!ReadFloat(Blob, Cursor, Tone.R) || !ReadFloat(Blob, Cursor, Tone.G) ||
		!ReadFloat(Blob, Cursor, Tone.B) ||
		!ReadInt(Blob, Cursor, LenOpen) || !ReadInt(Blob, Cursor, LenClosed) ||
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

	auto ImportPart = [&Blob, &Cursor](uint32 Len) -> UTexture2D*
	{
		TArray<uint8> Bytes(Blob.GetData() + Cursor, static_cast<int32>(Len));
		Cursor += Len;
		return FImageUtils::ImportBufferAsTexture2D(Bytes);
	};
	UTexture2D* Open = ImportPart(LenOpen);
	UTexture2D* Closed = ImportPart(LenClosed);
	UTexture2D* Mask = ImportPart(LenMask);
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
