#pragma once

#include "CoreMinimal.h"
#include "SelfieTypes.h"
#include "HairStyleDatabase.generated.h"

UCLASS(BlueprintType)
class NICEINK_API UHairStyleDatabase : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Hair")
	static const TArray<FHairStyleEntry>& GetAllStyles();

	UFUNCTION(BlueprintPure, Category = "Hair")
	static TArray<FHairStyleEntry> GetMaleStyles();

	UFUNCTION(BlueprintPure, Category = "Hair")
	static TArray<FHairStyleEntry> GetFemaleStyles();

	static const FHairStyleEntry* FindByIndex(int32 Index);

	UFUNCTION(BlueprintPure, Category = "Hair")
	static bool FindByIndexChecked(int32 Index, FHairStyleEntry& OutEntry);

	UFUNCTION(BlueprintPure, Category = "Hair")
	static int32 GetBaldStyleIndex(bool bMale);

	// Returns indices of styles most likely for bundled/tied-up hair
	UFUNCTION(BlueprintPure, Category = "Hair")
	static TArray<int32> GetBundledHairCandidates(bool bMale);

	// Cosine similarity between two embedding vectors
	static float CosineSimilarity(const TArray<float>& A, const TArray<float>& B);

	// Find best match from precomputed text embeddings.
	// ImageEmbedding: CLIP image encoder output.
	// TextEmbeddings: one per hair style, same order as GetAllStyles().
	// Returns index into GetAllStyles().
	static int32 FindBestMatch(const TArray<float>& ImageEmbedding, const TArray<TArray<float>>& TextEmbeddings);

	// Returns top-K matches with scores
	static TArray<TPair<int32, float>> FindTopMatches(const TArray<float>& ImageEmbedding, const TArray<TArray<float>>& TextEmbeddings, int32 TopK = 5);

private:
	static TArray<FHairStyleEntry> BuildDatabase();
};
