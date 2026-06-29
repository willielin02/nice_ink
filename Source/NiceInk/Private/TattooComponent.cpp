#include "TattooComponent.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "ImageUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"
#include "Net/UnrealNetwork.h"
#include "GlobalRenderResources.h"
#include "TattooNeedle.h"
#include "TattooSubsystem.h"

UTattooComponent::UTattooComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UTattooComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!RenderTarget)
	{
		InitializeRenderTarget(RenderTargetResolution);
	}
}

void UTattooComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bAutoDemoDrawing)
	{
		GenerateDemoStrokes(DeltaTime);
	}
}

void UTattooComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UTattooComponent, ReplicatedHistory);
}

UTextureRenderTarget2D* UTattooComponent::InitializeRenderTarget(int32 Resolution)
{
	RenderTargetResolution = FMath::Clamp(Resolution, 64, 4096);

	RenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("TattooAccumulationRT"));
	RenderTarget->RenderTargetFormat = RTF_RGBA8;
	RenderTarget->ClearColor = FLinearColor::Transparent;
	RenderTarget->AddressX = TA_Clamp;
	RenderTarget->AddressY = TA_Clamp;
	RenderTarget->bAutoGenerateMips = false;
	RenderTarget->InitAutoFormat(RenderTargetResolution, RenderTargetResolution);
	RenderTarget->UpdateResourceImmediate(true);

	ClearTattoo();
	return RenderTarget;
}

void UTattooComponent::SetRenderTarget(UTextureRenderTarget2D* InRenderTarget)
{
	RenderTarget = InRenderTarget;
	if (RenderTarget)
	{
		RenderTargetResolution = RenderTarget->SizeX;
	}
}

void UTattooComponent::ClearTattoo()
{
	if (RenderTarget)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, RenderTarget, FLinearColor::Transparent);
	}
}

void UTattooComponent::SelectNeedle(ENiceInkNeedleType NeedleType)
{
	CurrentNeedleType = NeedleType;
	OnNeedleChanged.Broadcast(CurrentNeedleType);
}

void UTattooComponent::SelectColor(FLinearColor InkColor)
{
	CurrentColor = InkColor;
}

void UTattooComponent::StartTattooing(const FHitResult& HitResult)
{
	FVector2D HitUV(0.5f, 0.5f);
	UGameplayStatics::FindCollisionUV(HitResult, 0, HitUV);
	BeginTattooingAtUV(HitUV, 1.0f);
}

void UTattooComponent::BeginTattooingAtUV(FVector2D UV, float Pressure)
{
	bIsTattooing = true;
	bHasLastStrokeUV = false;
	SubmitStrokeAtUV(UV, Pressure);
}

void UTattooComponent::StopTattooing()
{
	bIsTattooing = false;
	bHasLastStrokeUV = false;
	StrikeAccumulator = 0.0f;
}

void UTattooComponent::SubmitStrokeAtUV(FVector2D UV, float Pressure)
{
	TArray<FTattooStroke> Strokes;
	const FVector2D ClampedUV(FMath::Clamp(UV.X, 0.0f, 1.0f), FMath::Clamp(UV.Y, 0.0f, 1.0f));

	if (bIsTattooing && bHasLastStrokeUV)
	{
		const FNeedleConfig Config = GetCurrentNeedleConfig();
		const float Distance = FVector2D::Distance(LastStrokeUV, ClampedUV);
		const float StepSize = FMath::Max(Config.UvRadius * 1.25f, 0.002f);
		const int32 StepCount = FMath::Clamp(FMath::CeilToInt(Distance / StepSize), 1, 64);

		for (int32 StepIndex = 1; StepIndex <= StepCount; ++StepIndex)
		{
			const float Alpha = static_cast<float>(StepIndex) / static_cast<float>(StepCount);
			Strokes.Add(MakeStroke(FMath::Lerp(LastStrokeUV, ClampedUV, Alpha), Pressure));
		}
	}
	else
	{
		Strokes.Add(MakeStroke(ClampedUV, Pressure));
	}

	LastStrokeUV = ClampedUV;
	bHasLastStrokeUV = true;
	SubmitStrokeBatch(Strokes);
}

void UTattooComponent::SubmitStrokeBatch(const TArray<FTattooStroke>& Strokes)
{
	if (Strokes.IsEmpty())
	{
		return;
	}

	AActor* Owner = GetOwner();
	const bool bHasAuthority = Owner && Owner->HasAuthority();

	if (bHasAuthority)
	{
		TArray<FTattooStroke> ValidStrokes;
		for (const FTattooStroke& Stroke : Strokes)
		{
			if (ValidateStroke(Stroke))
			{
				ValidStrokes.Add(Stroke);
				ReplicatedHistory.AddStroke(Stroke);
			}
		}

		if (!ValidStrokes.IsEmpty())
		{
			ApplyStrokeBatchLocal(ValidStrokes);
			MulticastApplyStrokes(ValidStrokes);
		}
	}
	else
	{
		ServerApplyStrokes(Strokes);
	}
}

