#include "InkBodyComponent.h"

#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "InkCanvasComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiceInkTypes.h"
#include "StaticMeshResources.h"

UInkBodyComponent::UInkBodyComponent()
{
	SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}

void UInkBodyComponent::ApplyAvatar(const FNiceInkAvatarDef& Avatar)
{
	FaceOpenTexture = LoadObject<UTexture2D>(nullptr, *Avatar.FaceOpenPath);
	FaceClosedTexture = LoadObject<UTexture2D>(nullptr, *Avatar.FaceClosedPath);
	EyeMaskTexture = LoadObject<UTexture2D>(nullptr, *Avatar.EyeMaskPath);
	SkinTone = Avatar.SkinTone;

	if (DynamicBodyMaterial)
	{
		DynamicBodyMaterial->SetVectorParameterValue(SkinToneParam, SkinTone);
		if (EyeMaskTexture)
		{
			DynamicBodyMaterial->SetTextureParameterValue(EyeMaskParam, EyeMaskTexture);
		}
		ApplyFaceTexture();
	}
}

void UInkBodyComponent::ApplyCustomAvatar(UTexture2D* Open, UTexture2D* Closed, UTexture2D* EyeMask, FLinearColor Tone)
{
	if (!Open || !Closed)
	{
		return;
	}
	FaceOpenTexture = Open;
	FaceClosedTexture = Closed;
	if (EyeMask)
	{
		EyeMaskTexture = EyeMask;
	}
	SkinTone = Tone;

	if (DynamicBodyMaterial)
	{
		DynamicBodyMaterial->SetVectorParameterValue(SkinToneParam, SkinTone);
		if (EyeMaskTexture)
		{
			DynamicBodyMaterial->SetTextureParameterValue(EyeMaskParam, EyeMaskTexture);
		}
		ApplyFaceTexture();
	}
}

void UInkBodyComponent::ApplySkinToneOnly(FLinearColor Tone)
{
	SkinTone = Tone;
	if (DynamicBodyMaterial)
	{
		DynamicBodyMaterial->SetVectorParameterValue(SkinToneParam, SkinTone);
	}
}

void UInkBodyComponent::BindCanvas(UInkCanvasComponent* Canvas)
{
	UMaterialInterface* BaseMaterial = BodyMaterial ? BodyMaterial.Get() : GetMaterial(0);
	if (!BaseMaterial || !Canvas)
	{
		return;
	}

	if (!DynamicBodyMaterial || DynamicBodyMaterial->Parent != BaseMaterial)
	{
		DynamicBodyMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		SetMaterial(0, DynamicBodyMaterial);
	}

	DynamicBodyMaterial->SetTextureParameterValue(MarkerRTParam, Canvas->GetMarkerRenderTarget());
	DynamicBodyMaterial->SetTextureParameterValue(TattooRTParam, Canvas->GetTattooRenderTarget());
	DynamicBodyMaterial->SetTextureParameterValue(MistRTParam, Canvas->GetMistRenderTarget());
	DynamicBodyMaterial->SetVectorParameterValue(SkinToneParam, SkinTone);
	if (EyeMaskTexture)
	{
		DynamicBodyMaterial->SetTextureParameterValue(EyeMaskParam, EyeMaskTexture);
	}
	ApplyFaceTexture();
}

void UInkBodyComponent::SwapBodyMesh(UStaticMesh* NewMesh)
{
	if (!NewMesh || GetStaticMesh() == NewMesh)
	{
		return;
	}
	SetStaticMesh(NewMesh);
	bTriCacheBuilt = false;
	CachedTris.Reset();
	bSeamDataBuilt = false;
	TriNearSeam.Reset();
	UvGridCells.Reset();
	bPosGridBuilt = false;
	PosGridStart.Reset();
	PosGridItems.Reset();
	// SetStaticMesh 會重設材質槽為新網格預設——把 MID 綁回去
	if (DynamicBodyMaterial)
	{
		SetMaterial(0, DynamicBodyMaterial);
	}
}

void UInkBodyComponent::SetEyesClosed(bool bClosed)
{
	bEyesClosed = bClosed;
	ApplyFaceTexture();
}

void UInkBodyComponent::ApplyFaceTexture()
{
	if (!DynamicBodyMaterial)
	{
		return;
	}

	UTexture2D* Face = bEyesClosed && FaceClosedTexture ? FaceClosedTexture.Get() : FaceOpenTexture.Get();
	if (Face)
	{
		DynamicBodyMaterial->SetTextureParameterValue(FaceTexParam, Face);
	}
}

