#include "NeckStretchComponent.h"

#include "NeckSeamData.h"
#include "Components/PoseableMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"

// 0=舊制（NeckSeamData 烘焙表權重）1=身側環權重從渲染緩衝讀（預設；A/B 診斷旋鈕）
// 診斷：1=整條伸縮脖不畫（分辨縫上的破圖是脖子薄片還是殼本身）
static TAutoConsoleVariable<int32> CVarNiNeckStretchOff(
	TEXT("ni.NeckStretchOff"), 0, TEXT("Diagnostic: hide the procedural neck entirely"));
static TAutoConsoleVariable<int32> CVarNiNeckRingFromMesh(
	TEXT("ni.NeckRingFromMesh"), 1,
	TEXT("NeckStretch body ring: 1=skin with mesh render-buffer weights (GPU-identical), 0=baked NeckSeamData table"));

namespace
{
	constexpr int32 GRing = NeckSeamData::RingCount; // 84（cut_seam_head3）

	FVector SeamPos(const NeckSeamData::FSeamVert& V) { return FVector(V.Px, V.Py, V.Pz); }
	FVector SeamNrm(const NeckSeamData::FSeamVert& V) { return FVector(V.Nx, V.Ny, V.Nz); }

	// 邊界環 mini-skin：SkinT[boneIdx] 已是「refCS⁻¹ 再乘當前 CS」的皮膚矩陣
	void SkinRing(const NeckSeamData::FSeamVert* Ring, const TArray<FTransform>& SkinT,
		TArray<FVector>& OutPos, TArray<FVector>& OutNrm)
	{
		OutPos.SetNumUninitialized(GRing);
		OutNrm.SetNumUninitialized(GRing);
		for (int32 k = 0; k < GRing; ++k)
		{
			const NeckSeamData::FSeamVert& V = Ring[k];
			const FVector Rest = SeamPos(V);
			const FVector RestN = SeamNrm(V);
			FVector P = FVector::ZeroVector;
			FVector N = FVector::ZeroVector;
			for (int32 j = 0; j < 4; ++j)
			{
				if (V.W[j] <= 0.0f)
				{
					continue;
				}
				const FTransform& T = SkinT[V.Bone[j]];
				P += V.W[j] * T.TransformPosition(Rest);
				N += V.W[j] * T.TransformVectorNoScale(RestN);
			}
			OutPos[k] = P;
			OutNrm[k] = N.GetSafeNormal();
		}
	}

	FVector NewellNormal(const TArray<FVector>& Ring)
	{
		FVector N = FVector::ZeroVector;
		for (int32 k = 0; k < GRing; ++k)
		{
			const FVector& A = Ring[k];
			const FVector& B = Ring[(k + 1) % GRing];
			N.X += (A.Y - B.Y) * (A.Z + B.Z);
			N.Y += (A.Z - B.Z) * (A.X + B.X);
			N.Z += (A.X - B.X) * (A.Y + B.Y);
		}
		return N.GetSafeNormal();
	}

	float SmoothStep01(float X)
	{
		X = FMath::Clamp(X, 0.0f, 1.0f);
		return X * X * (3.0f - 2.0f * X);
	}
}

bool UNeckStretchComponent::DumpNeckMesh(const FString& Path)
{
	const FProcMeshSection* Sec = GetProcMeshSection(0);
	if (!Sec)
	{
		return false;
	}
	const int32 Rows = Sec->ProcVertexBuffer.Num() / NeckSeamData::RingCount;
	TArray<FString> L;
	L.Add(TEXT("row,col,px,py,pz,nx,ny,nz"));
	for (int32 i = 0; i < Sec->ProcVertexBuffer.Num(); ++i)
	{
		const FProcMeshVertex& V = Sec->ProcVertexBuffer[i];
		L.Add(FString::Printf(TEXT("%d,%d,%.4f,%.4f,%.4f,%.5f,%.5f,%.5f"),
			i / NeckSeamData::RingCount, i % NeckSeamData::RingCount,
			V.Position.X, V.Position.Y, V.Position.Z,
			V.Normal.X, V.Normal.Y, V.Normal.Z));
	}
	return FFileHelper::SaveStringArrayToFile(L, *Path);
}

FString UNeckStretchComponent::GetDebugSummary() const
{
	const FTransform WT = GetComponentTransform();
	const FVector BcW = WT.TransformPosition(DbgBc);
	const FVector HcW = WT.TransformPosition(DbgHc);
	const FBoxSphereBounds B = Bounds;
	return FString::Printf(
		TEXT("ready=%d vis=%d chord=%.1f meshw=%d tableErr=%.3f resampSkew=%.2f BcCS=(%.0f,%.0f,%.0f) HcCS=(%.0f,%.0f,%.0f) "
			 "BcW=(%.0f,%.0f,%.0f) HcW=(%.0f,%.0f,%.0f) compW=(%.0f,%.0f,%.0f) "
			 "boundsW=(%.0f,%.0f,%.0f) ext=(%.0f,%.0f,%.0f)"),
		bReady ? 1 : 0, IsVisible() ? 1 : 0, DbgChord, MeshWeightsMatched, DbgTableErrMax, DbgDirectVsResamp,
		DbgBc.X, DbgBc.Y, DbgBc.Z, DbgHc.X, DbgHc.Y, DbgHc.Z,
		BcW.X, BcW.Y, BcW.Z, HcW.X, HcW.Y, HcW.Z,
		WT.GetLocation().X, WT.GetLocation().Y, WT.GetLocation().Z,
		B.Origin.X, B.Origin.Y, B.Origin.Z, B.BoxExtent.X, B.BoxExtent.Y, B.BoxExtent.Z);
}

UNeckStretchComponent::UNeckStretchComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false; // 由角色在擺骨後顯式呼叫 UpdateNeck
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	CastShadow = false;         // fullbright 世界無投影
	bUseAsyncCooking = false;   // 無碰撞可煮
	SetVisibility(false);
}