void UTattooComponent::ApplyStrokeLocal(const FTattooStroke& Stroke)
{
	if (!RenderTarget || !ValidateStroke(Stroke))
	{
		return;
	}

	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize = FVector2D::ZeroVector;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, RenderTarget, Canvas, CanvasSize, Context);
	if (Canvas)
	{
		DrawStroke(Canvas, CanvasSize, Stroke);
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);

	if (UTattooSubsystem* TattooSubsystem = GetWorld() ? GetWorld()->GetSubsystem<UTattooSubsystem>() : nullptr)
	{
		TattooSubsystem->RecordStroke(GetOwner(), Stroke);
	}

	OnStrokeApplied.Broadcast(Stroke);
}

void UTattooComponent::ApplyStrokeBatchLocal(const TArray<FTattooStroke>& Strokes)
{
	if (!RenderTarget || Strokes.IsEmpty())
	{
		return;
	}

	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize = FVector2D::ZeroVector;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, RenderTarget, Canvas, CanvasSize, Context);
	if (Canvas)
	{
		for (const FTattooStroke& Stroke : Strokes)
		{
			if (ValidateStroke(Stroke))
			{
				DrawStroke(Canvas, CanvasSize, Stroke);
				if (UTattooSubsystem* TattooSubsystem = GetWorld() ? GetWorld()->GetSubsystem<UTattooSubsystem>() : nullptr)
				{
					TattooSubsystem->RecordStroke(GetOwner(), Stroke);
				}
				OnStrokeApplied.Broadcast(Stroke);
			}
		}
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
}

bool UTattooComponent::SaveTattooToPng(const FString& AbsoluteFilePath) const
{
	if (!RenderTarget || AbsoluteFilePath.IsEmpty())
	{
		return false;
	}

	FTextureRenderTargetResource* Resource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!Resource)
	{
		return false;
	}

	TArray<FColor> Pixels;
	if (!Resource->ReadPixels(Pixels) || Pixels.IsEmpty())
	{
		return false;
	}

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(AbsoluteFilePath), true);

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
	if (!ImageWrapper.IsValid())
	{
		return false;
	}

	ImageWrapper->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), RenderTarget->SizeX, RenderTarget->SizeY, ERGBFormat::BGRA, 8);
	const TArray64<uint8>& CompressedData = ImageWrapper->GetCompressed(90);

	TArray<uint8> SaveData;
	SaveData.Append(CompressedData.GetData(), CompressedData.Num());
	return FFileHelper::SaveArrayToFile(SaveData, *AbsoluteFilePath);
}

UTexture2D* UTattooComponent::LoadTattooTextureFromPng(const FString& AbsoluteFilePath) const
{
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *AbsoluteFilePath))
	{
		return nullptr;
	}

	return FImageUtils::ImportBufferAsTexture2D(FileData);
}

bool UTattooComponent::LoadTattooFromPng(const FString& AbsoluteFilePath)
{
	if (!RenderTarget)
	{
		InitializeRenderTarget(RenderTargetResolution);
	}

	UTexture2D* LoadedTexture = LoadTattooTextureFromPng(AbsoluteFilePath);
	if (!LoadedTexture || !RenderTarget)
	{
		return false;
	}

	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize = FVector2D::ZeroVector;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, RenderTarget, Canvas, CanvasSize, Context);
	if (Canvas)
	{
		Canvas->K2_DrawTexture(
			LoadedTexture,
			FVector2D::ZeroVector,
			CanvasSize,
			FVector2D::ZeroVector,
			FVector2D::UnitVector,
			FLinearColor::White,
			BLEND_Opaque,
			0.0f,
			FVector2D::ZeroVector);
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
	return Canvas != nullptr;
}

void UTattooComponent::ServerApplyStrokes_Implementation(const TArray<FTattooStroke>& Strokes)
{
	TArray<FTattooStroke> ValidStrokes;
	for (const FTattooStroke& Stroke : Strokes)
	{
		if (ValidateStroke(Stroke))
		{
			ValidStrokes.Add(Stroke);
			ReplicatedHistory.AddStroke(Stroke);
		}
	}

	if (!ValidStrokes.IsEmpty())
	{
		ApplyStrokeBatchLocal(ValidStrokes);
		MulticastApplyStrokes(ValidStrokes);
	}
}

void UTattooComponent::MulticastApplyStrokes_Implementation(const TArray<FTattooStroke>& Strokes)
{
	if (const AActor* Owner = GetOwner(); Owner && Owner->HasAuthority())
	{
		return;
	}

	ApplyStrokeBatchLocal(Strokes);
}