bool UInkBodyComponent::BuildTriCache()
{
	CachedTris.Reset();
	WeldPos.Reset();
	TriAdj.Reset();
	bTriCacheBuilt = false;
	bPosGridBuilt = false;
	PosGridStart.Reset();
	PosGridItems.Reset();

	UStaticMesh* SM = GetStaticMesh();
	if (!SM || !SM->GetRenderData() || SM->GetRenderData()->LODResources.Num() == 0)
	{
		return false;
	}

	const FStaticMeshLODResources& LOD = SM->GetRenderData()->LODResources[0];
	const FPositionVertexBuffer& Positions = LOD.VertexBuffers.PositionVertexBuffer;
	const FStaticMeshVertexBuffer& Vertices = LOD.VertexBuffers.StaticMeshVertexBuffer;
	const FRawStaticIndexBuffer& Indices = LOD.IndexBuffer;

	if (Positions.GetNumVertices() == 0 || Indices.GetNumIndices() == 0)
	{
		return false;
	}

	const uint32 SafeUvChannel = FMath::Clamp<uint32>(UvChannel, 0, Vertices.GetNumTexCoords() - 1);
	CachedTris.Reserve(Indices.GetNumIndices() / 3);
	// 逐 section 收集：褌（布料）section 不進快取——墨水數學只認皮膚。
	// 效果：UV→世界（雷射／巡禮錨點／實體筆）永遠落在皮膚上；
	// 世界→UV 的「最近距離」變成乾淨的皮膚距離（畫筆據此拒絕打在布上的筆劃，
	// SPEC「褌下的皮膚不可畫＝物理遮擋」）。判定用材質槽名（FaceIndex 對應在本網格上損壞，不可用）。
	const TArray<FStaticMaterial>& Mats = SM->GetStaticMaterials();
	for (const FStaticMeshSection& Section : LOD.Sections)
	{
		if (Mats.IsValidIndex(Section.MaterialIndex))
		{
			const FStaticMaterial& Mat = Mats[Section.MaterialIndex];
			if (Mat.MaterialSlotName.ToString().Contains(TEXT("Fundoshi")) ||
				Mat.ImportedMaterialSlotName.ToString().Contains(TEXT("Fundoshi")))
			{
				continue;
			}
		}
		// FaceUV 慣例＝UV 通道 1（存在才讀；缺席時退回通道 0＝臉貼圖不演但不炸）
		const uint32 Uv1Channel = Vertices.GetNumTexCoords() > 1 ? 1 : SafeUvChannel;
		const FColorVertexBuffer& Colors = LOD.VertexBuffers.ColorVertexBuffer;
		const bool bHasColors = Colors.GetNumVertices() == Positions.GetNumVertices();

		for (uint32 Tri = 0; Tri < Section.NumTriangles; ++Tri)
		{
			const uint32 Base = Section.FirstIndex + Tri * 3;
			const uint32 I0 = Indices.GetIndex(Base + 0);
			const uint32 I1 = Indices.GetIndex(Base + 1);
			const uint32 I2 = Indices.GetIndex(Base + 2);

			FCachedTri Cached;
			Cached.A = FVector(Positions.VertexPosition(I0));
			Cached.B = FVector(Positions.VertexPosition(I1));
			Cached.C = FVector(Positions.VertexPosition(I2));
			Cached.UVA = FVector2D(Vertices.GetVertexUV(I0, SafeUvChannel));
			Cached.UVB = FVector2D(Vertices.GetVertexUV(I1, SafeUvChannel));
			Cached.UVC = FVector2D(Vertices.GetVertexUV(I2, SafeUvChannel));
			Cached.UV1A = FVector2D(Vertices.GetVertexUV(I0, Uv1Channel));
			Cached.UV1B = FVector2D(Vertices.GetVertexUV(I1, Uv1Channel));
			Cached.UV1C = FVector2D(Vertices.GetVertexUV(I2, Uv1Channel));
			Cached.ColA = bHasColors ? Colors.VertexColor(I0) : FColor::Black;
			Cached.ColB = bHasColors ? Colors.VertexColor(I1) : FColor::Black;
			Cached.ColC = bHasColors ? Colors.VertexColor(I2) : FColor::Black;
			CachedTris.Add(Cached);
		}
	}

	// --- 位置焊接（跨 UV 縫的表面連續拓樸）＋三角形鄰接 ---
	// 渲染緩衝在 UV 縫上拆頂點：用頂點索引建鄰接會把圖集縫當成邊界、補丁被縫切斷
	// ——攤平畫布的存在意義正是讓縫隱形，焊接鍵必須是「位置」（0.01mm 量化）。
	{
		TMap<FIntVector, int32> WeldMap;
		WeldMap.Reserve(CachedTris.Num() * 2);
		auto WeldId = [&](const FVector& P) {
			const FIntVector Key(
				FMath::RoundToInt(P.X * 100.0f),
				FMath::RoundToInt(P.Y * 100.0f),
				FMath::RoundToInt(P.Z * 100.0f));
			if (const int32* Found = WeldMap.Find(Key))
			{
				return *Found;
			}
			const int32 NewId = WeldPos.Add(P);
			WeldMap.Add(Key, NewId);
			return NewId;
		};
		for (FCachedTri& Tri : CachedTris)
		{
			Tri.W[0] = WeldId(Tri.A);
			Tri.W[1] = WeldId(Tri.B);
			Tri.W[2] = WeldId(Tri.C);
		}

		TriAdj.Init(INDEX_NONE, CachedTris.Num() * 3);
		TMap<uint64, int32> EdgeOwner; // 焊接邊 → 先到的 (tri*3+edge)
		EdgeOwner.Reserve(CachedTris.Num() * 3);
		for (int32 T = 0; T < CachedTris.Num(); ++T)
		{
			for (int32 E = 0; E < 3; ++E)
			{
				const int32 Wa = CachedTris[T].W[E];
				const int32 Wb = CachedTris[T].W[(E + 1) % 3];
				if (Wa == Wb)
				{
					continue; // 退化邊（焊接後塌掉）
				}
				const uint64 Key = (static_cast<uint64>(FMath::Min(Wa, Wb)) << 32) |
					static_cast<uint32>(FMath::Max(Wa, Wb));
				if (int32* Other = EdgeOwner.Find(Key))
				{
					if (*Other != INDEX_NONE)
					{
						TriAdj[T * 3 + E] = *Other / 3;
						TriAdj[*Other] = T;
						*Other = INDEX_NONE; // 非流形邊（>2 面共邊）：只配第一對
					}
				}
				else
				{
					EdgeOwner.Add(Key, T * 3 + E);
				}
			}
		}
	}

	bTriCacheBuilt = CachedTris.Num() > 0;
	return bTriCacheBuilt;
}

// --- 世界→UV 空間索引（2026-08-25 延遲戰役；追記80 §5 兌現）---------------------
// 舊路徑＝204,398 三角形線性全掃，掛在**每一個筆劃點**上（縫區遲滯再掃第二遍）。
// 這裡建一張本地空間均勻網格（CSR），把「最近三角形」從 O(N) 變成 O(鄰格)。
// **等價性是構造保證**：三角形登記進自己 AABB 蓋到的每一格 ⇒ 若某三角形沒登記在
// 格盒 B 的任何一格，它的 AABB 就不與 B 相交 ⇒ 整個三角形落在 B 外 ⇒ 距離下界
// ＝查詢點到 B 邊界的距離。掃完殼 R 就拿到這個下界，下界 ≥ 目前最佳 ⇒ 可以收工。
// 平手規則沿用全掃的「嚴格 < ⇒ 最小索引勝」⇒ **輸出逐位相同**（追記80 唯一認可的
// 效能驗收形式；活體對照＝DebugResolveBodyUV 同時跑兩條路並印 match）。
bool UInkBodyComponent::BuildPosGrid()
{
	PosGridStart.Reset();
	PosGridItems.Reset();
	PosGridDim[0] = PosGridDim[1] = PosGridDim[2] = 0;
	bPosGridBuilt = false;

	const int32 NumTris = CachedTris.Num();
	if (NumTris <= 0)
	{
		return false;
	}

	FVector Mn(TNumericLimits<double>::Max());
	FVector Mx(-TNumericLimits<double>::Max());
	for (const FCachedTri& Tri : CachedTris)
	{
		Mn = Mn.ComponentMin(Tri.A).ComponentMin(Tri.B).ComponentMin(Tri.C);
		Mx = Mx.ComponentMax(Tri.A).ComponentMax(Tri.B).ComponentMax(Tri.C);
	}
	const FVector Ext = (Mx - Mn).ComponentMax(FVector(0.01));

	// 格邊長 2cm 起跳（皮膚三角形邊長 ~0.16cm ⇒ 幾乎每個三角形只落一格）；
	// 格數上限 1M＝4MB 索引（tri-cache 本身就 40MB 級，這點記憶體不是問題），
	// 超過就把格子放大——只影響速度不影響答案。
	constexpr int64 MaxCells = 1000000;
	float Cell = 2.0f;
	for (int32 Guard = 0; Guard < 24; ++Guard)
	{
		int64 D[3];
		for (int32 A = 0; A < 3; ++A)
		{
			D[A] = FMath::Max<int64>(1, static_cast<int64>(FMath::CeilToDouble(Ext[A] / Cell)));
		}
		if (D[0] * D[1] * D[2] <= MaxCells)
		{
			PosGridDim[0] = static_cast<int32>(D[0]);
			PosGridDim[1] = static_cast<int32>(D[1]);
			PosGridDim[2] = static_cast<int32>(D[2]);
			break;
		}
		Cell *= 1.5f;
	}
	if (PosGridDim[0] <= 0)
	{
		return false; // 保底：建不起來就走全掃（語義不變）
	}
	PosGridCell = Cell;
	PosGridMin = Mn;
	PosGridMax = Mx;

	const int32 NumCells = PosGridDim[0] * PosGridDim[1] * PosGridDim[2];
	PosGridStart.SetNumZeroed(NumCells + 1);

	// 兩趟 CSR：①數每格幾個 ②前綴和 ③照 T 遞增填入（格內清單自然遞增）
	auto CellRange = [this](const FCachedTri& Tri, int32 Lo[3], int32 Hi[3])
	{
		const FVector TMn = Tri.A.ComponentMin(Tri.B).ComponentMin(Tri.C);
		const FVector TMx = Tri.A.ComponentMax(Tri.B).ComponentMax(Tri.C);
		for (int32 A = 0; A < 3; ++A)
		{
			const int32 L = static_cast<int32>(FMath::FloorToDouble((TMn[A] - PosGridMin[A]) / PosGridCell));
			const int32 H = static_cast<int32>(FMath::FloorToDouble((TMx[A] - PosGridMin[A]) / PosGridCell));
			Lo[A] = FMath::Clamp(L, 0, PosGridDim[A] - 1);
			Hi[A] = FMath::Clamp(H, 0, PosGridDim[A] - 1);
		}
	};

	for (int32 T = 0; T < NumTris; ++T)
	{
		int32 Lo[3], Hi[3];
		CellRange(CachedTris[T], Lo, Hi);
		for (int32 Z = Lo[2]; Z <= Hi[2]; ++Z)
		{
			for (int32 Y = Lo[1]; Y <= Hi[1]; ++Y)
			{
				const int32 Row = (Z * PosGridDim[1] + Y) * PosGridDim[0];
				for (int32 X = Lo[0]; X <= Hi[0]; ++X)
				{
					++PosGridStart[Row + X + 1];
				}
			}
		}
	}
	for (int32 C = 0; C < NumCells; ++C)
	{
		PosGridStart[C + 1] += PosGridStart[C];
	}
	PosGridItems.SetNumUninitialized(PosGridStart[NumCells]);
	TArray<int32> Cursor(PosGridStart);
	for (int32 T = 0; T < NumTris; ++T)
	{
		int32 Lo[3], Hi[3];
		CellRange(CachedTris[T], Lo, Hi);
		for (int32 Z = Lo[2]; Z <= Hi[2]; ++Z)
		{
			for (int32 Y = Lo[1]; Y <= Hi[1]; ++Y)
			{
				const int32 Row = (Z * PosGridDim[1] + Y) * PosGridDim[0];
				for (int32 X = Lo[0]; X <= Hi[0]; ++X)
				{
					PosGridItems[Cursor[Row + X]++] = T;
				}
			}
		}
	}

	bPosGridBuilt = true;
	return true;
}

