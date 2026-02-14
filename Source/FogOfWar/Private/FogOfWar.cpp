// Copyright Winyunq, 2025. All Rights Reserved.

#include "FogOfWar.h"

#include "Components/BrushComponent.h"
#include "Components/PostProcessComponent.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Subsystems/MinimapDataSubsystem.h"
#include "Utils/ManagerComponent.h"
#include "Utils/ManagerStatics.h"
#include "Utils/Macros.h"

DEFINE_LOG_CATEGORY(LogFogOfWar);

DECLARE_STATS_GROUP(TEXT("FogOfWar"), STATGROUP_FogOfWar, STATCAT_Advanced);

namespace Names
{
	DECLARE_STATIC_FNAME(FOW_AccumulatedMask);
	DECLARE_STATIC_FNAME(FOW_NewSnapshot);
	DECLARE_STATIC_FNAME(FOW_MinimalVisibility);
	DECLARE_STATIC_FNAME(FOW_NewSnapshotAbsorption);
	DECLARE_STATIC_FNAME(FOW_VisibilityTextureRenderTarget);
	DECLARE_STATIC_FNAME(FOW_PreFinalVisibilityTextureRenderTarget);
	DECLARE_STATIC_FNAME(FOW_FinalVisibilityTexture);
	DECLARE_STATIC_FNAME(FOW_NotVisibleRegionBrightness);
	DECLARE_STATIC_FNAME(FOW_GridResolution);
	DECLARE_STATIC_FNAME(FOW_TileSize);
	DECLARE_STATIC_FNAME(FOW_BottomLeftWorldLocation);
}

AFogOfWar::AFogOfWar()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcessComponent"));
	PostProcess->SetupAttachment(RootComponent);
}

bool AFogOfWar::IsLocationVisible(FVector WorldLocation)
{
	FIntPoint TileIJ = UMinimapDataSubsystem::ConvertWorldLocationToVisionTileIJ_Static(FVector2D(WorldLocation));
	if (!UMinimapDataSubsystem::IsVisionGridIJValid_Static(TileIJ))
	{
		return false;
	}

	const FTile& Tile = GetGlobalTile(TileIJ);
	bool bIsVisible = Tile.VisibilityCounter > 0;
	return bIsVisible;
}

UTexture* AFogOfWar::GetFinalVisibilityTexture()
{
	return Cast<UTexture>(FinalVisibilityTextureRenderTarget);
}

void AFogOfWar::SetCommonMIDParameters(UMaterialInstanceDynamic* MID)
{
	MID->SetTextureParameterValue(Names::FOW_FinalVisibilityTexture, GetFinalVisibilityTexture());
	MID->SetVectorParameterValue(Names::FOW_GridResolution, FVector(GridResolution.X, GridResolution.Y, 0));
	MID->SetScalarParameterValue(Names::FOW_TileSize, TileSize);
	MID->SetVectorParameterValue(Names::FOW_BottomLeftWorldLocation, FVector(GridBottomLeftWorldLocation.X, GridBottomLeftWorldLocation.Y, 0));
}

