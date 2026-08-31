#include "NiceInkNotary.h"

#include "Dom/JsonObject.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "String/HexToBytes.h"

// OpenSSL（引擎 ThirdParty 1.1.1t）：UI 巨集撞名是引擎慣例坑，照 SSL 模組的包法
#define UI UI_ST
THIRD_PARTY_INCLUDES_START
#include <openssl/evp.h>
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END
#undef UI

namespace
{
	constexpr float NotaryHttpTimeoutS = 8.0f;

	FString HexLower(const uint8* Data, int32 Len)
	{
		static const TCHAR* Digits = TEXT("0123456789abcdef");
		FString Out;
		Out.Reserve(Len * 2);
		for (int32 i = 0; i < Len; ++i)
		{
			Out.AppendChar(Digits[Data[i] >> 4]);
			Out.AppendChar(Digits[Data[i] & 0xF]);
		}
		return Out;
	}

	FString JoinUrl(const FString& Base, const FString& Path)
	{
		FString B = Base;
		B.RemoveFromEnd(TEXT("/"));
		return B + Path; // Path 恆以 '/' 開頭
	}

	void AppendU32LE(TArray<uint8>& Out, uint32 V)
	{
		Out.Add(V & 0xFF);
		Out.Add((V >> 8) & 0xFF);
		Out.Add((V >> 16) & 0xFF);
		Out.Add((V >> 24) & 0xFF);
	}

	bool ReadU32LE(const TArray<uint8>& In, int32& Cursor, uint32& Out)
	{
		if (Cursor + 4 > In.Num())
		{
			return false;
		}
		Out = In[Cursor] | (In[Cursor + 1] << 8) | (In[Cursor + 2] << 16) |
			(static_cast<uint32>(In[Cursor + 3]) << 24);
		Cursor += 4;
		return true;
	}

	// 共用 POST JSON 骨架：回呼恆在 game thread（FHttpModule 保證）
	void PostJson(const FString& Path, const TSharedRef<FJsonObject>& Body,
		TFunction<void(bool bOk, TSharedPtr<FJsonObject> Json)> Done)
	{
		FString Payload;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Payload);
		FJsonSerializer::Serialize(Body, Writer);

		const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
		Req->SetURL(JoinUrl(FNiceInkNotary::GetBaseUrl(), Path));
		Req->SetVerb(TEXT("POST"));
		Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		Req->SetContentAsString(Payload);
		Req->SetTimeout(NotaryHttpTimeoutS);
		Req->OnProcessRequestComplete().BindLambda(
			[Done](FHttpRequestPtr, FHttpResponsePtr Resp, bool bConnected)
			{
				TSharedPtr<FJsonObject> Json;
				const bool bOk = bConnected && Resp.IsValid() &&
					Resp->GetResponseCode() >= 200 && Resp->GetResponseCode() < 300;
				if (bConnected && Resp.IsValid())
				{
					const TSharedRef<TJsonReader<>> Reader =
						TJsonReaderFactory<>::Create(Resp->GetContentAsString());
					FJsonSerializer::Deserialize(Reader, Json);
				}
				Done(bOk, Json);
			});
		Req->ProcessRequest();
	}
}

bool FNiceInkNotary::IsConfigured()
{
	if (FParse::Param(FCommandLine::Get(), TEXT("nonotary")))
	{
		return false;
	}
	return !GetBaseUrl().IsEmpty();
}

FString FNiceInkNotary::GetBaseUrl()
{
	FString Url;
	if (FParse::Value(FCommandLine::Get(), TEXT("notary="), Url) && !Url.IsEmpty())
	{
		return Url;
	}
	GConfig->GetString(TEXT("NiceInk.Notary"), TEXT("BaseUrl"), Url, GGameIni);
	return Url;
}

FString FNiceInkNotary::GetPublicKeyHex()
{
	FString Hex;
	GConfig->GetString(TEXT("NiceInk.Notary"), TEXT("PublicKeyHex"), Hex, GGameIni);
	return Hex;
}

FString FNiceInkNotary::Sha256Hex(const TArray<uint8>& Bytes)
{
	uint8 Digest[SHA256_DIGEST_LENGTH];
	SHA256(Bytes.GetData(), Bytes.Num(), Digest);
	return HexLower(Digest, SHA256_DIGEST_LENGTH);
}