int32 UInkBodyComponent::FindClosestTriLocal(const FVector& Local, float MaxDistance,
	FVector& OutClosest, float& OutDistSq) const
{
	OutDistSq = TNumericLimits<float>::Max();
	int32 BestTri = INDEX_NONE;
	if (!bPosGridBuilt)
	{
		return INDEX_NONE;
	}

	// 查詢點離整張網格的 AABB 就已經超出容許值 ⇒ 任何三角形都不可能在範圍內
	//（三角形全在 AABB 內＝距離下界）。全掃版在這種情況同樣回失敗＝語義一致。
	{
		const FVector Cl(
			FMath::Clamp(Local.X, PosGridMin.X, PosGridMax.X),
			FMath::Clamp(Local.Y, PosGridMin.Y, PosGridMax.Y),
			FMath::Clamp(Local.Z, PosGridMin.Z, PosGridMax.Z));
		if (FVector::DistSquared(Cl, Local) > static_cast<double>(MaxDistance) * MaxDistance)
		{
			return INDEX_NONE;
		}
	}

	int32 C[3];
	for (int32 A = 0; A < 3; ++A)
	{
		const int32 I = static_cast<int32>(FMath::FloorToDouble((Local[A] - PosGridMin[A]) / PosGridCell));
		C[A] = FMath::Clamp(I, 0, PosGridDim[A] - 1);
	}

	// 預算保底（08-25 首次量測抓到的坑）：查詢點遠離網格、且 MaxDistance 又很寬時，
	// 殼展開要走過大量**空格**才建立得起距離下界——實測 1e6 容差的遠距查詢會走完
	// 45 萬格＝17.9ms，比全掃的 3.5ms 還慢 5 倍。所以給殼展開一個格數預算，超過
	// 就退回線性全掃：**保證永不比舊路徑慢**，而答案本來就相同（同一個平手規則）。
	// 正常作畫（容差 0.15cm）根本碰不到這條——AABB 早退或 R≤1 就收工。
	const int32 CellBudget = FMath::Max(4096, CachedTris.Num() / 8);
	int32 CellsVisited = 0;

	auto TestCell = [&](int32 X, int32 Y, int32 Z)
	{
		++CellsVisited;
		const int32 Idx = (Z * PosGridDim[1] + Y) * PosGridDim[0] + X;
		const int32 End = PosGridStart[Idx + 1];
		for (int32 I = PosGridStart[Idx]; I < End; ++I)
		{
			const int32 T = PosGridItems[I];
			const FCachedTri& Tri = CachedTris[T];
			const FVector Closest = FMath::ClosestPointOnTriangleToPoint(Local, Tri.A, Tri.B, Tri.C);
			const float DistSq = FVector::DistSquared(Closest, Local);
			// 平手取最小索引＝重現線性全掃的「嚴格 < 保留先到者」
			if (BestTri == INDEX_NONE || DistSq < OutDistSq || (DistSq == OutDistSq && T < BestTri))
			{
				OutDistSq = DistSq;
				BestTri = T;
				OutClosest = Closest;
			}
		}
	};

	const int32 MaxR = FMath::Max3(PosGridDim[0], PosGridDim[1], PosGridDim[2]);
	for (int32 R = 0; R <= MaxR; ++R)
	{
		if (CellsVisited > CellBudget)
		{
			return ClosestTriLinear(Local, OutClosest, OutDistSq);
		}
		// Chebyshev 殼 R：只走殼面，不重掃內部（遠距失敗案例才不會退化成 O(R^4)）
		const int32 Z0 = C[2] - R, Z1 = C[2] + R;
		const int32 Y0 = C[1] - R, Y1 = C[1] + R;
		const int32 X0 = C[0] - R, X1 = C[0] + R;
		for (int32 Z = FMath::Max(Z0, 0); Z <= FMath::Min(Z1, PosGridDim[2] - 1); ++Z)
		{
			const bool bZCap = (Z == Z0 || Z == Z1);
			for (int32 Y = FMath::Max(Y0, 0); Y <= FMath::Min(Y1, PosGridDim[1] - 1); ++Y)
			{
				const bool bYCap = (Y == Y0 || Y == Y1);
				if (bZCap || bYCap)
				{
					for (int32 X = FMath::Max(X0, 0); X <= FMath::Min(X1, PosGridDim[0] - 1); ++X)
					{
						TestCell(X, Y, Z);
					}
				}
				else
				{
					if (X0 >= 0) { TestCell(X0, Y, Z); }
					if (X1 <= PosGridDim[0] - 1 && X1 != X0) { TestCell(X1, Y, Z); }
				}
			}
		}

		// 掃完的區域＝格盒 [C-R, C+R]。未掃到的三角形整個落在盒外 ⇒ 其距離
		// ≥ 查詢點到盒面的最短距離＝下界 D。
		double D = TNumericLimits<double>::Max();
		for (int32 A = 0; A < 3; ++A)
		{
			const double Lo = PosGridMin[A] + static_cast<double>(C[A] - R) * PosGridCell;
			const double Hi = PosGridMin[A] + static_cast<double>(C[A] + R + 1) * PosGridCell;
			D = FMath::Min(D, FMath::Min(Local[A] - Lo, Hi - Local[A]));
		}
		D = FMath::Max(D, 0.0);

		if (BestTri != INDEX_NONE && static_cast<double>(OutDistSq) <= D * D)
		{
			break; // 下界已超過目前最佳＝目前最佳就是全域最佳
		}
		if (D > static_cast<double>(MaxDistance) &&
			(BestTri == INDEX_NONE || static_cast<double>(OutDistSq) > static_cast<double>(MaxDistance) * MaxDistance))
		{
			return INDEX_NONE; // 容許範圍內確定沒有（呼叫端本來也會判失敗）
		}
		if (X0 <= 0 && Y0 <= 0 && Z0 <= 0 &&
			X1 >= PosGridDim[0] - 1 && Y1 >= PosGridDim[1] - 1 && Z1 >= PosGridDim[2] - 1)
		{
			break; // 盒已涵蓋整張網格＝等同全掃
		}
	}
	return BestTri;
}