void AFogOfWar::Activate()
{
	if (!ensure(!bActivated))
	{
		return;
	}
	bActivated = true;

	checkf(IsValid(GridVolume), TEXT("Volume was not set for the FogOfWar Volume"));
	check(TileSize > 0);

	Initialize();

	checkf(GridResolution.X + GridResolution.Y <= 10000, TEXT("Grid resolution is too big (possible int32 overflow when calculating square distance)"));

	const int GridTilesNum = GridResolution.X * GridResolution.Y;
	Tiles.SetNum(GridTilesNum);
	TextureDataBuffer.SetNum(GridTilesNum);

	for (int I = 0; I < GridResolution.X; I++)
	{
		for (int J = 0; J < GridResolution.Y; J++)
		{
			FTile& Tile = GetGlobalTile({ I, J });
			CalculateTileHeight(Tile, { I,J });
		}
	}

#if WITH_EDITORONLY_DATA
	HeightmapTexture = CreateSnapshotTexture();
	HeightmapTexture->Filter = TF_Nearest;
	WriteHeightmapDataToTexture(HeightmapTexture);
#endif

	SnapshotTexture = CreateSnapshotTexture();
	VisibilityTextureRenderTarget = CreateRenderTarget();
	PreFinalVisibilityTextureRenderTarget = CreateRenderTarget();
	FinalVisibilityTextureRenderTarget = CreateRenderTarget();

	InterpolationMID = UMaterialInstanceDynamic::Create(InterpolationMaterial, this);
	InterpolationMID->SetTextureParameterValue(Names::FOW_AccumulatedMask, VisibilityTextureRenderTarget);
	InterpolationMID->SetTextureParameterValue(Names::FOW_NewSnapshot, SnapshotTexture);

	AfterInterpolationMID = UMaterialInstanceDynamic::Create(AfterInterpolationMaterial, this);
	AfterInterpolationMID->SetTextureParameterValue(Names::FOW_VisibilityTextureRenderTarget, VisibilityTextureRenderTarget);
	AfterInterpolationMID->SetScalarParameterValue(Names::FOW_MinimalVisibility, MinimalVisibility);

	SuperSamplingMID = UMaterialInstanceDynamic::Create(SuperSamplingMaterial, this);
	SuperSamplingMID->SetTextureParameterValue(Names::FOW_PreFinalVisibilityTextureRenderTarget, PreFinalVisibilityTextureRenderTarget);
	SuperSamplingMID->SetVectorParameterValue(Names::FOW_GridResolution, FVector(GridResolution.X, GridResolution.Y, 0));

	PostProcessingMID = UMaterialInstanceDynamic::Create(PostProcessingMaterial, this);
	SetCommonMIDParameters(PostProcessingMID);
	PostProcessingMID->SetScalarParameterValue(Names::FOW_NotVisibleRegionBrightness, NotVisibleRegionBrightness);

	PostProcess->AddOrUpdateBlendable(PostProcessingMID);

	// Deprecated: MinimapSubsystem is now decoupled from AFogOfWar.

	auto GameManager = UManagerStatics::GetGameManager(this);
	GameManager->Register<ThisClass>(this);
	PrimaryActorTick.SetTickFunctionEnable(true);
}

void AFogOfWar::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoActivate)
	{
		Activate();
	}
}

#if WITH_EDITOR
void AFogOfWar::RefreshVolumeInEditor()
{
	if (GetWorld() && !GetWorld()->IsGameWorld())
	{
		Initialize();
	}
}
#endif

#if WITH_EDITOR
bool AFogOfWar::CanEditChange(const FProperty* InProperty) const
{
	if (!Super::CanEditChange(InProperty))
	{
		return false;
	}

	const FName PropertyName = InProperty->GetFName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(AFogOfWar, TileSize) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(AFogOfWar, GridVolume) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(AFogOfWar, InterpolationMaterial) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(AFogOfWar, AfterInterpolationMaterial) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(AFogOfWar, SuperSamplingMaterial) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(AFogOfWar, PostProcessingMaterial))
	{
		return !GetWorld() || !GetWorld()->IsGameWorld();
	}

	return true;
}
#endif

