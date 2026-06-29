// Copyright Winyunq, 2025. All Rights Reserved.

#include "FogOfWar.h"

#include "FogOfWarMassBinding.h"
#include "Components/BrushComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "MassEntitySubsystem.h"
#include "RTSCamera.h"
#include "Subsystems/MassBattleHashGridSubsystem.h"
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
	DECLARE_STATIC_FNAME(FOW_SceneGpuVisionSourceTexture);
	DECLARE_STATIC_FNAME(FOW_SceneGpuVisionSourceCount);
	DECLARE_STATIC_FNAME(FOW_EnableSceneGpuVisionSources);

	UTexture2D* CreateSceneGpuVisionDataTexture(UObject* Outer, int32 Width)
	{
		if (!Outer || Width <= 0)
		{
			return nullptr;
		}

		UTexture2D* Texture = UTexture2D::CreateTransient(Width, 1, PF_A32B32G32R32F, TEXT("SceneGpuVisionSourceTexture"));
		if (!Texture)
		{
			return nullptr;
		}

		Texture->CompressionSettings = TextureCompressionSettings::TC_VectorDisplacementmap;
		Texture->SRGB = 0;
		Texture->Filter = TF_Nearest;
		Texture->UpdateResource();
		return Texture;
	}

	bool IntersectRayWithZPlane(const FVector& RayOrigin, const FVector& RayDirection, float PlaneZ, FVector& OutPoint)
	{
		if (FMath::IsNearlyZero(RayDirection.Z))
		{
			return false;
		}

		const double T = (PlaneZ - RayOrigin.Z) / RayDirection.Z;
		if (T < 0.0)
		{
			return false;
		}

		OutPoint = RayOrigin + RayDirection * T;
		return true;
	}

	float Cross2D(const FVector2D& A, const FVector2D& B)
	{
		return A.X * B.Y - A.Y * B.X;
	}

	float DistanceSquaredPointToSegment2D(const FVector2D& Point, const FVector2D& SegmentStart, const FVector2D& SegmentEnd)
	{
		const FVector2D Segment = SegmentEnd - SegmentStart;
		const float SegmentLengthSq = Segment.SizeSquared();
		if (SegmentLengthSq <= KINDA_SMALL_NUMBER)
		{
			return FVector2D::DistSquared(Point, SegmentStart);
		}

		const float Alpha = FMath::Clamp(FVector2D::DotProduct(Point - SegmentStart, Segment) / SegmentLengthSq, 0.0f, 1.0f);
		const FVector2D ClosestPoint = SegmentStart + Segment * Alpha;
		return FVector2D::DistSquared(Point, ClosestPoint);
	}

	bool IsPointInsideConvexQuad2D(const FVector2D& Point, const FVector2D Quad[4])
	{
		float Sign = 0.0f;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const FVector2D& A = Quad[Index];
			const FVector2D& B = Quad[(Index + 1) % 4];
			const float Cross = Cross2D(B - A, Point - A);
			if (FMath::IsNearlyZero(Cross, 0.01f))
			{
				continue;
			}

			const float CurrentSign = FMath::Sign(Cross);
			if (FMath::IsNearlyZero(Sign))
			{
				Sign = CurrentSign;
			}
			else if (CurrentSign != Sign)
			{
				return false;
			}
		}

		return true;
	}

	bool DoesCircleIntersectConvexQuad2D(const FVector2D& Center, float Radius, const FVector2D Quad[4])
	{
		if (IsPointInsideConvexQuad2D(Center, Quad))
		{
			return true;
		}

		const float RadiusSq = FMath::Square(FMath::Max(0.0f, Radius));
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const FVector2D& A = Quad[Index];
			const FVector2D& B = Quad[(Index + 1) % 4];
			if (FVector2D::DistSquared(Center, A) <= RadiusSq ||
				DistanceSquaredPointToSegment2D(Center, A, B) <= RadiusSq)
			{
				return true;
			}
		}

		return false;
	}
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
	const UMinimapDataSubsystem* MinimapSubsystem = UMinimapDataSubsystem::Get();
	return MinimapSubsystem && MinimapSubsystem->bVisionGridActive && MinimapSubsystem->IsVisionGridReady() && MinimapSubsystem->IsLocationVisible(WorldLocation);
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

	UMinimapDataSubsystem* MinimapSubsystem = UMinimapDataSubsystem::Get();
	check(MinimapSubsystem);
	MinimapSubsystem->SyncFogOfWarRuntimeOptions(
		VisionBlockingDeltaHeightThreshold,
		VisionUpdateWorldDistanceThreshold,
		bDebugStressTestIgnoreCache,
		bDebugStressTestMinimap);

	const int GridTilesNum = GridResolution.X * GridResolution.Y;
	TextureDataBuffer.SetNum(GridTilesNum);
	check(MinimapSubsystem->VisionTiles.Num() == GridTilesNum);

	for (int I = 0; I < GridResolution.X; I++)
	{
		for (int J = 0; J < GridResolution.Y; J++)
		{
			FTile& Tile = MinimapSubsystem->GetVisionTile({ I, J });
			CalculateTileHeight(Tile, { I,J });
		}
	}

	MinimapSubsystem->SetVisionGridActive(true);

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
	SceneGpuVisionSourceTexture = Names::CreateSceneGpuVisionDataTexture(this, FMath::Max(1, MaxSceneGpuVisionSources));
	if (SceneGpuVisionSourceTexture)
	{
		PostProcessingMID->SetTextureParameterValue(Names::FOW_SceneGpuVisionSourceTexture, SceneGpuVisionSourceTexture);
	}
	PostProcessingMID->SetScalarParameterValue(Names::FOW_SceneGpuVisionSourceCount, 0.0f);
	PostProcessingMID->SetScalarParameterValue(Names::FOW_EnableSceneGpuVisionSources, bEnableSceneGpuVisionSources ? 1.0f : 0.0f);

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
	if (UMinimapDataSubsystem* MinimapSubsystem = UMinimapDataSubsystem::Get())
	{
		MinimapSubsystem->SyncFogOfWarRuntimeOptions(
			VisionBlockingDeltaHeightThreshold,
			VisionUpdateWorldDistanceThreshold,
			bDebugStressTestIgnoreCache,
			bDebugStressTestMinimap);
	}

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

	UpdateSceneGpuVisionSourceTexture();

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

	if (UMinimapDataSubsystem* MinimapSubsystem = UMinimapDataSubsystem::Get())
	{
		MinimapSubsystem->SyncVisionGridParameters(GridBottomLeftWorldLocation, GridSize, TileSize, GridResolution);
	}
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
	const UMinimapDataSubsystem* MinimapSubsystem = UMinimapDataSubsystem::Get();
	if (!MinimapSubsystem || !MinimapSubsystem->IsVisionGridReady())
	{
		return;
	}

	if (TextureDataBuffer.Num() != MinimapSubsystem->VisionTiles.Num())
	{
		TextureDataBuffer.SetNum(MinimapSubsystem->VisionTiles.Num());
	}

	for (int TileIndex = 0; TileIndex < MinimapSubsystem->VisionTiles.Num(); TileIndex++)
	{
		const FTile& Tile = MinimapSubsystem->VisionTiles[TileIndex];
		TextureDataBuffer[TileIndex] = Tile.VisibilityCounter > 0 ? 0xFF : 0;
	}

	void* TextureData = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(TextureData, TextureDataBuffer.GetData(), sizeof(TextureDataBuffer[0]) * TextureDataBuffer.Num());
	Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
	// TODO: likely a better version exists
	Texture->UpdateResource();
}