void UInkBodyComponent::GatherTrisNearLocal(const FVector& Local, float Radius, TArray<int32>& OutTris) const
{
	OutTris.Reset();
	if (!bPosGridBuilt || Radius < 0.0f)
	{
		return;
	}
	int32 Lo[3], Hi[3];
	for (int32 A = 0; A < 3; ++A)
	{
		const int32 L = static_cast<int32>(FMath::FloorToDouble((Local[A] - Radius - PosGridMin[A]) / PosGridCell));
		const int32 H = static_cast<int32>(FMath::FloorToDouble((Local[A] + Radius - PosGridMin[A]) / PosGridCell));
		if (H < 0 || L > PosGridDim[A] - 1)
		{
			return; // 整個查詢盒在網格外
		}
		Lo[A] = FMath::Clamp(L, 0, PosGridDim[A] - 1);
		Hi[A] = FMath::Clamp(H, 0, PosGridDim[A] - 1);
	}
	for (int32 Z = Lo[2]; Z <= Hi[2]; ++Z)
	{
		for (int32 Y = Lo[1]; Y <= Hi[1]; ++Y)
		{
			const int32 Row = (Z * PosGridDim[1] + Y) * PosGridDim[0];
			for (int32 X = Lo[0]; X <= Hi[0]; ++X)
			{
				const int32 Idx = Row + X;
				const int32 End = PosGridStart[Idx + 1];
				for (int32 I = PosGridStart[Idx]; I < End; ++I)
				{
					OutTris.Add(PosGridItems[I]);
				}
			}
		}
	}
	// 升序＝重現全掃的走訪順序（遲滯挑選用嚴格 <，順序決定平手歸屬）；
	// 同一 tri 跨格重複由呼叫端跳過（排序後必相鄰）
	OutTris.Sort();
}

bool UInkBodyComponent::BuildSeamData()
{
	if (bSeamDataBuilt)
	{
		return TriNearSeam.Num() > 0;
	}
	if (!bTriCacheBuilt && !BuildTriCache())
	{
		return false;
	}
	bSeamDataBuilt = true;

	const int32 NumTris = CachedTris.Num();
	TriNearSeam.Init(0, NumTris);

	// --- 縫邊偵測：焊接鄰接存在（表面連續）但共享頂點的 UV 兩側不一致=UV 縫；
	// 快取邊界邊（褌洞/外緣）同樣入列——出界平面蓋章一樣是汙染 ---
	auto UVOfWeld = [&](const FCachedTri& Tri, int32 W) -> FVector2D
	{
		if (Tri.W[0] == W) { return Tri.UVA; }
		if (Tri.W[1] == W) { return Tri.UVB; }
		return Tri.UVC;
	};
	TArray<float> DistCm;
	DistCm.Init(TNumericLimits<float>::Max(), NumTris);
	TArray<int32> Queue;
	constexpr float SeamUvTol = 1e-5f;
	for (int32 T = 0; T < NumTris; ++T)
	{
		bool bSeamTri = false;
		for (int32 E = 0; E < 3 && !bSeamTri; ++E)
		{
			const int32 N = TriAdj[T * 3 + E];
			if (N == INDEX_NONE)
			{
				bSeamTri = true; // 邊界邊
				continue;
			}
			const int32 Wa = CachedTris[T].W[E];
			const int32 Wb = CachedTris[T].W[(E + 1) % 3];
			if (!UVOfWeld(CachedTris[T], Wa).Equals(UVOfWeld(CachedTris[N], Wa), SeamUvTol) ||
				!UVOfWeld(CachedTris[T], Wb).Equals(UVOfWeld(CachedTris[N], Wb), SeamUvTol))
			{
				bSeamTri = true; // UV 不連續＝圖集縫
			}
		}
		if (bSeamTri)
		{
			DistCm[T] = 0.0f;
			TriNearSeam[T] = 1;
			Queue.Add(T);
		}
	}

	// --- 距離膨脹（多源標號修正法，質心距近似）：距縫 < NearCm 的 tri 全旗標——
	// 排帶半寬 1.5cm＋抖動＋點半徑 < 2.5cm ⇒ 快速路徑的平面蓋章保證不越縫 ---
	constexpr float NearCm = 2.5f;
	auto Centroid = [&](int32 T)
	{
		const FCachedTri& Tri = CachedTris[T];
		return (Tri.A + Tri.B + Tri.C) / 3.0f;
	};
	for (int32 Head = 0; Head < Queue.Num(); ++Head)
	{
		const int32 T = Queue[Head];
		for (int32 E = 0; E < 3; ++E)
		{
			const int32 N = TriAdj[T * 3 + E];
			if (N == INDEX_NONE)
			{
				continue;
			}
			const float Cand = DistCm[T] + FVector::Dist(Centroid(T), Centroid(N));
			if (Cand < DistCm[N] && Cand < NearCm)
			{
				DistCm[N] = Cand;
				TriNearSeam[N] = 1;
				Queue.Add(N);
			}
		}
	}

	// --- UV 網格索引（UV bbox 撒格）---
	UvGridCells.Reset();
	UvGridCells.SetNum(UvGridDim * UvGridDim);
	for (int32 T = 0; T < NumTris; ++T)
	{
		const FCachedTri& Tri = CachedTris[T];
		const float MinU = FMath::Min3(Tri.UVA.X, Tri.UVB.X, Tri.UVC.X);
		const float MaxU = FMath::Max3(Tri.UVA.X, Tri.UVB.X, Tri.UVC.X);
		const float MinV = FMath::Min3(Tri.UVA.Y, Tri.UVB.Y, Tri.UVC.Y);
		const float MaxV = FMath::Max3(Tri.UVA.Y, Tri.UVB.Y, Tri.UVC.Y);
		const int32 X0 = FMath::Clamp(FMath::FloorToInt(MinU * UvGridDim), 0, UvGridDim - 1);
		const int32 X1 = FMath::Clamp(FMath::FloorToInt(MaxU * UvGridDim), 0, UvGridDim - 1);
		const int32 Y0 = FMath::Clamp(FMath::FloorToInt(MinV * UvGridDim), 0, UvGridDim - 1);
		const int32 Y1 = FMath::Clamp(FMath::FloorToInt(MaxV * UvGridDim), 0, UvGridDim - 1);
		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			for (int32 X = X0; X <= X1; ++X)
			{
				UvGridCells[Y * UvGridDim + X].Add(T);
			}
		}
	}
	return true;
}

int32 UInkBodyComponent::FindTriAtUV(const FVector2D& UV)
{
	if (!BuildSeamData())
	{
		return INDEX_NONE;
	}
	const int32 X = FMath::Clamp(FMath::FloorToInt(UV.X * UvGridDim), 0, UvGridDim - 1);
	const int32 Y = FMath::Clamp(FMath::FloorToInt(UV.Y * UvGridDim), 0, UvGridDim - 1);
	for (const int32 T : UvGridCells[Y * UvGridDim + X])
	{
		const FCachedTri& Tri = CachedTris[T];
		const FVector2D V0 = Tri.UVB - Tri.UVA;
		const FVector2D V1 = Tri.UVC - Tri.UVA;
		const FVector2D V2 = UV - Tri.UVA;
		const float Denom = V0.X * V1.Y - V1.X * V0.Y;
		if (FMath::Abs(Denom) < 1e-12f)
		{
			continue;
		}
		const float B1 = (V2.X * V1.Y - V1.X * V2.Y) / Denom;
		const float B2 = (V0.X * V2.Y - V2.X * V0.Y) / Denom;
		if (B1 >= -0.001f && B2 >= -0.001f && (B1 + B2) <= 1.001f)
		{
			return T;
		}
	}
	return INDEX_NONE;
}

bool UInkBodyComponent::IsUVNearSeam(const FVector2D& UV)
{
	const int32 T = FindTriAtUV(UV);
	return T != INDEX_NONE && TriNearSeam.IsValidIndex(T) && TriNearSeam[T] != 0;
}