#if WITH_EDITOR
void AFogOfWar::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.Property->GetFName();

	if (GetWorld() && GetWorld()->IsGameWorld())
	{
		if (PropertyName == GET_MEMBER_NAME_CHECKED(AFogOfWar, MinimalVisibility))
		{
			if (IsValid(AfterInterpolationMID))
			{
				AfterInterpolationMID->SetScalarParameterValue(Names::FOW_MinimalVisibility, MinimalVisibility);
			}
			return;
		}

		if (PropertyName == GET_MEMBER_NAME_CHECKED(AFogOfWar, NotVisibleRegionBrightness))
		{
			if (IsValid(PostProcessingMID))
			{
				PostProcessingMID->SetScalarParameterValue(Names::FOW_NotVisibleRegionBrightness, NotVisibleRegionBrightness);
			}
			return;
		}

		if (PropertyName == GET_MEMBER_NAME_CHECKED(AFogOfWar, ApproximateSecondsToAbsorbNewSnapshot))
		{
			bFirstTick = true;
			return;
		}

		if (PropertyName == GET_MEMBER_NAME_CHECKED(AFogOfWar, VisionBlockingDeltaHeightThreshold))
		{
			// This part is obsolete in Mass. The Mass processors will handle vision recalculation.
			return;
		}
	}

	if (GetWorld() && !GetWorld()->IsGameWorld())
	{
		if (PropertyName == GET_MEMBER_NAME_CHECKED(AFogOfWar, TileSize) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(AFogOfWar, GridVolume))
		{
			RefreshVolumeInEditor();
			return;
		}
	}
}
#endif

void AFogOfWar::Tick(float DeltaSeconds)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("Tick"), STAT_FogOfWarTick, STATGROUP_FogOfWar);

	Super::Tick(DeltaSeconds);

	// The vision update loop is now handled by Mass processors.

	{
		DECLARE_SCOPE_CYCLE_COUNTER(TEXT("Pipeline"), STAT_FogOfWarPipeline, STATGROUP_FogOfWar);
		{
			// step 1: creating a snapshot texture from the newest vision data
			DECLARE_SCOPE_CYCLE_COUNTER(TEXT("Pipeline: step 1"), STAT_FogOfWarPipelineStep1, STATGROUP_FogOfWar);
			WriteVisionDataToTexture(SnapshotTexture);
		}
		{
			// step 2: interpolating the snapshot with the previous visibility texture (to avoid flickering)
			DECLARE_SCOPE_CYCLE_COUNTER(TEXT("Pipeline: step 2"), STAT_FogOfWarPipelineStep2, STATGROUP_FogOfWar);
			const float NewSnapshotAbsorption = bFirstTick ? 1.0f : FMath::Min(DeltaSeconds / ApproximateSecondsToAbsorbNewSnapshot, 1.0f);
			InterpolationMID->SetScalarParameterValue(Names::FOW_NewSnapshotAbsorption, NewSnapshotAbsorption);
			UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, VisibilityTextureRenderTarget, InterpolationMID);
		}
		{
			// step 3: cutting off the minimal visibility
			DECLARE_SCOPE_CYCLE_COUNTER(TEXT("Pipeline: step 3"), STAT_FogOfWarPipelineStep3, STATGROUP_FogOfWar);
			UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, PreFinalVisibilityTextureRenderTarget, AfterInterpolationMID);
		}
		{
			// step 4: super sampling
			DECLARE_SCOPE_CYCLE_COUNTER(TEXT("Pipeline: step 4"), STAT_FogOfWarPipelineStep4, STATGROUP_FogOfWar);
			UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, FinalVisibilityTextureRenderTarget, SuperSamplingMID);
		}
	}

	bFirstTick = false;
}

void AFogOfWar::Initialize()
{
	if (!IsValid(GridVolume))
	{
		GridSize = FVector2D::Zero();
		GridBottomLeftWorldLocation = FVector2D::Zero();
		GridResolution = {};

		return;
	}

	UBrushComponent* VolumeBrush = GridVolume->GetBrushComponent();
	FBoxSphereBounds Bounds = VolumeBrush->CalcBounds(VolumeBrush->GetComponentTransform());

	GridSize = {
		Bounds.BoxExtent.X * 2,
		Bounds.BoxExtent.Y * 2
	};
	GridBottomLeftWorldLocation = {
		Bounds.Origin.X - GridSize.X / 2,
		Bounds.Origin.Y - GridSize.Y / 2
	};
	GridResolution = {
		FMath::CeilToInt32(GridSize.X / TileSize),
		FMath::CeilToInt32(GridSize.Y / TileSize)
	};
}