FNeedleConfig UTattooComponent::GetCurrentNeedleConfig() const
{
	return UTattooNeedle::MakeDefaultConfig(CurrentNeedleType);
}

FTattooStroke UTattooComponent::MakeStroke(FVector2D UV, float Pressure) const
{
	const FNeedleConfig Config = GetCurrentNeedleConfig();

	FTattooStroke Stroke;
	Stroke.UV = FVector2D(FMath::Clamp(UV.X, 0.0f, 1.0f), FMath::Clamp(UV.Y, 0.0f, 1.0f));
	Stroke.Color = CurrentColor;
	Stroke.Color.A = Config.Opacity;
	Stroke.NeedleType = CurrentNeedleType;
	Stroke.Pressure = FMath::Clamp(Pressure, 0.0f, 1.0f);
	Stroke.Radius = Config.UvRadius;
	Stroke.Timestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	return Stroke;
}

void UTattooComponent::DrawStroke(UCanvas* Canvas, const FVector2D& CanvasSize, const FTattooStroke& Stroke) const
{
	const FNeedleConfig Config = UTattooNeedle::MakeDefaultConfig(Stroke.NeedleType);
	const int32 DotCount = FMath::Max(1, Config.DotsPerStrike);
	const float PixelRadius = FMath::Max(1.0f, Stroke.Radius * CanvasSize.X * Stroke.Pressure);
	const FVector2D Center(Stroke.UV.X * CanvasSize.X, Stroke.UV.Y * CanvasSize.Y);

	for (int32 DotIndex = 0; DotIndex < DotCount; ++DotIndex)
	{
		FVector2D Offset = FVector2D::ZeroVector;
		if (DotCount > 1)
		{
			const float T = DotCount > 1 ? static_cast<float>(DotIndex) / static_cast<float>(DotCount - 1) : 0.0f;
			const float Angle = T * TWO_PI;
			const float Spread = PixelRadius * 0.65f;

			if (Stroke.NeedleType == ENiceInkNeedleType::Magnum || Stroke.NeedleType == ENiceInkNeedleType::CurvedMagnum)
			{
				const float Linear = (T - 0.5f) * 2.0f;
				const float Curve = Stroke.NeedleType == ENiceInkNeedleType::CurvedMagnum ? FMath::Sin(T * PI) * 0.55f : 0.0f;
				Offset = FVector2D(Linear * Spread * Config.PatternScale.X, Curve * Spread * Config.PatternScale.Y);
			}
			else
			{
				Offset = FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Spread;
			}
		}

		const float DotSize = PixelRadius * (Stroke.NeedleType == ENiceInkNeedleType::RoundLiner ? 1.0f : 0.8f);
		const FVector2D Position = Center + Offset - FVector2D(DotSize * 0.5f, DotSize * 0.5f);
		FCanvasTileItem TileItem(Position, GWhiteTexture, FVector2D(DotSize, DotSize), Stroke.Color);
		TileItem.BlendMode = SE_BLEND_Opaque;
		Canvas->DrawItem(TileItem);
	}
}

void UTattooComponent::GenerateDemoStrokes(float DeltaTime)
{
	const FNeedleConfig Config = GetCurrentNeedleConfig();
	StrikeAccumulator += DeltaTime * Config.StrikesPerSecond;
	DemoTime += DeltaTime * DemoAngularSpeed;

	TArray<FTattooStroke> Strokes;
	while (StrikeAccumulator >= 1.0f)
	{
		StrikeAccumulator -= 1.0f;

		const float Radius = 0.22f + 0.08f * FMath::Sin(DemoTime * 0.7f);
		const FVector2D UV(
			0.5f + FMath::Cos(DemoTime) * Radius,
			0.5f + FMath::Sin(DemoTime * 1.17f) * Radius
		);

		Strokes.Add(MakeStroke(UV, 1.0f));
		LastDemoUV = UV;
		DemoTime += 0.006f;

		if (Strokes.Num() >= 20)
		{
			break;
		}
	}

	if (!Strokes.IsEmpty())
	{
		SubmitStrokeBatch(Strokes);
	}
}

bool UTattooComponent::ValidateStroke(const FTattooStroke& Stroke) const
{
	return FMath::IsFinite(Stroke.UV.X)
		&& FMath::IsFinite(Stroke.UV.Y)
		&& Stroke.UV.X >= -0.01f && Stroke.UV.X <= 1.01f
		&& Stroke.UV.Y >= -0.01f && Stroke.UV.Y <= 1.01f
		&& Stroke.Radius > 0.0f
		&& Stroke.Radius <= 0.25f
		&& Stroke.Pressure >= 0.0f
		&& Stroke.Pressure <= 1.0f;
}