bool UInkBodyComponent::UVToWorldOnTri(int32 TriIndex, const FVector2D& UV, FVector& OutWorld)
{
	if (!CachedTris.IsValidIndex(TriIndex))
	{
		return false;
	}
	const FCachedTri& Tri = CachedTris[TriIndex];
	const FVector2D V0 = Tri.UVB - Tri.UVA;
	const FVector2D V1 = Tri.UVC - Tri.UVA;
	const FVector2D V2 = UV - Tri.UVA;
	const float Denom = V0.X * V1.Y - V1.X * V0.Y;
	if (FMath::Abs(Denom) < 1e-12f)
	{
		return false;
	}
	const float B1 = (V2.X * V1.Y - V1.X * V2.Y) / Denom;
	const float B2 = (V0.X * V2.Y - V2.X * V0.Y) / Denom;
	const FVector Local = Tri.A + (Tri.B - Tri.A) * B1 + (Tri.C - Tri.A) * B2;
	OutWorld = GetComponentTransform().TransformPosition(Local);
	return true;
}

bool UInkBodyComponent::BuildSurfacePatch(const FVector& WorldCenter, float RadiusCm,
	FInkSurfacePatch& OutPatch, float MaxSeedDistance)
{
	OutPatch.Reset();
	if ((!bTriCacheBuilt && !BuildTriCache()) || RadiusCm <= 1.0f)
	{
		return false;
	}

	// --- 種子三角形＝離世界點最近的皮膚三角形 ---
	// 08-25：同樣走空間索引（追記80 §5 點名的第二個 204k 全掃；縫區排針逐排都在付）。
	// 平手規則與全掃相同（最小索引勝）⇒ 種子逐位相同 ⇒ 補丁展開結果不變。
	const FVector Local = GetComponentTransform().InverseTransformPosition(WorldCenter);
	if (!bPosGridBuilt)
	{
		BuildPosGrid();
	}
	int32 Seed = INDEX_NONE;
	FVector SeedPoint = FVector::ZeroVector;
	float BestDistSq = TNumericLimits<float>::Max();
	if (bPosGridBuilt)
	{
		Seed = FindClosestTriLocal(Local, MaxSeedDistance, SeedPoint, BestDistSq);
	}
	else
	{
		for (int32 T = 0; T < CachedTris.Num(); ++T)
		{
			const FCachedTri& Tri = CachedTris[T];
			const FVector Closest = FMath::ClosestPointOnTriangleToPoint(Local, Tri.A, Tri.B, Tri.C);
			const float DistSq = FVector::DistSquared(Closest, Local);
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				Seed = T;
				SeedPoint = Closest;
			}
		}
	}
	if (Seed == INDEX_NONE || BestDistSq > FMath::Square(MaxSeedDistance))
	{
		return false;
	}
	return BuildSurfacePatchInternal(Seed, SeedPoint, RadiusCm, OutPatch);
}

bool UInkBodyComponent::BuildSurfacePatchFromTri(int32 SeedTri, const FVector& WorldCenter,
	float RadiusCm, FInkSurfacePatch& OutPatch)
{
	OutPatch.Reset();
	if ((!bTriCacheBuilt && !BuildTriCache()) || !CachedTris.IsValidIndex(SeedTri) || RadiusCm <= 1.0f)
	{
		return false;
	}
	const FVector Local = GetComponentTransform().InverseTransformPosition(WorldCenter);
	const FCachedTri& Tri = CachedTris[SeedTri];
	const FVector SeedPoint = FMath::ClosestPointOnTriangleToPoint(Local, Tri.A, Tri.B, Tri.C);
	return BuildSurfacePatchInternal(SeedTri, SeedPoint, RadiusCm, OutPatch);
}

bool UInkBodyComponent::BuildSurfacePatchInternal(int32 Seed, const FVector& SeedPoint,
	float RadiusCm, FInkSurfacePatch& OutPatch)
{
	// --- 種子平面框（chart 原點=選點；+Y≈網格本地 +Z 朝頭側的投影——畫布上下有穩定語義）---
	const FCachedTri& S = CachedTris[Seed];
	const FVector SeedN = FVector::CrossProduct(S.B - S.A, S.C - S.A).GetSafeNormal();
	FVector UpHint = FMath::Abs(SeedN.Z) < 0.9f ? FVector::UpVector : FVector::YAxisVector;
	FVector AxisY = (UpHint - FVector::DotProduct(UpHint, SeedN) * SeedN).GetSafeNormal();
	if (AxisY.IsNearlyZero())
	{
		AxisY = FVector::XAxisVector;
	}
	const FVector AxisX = FVector::CrossProduct(AxisY, SeedN).GetSafeNormal();

	// --- BFS 鉸鏈展開：每焊接頂點一個 chart 座標（先到先定＝圖表連續）---
	TMap<int32, FVector2D> ChartByWeld;
	ChartByWeld.Reserve(512);
	auto SeedChart = [&](const FVector& P) {
		const FVector D = P - SeedPoint;
		return FVector2D(FVector::DotProduct(D, AxisX), FVector::DotProduct(D, AxisY));
	};
	ChartByWeld.Add(S.W[0], SeedChart(S.A));
	ChartByWeld.Add(S.W[1], SeedChart(S.B));
	ChartByWeld.Add(S.W[2], SeedChart(S.C));

	TSet<int32> Visited;
	Visited.Add(Seed);
	TArray<int32> Queue;
	Queue.Add(Seed);
	TArray<int32> Accepted;

	auto CornerPos = [&](int32 T, int32 Corner) -> const FVector& {
		const FCachedTri& Tri = CachedTris[T];
		return Corner == 0 ? Tri.A : (Corner == 1 ? Tri.B : Tri.C);
	};

	for (int32 Head = 0; Head < Queue.Num(); ++Head)
	{
		const int32 T = Queue[Head];
		const FCachedTri& Tri = CachedTris[T];

		// 收錄判定：任一角在半徑內（邊界三角形保留＝畫布邊緣是三角形邊，誠實的紙緣）
		bool bInside = false;
		for (int32 C = 0; C < 3; ++C)
		{
			const FVector2D* Chart = ChartByWeld.Find(Tri.W[C]);
			if (Chart && Chart->SizeSquared() <= FMath::Square(RadiusCm))
			{
				bInside = true;
				break;
			}
		}
		if (!bInside)
		{
			continue;
		}
		Accepted.Add(T);

		// 擴張：三條邊的鄰居展開進平面
		for (int32 E = 0; E < 3; ++E)
		{
			const int32 N = TriAdj[T * 3 + E];
			if (N == INDEX_NONE || Visited.Contains(N))
			{
				continue;
			}
			const FCachedTri& NT = CachedTris[N];
			// 鄰居的第三個焊接頂點（不在共享邊上的那個）
			const int32 Wa = Tri.W[E];
			const int32 Wb = Tri.W[(E + 1) % 3];
			int32 ThirdCorner = INDEX_NONE;
			for (int32 C = 0; C < 3; ++C)
			{
				if (NT.W[C] != Wa && NT.W[C] != Wb)
				{
					ThirdCorner = C;
					break;
				}
			}
			const FVector2D* CA = ChartByWeld.Find(Wa);
			const FVector2D* CB = ChartByWeld.Find(Wb);
			if (ThirdCorner == INDEX_NONE || !CA || !CB)
			{
				continue; // 退化或父邊未定——不從這條邊擴
			}
			if (!ChartByWeld.Contains(NT.W[ThirdCorner]))
			{
				// 鉸鏈展開：保長放平——沿 AB 求垂足＋高度，放在父三角形 apex 的對側
				const FVector& PA = WeldPos[Wa];
				const FVector& PB = WeldPos[Wb];
				const FVector& PC = WeldPos[NT.W[ThirdCorner]];
				const float DAB = FMath::Max(FVector::Dist(PA, PB), 0.01f);
				const float Da = FVector::Dist(PC, PA);
				const float Db = FVector::Dist(PC, PB);
				const float Along = (DAB * DAB + Da * Da - Db * Db) / (2.0f * DAB);
				const float H = FMath::Sqrt(FMath::Max(0.0f, Da * Da - Along * Along));
				const FVector2D U = (*CB - *CA) / DAB;
				const FVector2D V(-U.Y, U.X);
				// 父 apex（本三角形不在共享邊上的角）在 AB 的哪一側——鄰居放對側
				const int32 ParentApexW = Tri.W[(E + 2) % 3];
				float Side = 1.0f;
				if (const FVector2D* PApex = ChartByWeld.Find(ParentApexW))
				{
					const FVector2D Rel = *PApex - *CA;
					Side = (Rel.X * V.X + Rel.Y * V.Y) > 0.0f ? -1.0f : 1.0f;
				}
				ChartByWeld.Add(NT.W[ThirdCorner], *CA + U * Along + V * (H * Side));
			}
			Visited.Add(N);
			Queue.Add(N);
		}
	}

	if (Accepted.Num() == 0)
	{
		return false;
	}

	// --- 輸出補丁 ---
	OutPatch.SeedTri = Seed;
	OutPatch.RadiusCm = RadiusCm;
	OutPatch.TriMask.Init(false, CachedTris.Num());
	OutPatch.Tris.Reserve(Accepted.Num());
	TSet<int32> AcceptedSet(Accepted);
	for (const int32 T : Accepted)
	{
		const FCachedTri& Tri = CachedTris[T];
		FInkSurfacePatch::FPatchTri P;
		P.CacheTri = T;
		const FVector2D* Charts[3] = { ChartByWeld.Find(Tri.W[0]), ChartByWeld.Find(Tri.W[1]), ChartByWeld.Find(Tri.W[2]) };
		if (!Charts[0] || !Charts[1] || !Charts[2])
		{
			continue;
		}
		P.C[0] = { *Charts[0], Tri.UVA, Tri.UV1A, Tri.ColA };
		P.C[1] = { *Charts[1], Tri.UVB, Tri.UV1B, Tri.ColB };
		P.C[2] = { *Charts[2], Tri.UVC, Tri.UV1C, Tri.ColC };
		OutPatch.Tris.Add(P);
		OutPatch.TriMask[T] = true;

		// 邊界線段：鄰居不在補丁內（或無鄰居）的邊
		for (int32 E = 0; E < 3; ++E)
		{
			const int32 N = TriAdj[T * 3 + E];
			if (N == INDEX_NONE || !AcceptedSet.Contains(N))
			{
				OutPatch.BoundarySegs.Add(CornerPos(T, E));
				OutPatch.BoundarySegs.Add(CornerPos(T, (E + 1) % 3));
			}
		}
	}

	return OutPatch.IsValid();
}

