#include "HairStyleDatabase.h"

namespace
{
FHairStyleEntry MakeEntry(int32 Index, bool bMale, const TCHAR* Name, const TCHAR* Prompt)
{
	FHairStyleEntry E;
	E.Index = Index;
	E.bMale = bMale;
	E.DisplayName = Name;
	E.ClipPrompt = Prompt;
	return E;
}
}

TArray<FHairStyleEntry> UHairStyleDatabase::BuildDatabase()
{
	TArray<FHairStyleEntry> DB;
	DB.Reserve(50);

	// Male (0-24)
	DB.Add(MakeEntry(0, true, TEXT("Broccoli"), TEXT("a person with a broccoli haircut short curly top with faded sides")));
	DB.Add(MakeEntry(1, true, TEXT("Korean Two Block"), TEXT("a person with a comma bangs korean two block haircut")));
	DB.Add(MakeEntry(2, true, TEXT("Short Fade"), TEXT("a person with a short fade haircut tapered sides")));
	DB.Add(MakeEntry(3, true, TEXT("Crew Cut"), TEXT("a person with a crew cut short military style haircut")));
	DB.Add(MakeEntry(4, true, TEXT("Buzz Cut"), TEXT("a person with a buzz cut very short all over")));
	DB.Add(MakeEntry(5, true, TEXT("Bald"), TEXT("a bald person with no hair shaved head")));
	DB.Add(MakeEntry(6, true, TEXT("Side Part"), TEXT("a person with a classic side part short haircut")));
	DB.Add(MakeEntry(7, true, TEXT("Textured Crop"), TEXT("a person with a textured crop haircut messy fringe")));
	DB.Add(MakeEntry(8, true, TEXT("Quiff"), TEXT("a person with a quiff hairstyle volume on top swept back")));
	DB.Add(MakeEntry(9, true, TEXT("Pompadour"), TEXT("a person with a pompadour hairstyle slicked back volume")));
	DB.Add(MakeEntry(10, true, TEXT("Slick Back"), TEXT("a person with slicked back hair undercut")));
	DB.Add(MakeEntry(11, true, TEXT("Curtain Bangs"), TEXT("a person with middle part curtain bangs medium length hair")));
	DB.Add(MakeEntry(12, true, TEXT("Edgar Cut"), TEXT("a person with an Edgar cut straight bangs across forehead")));
	DB.Add(MakeEntry(13, true, TEXT("Mullet"), TEXT("a person with a mullet haircut short front long back")));
	DB.Add(MakeEntry(14, true, TEXT("Two Block"), TEXT("a person with a two block haircut bowl cut shape")));
	DB.Add(MakeEntry(15, true, TEXT("Medium Straight"), TEXT("a person with medium length straight hair natural")));
	DB.Add(MakeEntry(16, true, TEXT("Medium Curly"), TEXT("a person with medium length curly hair natural")));
	DB.Add(MakeEntry(17, true, TEXT("Long Straight"), TEXT("a person with long straight hair male")));
	DB.Add(MakeEntry(18, true, TEXT("Long Curly"), TEXT("a person with long curly hair male")));
	DB.Add(MakeEntry(19, true, TEXT("Man Bun"), TEXT("a person with a man bun tied up hair on top")));
	DB.Add(MakeEntry(20, true, TEXT("Short Afro"), TEXT("a person with a short afro natural hair")));
	DB.Add(MakeEntry(21, true, TEXT("High Top Fade"), TEXT("a person with a high top fade flat top afro")));
	DB.Add(MakeEntry(22, true, TEXT("Short Dreadlocks"), TEXT("a person with short dreadlocks locs")));
	DB.Add(MakeEntry(23, true, TEXT("Medium Dreadlocks"), TEXT("a person with medium length dreadlocks locs")));
	DB.Add(MakeEntry(24, true, TEXT("Cornrows"), TEXT("a person with cornrows braided close to scalp")));

	// Female (25-49)
	DB.Add(MakeEntry(25, false, TEXT("Long Straight Center Part"), TEXT("a person with long straight hair center middle part")));
	DB.Add(MakeEntry(26, false, TEXT("Long Straight Side Part"), TEXT("a person with long straight hair side part")));
	DB.Add(MakeEntry(27, false, TEXT("Long Straight Blunt Bangs"), TEXT("a person with long straight hair blunt bangs across forehead")));
	DB.Add(MakeEntry(28, false, TEXT("Long Straight Curtain Bangs"), TEXT("a person with long straight hair curtain bangs parted")));
	DB.Add(MakeEntry(29, false, TEXT("Long Straight Wispy Bangs"), TEXT("a person with long straight hair wispy see through bangs")));
	DB.Add(MakeEntry(30, false, TEXT("Long Wavy"), TEXT("a person with long wavy hair flowing")));
	DB.Add(MakeEntry(31, false, TEXT("Long Curly"), TEXT("a person with long curly hair female voluminous")));
	DB.Add(MakeEntry(32, false, TEXT("Long Big Curly"), TEXT("a person with long big curly hair voluminous dense curls")));
	DB.Add(MakeEntry(33, false, TEXT("Wolf Cut"), TEXT("a person with a wolf cut layered shaggy haircut")));
	DB.Add(MakeEntry(34, false, TEXT("Hime Cut"), TEXT("a person with a hime cut princess cut straight bangs sidelocks")));
	DB.Add(MakeEntry(35, false, TEXT("Lob"), TEXT("a person with a lob long bob haircut shoulder length")));
	DB.Add(MakeEntry(36, false, TEXT("Classic Bob"), TEXT("a person with a classic bob haircut chin length straight")));
	DB.Add(MakeEntry(37, false, TEXT("Wavy Bob"), TEXT("a person with a wavy bob haircut chin length waves")));
	DB.Add(MakeEntry(38, false, TEXT("Short Bob"), TEXT("a person with a short bob haircut above chin")));
	DB.Add(MakeEntry(39, false, TEXT("Pixie Cut"), TEXT("a person with a pixie cut very short feminine haircut")));
	DB.Add(MakeEntry(40, false, TEXT("Short Curly"), TEXT("a person with short curly hair female natural curls")));
	DB.Add(MakeEntry(41, false, TEXT("Medium Curtain Bangs Wavy"), TEXT("a person with medium length curtain bangs wavy hair")));
	DB.Add(MakeEntry(42, false, TEXT("Medium Natural Curly"), TEXT("a person with medium length natural curly hair")));
	DB.Add(MakeEntry(43, false, TEXT("Afro Female"), TEXT("a person with an afro natural hair female")));
	DB.Add(MakeEntry(44, false, TEXT("Box Braids"), TEXT("a person with box braids long braided hair")));
	DB.Add(MakeEntry(45, false, TEXT("Cornrows Female"), TEXT("a person with cornrows braided close to scalp female")));
	DB.Add(MakeEntry(46, false, TEXT("Long Locs Female"), TEXT("a person with long locs dreadlocks female")));
	DB.Add(MakeEntry(47, false, TEXT("French Bob Bangs"), TEXT("a person with a French bob with bangs chin length")));
	DB.Add(MakeEntry(48, false, TEXT("Bixie"), TEXT("a person with a bixie haircut between bob and pixie")));
	DB.Add(MakeEntry(49, false, TEXT("Layered Long Curly"), TEXT("a person with layered long curly hair voluminous layers")));

	return DB;
}