void AFogOfWar::CalculateTileHeight(FTile& Tile, FIntPoint TileIJ)
{
	FVector2D WorldLocation = UMinimapDataSubsystem::ConvertVisionTileIJToTileCenterWorldLocation_Static(TileIJ);
	FHitResult HitResult;
	bool bFoundBlockingHit = GetWorld()->LineTraceSingleByChannel(
		HitResult,
		FVector(WorldLocation.X, WorldLocation.Y, 10000.0),
		FVector(WorldLocation.X, WorldLocation.Y, -10000.0),
		HeightScanCollisionChannel);

	if (bFoundBlockingHit && HitResult.HasValidHitObjectHandle())
	{
		Tile.Height = HitResult.ImpactPoint.Z;
		return;
	}

	Tile.Height = -std::numeric_limits<decltype(Tile.Height)>::infinity();
}

UTexture2D* AFogOfWar::CreateSnapshotTexture()
{
	UTexture2D* Texture = UTexture2D::CreateTransient(GridResolution.Y, GridResolution.X, PF_R8);
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;
	Texture->SRGB = 0;
#if WITH_EDITORONLY_DATA
	if (bDebugFilterNearest)
	{
		Texture->Filter = TF_Nearest;
	}
#endif

	return Texture;
}

UTextureRenderTarget2D* AFogOfWar::CreateRenderTarget()
{
	UTextureRenderTarget2D* RenderTarget = UKismetRenderingLibrary::CreateRenderTarget2D(this, GridResolution.Y, GridResolution.X, RTF_R8);
	RenderTarget->AddressX = TA_Clamp;
	RenderTarget->AddressY = TA_Clamp;
	RenderTarget->SRGB = 0;
#if WITH_EDITORONLY_DATA
	if (bDebugFilterNearest)
	{
		RenderTarget->Filter = TF_Nearest;
	}
#endif

	return RenderTarget;
}

void AFogOfWar::WriteVisionDataToTexture(UTexture2D* Texture)
{
	for (int TileIndex = 0; TileIndex < Tiles.Num(); TileIndex++)
	{
		const FTile& Tile = Tiles[TileIndex];
		TextureDataBuffer[TileIndex] = Tile.VisibilityCounter > 0 ? 0xFF : 0;
	}

	void* TextureData = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(TextureData, TextureDataBuffer.GetData(), sizeof(TextureDataBuffer[0]) * TextureDataBuffer.Num());
	Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
	// TODO: likely a better version exists
	Texture->UpdateResource();
}

#if WITH_EDITORONLY_DATA
void AFogOfWar::WriteHeightmapDataToTexture(UTexture2D* Texture)
{
	TArray<uint8> HeightmapDataBuffer;
	HeightmapDataBuffer.SetNum(Tiles.Num());

	for (int TileIndex = 0; TileIndex < Tiles.Num(); TileIndex++)
	{
		const FTile& Tile = Tiles[TileIndex];
		HeightmapDataBuffer[TileIndex] = FMath::RoundToInt(FMath::Clamp(FMath::GetRangePct(DebugHeightmapLowestZ, DebugHeightmapHightestZ, Tile.Height), 0.0f, 1.0f) * 0xFF);
	}

	void* TextureData = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(TextureData, HeightmapDataBuffer.GetData(), sizeof(HeightmapDataBuffer[0]) * HeightmapDataBuffer.Num());
	Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
	// TODO: likely a better version exists
	Texture->UpdateResource();
}
#endif

bool AFogOfWar::IsBlockingVision(float ObserverHeight, float PotentialObstacleHeight)
{
	return PotentialObstacleHeight - ObserverHeight > VisionBlockingDeltaHeightThreshold;
}