bool FInkSurfacePatch::ChartToUV0(const FVector2D& ChartPt, FVector2D& OutUV0) const
{
	for (const FPatchTri& Tri : Tris)
	{
		const FVector2D V0 = Tri.C[1].Chart - Tri.C[0].Chart;
		const FVector2D V1 = Tri.C[2].Chart - Tri.C[0].Chart;
		const FVector2D V2 = ChartPt - Tri.C[0].Chart;
		const float D00 = FVector2D::DotProduct(V0, V0);
		const float D01 = FVector2D::DotProduct(V0, V1);
		const float D11 = FVector2D::DotProduct(V1, V1);
		const float D20 = FVector2D::DotProduct(V2, V0);
		const float D21 = FVector2D::DotProduct(V2, V1);
		const float Denom = D00 * D11 - D01 * D01;
		if (Denom <= D00 * D11 * 1e-4f)
		{
			continue; // 退化判定同 ResolveUVToWorld（相對尺度）
		}
		const float V = (D11 * D20 - D01 * D21) / Denom;
		const float W = (D00 * D21 - D01 * D20) / Denom;
		const float U = 1.0f - V - W;
		constexpr float Eps = -0.001f;
		if (U >= Eps && V >= Eps && W >= Eps)
		{
			OutUV0 = Tri.C[0].UV0 * U + Tri.C[1].UV0 * V + Tri.C[2].UV0 * W;
			return true;
		}
	}
	return false;
}

bool FInkSurfacePatch::Overlaps(const FInkSurfacePatch& A, const FInkSurfacePatch& B)
{
	if (A.TriMask.Num() != B.TriMask.Num() || A.TriMask.Num() == 0)
	{
		return false; // 不同網格（站/睡切換瞬間）＝不重疊；空補丁＝不重疊
	}
	const int32 NumWords = FBitSet::CalculateNumWords(A.TriMask.Num());
	const uint32* WordsA = A.TriMask.GetData();
	const uint32* WordsB = B.TriMask.GetData();
	for (int32 i = 0; i < NumWords; ++i)
	{
		if (WordsA[i] & WordsB[i])
		{
			return true;
		}
	}
	return false;
}

bool UInkBodyComponent::ResolveUVToWorld(FVector2D UV, FVector& OutWorldPosition)
{
	FVector UnusedNormal;
	return ResolveUVToWorldWithNormal(UV, OutWorldPosition, UnusedNormal);
}

bool UInkBodyComponent::ResolveUVToWorldWithNormal(FVector2D UV, FVector& OutWorldPosition, FVector& OutNormal)
{
	if (!bTriCacheBuilt && !BuildTriCache())
	{
		return false;
	}

	// UV→世界＝走 UV 網格索引（2026-08-25 效能修）。原本是 CachedTris 線性全掃，
	// 而快取實測 **204,398 tris**（褌戰役細分後；程式裡「sumo 23k tris」是舊世界的
	// 數字）＝單次 1.46ms ⇒ 可畫域 64² 採樣（3ms/tick 預算）要 **21.6 秒**才擬合得完
	// （robo_veiltime 實測 21,833ms／1431 ticks；三段分解 resolve 5978ms＝98.8%）。
	// 語義等價的理由（不是近似）：任何「含此 UV 點」的三角形，其 UV bbox 必定蓋到
	// 該點所在的格 ⇒ 格內候選是全掃的超集；格內清單依 T 遞增插入 ⇒ 命中的是同一個
	// 最小索引三角形；含點判準（±0.001 重心容差）兩邊逐字相同。
	const int32 HitTri = FindTriAtUV(UV);
	if (HitTri != INDEX_NONE && UVToWorldOnTri(HitTri, UV, OutWorldPosition))
	{
		const FCachedTri& Tri = CachedTris[HitTri];
		const FVector LocalNormal = FVector::CrossProduct(Tri.B - Tri.A, Tri.C - Tri.A).GetSafeNormal();
		OutNormal = GetComponentTransform().TransformVectorNoScale(LocalNormal).GetSafeNormal();
		return true;
	}
	if (UvGridCells.Num() > 0)
	{
		return false; // 網格已建成＝「查無此點」是確定答案（別退回全掃：落在圖集
		              // 空白的樣本正好是最貴的那一類——本例 4096 點裡有 1633 點）
	}

	// 保底：網格建不起來（BuildSeamData 失敗）才走全掃
	for (const FCachedTri& Tri : CachedTris)
	{
		// UV 空間的重心座標（2D）
		const FVector2D V0 = Tri.UVB - Tri.UVA;
		const FVector2D V1 = Tri.UVC - Tri.UVA;
		const FVector2D V2 = UV - Tri.UVA;
		const float D00 = FVector2D::DotProduct(V0, V0);
		const float D01 = FVector2D::DotProduct(V0, V1);
		const float D11 = FVector2D::DotProduct(V1, V1);
		const float D20 = FVector2D::DotProduct(V2, V0);
		const float D21 = FVector2D::DotProduct(V2, V1);
		const float Denom = D00 * D11 - D01 * D01;
		// 退化判定必須用相對尺度：高密度網格的 UV 三角形極小（Denom ~1e-9），
		// 絕對容差 IsNearlyZero(1e-8) 會把整張圖集當退化跳過（當年 sumo 23k tris 實測全滅；
		// **2026-08-25 實測快取已達 204,398 tris**——褌戰役細分後的規模，UV 三角形更小、
		// 相對尺度判定更是唯一正解）
		if (Denom <= D00 * D11 * 1e-4f)
		{
			continue;
		}
		const float V = (D11 * D20 - D01 * D21) / Denom;
		const float W = (D00 * D21 - D01 * D20) / Denom;
		const float U = 1.0f - V - W;
		constexpr float Eps = -0.001f;
		if (U >= Eps && V >= Eps && W >= Eps)
		{
			const FVector Local = Tri.A * U + Tri.B * V + Tri.C * W;
			OutWorldPosition = GetComponentTransform().TransformPosition(Local);
			const FVector LocalNormal = FVector::CrossProduct(Tri.B - Tri.A, Tri.C - Tri.A).GetSafeNormal();
			OutNormal = GetComponentTransform().TransformVectorNoScale(LocalNormal).GetSafeNormal();
			return true;
		}
	}
	return false;
}