bool AFogOfWar::TryGetCameraGroundFrustum(FVector2D OutFrustumPoints[4]) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	APlayerController* PlayerController = World->GetFirstPlayerController();
	if (!PlayerController)
	{
		return false;
	}

	URTSCamera* RTSCamera = nullptr;
	if (AActor* ViewTarget = PlayerController->GetViewTarget())
	{
		RTSCamera = ViewTarget->FindComponentByClass<URTSCamera>();
	}
	if (!RTSCamera)
	{
		if (APawn* Pawn = PlayerController->GetPawn())
		{
			RTSCamera = Pawn->FindComponentByClass<URTSCamera>();
		}
	}
	if (RTSCamera)
	{
		RTSCamera->updateMinimapFrustum();
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const FVector& Point = RTSCamera->minimapFrustumPoints[Index];
			if (Point.ContainsNaN())
			{
				return false;
			}
			OutFrustumPoints[Index] = FVector2D(Point.X, Point.Y);
		}
		return true;
	}

	int32 ViewportSizeX = 0;
	int32 ViewportSizeY = 0;
	PlayerController->GetViewportSize(ViewportSizeX, ViewportSizeY);
	if (ViewportSizeX <= 0 || ViewportSizeY <= 0)
	{
		return false;
	}

	const FVector2D ScreenCorners[] = {
		FVector2D(0.0, 0.0),
		FVector2D(ViewportSizeX, 0.0),
		FVector2D(ViewportSizeX, ViewportSizeY),
		FVector2D(0.0, ViewportSizeY)
	};

	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FVector2D& ScreenCorner = ScreenCorners[Index];
		FVector RayOrigin = FVector::ZeroVector;
		FVector RayDirection = FVector::ZeroVector;
		if (!PlayerController->DeprojectScreenPositionToWorld(ScreenCorner.X, ScreenCorner.Y, RayOrigin, RayDirection))
		{
			return false;
		}

		FVector GroundPoint = FVector::ZeroVector;
		if (Names::IntersectRayWithZPlane(RayOrigin, RayDirection, 0.0f, GroundPoint))
		{
			OutFrustumPoints[Index] = FVector2D(GroundPoint.X, GroundPoint.Y);
		}
		else
		{
			return false;
		}
	}

	return true;
}

