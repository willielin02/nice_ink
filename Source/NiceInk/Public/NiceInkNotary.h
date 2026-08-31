#pragma once

#include "CoreMinimal.h"

// 公證後端客戶端（防作弊 Phase 1；規格＝Docs/ANTICHEAT_PLAN.md §4、Backend/README.md）。
// 職責只有三件：簽章格式（NIPS1 訊息＋NIP1 信封）、Ed25519 驗簽（引擎 OpenSSL 1.1.1t）、
// 對後端的三個 HTTP 呼叫。全 static——唯一狀態是 config。
//
// **「未配置＝行為完全不變」是承重閘**：BaseUrl 空字串＝整套功能關閉，所有呼叫端
// 以 IsConfigured() 為 && 疊加閘。它與 IsOnlineServiceConfigured()（EOS 軸）是兩個
// 獨立的軸——開發期常態＝EOS 配了、後端還沒起，此時 EOS 雲端路照跑、簽章全程 bypass。
//
// 身分軸記帳（B5 未爆彈，Docs/ANTICHEAT_PLAN.md §4.1）：簽章訊息裡的 puid＝EOS Connect
// PUID（PuidFromNetIdString 的輸出）；後端 dev 模式收 dev_puid 自報＝自洽。B5 換 Steam
// 票證驗證時，後端 resolve 出的是 SteamID64——屆時要嘛後端維護 SteamID↔PUID 對照、
// 要嘛訊息換軸（舊簽章全失效＝要遷移），接 B5 前必先裁。
struct NICEINK_API FNiceInkNotary
{
	// [NiceInk.Notary] BaseUrl（DefaultGame.ini）非空＝啟用；
	// 命令列 -notary=<url> 覆寫、-nonotary 強關（robo/診斷用）
	static bool IsConfigured();
	static FString GetBaseUrl();
	static FString GetPublicKeyHex(); // gen_keys.py 印出的 32B raw 公鑰 hex

	// --- 密碼學（同步、CPU-only、~µs 級）---
	static FString Sha256Hex(const TArray<uint8>& Bytes); // 小寫 hex
	// 驗 "NIPS1|puid|seq|blob_sha256hex(小寫)" 的 Ed25519 簽章（公鑰＝config）
	static bool VerifyPersonaSig(const FString& Puid, int32 Seq,
		const FString& BlobSha256Hex, const FString& SigHex);

	// --- NIP1 信封（只存在於 PDS 與線上；CachedAssets 語意恆＝裸 payload，
	// 否則 Character/GameMode 直引 CachedAssets 的兩處會靜默送錯東西）---
	// 佈局：'N''I''P''1' | seq(u32 LE) | siglen(u32 LE) | sig bytes | payloadlen(u32 LE) | payload
	static void BuildEnvelope(const TArray<uint8>& Payload, int32 Seq, const FString& SigHex,
		TArray<uint8>& Out);
	// 非 NIP1 magic＝舊裸 blob：OutPayload=原樣、OutSeq=0、sig 空（遷移相容、恆成功）；
	// 回 false 只在 NIP1 頭損壞（此時 OutPayload 清空）
	static bool ParseEnvelope(const TArray<uint8>& In, TArray<uint8>& OutPayload,
		int32& OutSeq, FString& OutSigHex);

	// --- HTTP（非同步；回呼在 game thread；逾時 8s＝bOk false）---
	// POST /sign-persona {dev_puid, blob_sha256, settlement_token?} → {seq, sig_hex}
	static void RequestSignPersona(const FString& Puid, const FString& BlobSha256Hex,
		const FString& SettlementToken, TFunction<void(bool bOk, int32 Seq, FString SigHex)> Done);
	// GET /latest-seq/<puid> → {seq}
	static void RequestLatestSeq(const FString& Puid, TFunction<void(bool bOk, int32 Seq)> Done);
	// POST /attest {dev_puid, room, round, digest, roster_size} → {settled, settlement_token?}
	static void RequestAttest(const FString& Puid, const FString& Room, int32 Round,
		const FString& DigestHex, int32 RosterSize,
		TFunction<void(bool bSettled, FString Token)> Done);

	// --- P2（Docs/ANTICHEAT_PLAN.md §4.2/4.3）---
	// POST /escrow/register：作畫者直接登記「本回合 slot X＝我」（不經 host）
	static void RequestEscrowRegister(const FString& Puid, const FString& Room, int32 Round,
		int32 Slot, TFunction<void(bool bOk)> Done);
	// POST /escrow/reveal：單 slot 揭示（後端閘＝該回合已結算才放；只回被指認那顆
	// slot 的作者＝未指認作品的作者保密到底）
	static void RequestEscrowReveal(const FString& Puid, const FString& Room, int32 Round,
		int32 Slot, TFunction<void(bool bOk, FString AuthorPuid)> Done);
	// GET /settlement/{room}/{round}：host 輪詢結算單（quorum 由其他見證人補齊時
	// token 落在別人的回應裡）
	static void RequestSettlement(const FString& Room, int32 Round,
		TFunction<void(bool bOk, FString Token)> Done);
};