FString UInkBodyComponent::DebugResolveBodyUV(const FVector& WorldPosition)
{
	// 08-25 起這支同時是**等價性儀器**：一次呼叫跑兩條路（全掃參考 vs 空間索引），
	// 印出兩邊的 UV／最近距離與 match 旗標＋各自牆鐘。效能修的唯一驗收形式是
	// 「輸出逐位相同」（追記80 鐵則），所以對照組永遠留在程式裡、不准拆。
	const bool bCache = bTriCacheBuilt || BuildTriCache();
	const FVector Local = GetComponentTransform().InverseTransformPosition(WorldPosition);

	// ① 全掃參考（舊路徑原文）
	const double T0 = FPlatformTime::Seconds();
	float BestDistSq = TNumericLimits<float>::Max();
	FVector2D BestUV = FVector2D::ZeroVector;
	for (const FCachedTri& Tri : CachedTris)
	{
		const FVector Closest = FMath::ClosestPointOnTriangleToPoint(Local, Tri.A, Tri.B, Tri.C);
		const float DistSq = FVector::DistSquared(Closest, Local);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			const FVector Bary = FMath::ComputeBaryCentric2D(Closest, Tri.A, Tri.B, Tri.C);
			BestUV = Tri.UVA * Bary.X + Tri.UVB * Bary.Y + Tri.UVC * Bary.Z;
		}
	}
	const double T1 = FPlatformTime::Seconds();

	// ② 空間索引路徑（建置成本另計，不算進查詢牆鐘）
	if (!bPosGridBuilt)
	{
		BuildPosGrid();
	}
	const double T2 = FPlatformTime::Seconds();
	FVector GClosest = FVector::ZeroVector;
	float GDistSq = TNumericLimits<float>::Max();
	const int32 GTri = FindClosestTriLocal(Local, 1.0e6f, GClosest, GDistSq);
	FVector2D GUV = FVector2D::ZeroVector;
	if (GTri != INDEX_NONE)
	{
		const FCachedTri& Tri = CachedTris[GTri];
		const FVector Bary = FMath::ComputeBaryCentric2D(GClosest, Tri.A, Tri.B, Tri.C);
		GUV = Tri.UVA * Bary.X + Tri.UVB * Bary.Y + Tri.UVC * Bary.Z;
	}
	const double T3 = FPlatformTime::Seconds();

	const bool bMatch = (GTri != INDEX_NONE) && (GDistSq == BestDistSq) &&
		(GUV.X == BestUV.X) && (GUV.Y == BestUV.Y);

	UStaticMesh* SM = GetStaticMesh();
	const int32 NumLods = (SM && SM->GetRenderData()) ? SM->GetRenderData()->LODResources.Num() : -1;
	return FString::Printf(
		TEXT("cache=%d tris=%d lods=%d local=(%.1f,%.1f,%.1f) bestDist=%.2f uv=(%.4f,%.4f) ")
		TEXT("gridUv=(%.4f,%.4f) gridDist=%.2f match=%d scanMs=%.3f gridMs=%.3f buildMs=%.1f ")
		TEXT("cells=%dx%dx%d cell=%.2f items=%d"),
		bCache ? 1 : 0, CachedTris.Num(), NumLods, Local.X, Local.Y, Local.Z,
		CachedTris.Num() > 0 ? FMath::Sqrt(BestDistSq) : -1.0f, BestUV.X, BestUV.Y,
		GUV.X, GUV.Y, GTri != INDEX_NONE ? FMath::Sqrt(GDistSq) : -1.0f, bMatch ? 1 : 0,
		(T1 - T0) * 1000.0, (T3 - T2) * 1000.0, (T2 - T1) * 1000.0,
		PosGridDim[0], PosGridDim[1], PosGridDim[2], PosGridCell, PosGridItems.Num());
}

bool UInkBodyComponent::ResolveBodyUV(const FVector& WorldPosition, FVector2D& OutUV, float MaxDistance)
{
	return ResolveBodyUV(WorldPosition, OutUV, MaxDistance, nullptr);
}

int32 UInkBodyComponent::ClosestTriLinear(const FVector& Local, FVector& OutClosest, float& OutDistSq) const
{
	// 舊路徑原文（含平手規則「嚴格 < ⇒ 最小索引勝」）——等價性對照組＋保底。
	OutDistSq = TNumericLimits<float>::Max();
	int32 BestTri = INDEX_NONE;
	for (int32 T = 0; T < CachedTris.Num(); ++T)
	{
		const FCachedTri& Tri = CachedTris[T];
		const FVector Closest = FMath::ClosestPointOnTriangleToPoint(Local, Tri.A, Tri.B, Tri.C);
		const float DistSq = FVector::DistSquared(Closest, Local);
		if (DistSq < OutDistSq)
		{
			OutDistSq = DistSq;
			BestTri = T;
			OutClosest = Closest;
		}
	}
	return BestTri;
}

bool UInkBodyComponent::ResolveBodyUVImpl(const FVector& Local, float MaxDistance,
	const FVector2D* PreferNearUV, bool bUseGrid, FVector2D& OutUV) const
{
	// 兩條路的**唯一**差別＝候選從哪來（網格 vs 全掃）；挑選規則以下一字不分岔。
	float BestDistSq = TNumericLimits<float>::Max();
	FVector Closest = FVector::ZeroVector;
	const int32 BestTri = bUseGrid
		? FindClosestTriLocal(Local, MaxDistance, Closest, BestDistSq)
		: ClosestTriLinear(Local, Closest, BestDistSq);
	if (BestTri == INDEX_NONE)
	{
		return false; // 網格路徑：容許範圍內確定沒有；全掃路徑：空網格
	}
	const FCachedTri& BTri = CachedTris[BestTri];
	const FVector BBary = FMath::ComputeBaryCentric2D(Closest, BTri.A, BTri.B, BTri.C);
	FVector2D BestUV = BTri.UVA * BBary.X + BTri.UVB * BBary.Y + BTri.UVC * BBary.Z;

	// 命中點離網格太遠視為無效——通常代表打到別的東西
	if (BestDistSq > FMath::Square(MaxDistance))
	{
		return false;
	}

	// 縫區遲滯：距離「真並列」（最近距離＋0.5mm 帶）的候選裡選 UV 離上一點最近的——
	// 縫上兩島的浮點搶點被上一點錨死。帶寬鐵則：只准蓋浮點平手（縫上兩側真等距），
	// 5mm 帶會把平滑區的相鄰三角形全捲進來＝解算黏滑（stick-slip）＝每條筆跡
	// 高頻方波鋸齒（07-20 viewport 實錘、二改 0.5mm）。
	// 08-25：候選集可由空間索引提供（升序索引＝重現全掃走訪順序），規則未動。
	if (PreferNearUV)
	{
		const float Slack = FMath::Sqrt(BestDistSq) + 0.05f;
		const float SlackSq = FMath::Square(Slack);
		float BestUvDistSq = FVector2D::DistSquared(BestUV, *PreferNearUV);
		auto Consider = [&](int32 T)
		{
			const FCachedTri& Tri = CachedTris[T];
			const FVector Cl = FMath::ClosestPointOnTriangleToPoint(Local, Tri.A, Tri.B, Tri.C);
			if (FVector::DistSquared(Cl, Local) > SlackSq)
			{
				return;
			}
			const FVector Bary = FMath::ComputeBaryCentric2D(Cl, Tri.A, Tri.B, Tri.C);
			const FVector2D UV = Tri.UVA * Bary.X + Tri.UVB * Bary.Y + Tri.UVC * Bary.Z;
			const float UvDistSq = FVector2D::DistSquared(UV, *PreferNearUV);
			if (UvDistSq < BestUvDistSq)
			{
				BestUvDistSq = UvDistSq;
				BestUV = UV;
			}
		};
		if (bUseGrid)
		{
			TArray<int32> Near;
			Near.Reserve(64);
			GatherTrisNearLocal(Local, Slack, Near);
			int32 Prev = INDEX_NONE;
			for (const int32 T : Near)
			{
				if (T == Prev)
				{
					continue; // 排序後相鄰的跨格重複（重複無害，跳過純為省算）
				}
				Prev = T;
				Consider(T);
			}
		}
		else
		{
			for (int32 T = 0; T < CachedTris.Num(); ++T)
			{
				Consider(T);
			}
		}
	}

	OutUV = BestUV;
	return true;
}