void UNeckStretchComponent::InitFromSource(UPoseableMeshComponent* InSource, UMaterialInterface* BodyMaterial)
{
	Source = InSource;
	bReady = false;
	if (!Source || !Source->GetSkinnedAsset())
	{
		return;
	}

	// ref pose CS 反矩陣（資產常數，一次快取）
	const FReferenceSkeleton& Ref = Source->GetSkinnedAsset()->GetRefSkeleton();
	const TArray<FTransform>& Local = Ref.GetRefBonePose();
	SkinBoneNames.Reset();
	RefInvCS.Reset();
	for (int32 i = 0; i < NeckSeamData::BoneNameCount; ++i)
	{
		const FName BoneName(NeckSeamData::BoneNames[i]);
		int32 Idx = Ref.FindBoneIndex(BoneName);
		if (Idx == INDEX_NONE)
		{
			UE_LOG(LogTemp, Error, TEXT("NeckStretch: bone %s missing"), *BoneName.ToString());
			return;
		}
		FTransform CS = Local[Idx];
		for (int32 P = Ref.GetParentIndex(Idx); P != INDEX_NONE; P = Ref.GetParentIndex(P))
		{
			CS = CS * Local[P];
		}
		SkinBoneNames.Add(BoneName);
		RefInvCS.Add(CS.Inverse());
	}
	// 全骨 ref pose CS 反矩陣（真皮膚路徑用；影響骨集合由資產決定、不限六骨）
	RefInvCSAll.Reset();
	RefInvCSAll.SetNum(Ref.GetNum());
	for (int32 i = 0; i < Ref.GetNum(); ++i)
	{
		FTransform CS = Local[i];
		for (int32 P = Ref.GetParentIndex(i); P != INDEX_NONE; P = Ref.GetParentIndex(P))
		{
			CS = CS * Local[P];
		}
		RefInvCSAll[i] = CS.Inverse();
	}
	BindRingToMeshWeights();

	// 端色（FColor：rgb=linear/2 編碼、a=hair 支路強度；shader 端 ×2 解碼）
	auto ToColor = [](const NeckSeamData::FSeamVert& V)
	{
		return FColor(
			static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(V.R * 255.0f), 0, 255)),
			static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(V.G * 255.0f), 0, 255)),
			static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(V.B * 255.0f), 0, 255)),
			static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(V.HairW * 255.0f), 0, 255)));
	};
	BodyRingColor.SetNumUninitialized(GRing);
	HeadRingColor.SetNumUninitialized(GRing);
	for (int32 k = 0; k < GRing; ++k)
	{
		BodyRingColor[k] = ToColor(NeckSeamData::BodyRing[k]);
		HeadRingColor[k] = ToColor(NeckSeamData::HeadRing[k]);
	}

	// 頭側環 rest 參數化：環自身平面（Newell 法線）＋平面內角度表
	HeadBoneSlot = SkinBoneNames.IndexOfByKey(FName(TEXT("Head")));
	check(HeadBoneSlot != INDEX_NONE);
	HeadC0 = FVector::ZeroVector;
	for (int32 k = 0; k < GRing; ++k)
	{
		HeadC0 += SeamPos(NeckSeamData::HeadRing[k]);
	}
	HeadC0 /= GRing;
	{
		FVector N = FVector::ZeroVector;
		for (int32 k = 0; k < GRing; ++k)
		{
			const FVector A = SeamPos(NeckSeamData::HeadRing[k]);
			const FVector B = SeamPos(NeckSeamData::HeadRing[(k + 1) % GRing]);
			N.X += (A.Y - B.Y) * (A.Z + B.Z);
			N.Y += (A.Z - B.Z) * (A.X + B.X);
			N.Z += (A.X - B.X) * (A.Y + B.Y);
		}
		HeadN0 = N.GetSafeNormal();
	}
	HeadE1 = FVector::VectorPlaneProject(
		SeamPos(NeckSeamData::HeadRing[0]) - HeadC0, HeadN0).GetSafeNormal();
	HeadE2 = FVector::CrossProduct(HeadN0, HeadE1).GetSafeNormal();
	HeadRingRadius = 0.0f;
	for (int32 k = 0; k < GRing; ++k)
	{
		HeadRingRadius += static_cast<float>(FVector::Dist(SeamPos(NeckSeamData::HeadRing[k]), HeadC0));
	}
	HeadRingRadius /= GRing; // 隱藏判定的縫寬估計用（rest 環半徑 ≈17.8cm）
	// 環向形狀曲線的座標基準：頭環 rest 平面內的臉方向（CS +Y 投影）——頭部錨定
	HeadFaceInPlane = FVector::VectorPlaneProject(FVector::YAxisVector, HeadN0).GetSafeNormal();
	HeadAng0.SetNumUninitialized(GRing);
	for (int32 k = 0; k < GRing; ++k)
	{
		const FVector V = SeamPos(NeckSeamData::HeadRing[k]) - HeadC0;
		HeadAng0[k] = FMath::Atan2(FVector::DotProduct(V, HeadE2), FVector::DotProduct(V, HeadE1));
	}
	{
		float Sweep = 0.0f;
		for (int32 k = 0; k < GRing; ++k)
		{
			Sweep += FMath::FindDeltaAngleRadians(HeadAng0[k], HeadAng0[(k + 1) % GRing]);
		}
		bHeadAngAscending = Sweep > 0.0f;
	}

	// 材質：M_NeckStretch 的 MID＋整組參數抄身體 MID（SkinTone/頭燈/亮度同步）
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/Characters/M_NeckStretch.M_NeckStretch")))
	{
		NeckMid = UMaterialInstanceDynamic::Create(Base, this);
		BodyMidRef = Cast<UMaterialInstanceDynamic>(BodyMaterial);
		if (NeckMid && BodyMaterial)
		{
			NeckMid->CopyMaterialUniformParameters(BodyMaterial);
			if (BodyMidRef)
			{
				BodyMidRef->GetVectorParameterValue(FMaterialParameterInfo(TEXT("SkinTone")), LastSyncedTone);
			}
		}
		SetMaterial(0, NeckMid);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("NeckStretch: M_NeckStretch missing - neck renders default material"));
	}

	bSectionCreated = false;
	LastHeadCenter = FVector(FLT_MAX);
	bReady = true;
}

void UNeckStretchComponent::ForceRebuild()
{
	LastHeadCenter = FVector(FLT_MAX);
}