bool AFogOfWar::BuildGroundBoundsFromFrustum(const FVector2D FrustumPoints[4], FBox2D& OutBounds)
{
	OutBounds = FBox2D(ForceInit);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		OutBounds += FrustumPoints[Index];
	}
	return OutBounds.bIsValid;
}

void AFogOfWar::UpdateSceneGpuVisionSourceTexture()
{
	if (!PostProcessingMID)
	{
		return;
	}

	PostProcessingMID->SetScalarParameterValue(Names::FOW_EnableSceneGpuVisionSources, bEnableSceneGpuVisionSources ? 1.0f : 0.0f);
	if (!bEnableSceneGpuVisionSources)
	{
		PostProcessingMID->SetScalarParameterValue(Names::FOW_SceneGpuVisionSourceCount, 0.0f);
		SceneGpuVisionSourceCount = 0;
		return;
	}

	if (!SceneGpuVisionSourceTexture || SceneGpuVisionSourceTexture->GetSizeX() != FMath::Max(1, MaxSceneGpuVisionSources))
	{
		SceneGpuVisionSourceTexture = Names::CreateSceneGpuVisionDataTexture(this, FMath::Max(1, MaxSceneGpuVisionSources));
		if (SceneGpuVisionSourceTexture)
		{
			PostProcessingMID->SetTextureParameterValue(Names::FOW_SceneGpuVisionSourceTexture, SceneGpuVisionSourceTexture);
		}
	}
	if (!SceneGpuVisionSourceTexture)
	{
		return;
	}

	UWorld* World = GetWorld();
	UMassEntitySubsystem* EntitySubsystem = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	UMassBattleHashGridSubsystem* HashGrid = World ? World->GetSubsystem<UMassBattleHashGridSubsystem>() : nullptr;
	if (!EntitySubsystem || !HashGrid)
	{
		return;
	}

	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();

	FVector2D CameraGroundFrustum[4];
	FBox2D CameraQueryBounds(ForceInit);
	const bool bUseCameraGroundFrustum =
		bCullSceneGpuVisionSourcesToCamera &&
		TryGetCameraGroundFrustum(CameraGroundFrustum) &&
		BuildGroundBoundsFromFrustum(CameraGroundFrustum, CameraQueryBounds);
	const int32 SafeMaxSources = FMath::Max(1, MaxSceneGpuVisionSources);
	SceneGpuVisionSourceDataBuffer.SetNumZeroed(SafeMaxSources);
	SceneGpuVisionSourceCount = 0;

	const float QueryPadding = FMath::Max(SceneGpuVisionCullPadding, SceneGpuVisionSourceSearchPadding);
	if (bUseCameraGroundFrustum)
	{
		const FVector2D Padding(QueryPadding, QueryPadding);
		CameraQueryBounds.Min -= Padding;
		CameraQueryBounds.Max += Padding;
	}

	// Broad-phase only: the final scene reveal shape is the uploaded circle source radius.
	const FVector QueryMin(
		bUseCameraGroundFrustum ? CameraQueryBounds.Min.X : GridBottomLeftWorldLocation.X,
		bUseCameraGroundFrustum ? CameraQueryBounds.Min.Y : GridBottomLeftWorldLocation.Y,
		-SceneGpuVisionQueryZHalfRange);
	const FVector QueryMax(
		bUseCameraGroundFrustum ? CameraQueryBounds.Max.X : GridBottomLeftWorldLocation.X + GridSize.X,
		bUseCameraGroundFrustum ? CameraQueryBounds.Max.Y : GridBottomLeftWorldLocation.Y + GridSize.Y,
		SceneGpuVisionQueryZHalfRange);

	const FIntVector MinCoord = HashGrid->AgentLocationToCoord(QueryMin);
	const FIntVector MaxCoord = HashGrid->AgentLocationToCoord(QueryMax);

	HashGrid->ForEachOccupiedAgentCellInRange(MinCoord, MaxCoord, [this, SafeMaxSources, &EntityManager, bUseCameraGroundFrustum, CameraGroundFrustum](const FHashGridAgentCell& Cell)
	{
		if (SceneGpuVisionSourceCount >= SafeMaxSources)
		{
			return;
		}

		const FVector2D CellCenter2D(Cell.CellLocation.X, Cell.CellLocation.Y);
		float MergedSightRadius = 0.0f;

		for (const FAgentGridData& AgentData : Cell.Agents)
		{
			if (!EntityManager.IsEntityValid(AgentData.EntityHandle))
			{
				continue;
			}

			const FMassVisionFragment* VisionFrag = EntityManager.GetFragmentDataPtr<FMassVisionFragment>(AgentData.EntityHandle);
			if (!VisionFrag)
			{
				continue;
			}

			const float SightRadius = VisionFrag->SightRadius;
			if (SightRadius <= 0.0f)
			{
				continue;
			}

			const FVector WorldLocation = Cell.CellLocation + AgentData.GetRelativeLocation();
			const FVector2D WorldLocation2D(WorldLocation.X, WorldLocation.Y);
			const float RadiusNeededToCoverSource = FVector2D::Distance(WorldLocation2D, CellCenter2D) + SightRadius;
			MergedSightRadius = FMath::Max(MergedSightRadius, RadiusNeededToCoverSource);
		}

		if (MergedSightRadius <= 0.0f)
		{
			return;
		}

		if (bUseCameraGroundFrustum && !Names::DoesCircleIntersectConvexQuad2D(CellCenter2D, MergedSightRadius, CameraGroundFrustum))
		{
			return;
		}

		SceneGpuVisionSourceDataBuffer[SceneGpuVisionSourceCount] = FLinearColor(Cell.CellLocation.X, Cell.CellLocation.Y, MergedSightRadius, 0.0f);
		SceneGpuVisionSourceCount++;
	});

	FTexture2DMipMap& Mip = SceneGpuVisionSourceTexture->GetPlatformData()->Mips[0];
	void* TextureData = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(TextureData, SceneGpuVisionSourceDataBuffer.GetData(), sizeof(FLinearColor) * SafeMaxSources);
	Mip.BulkData.Unlock();
	SceneGpuVisionSourceTexture->UpdateResource();

	PostProcessingMID->SetScalarParameterValue(Names::FOW_SceneGpuVisionSourceCount, static_cast<float>(SceneGpuVisionSourceCount));
	PostProcessingMID->SetTextureParameterValue(Names::FOW_SceneGpuVisionSourceTexture, SceneGpuVisionSourceTexture);
}

#if WITH_EDITORONLY_DATA
void AFogOfWar::WriteHeightmapDataToTexture(UTexture2D* Texture)
{
	const UMinimapDataSubsystem* MinimapSubsystem = UMinimapDataSubsystem::Get();
	if (!MinimapSubsystem || !MinimapSubsystem->IsVisionGridReady())
	{
		return;
	}

	TArray<uint8> HeightmapDataBuffer;
	HeightmapDataBuffer.SetNum(MinimapSubsystem->VisionTiles.Num());

	for (int TileIndex = 0; TileIndex < MinimapSubsystem->VisionTiles.Num(); TileIndex++)
	{
		const FTile& Tile = MinimapSubsystem->VisionTiles[TileIndex];
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