bool UInkBodyComponent::ResolveBodyUV(const FVector& WorldPosition, FVector2D& OutUV, float MaxDistance,
	const FVector2D* PreferNearUV)
{
	if (!bTriCacheBuilt && !BuildTriCache())
	{
		return false;
	}
	if (!bPosGridBuilt)
	{
		BuildPosGrid(); // 一次性建置；建不起來就保底走全掃（答案不變、只是慢）
	}
	const FVector Local = GetComponentTransform().InverseTransformPosition(WorldPosition);
	return ResolveBodyUVImpl(Local, MaxDistance, PreferNearUV, bPosGridBuilt, OutUV);
}

FString UInkBodyComponent::DebugUvGridBench(int32 NumSamples)
{
	// 樣本＝tri-cache 均勻抽樣、重心沿面法線外推 0.5mm ＝ 作畫時射線命中點的形狀
	//（`ResolveAimToTargetUV` 容差 0.15cm、且恆帶 PreferNearUV＝縫區遲滯）。
	if (!bTriCacheBuilt && !BuildTriCache())
	{
		return TEXT("bench: no tri cache");
	}
	const double B0 = FPlatformTime::Seconds();
	if (!bPosGridBuilt)
	{
		BuildPosGrid();
	}
	const double BuildMs = (FPlatformTime::Seconds() - B0) * 1000.0;
	if (!bPosGridBuilt)
	{
		return TEXT("bench: grid build failed");
	}

	const int32 N = FMath::Clamp(NumSamples, 1, 20000);
	TArray<FVector> Pts;
	TArray<FVector2D> Prevs;
	Pts.Reserve(N);
	Prevs.Reserve(N);
	const int32 Step = FMath::Max(1, CachedTris.Num() / N);
	for (int32 T = 0; T < CachedTris.Num() && Pts.Num() < N; T += Step)
	{
		const FCachedTri& Tri = CachedTris[T];
		const FVector Cen = (Tri.A + Tri.B + Tri.C) / 3.0;
		const FVector Nrm = FVector::CrossProduct(Tri.B - Tri.A, Tri.C - Tri.A).GetSafeNormal();
		Pts.Add(Cen + Nrm * 0.05);                                  // 本地空間，直接餵 Impl
		Prevs.Add((Tri.UVA + Tri.UVB + Tri.UVC) / 3.0f);            // 上一點 UV 的替身
	}

	// 兩條路各跑一輪；逐點比對回傳值與 UV（逐位）
	TArray<FVector2D> RefUV, GridUV;
	TArray<uint8> RefOk, GridOk;
	RefUV.SetNum(Pts.Num()); GridUV.SetNum(Pts.Num());
	RefOk.SetNum(Pts.Num()); GridOk.SetNum(Pts.Num());

	const double R0 = FPlatformTime::Seconds();
	for (int32 I = 0; I < Pts.Num(); ++I)
	{
		FVector2D UV = FVector2D::ZeroVector;
		RefOk[I] = ResolveBodyUVImpl(Pts[I], 0.15f, &Prevs[I], /*bUseGrid=*/false, UV) ? 1 : 0;
		RefUV[I] = UV;
	}
	const double R1 = FPlatformTime::Seconds();
	for (int32 I = 0; I < Pts.Num(); ++I)
	{
		FVector2D UV = FVector2D::ZeroVector;
		GridOk[I] = ResolveBodyUVImpl(Pts[I], 0.15f, &Prevs[I], /*bUseGrid=*/true, UV) ? 1 : 0;
		GridUV[I] = UV;
	}
	const double R2 = FPlatformTime::Seconds();

	int32 Mismatch = 0;
	int32 Hits = 0;
	for (int32 I = 0; I < Pts.Num(); ++I)
	{
		if (RefOk[I]) { ++Hits; }
		const bool bSame = (RefOk[I] == GridOk[I]) &&
			(!RefOk[I] || (RefUV[I].X == GridUV[I].X && RefUV[I].Y == GridUV[I].Y));
		if (!bSame) { ++Mismatch; }
	}

	// 遠距（打空）樣本：舊路徑照樣全掃 204k，新路徑靠 AABB／MaxDistance 早退
	TArray<FVector> Far;
	const FVector Mid = (PosGridMin + PosGridMax) * 0.5;
	const FVector Ext = (PosGridMax - PosGridMin);
	for (int32 I = 0; I < 64; ++I)
	{
		const double A = 2.0 * PI * I / 64.0;
		Far.Add(Mid + FVector(FMath::Cos(A), FMath::Sin(A), 0.0) * (Ext.Size() * 0.75));
	}
	const double F0 = FPlatformTime::Seconds();
	for (const FVector& P : Far) { FVector2D UV; ResolveBodyUVImpl(P, 0.15f, nullptr, false, UV); }
	const double F1 = FPlatformTime::Seconds();
	for (const FVector& P : Far) { FVector2D UV; ResolveBodyUVImpl(P, 0.15f, nullptr, true, UV); }
	const double F2 = FPlatformTime::Seconds();

	const double RefMs = (R1 - R0) * 1000.0 / FMath::Max(1, Pts.Num());
	const double GridMs = (R2 - R1) * 1000.0 / FMath::Max(1, Pts.Num());
	const double FarRefMs = (F1 - F0) * 1000.0 / Far.Num();
	const double FarGridMs = (F2 - F1) * 1000.0 / Far.Num();
	return FString::Printf(
		TEXT("bench tris=%d cells=%dx%dx%d cell=%.2f items=%d buildMs=%.1f n=%d hits=%d mismatch=%d ")
		TEXT("refMs=%.4f gridMs=%.4f speedup=%.1f farRefMs=%.4f farGridMs=%.4f farSpeedup=%.1f"),
		CachedTris.Num(), PosGridDim[0], PosGridDim[1], PosGridDim[2], PosGridCell,
		PosGridItems.Num(), BuildMs, Pts.Num(), Hits, Mismatch,
		RefMs, GridMs, GridMs > 1e-9 ? RefMs / GridMs : -1.0,
		FarRefMs, FarGridMs, FarGridMs > 1e-9 ? FarRefMs / FarGridMs : -1.0);
}