void UNeckStretchComponent::SetBodyMaterialRef(UMaterialInstanceDynamic* BodyMid)
{
	if (BodyMid && BodyMid != BodyMidRef)
	{
		BodyMidRef = BodyMid;
		LastSyncedTone = FLinearColor::Black; // 強制下一 UpdateNeck 整組重抄
	}
}

void UNeckStretchComponent::BindRingToMeshWeights()
{
	BodyRingInfl.Reset();
	BodyRingVertIdx.Reset();
	MeshWeightsMatched = 0;
	BodyRingInfl.SetNum(GRing);
	BodyRingVertIdx.Init(-1, GRing);
	BodyRestN.SetNum(GRing);
	HeadRestN.SetNum(GRing);
	for (int32 k = 0; k < GRing; ++k)
	{
		BodyRestN[k] = SeamNrm(NeckSeamData::BodyRing[k]);
		HeadRestN[k] = SeamNrm(NeckSeamData::HeadRing[k]);
	}
	const FSkeletalMeshRenderData* RD = Source ? Source->GetSkeletalMeshRenderData() : nullptr;
	if (!RD || RD->LODRenderData.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("NeckStretch: no render data - body ring falls back to baked table"));
		return;
	}
	const FSkeletalMeshLODRenderData& LOD = RD->LODRenderData[0];
	const FPositionVertexBuffer& PB = LOD.StaticVertexBuffers.PositionVertexBuffer;
	const FSkinWeightVertexBuffer* SW = LOD.GetSkinWeightVertexBuffer();
	if (!SW || PB.GetNumVertices() == 0 || PB.GetVertexData() == nullptr || !SW->GetNeedsCPUAccess())
	{
		UE_LOG(LogTemp, Warning, TEXT("NeckStretch: render buffers not CPU-accessible (pos=%u sw=%d) - baked table"),
			PB.GetNumVertices(), SW ? (int32)SW->GetNeedsCPUAccess() : -1);
		return;
	}
	const FReferenceSkeleton& Ref = Source->GetSkinnedAsset()->GetRefSkeleton();
	const int32 HeadIdx = Ref.FindBoneIndex(FName(TEXT("Head")));
	const uint32 NumV = PB.GetNumVertices();
	const uint32 MaxInfl = SW->GetMaxBoneInfluences();
	// 診斷：切縫兩側同位頂點的頂點法線夾角（>0＝著色在縫上跳階＝可見鋸齒線的候選真兇）
	TArray<FVector> HeadSideN, BodySideN;
	HeadSideN.Init(FVector::ZeroVector, GRing);
	BodySideN.Init(FVector::ZeroVector, GRing);
	const FStaticMeshVertexBuffer& SVB = LOD.StaticVertexBuffers.StaticMeshVertexBuffer;
	const bool bHasNrm = SVB.GetNumVertices() == NumV && SVB.GetTangentData() != nullptr;
	// 切縫兩側頂點同位：身側＝Head 權重 <0.5 的那個
	for (uint32 v = 0; v < NumV; ++v)
	{
		const FVector P(PB.VertexPosition(v));
		for (int32 k = 0; k < GRing; ++k)
		{
			if (!P.Equals(SeamPos(NeckSeamData::BodyRing[k]), 0.02))
			{
				continue;
			}
			int32 SecIdx = 0, LocalV = 0;
			LOD.GetSectionFromVertexIndex(static_cast<int32>(v), SecIdx, LocalV);
			const FSkelMeshRenderSection& Sec = LOD.RenderSections[SecIdx];
			TArray<FRingInfl> Infl;
			float HeadW = 0.0f, Sum = 0.0f;
			for (uint32 i = 0; i < MaxInfl; ++i)
			{
				const float W = SW->GetBoneWeight(v, i) / 65535.0f;
				if (W <= 0.0f)
				{
					continue;
				}
				const int32 Local = static_cast<int32>(SW->GetBoneIndex(v, i));
				const int32 Bone = Sec.BoneMap.IsValidIndex(Local) ? static_cast<int32>(Sec.BoneMap[Local]) : INDEX_NONE;
				if (Bone == INDEX_NONE)
				{
					continue;
				}
				if (Bone == HeadIdx)
				{
					HeadW += W;
				}
				Infl.Add({ Bone, W });
				Sum += W;
			}
			if (bHasNrm)
			{
				{ const FVector4f Nz = SVB.VertexTangentZ(v); (HeadW >= 0.5f ? HeadSideN : BodySideN)[k] = FVector(Nz.X, Nz.Y, Nz.Z); }
			}
			if (Sum <= 0.0f || HeadW >= 0.5f)
			{
				continue; // 頭側同位頂點（或空權重）
			}
			for (FRingInfl& I : Infl)
			{
				I.W /= Sum; // 量化殘差歸一
			}
			if (BodyRingVertIdx[k] < 0)
			{
				BodyRingVertIdx[k] = static_cast<int32>(v);
				BodyRingInfl[k] = MoveTemp(Infl);
				++MeshWeightsMatched;
			}
		}
	}
	BodyRestN.SetNum(GRing);
	HeadRestN.SetNum(GRing);
	for (int32 k = 0; k < GRing; ++k)
	{
		BodyRestN[k] = BodySideN[k].IsNearlyZero() ? SeamNrm(NeckSeamData::BodyRing[k]) : BodySideN[k].GetSafeNormal();
		HeadRestN[k] = HeadSideN[k].IsNearlyZero() ? SeamNrm(NeckSeamData::HeadRing[k]) : HeadSideN[k].GetSafeNormal();
	}
	float NrmMaxDeg = 0.0f, NrmMeanDeg = 0.0f, TblMaxDeg = 0.0f;
	int32 NrmPairs = 0;
	for (int32 k = 0; k < GRing; ++k)
	{
		if (!HeadSideN[k].IsNearlyZero() && !BodySideN[k].IsNearlyZero())
		{
			const float Deg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
				static_cast<float>(FVector::DotProduct(HeadSideN[k].GetSafeNormal(), BodySideN[k].GetSafeNormal())), -1.0f, 1.0f)));
			NrmMaxDeg = FMath::Max(NrmMaxDeg, Deg);
			NrmMeanDeg += Deg;
			++NrmPairs;
			const float TDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
				static_cast<float>(FVector::DotProduct(BodySideN[k].GetSafeNormal(), SeamNrm(NeckSeamData::BodyRing[k]).GetSafeNormal())), -1.0f, 1.0f)));
			TblMaxDeg = FMath::Max(TblMaxDeg, TDeg);
		}
	}
	if (NrmPairs > 0)
	{
		NrmMeanDeg /= NrmPairs;
	}
	UE_LOG(LogTemp, Log, TEXT("NeckStretch: body ring bound to mesh weights %d/%d (verts=%u maxInfl=%u) seamNormalGap pairs=%d max=%.1fdeg mean=%.1fdeg tableVsBody max=%.1fdeg on %s"),
		MeshWeightsMatched, GRing, NumV, MaxInfl, NrmPairs, NrmMaxDeg, NrmMeanDeg, TblMaxDeg, *GetNameSafe(GetOwner()));
}