bool FNiceInkNotary::VerifyPersonaSig(const FString& Puid, int32 Seq,
	const FString& BlobSha256Hex, const FString& SigHex)
{
	const FString PubHex = GetPublicKeyHex();
	if (PubHex.Len() != 64 || SigHex.Len() != 128 || Seq <= 0)
	{
		return false;
	}
	uint8 Pub[32];
	uint8 Sig[64];
	HexToBytes(PubHex, Pub);
	HexToBytes(SigHex, Sig);

	// 訊息格式與 Backend/signing.py 逐字對齊："NIPS1|puid|seq|sha(小寫)"
	const FString Message = FString::Printf(TEXT("NIPS1|%s|%d|%s"),
		*Puid, Seq, *BlobSha256Hex.ToLower());
	const FTCHARToUTF8 Utf8(*Message);

	EVP_PKEY* Key = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, Pub, 32);
	if (!Key)
	{
		return false;
	}
	bool bOk = false;
	EVP_MD_CTX* Ctx = EVP_MD_CTX_new();
	if (Ctx && EVP_DigestVerifyInit(Ctx, nullptr, nullptr, nullptr, Key) == 1)
	{
		bOk = EVP_DigestVerify(Ctx, Sig, 64,
			reinterpret_cast<const unsigned char*>(Utf8.Get()), Utf8.Length()) == 1;
	}
	if (Ctx)
	{
		EVP_MD_CTX_free(Ctx);
	}
	EVP_PKEY_free(Key);
	return bOk;
}

void FNiceInkNotary::BuildEnvelope(const TArray<uint8>& Payload, int32 Seq,
	const FString& SigHex, TArray<uint8>& Out)
{
	TArray<uint8> Sig;
	Sig.SetNumUninitialized(SigHex.Len() / 2);
	HexToBytes(SigHex, Sig.GetData());

	Out.Reset(4 + 4 + 4 + Sig.Num() + 4 + Payload.Num());
	Out.Add('N');
	Out.Add('I');
	Out.Add('P');
	Out.Add('1');
	AppendU32LE(Out, static_cast<uint32>(Seq));
	AppendU32LE(Out, static_cast<uint32>(Sig.Num()));
	Out.Append(Sig);
	AppendU32LE(Out, static_cast<uint32>(Payload.Num()));
	Out.Append(Payload);
}

bool FNiceInkNotary::ParseEnvelope(const TArray<uint8>& In, TArray<uint8>& OutPayload,
	int32& OutSeq, FString& OutSigHex)
{
	OutSeq = 0;
	OutSigHex.Reset();
	if (In.Num() < 4 || In[0] != 'N' || In[1] != 'I' || In[2] != 'P' || In[3] != '1')
	{
		OutPayload = In; // 舊裸 blob（遷移相容）：原樣視為 payload、未簽章
		return true;
	}
	int32 Cursor = 4;
	uint32 Seq = 0;
	uint32 SigLen = 0;
	uint32 PayloadLen = 0;
	OutPayload.Reset();
	if (!ReadU32LE(In, Cursor, Seq) || !ReadU32LE(In, Cursor, SigLen) ||
		SigLen > 256 || Cursor + static_cast<int32>(SigLen) > In.Num())
	{
		return false;
	}
	OutSigHex = HexLower(In.GetData() + Cursor, SigLen);
	Cursor += SigLen;
	if (!ReadU32LE(In, Cursor, PayloadLen) ||
		Cursor + static_cast<int32>(PayloadLen) != In.Num())
	{
		OutSigHex.Reset();
		return false;
	}
	OutPayload.Append(In.GetData() + Cursor, PayloadLen);
	OutSeq = static_cast<int32>(Seq);
	return true;
}

void FNiceInkNotary::RequestSignPersona(const FString& Puid, const FString& BlobSha256Hex,
	const FString& SettlementToken, TFunction<void(bool, int32, FString)> Done)
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("dev_puid"), Puid);
	Body->SetStringField(TEXT("blob_sha256"), BlobSha256Hex.ToLower());
	if (!SettlementToken.IsEmpty())
	{
		Body->SetStringField(TEXT("settlement_token"), SettlementToken);
	}
	PostJson(TEXT("/sign-persona"), Body,
		[Done](bool bOk, TSharedPtr<FJsonObject> Json)
		{
			int32 Seq = 0;
			FString Sig;
			if (bOk && Json.IsValid())
			{
				Seq = static_cast<int32>(Json->GetNumberField(TEXT("seq")));
				Sig = Json->GetStringField(TEXT("sig_hex"));
			}
			Done(bOk && Seq > 0 && Sig.Len() == 128, Seq, Sig);
		});
}