const TArray<FHairStyleEntry>& UHairStyleDatabase::GetAllStyles()
{
	static const TArray<FHairStyleEntry> Database = BuildDatabase();
	return Database;
}

TArray<FHairStyleEntry> UHairStyleDatabase::GetMaleStyles()
{
	TArray<FHairStyleEntry> Result;
	for (const FHairStyleEntry& E : GetAllStyles())
	{
		if (E.bMale)
		{
			Result.Add(E);
		}
	}
	return Result;
}

TArray<FHairStyleEntry> UHairStyleDatabase::GetFemaleStyles()
{
	TArray<FHairStyleEntry> Result;
	for (const FHairStyleEntry& E : GetAllStyles())
	{
		if (!E.bMale)
		{
			Result.Add(E);
		}
	}
	return Result;
}

const FHairStyleEntry* UHairStyleDatabase::FindByIndex(int32 Index)
{
	const TArray<FHairStyleEntry>& DB = GetAllStyles();
	for (const FHairStyleEntry& E : DB)
	{
		if (E.Index == Index)
		{
			return &E;
		}
	}
	return nullptr;
}

bool UHairStyleDatabase::FindByIndexChecked(int32 Index, FHairStyleEntry& OutEntry)
{
	if (const FHairStyleEntry* Found = FindByIndex(Index))
	{
		OutEntry = *Found;
		return true;
	}
	return false;
}

int32 UHairStyleDatabase::GetBaldStyleIndex(bool bMale)
{
	return bMale ? 5 : -1;
}

TArray<int32> UHairStyleDatabase::GetBundledHairCandidates(bool bMale)
{
	if (bMale)
	{
		return {15, 16, 17, 18, 11};
	}
	return {25, 26, 30, 31, 28};
}

float UHairStyleDatabase::CosineSimilarity(const TArray<float>& A, const TArray<float>& B)
{
	if (A.Num() != B.Num() || A.Num() == 0)
	{
		return 0.0f;
	}

	float Dot = 0.0f, NormA = 0.0f, NormB = 0.0f;
	for (int32 I = 0; I < A.Num(); ++I)
	{
		Dot += A[I] * B[I];
		NormA += A[I] * A[I];
		NormB += B[I] * B[I];
	}

	const float Denom = FMath::Sqrt(NormA) * FMath::Sqrt(NormB);
	return Denom > SMALL_NUMBER ? Dot / Denom : 0.0f;
}

int32 UHairStyleDatabase::FindBestMatch(const TArray<float>& ImageEmbedding, const TArray<TArray<float>>& TextEmbeddings)
{
	float BestScore = -1.0f;
	int32 BestIndex = -1;

	for (int32 I = 0; I < TextEmbeddings.Num(); ++I)
	{
		const float Score = CosineSimilarity(ImageEmbedding, TextEmbeddings[I]);
		if (Score > BestScore)
		{
			BestScore = Score;
			BestIndex = I;
		}
	}
	return BestIndex;
}

TArray<TPair<int32, float>> UHairStyleDatabase::FindTopMatches(const TArray<float>& ImageEmbedding, const TArray<TArray<float>>& TextEmbeddings, int32 TopK)
{
	TArray<TPair<int32, float>> Scores;
	for (int32 I = 0; I < TextEmbeddings.Num(); ++I)
	{
		Scores.Emplace(I, CosineSimilarity(ImageEmbedding, TextEmbeddings[I]));
	}

	Scores.Sort([](const TPair<int32, float>& A, const TPair<int32, float>& B) { return A.Value > B.Value; });

	if (Scores.Num() > TopK)
	{
		Scores.SetNum(TopK);
	}
	return Scores;
}