void UNeckStretchComponent::SkinBodyRingFromMesh(TArray<FVector>& OutPos, TArray<FVector>& OutNrm,
	const TArray<FTransform>& SkinT) const
{
	// SkinT＝六骨舊表路徑的皮膚矩陣（未配到的頂點退回舊表）；配到的走全骨真權重
	const TArray<FTransform>& CS = Source->GetComponentSpaceTransforms();
	OutPos.SetNumUninitialized(GRing);
	OutNrm.SetNumUninitialized(GRing);
	for (int32 k = 0; k < GRing; ++k)
	{
		const NeckSeamData::FSeamVert& V = NeckSeamData::BodyRing[k];
		const FVector Rest = SeamPos(V);
		const FVector RestN = BodyRestN.IsValidIndex(k) ? BodyRestN[k] : SeamNrm(V);
		FVector P = FVector::ZeroVector, N = FVector::ZeroVector;
		if (BodyRingVertIdx[k] >= 0 && CS.Num() == RefInvCSAll.Num())
		{
			for (const FRingInfl& I : BodyRingInfl[k])
			{
				const FTransform T = RefInvCSAll[I.Bone] * CS[I.Bone];
				P += I.W * T.TransformPosition(Rest);
				N += I.W * T.TransformVectorNoScale(RestN);
			}
		}
		else
		{
			for (int32 j = 0; j < 4; ++j)
			{
				if (V.W[j] <= 0.0f)
				{
					continue;
				}
				const FTransform& T = SkinT[V.Bone[j]];
				P += V.W[j] * T.TransformPosition(Rest);
				N += V.W[j] * T.TransformVectorNoScale(RestN);
			}
		}
		OutPos[k] = P;
		OutNrm[k] = N.GetSafeNormal();
	}
}