void FNiceInkNotary::RequestLatestSeq(const FString& Puid, TFunction<void(bool, int32)> Done)
{
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(JoinUrl(GetBaseUrl(), TEXT("/latest-seq/") + Puid));
	Req->SetVerb(TEXT("GET"));
	Req->SetTimeout(NotaryHttpTimeoutS);
	Req->OnProcessRequestComplete().BindLambda(
		[Done](FHttpRequestPtr, FHttpResponsePtr Resp, bool bConnected)
		{
			if (!bConnected || !Resp.IsValid() || Resp->GetResponseCode() != 200)
			{
				Done(false, 0);
				return;
			}
			TSharedPtr<FJsonObject> Json;
			const TSharedRef<TJsonReader<>> Reader =
				TJsonReaderFactory<>::Create(Resp->GetContentAsString());
			if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
			{
				Done(false, 0);
				return;
			}
			Done(true, static_cast<int32>(Json->GetNumberField(TEXT("seq"))));
		});
	Req->ProcessRequest();
}

void FNiceInkNotary::RequestEscrowRegister(const FString& Puid, const FString& Room, int32 Round,
	int32 Slot, TFunction<void(bool)> Done)
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("dev_puid"), Puid);
	Body->SetStringField(TEXT("room"), Room);
	Body->SetNumberField(TEXT("round"), Round);
	Body->SetNumberField(TEXT("slot"), Slot);
	PostJson(TEXT("/escrow/register"), Body,
		[Done](bool bOk, TSharedPtr<FJsonObject>)
		{
			Done(bOk);
		});
}

void FNiceInkNotary::RequestEscrowReveal(const FString& Puid, const FString& Room, int32 Round,
	int32 Slot, TFunction<void(bool, FString)> Done)
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("dev_puid"), Puid);
	Body->SetStringField(TEXT("room"), Room);
	Body->SetNumberField(TEXT("round"), Round);
	Body->SetNumberField(TEXT("slot"), Slot);
	PostJson(TEXT("/escrow/reveal"), Body,
		[Done](bool bOk, TSharedPtr<FJsonObject> Json)
		{
			FString Author;
			if (bOk && Json.IsValid())
			{
				Author = Json->GetStringField(TEXT("author_puid"));
			}
			Done(bOk && !Author.IsEmpty(), Author);
		});
}

void FNiceInkNotary::RequestSettlement(const FString& Room, int32 Round,
	TFunction<void(bool, FString)> Done)
{
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(JoinUrl(GetBaseUrl(),
		FString::Printf(TEXT("/settlement/%s/%d"), *Room, Round)));
	Req->SetVerb(TEXT("GET"));
	Req->SetTimeout(NotaryHttpTimeoutS);
	Req->OnProcessRequestComplete().BindLambda(
		[Done](FHttpRequestPtr, FHttpResponsePtr Resp, bool bConnected)
		{
			FString Token;
			if (bConnected && Resp.IsValid() && Resp->GetResponseCode() == 200)
			{
				TSharedPtr<FJsonObject> Json;
				const TSharedRef<TJsonReader<>> Reader =
					TJsonReaderFactory<>::Create(Resp->GetContentAsString());
				if (FJsonSerializer::Deserialize(Reader, Json) && Json.IsValid())
				{
					Json->TryGetStringField(TEXT("settlement_token"), Token);
				}
			}
			Done(!Token.IsEmpty(), Token);
		});
	Req->ProcessRequest();
}

void FNiceInkNotary::RequestAttest(const FString& Puid, const FString& Room, int32 Round,
	const FString& DigestHex, int32 RosterSize, TFunction<void(bool, FString)> Done)
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("dev_puid"), Puid);
	Body->SetStringField(TEXT("room"), Room);
	Body->SetNumberField(TEXT("round"), Round);
	Body->SetStringField(TEXT("digest"), DigestHex.ToLower());
	Body->SetNumberField(TEXT("roster_size"), RosterSize);
	PostJson(TEXT("/attest"), Body,
		[Done](bool bOk, TSharedPtr<FJsonObject> Json)
		{
			FString Token;
			bool bSettled = false;
			if (bOk && Json.IsValid())
			{
				bSettled = Json->GetBoolField(TEXT("settled"));
				if (bSettled)
				{
					Token = Json->GetStringField(TEXT("settlement_token"));
				}
			}
			Done(bSettled && !Token.IsEmpty(), Token);
		});
}