void UNeckStretchComponent::UpdateNeck()
{
	if (!bReady || !Source)
	{
		return;
	}
	// 膚色同步（08-15 user 抓「脖子膚色不對」）：InitFromSource 只抄一次身體 MID——
	// 自訂臉的 SkinTone 是進房後才寫進身體 MID（ApplyCustomAvatar/ApplySkinToneOnly）
	// ＝脖子停在預設/名冊膚色。每 tick 比對 SkinTone（一個 vector 讀取＝廉價），變了
	// 整組 uniform 重抄（頭燈/亮度/去飽和一併跟）。
	if (NeckMid && BodyMidRef)
	{
		FLinearColor Tone;
		if (BodyMidRef->GetVectorParameterValue(FMaterialParameterInfo(TEXT("SkinTone")), Tone) &&
			!Tone.Equals(LastSyncedTone, 1e-4f))
		{
			NeckMid->CopyMaterialUniformParameters(BodyMidRef);
			LastSyncedTone = Tone;
			UE_LOG(LogTemp, Log, TEXT("NeckStretch: skin tone synced (%.3f,%.3f,%.3f) on %s"),
				Tone.R, Tone.G, Tone.B, *GetNameSafe(GetOwner()));
		}
	}
	if (!bNeckStretchEnabled || CVarNiNeckStretchOff.GetValueOnGameThread() != 0)
	{
		if (IsVisible())
		{
			SetVisibility(false);
			LastHeadCenter = FVector(FLT_MAX);
		}
		return;
	}
	if (!Source->IsVisible())
	{
		SetVisibility(false);
		LastHeadCenter = FVector(FLT_MAX); // 下次現身必重建
		return;
	}

	// 皮膚矩陣
	TArray<FTransform> SkinT;
	SkinT.SetNum(SkinBoneNames.Num());
	for (int32 i = 0; i < SkinBoneNames.Num(); ++i)
	{
		SkinT[i] = RefInvCS[i] *
			Source->GetBoneTransformByName(SkinBoneNames[i], EBoneSpaces::ComponentSpace);
	}

	TArray<FVector> BP, BN;
	const bool bFromMesh = CVarNiNeckRingFromMesh.GetValueOnGameThread() != 0 && MeshWeightsMatched > 0;
	if (bFromMesh)
	{
		SkinBodyRingFromMesh(BP, BN, SkinT);
		// 診斷：舊烘焙表與真權重的環點差（站立/作畫時 >0 就是舊制縫隙的來源之一）
		TArray<FVector> TP, TN;
		SkinRing(NeckSeamData::BodyRing, SkinT, TP, TN);
		float Err = 0.0f;
		for (int32 k = 0; k < GRing; ++k)
		{
			Err = FMath::Max(Err, static_cast<float>(FVector::Dist(TP[k], BP[k])));
		}
		DbgTableErrMax = Err;
	}
	else
	{
		SkinRing(NeckSeamData::BodyRing, SkinT, BP, BN);
	}
	// 頭側環＝剛體（Head=1.0 硬權重）：整環走 Head 皮膚矩陣、重取樣走 rest 參數表
	const FTransform& HeadT = SkinT[HeadBoneSlot];

	FVector Bc = FVector::ZeroVector;
	for (int32 k = 0; k < GRing; ++k)
	{
		Bc += BP[k];
	}
	Bc /= GRing;
	const FVector Hc = HeadT.TransformPosition(HeadC0);

	// 姿勢沒動＝跳過（含 GPU 上傳）——頭與身側環都沒動才跳（舊制只看頭：肚骨/肩骨
	// 把身側環帶走而頭釘住時，脖子留在原地＝縫）
	const FQuat HeadQ = HeadT.GetRotation();
	bool bBodyMoved = LastBP.Num() != GRing;
	for (int32 k = 0; !bBodyMoved && k < GRing; ++k)
	{
		bBodyMoved = !LastBP[k].Equals(BP[k], 0.005);
	}
	if (!bBodyMoved && LastHeadCenter.Equals(Hc, 0.005) && LastHeadQ.Equals(HeadQ, 1e-5f) && IsVisible())
	{
		return;
	}
	LastHeadCenter = Hc;
	LastHeadQ = HeadQ;
	LastBP = BP;
	// 本人永不看見自己的脖子（人看不到自己的脖子；相機在眉心＝管面只可能糊鏡頭——
	// A/B 截圖實錘 φ=45 滿屏皮膚的真兇）。他端照常看到完整伸長脖＝訊號不減。
	SetOwnerNoSee(true);

	DbgBc = Bc;
	DbgHc = Hc;
	DbgChord = static_cast<float>(FVector::Dist(Bc, Hc));
	const float ChordCm = DbgChord;

	const FVector NB = NewellNormal(BP);
	const FVector NH = HeadT.TransformVectorNoScale(HeadN0).GetSafeNormal();

	// 隱藏判定＝「縫真正閉合」（07-26 破洞修）：舊制只量環心距——埋頭是「原地旋轉」，
	// 環心距 ~1.5cm 但後頸已張開 10cm 楔縫，一過門檻整件被隱藏＝後頸開天窗
	//（user viewport 實錘）。最壞縫寬估計＝環心距＋sin(彎角)×環半徑（旋轉把縫寬
	// 攤在環緣）；安座（彎角≈0）時退化回舊判定。
	const float BendSin = FVector::CrossProduct(NB, NH).Size();
	const float WorstGapCm = ChordCm + BendSin * HeadRingRadius;
	if (WorstGapCm < NeckHideChordCm)
	{
		SetVisibility(false); // 頭安座＝脖子收合、物理上不存在
		return;
	}
	SetVisibility(true);

	// --- 中線 Hermite（端切向＝環 Newell 法線；兩環同繞向 → 兩端 Newell 都指向頭側）---
	const FVector T0v = NB * ChordCm * NeckTangentK;
	const FVector T1v = NH * ChordCm * NeckTangentK;
	const int32 Rows = FMath::Clamp(NeckRows, 6, 32);

	auto CurveP = [&](float T)
	{
		const float T2 = T * T, T3 = T2 * T;
		return (2 * T3 - 3 * T2 + 1) * Bc + (T3 - 2 * T2 + T) * T0v +
			(-2 * T3 + 3 * T2) * Hc + (T3 - T2) * T1v;
	};

	// --- 行框架＝兩個真實環框架的球面插值（2026-07-16 平滑度調查定案）---
	// 前版用中線 RMF：環面 ~100° 的轉角被端切向擠進兩端幾公分＝彎曲半徑 << 管半徑
	// ＝環緣反折（robo 傾印實錘折角最大 150°、「被切很多段」的真兇）。
	// slerp 均攤＝每行 ~7.7°、環緣掃移 ≈ 行距＝臨界不摺；殘餘由單調鉗位收成密貼皺褶。
	// 扭轉仍為零：QRel=FindBetweenNormals 最小旋轉（無 roll 分量），頭端列對應由
	// rest 平面重取樣按方向映射。frame0 平面=身環平面、frameE 平面=頭環平面
	// ＝端排局部座標零出面分量（銜接更嚴格）。
	TArray<FVector> CP, CT, CU, CW;
	CP.SetNumUninitialized(Rows);
	CT.SetNumUninitialized(Rows);
	CU.SetNumUninitialized(Rows);
	CW.SetNumUninitialized(Rows);
	FVector U0 = (BP[0] - Bc) - FVector::DotProduct(BP[0] - Bc, NB) * NB;
	if (!U0.Normalize())
	{
		U0 = FVector::VectorPlaneProject(FVector::UpVector, NB).GetSafeNormal();
	}
	const FVector W0 = FVector::CrossProduct(NB, U0).GetSafeNormal();
	const FQuat QRel = FQuat::FindBetweenNormals(NB, NH);

	// 深彎自適應收細已停用（user 2026-07-16 改定「中間不要往內縮」）：v8 環間淨空
	// 抬升表把深彎弧長拉到 ≥ 彎角×管半徑（摺疊判據成立），細腰不再是必需品——
	// 傾印量測回驗。彎量仍按 1/taper 分配（taper=1 時退化為均攤）。
	const float EffTaperMid = NeckTaperMid;
	auto TaperAt = [&](float T)
	{
		return 1.0f - (1.0f - EffTaperMid) * FMath::Square(FMath::Sin(PI * T));
	};
	TArray<float> SFrac;
	SFrac.SetNumUninitialized(Rows);
	{
		float Acc = 0.0f;
		SFrac[0] = 0.0f;
		for (int32 j = 1; j < Rows; ++j)
		{
			Acc += 1.0f / TaperAt((static_cast<float>(j) - 0.5f) / (Rows - 1));
			SFrac[j] = Acc;
		}
		for (int32 j = 1; j < Rows; ++j)
		{
			SFrac[j] /= Acc;
		}
	}
	for (int32 j = 0; j < Rows; ++j)
	{
		const float T = static_cast<float>(j) / (Rows - 1);
		CP[j] = CurveP(T);
		const FQuat A = FQuat::Slerp(FQuat::Identity, QRel, SFrac[j]);
		CT[j] = A.RotateVector(NB);
		CU[j] = A.RotateVector(U0);
		CW[j] = A.RotateVector(W0);
	}

	// --- 身側輪廓（frame0 局部座標）＋列角座標 ---
	TArray<FVector> BodyLocal;
	TArray<float> ColAng;
	BodyLocal.SetNumUninitialized(GRing);
	ColAng.SetNumUninitialized(GRing);
	for (int32 k = 0; k < GRing; ++k)
	{
		const FVector V = BP[k] - Bc;
		BodyLocal[k] = FVector(FVector::DotProduct(V, CU[0]), FVector::DotProduct(V, CW[0]),
			FVector::DotProduct(V, CT[0]));
		ColAng[k] = FMath::Atan2(BodyLocal[k].Y, BodyLocal[k].X);
	}

	// --- 頭側環重取樣（零剪切對應：扭轉在構造上不存在）---
	// 傾斜端框直接取角會摺疊（robo 實錘摺痕）：正解＝RMF 端框方向 → 頭 rest 空間 →
	// 投影到環自身平面取角（星形＝單調保證、每方向恰一段包夾），rest 表插值後剛體變換。
	// RMF 傳輸保角＝列角座標在兩端同義。
	const int32 E = Rows - 1;
	const FQuat HeadQInv = HeadQ.Inverse();
	auto ResampleHead = [&](float Angle, FVector& OutPos, FVector& OutNrm, FLinearColor& OutCol,
		float& OutFrontFactor)
	{
		const FVector DirRMF = CU[E] * FMath::Cos(Angle) + CW[E] * FMath::Sin(Angle);
		FVector DP = FVector::VectorPlaneProject(HeadQInv.RotateVector(DirRMF), HeadN0);
		if (!DP.Normalize())
		{
			DP = HeadE1; // 退化保底（端切向⊥環平面時不會發生——切向≈環法線）
		}
		// 環向形狀係數（頭部錨定）：+1=下巴側、−1=後頸側——跟著頭的朝向走（user 定案）
		OutFrontFactor = static_cast<float>(FVector::DotProduct(DP, HeadFaceInPlane));
		const float G = FMath::Atan2(FVector::DotProduct(DP, HeadE2), FVector::DotProduct(DP, HeadE1));
		for (int32 m = 0; m < GRing; ++m)
		{
			const int32 M2 = (m + 1) % GRing;
			const float Span = FMath::FindDeltaAngleRadians(HeadAng0[m], HeadAng0[M2]);
			const float Off = FMath::FindDeltaAngleRadians(HeadAng0[m], G);
			if (FMath::Abs(Span) > 1e-6f && Off * Span >= 0.0f && FMath::Abs(Off) <= FMath::Abs(Span))
			{
				const float F = Off / Span;
				const FVector RestP = FMath::Lerp(SeamPos(NeckSeamData::HeadRing[m]),
					SeamPos(NeckSeamData::HeadRing[M2]), F);
				const FVector RestN = FMath::Lerp(HeadRestN.IsValidIndex(m) ? HeadRestN[m] : SeamNrm(NeckSeamData::HeadRing[m]),
					HeadRestN.IsValidIndex(M2) ? HeadRestN[M2] : SeamNrm(NeckSeamData::HeadRing[M2]), F);
				OutPos = HeadT.TransformPosition(RestP);
				OutNrm = HeadT.TransformVectorNoScale(RestN).GetSafeNormal();
				// ReinterpretAsLinear＝純 /255（FLinearColor(FColor) 是 sRGB 解碼——毀編碼，勿用）
				OutCol = FMath::Lerp(HeadRingColor[m].ReinterpretAsLinear(),
					HeadRingColor[M2].ReinterpretAsLinear(), F);
				return;
			}
		}
		OutPos = HeadT.TransformPosition(SeamPos(NeckSeamData::HeadRing[0]));
		OutNrm = HeadT.TransformVectorNoScale(HeadRestN.IsValidIndex(0) ? HeadRestN[0] : SeamNrm(NeckSeamData::HeadRing[0])).GetSafeNormal();
		OutCol = HeadRingColor[0].ReinterpretAsLinear();
		OutFrontFactor = 1.0f;
	};

	// 壓縮域＝直紋面（2026-07-26 作畫姿勢脖子戰役）：管面解算器的設計域是「伸長」
	//（弦長 20~46cm 的轆轤首）；lean-lock 埋頭的脖子是「壓縮＋彎折」（弦長 ~1.6cm、
	// 彎 25~30°）——實測 40/84 列的頭端點穿到身環平面下 5~7cm＝無平滑管面解，
	// 逐列折角最壞 111°（喉嚨褶皺真兇）；後頸側 10cm 楔縫再被 NapeBulge 中段膨脹
	// 加料（後頸凸起真兇）。壓縮域正解＝逐列直線帶（BP[k]→頭環對應點）：折角構造上
	// 為零、互穿列沉進殼內不外翻、裝飾曲線不參與；弦長 4→12cm smoothstep 交叉回管面
	//（沉睡平台 46cm＝純管面，路徑零改動）。
	const float Compress = SmoothStep01((ChordCm - 4.0f) / 8.0f);

	TArray<FVector> HeadResPos, HeadResNrm, HeadLocal;
	TArray<FLinearColor> HeadResCol;
	TArray<float> ColFront; // 每列環向形狀係數（頭部錨定，每幀隨頭向重算）
	HeadResPos.SetNumUninitialized(GRing);
	HeadResNrm.SetNumUninitialized(GRing);
	HeadResCol.SetNumUninitialized(GRing);
	HeadLocal.SetNumUninitialized(GRing);
	ColFront.SetNumUninitialized(GRing);
	// 壓縮域列對應＝切縫孿生頂點 k→k（08-16 user「站姿小彎角也破圖」修）：角度重取樣是
	// 長管的零剪切對應，但在弦長 mm~cm 的楔縫上它把列端點沿頭殼邊界折線滑開＝列傾斜/
	// 折疊＋T 接點＝針孔。孿生對應（HeadRing[k] rest 位置≡BodyRing[k]）＝補丁恰是切縫
	// 本身張開的那塊面：兩端頂點與殼面頂點逐位相同、零扭轉、無 T 接點。弦長 4→12cm
	// 隨 Compress 交叉回角度重取樣（長管照舊）。
	float DirectVsResampMax = 0.0f;
	for (int32 k = 0; k < GRing; ++k)
	{
		FVector RP, RN;
		FLinearColor RC;
		ResampleHead(ColAng[k], RP, RN, RC, ColFront[k]);
		const FVector DP = HeadT.TransformPosition(SeamPos(NeckSeamData::HeadRing[k]));
		const FVector DN = HeadT.TransformVectorNoScale(
			HeadRestN.IsValidIndex(k) ? HeadRestN[k] : SeamNrm(NeckSeamData::HeadRing[k])).GetSafeNormal();
		const FLinearColor DC = HeadRingColor[k].ReinterpretAsLinear();
		DirectVsResampMax = FMath::Max(DirectVsResampMax, static_cast<float>(FVector::Dist(DP, RP)));
		HeadResPos[k] = FMath::Lerp(DP, RP, Compress);
		HeadResNrm[k] = FMath::Lerp(DN, RN, Compress).GetSafeNormal();
		HeadResCol[k] = FMath::Lerp(DC, RC, Compress);
		const FVector V = HeadResPos[k] - Hc;
		HeadLocal[k] = FVector(FVector::DotProduct(V, CU[E]), FVector::DotProduct(V, CW[E]),
			FVector::DotProduct(V, CT[E]));
	}
	DbgDirectVsResamp = DirectVsResampMax;

	// --- 頂點網格 ---
	const int32 NumV = Rows * GRing;
	TArray<FVector> Verts;
	TArray<FVector> Normals;
	TArray<FVector2D> UV0;
	TArray<FColor> Cols;
	Verts.SetNumUninitialized(NumV);
	Normals.SetNumUninitialized(NumV);
	UV0.SetNumUninitialized(NumV);
	Cols.SetNumUninitialized(NumV);

	for (int32 j = 0; j < Rows; ++j)
	{
		const float T = static_cast<float>(j) / (Rows - 1);
		const float Blend = SmoothStep01(T);
		const float Taper = TaperAt(T);
		const float MidEnv = FMath::Square(FMath::Sin(PI * T)); // 環向形狀的沿長包絡（端排=0）
		for (int32 k = 0; k < GRing; ++k)
		{
			const int32 Idx = j * GRing + k;
			if (j == 0)
			{
				Verts[Idx] = BP[k]; // 端排＝邊界環原點原樣（視覺水密）
			}
			else if (j == E)
			{
				Verts[Idx] = HeadResPos[k];
			}
			else
			{
				// 環向形狀曲線：下巴側（f→+1）收 NeckChinTuck、後頸側（f→−1）凸
				// NeckNapeBulge、兩側 1.0——smoothstep 沿環平滑過渡（頭部錨定）
				const float F = ColFront[k];
				const float SCol = 1.0f
					+ (NeckNapeBulge - 1.0f) * SmoothStep01(-F)
					+ (NeckChinTuck - 1.0f) * SmoothStep01(F);
				const float Scale = Taper * (1.0f + (SCol - 1.0f) * MidEnv);
				const FVector L = FMath::Lerp(BodyLocal[k], HeadLocal[k], Blend) * Scale;
				const FVector Tube = CP[j] + CU[j] * L.X + CW[j] * L.Y + CT[j] * L.Z;
				// 壓縮域直紋帶：端點對應沿用零扭轉重取樣＝列身分同一套
				const FVector Ruled = FMath::Lerp(BP[k], HeadResPos[k], Blend);
				Verts[Idx] = FMath::Lerp(Ruled, Tube, Compress);
			}
			UV0[Idx] = FVector2D(static_cast<float>(k) / GRing, T);
			// 端色沿長逐列 lerp（08-15 質感修：舊制 35% 端帶後淡到 Neutral＝管中段一條
			// 平膚色帶＝「另一塊材料」讀感真兇；兩端色都是該列在身體 chroma 場的實采，
			// 沿長插值＝整管帶著當地血色、與身體同源）
			// 環向 3-tap 平滑（同法線：逐頂點 chroma 采樣的列間差在頭燈下也讀成直紋）
			auto BodyC = [&](int32 kk) { return BodyRingColor[(kk + GRing) % GRing].ReinterpretAsLinear(); };
			auto HeadC = [&](int32 kk) { return HeadResCol[(kk + GRing) % GRing]; };
			const FLinearColor BodySm = BodyC(k - 1) * 0.25f + BodyC(k) * 0.5f + BodyC(k + 1) * 0.25f;
			const FLinearColor HeadSm = HeadC(k - 1) * 0.25f + HeadC(k) * 0.5f + HeadC(k + 1) * 0.25f;
			// 端排保原值（與殼面逐頂點同色＝縫無色階）；內排用平滑值
			const FLinearColor RingCol = (j == 0) ? BodyC(k) : (j == E) ? HeadC(k) : FMath::Lerp(BodySm, HeadSm, Blend);
			Cols[Idx] = RingCol.QuantizeRound();
		}
	}

	// 單調鉗位已拆除（2026-07-16 二分實錘：鉗位本身是分段感災難的兇手——
	// 「沿當地軸投影不得後退」在彎外側把切向前進誤判成後退、連鎖推擠放大到 46cm；
	// 關掉後的原始曲面在「環間淨空」L 表＋slerp 框架下自然平滑，下巴側殘留的
	// 幾公分 U 形回折＝真實的皮肉擠壓褶讀感，不修）。

	// 法線：端排抄縫區移植法線（光影無縫）、內排幾何中央差分（外向校正）
	for (int32 j = 0; j < Rows; ++j)
	{
		for (int32 k = 0; k < GRing; ++k)
		{
			const int32 Idx = j * GRing + k;
			if (j == 0)
			{
				Normals[Idx] = BN[k];
			}
			else if (j == E)
			{
				Normals[Idx] = HeadResNrm[k];
			}
			else
			{
				const FVector DCol = Verts[j * GRing + (k + 1) % GRing] - Verts[j * GRing + (k + GRing - 1) % GRing];
				const FVector DRow = Verts[(j + 1) * GRing + k] - Verts[(j - 1) * GRing + k];
				FVector N = FVector::CrossProduct(DCol, DRow).GetSafeNormal();
				if (N.IsNearlyZero())
				{
					// 鉗位後行間密貼＝退化——徑向保底
					N = (Verts[Idx] - CP[j]).GetSafeNormal();
				}
				else if (FVector::DotProduct(N, Verts[Idx] - CP[j]) < 0.0f)
				{
					N = -N;
				}
				// 壓縮域法線＝兩端縫區法線插值（07-26 側邊摺痕修）：張開/互穿過渡帶
				// （側邊）的直紋列近退化＝DRow 逐列翻向→中央差分法線翻面＝著色摺痕
				//（幾何折角實測 ≤2°、摺是「光」不是「形」）；端法線插值構造上連續、
				// 兩端與殼面光影無縫。伸長域（Compress=1）維持幾何法線。
				const FVector RuledN = FMath::Lerp(BN[k], HeadResNrm[k],
					SmoothStep01(static_cast<float>(j) / E)).GetSafeNormal();
				Normals[Idx] = RuledN.IsNearlyZero()
					? N : FMath::Lerp(RuledN, N, Compress).GetSafeNormal();
			}
		}
	}

	// 環向法線平滑（08-15 user 抓「脖子一條條縱紋」）：管面每列各自沿中線推、相鄰列
	// 微凹凸被中央差分法線逐列放大＝頭燈假光下 84 條明暗直紋（端色連續化後更顯眼）。
	// 內排幾何法線沿環做 3-tap 兩趟＝列頻率抹平、真曲率保留（環向 84 列＝抹掉的是
	// <5° 弧度的鋸齒）；端排不動（縫區移植法線＝與殼面無縫的錨）。
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		TArray<FVector> Prev = Normals;
		for (int32 j = 1; j < E; ++j)
		{
			for (int32 k = 0; k < GRing; ++k)
			{
				const FVector Sm = (Prev[j * GRing + (k + GRing - 1) % GRing] * 0.25f
					+ Prev[j * GRing + k] * 0.5f
					+ Prev[j * GRing + (k + 1) % GRing] * 0.25f).GetSafeNormal();
				if (!Sm.IsNearlyZero())
				{
					Normals[j * GRing + k] = Sm;
				}
			}
		}
	}

	// 端排法線源（縫區移植）與內排幾何法線的硬切換＝亮度帶——鄰帶（30%）漸混
	for (int32 j = 1; j < E; ++j)
	{
		const float T = static_cast<float>(j) / E;
		const float WB = FMath::Clamp(1.0f - T / 0.3f, 0.0f, 1.0f);
		const float WH = FMath::Clamp(1.0f - (1.0f - T) / 0.3f, 0.0f, 1.0f);
		if (WB <= 0.0f && WH <= 0.0f)
		{
			continue;
		}
		for (int32 k = 0; k < GRing; ++k)
		{
			FVector& Nr = Normals[j * GRing + k];
			if (WB > 0.0f)
			{
				Nr = FMath::Lerp(Nr, Normals[k], WB).GetSafeNormal();
			}
			if (WH > 0.0f)
			{
				Nr = FMath::Lerp(Nr, Normals[E * GRing + k], WH).GetSafeNormal();
			}
		}
	}

	// 圍裙排（08-16 user「零星角度看到透明破圖」）：兩端排再各伸一排到殼面之下——
	// 頭端排是重取樣點（落在頭殼邊界折線上=T 接點）、身端排是 CPU 蒙皮（double）
	// vs GPU（float）的同位近似，光柵化在掠射角必留亞像素裂縫＝透明針孔。圍裙沿管軸
	// 伸進殼內 NeckApronExtCm、再沿 −法線沉 NeckApronSinkCm＝殼面蓋住裂縫（水密靠重疊
	// 不靠逐位相等）。法線/色/UV 抄端排。
	{
		const int32 Base = Verts.Num();
		Verts.AddUninitialized(2 * GRing);
		Normals.AddUninitialized(2 * GRing);
		UV0.AddUninitialized(2 * GRing);
		Cols.AddUninitialized(2 * GRing);
		for (int32 k = 0; k < GRing; ++k)
		{
			const int32 IB = Base + k;           // 身側圍裙
			const int32 IH = Base + GRing + k;   // 頭側圍裙
			Verts[IB] = BP[k] - NB * NeckApronExtCm - BN[k] * NeckApronSinkCm;
			Verts[IH] = HeadResPos[k] + NH * NeckApronExtCm - HeadResNrm[k] * NeckApronSinkCm;
			Normals[IB] = Normals[k];
			Normals[IH] = Normals[E * GRing + k];
			UV0[IB] = UV0[k];
			UV0[IH] = UV0[E * GRing + k];
			Cols[IB] = Cols[k];
			Cols[IH] = Cols[E * GRing + k];
		}
	}

	// 索引（一次建；winding 以「幾何法線=外向法線同向」判定——取中段良態四邊形，
	// row0 貼著邊界環面積趨零＝噪聲判定源，robo 實錘判反→two-sided 背面翻法線＝暗盤）
	if (!bSectionCreated)
	{
		const int32 MJ = Rows / 2;
		const int32 MA = MJ * GRing;
		const FVector GN = FVector::CrossProduct(
			Verts[MA + 1] - Verts[MA], Verts[(MJ + 1) * GRing] - Verts[MA]).GetSafeNormal();
		const bool bFlip = FVector::DotProduct(GN, Normals[MA]) < 0.0f;
		TArray<int32> Tris;
		Tris.Reserve((Rows + 1) * GRing * 6);
		const int32 ApronB = Rows * GRing;          // 身側圍裙排起點
		const int32 ApronH = ApronB + GRing;        // 頭側圍裙排起點
		auto RowStart = [&](int32 j) { return j < 0 ? ApronB : (j >= Rows ? ApronH : j * GRing); };
		for (int32 j = -1; j < Rows; ++j) // -1＝身側圍裙→row0；Rows-1＝row E→頭側圍裙
		{
			const int32 R0 = RowStart(j), R1 = RowStart(j + 1);
			for (int32 k = 0; k < GRing; ++k)
			{
				const int32 K1 = (k + 1) % GRing;
				const int32 A = R0 + k;
				const int32 B = R0 + K1;
				const int32 C = R1 + K1;
				const int32 D = R1 + k;
				if (bFlip)
				{
					Tris.Append({ A, C, B, A, D, C });
				}
				else
				{
					Tris.Append({ A, B, C, A, C, D });
				}
			}
		}
		CreateMeshSection(0, Verts, Tris, Normals, UV0, Cols, TArray<FProcMeshTangent>(), false);
		bSectionCreated = true;
	}
	else
	{
		UpdateMeshSection(0, Verts, Normals, UV0, Cols, TArray<FProcMeshTangent>());
	}
}
