// Copyright Winyunq, 2025. All Rights Reserved.

#include "FogOfWar.h"

#include "FogOfWarMassBinding.h"
#include "Components/BrushComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
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
	DECLARE_STATIC_FNAME(FOW_NotVisibleRegionBrightness);
	DECLARE_STATIC_FNAME(FOW_BottomLeftWorldLocation);
	DECLARE_STATIC_FNAME(FOW_GridSize);
	DECLARE_STATIC_FNAME(FOW_GridWorldSize);
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
	return false;
}

UTexture* AFogOfWar::GetFinalVisibilityTexture()
{
	return nullptr;
}

void AFogOfWar::SetCommonMIDParameters(UMaterialInstanceDynamic* MID)
{
	if (!MID)
	{
		return;
	}

	MID->SetVectorParameterValue(Names::FOW_BottomLeftWorldLocation, FVector(GridBottomLeftWorldLocation.X, GridBottomLeftWorldLocation.Y, 0));
	MID->SetVectorParameterValue(Names::FOW_GridSize, FVector(GridSize.X, GridSize.Y, 0));
	MID->SetVectorParameterValue(Names::FOW_GridWorldSize, FVector(GridSize.X, GridSize.Y, 0));
	MID->SetScalarParameterValue(Names::FOW_NotVisibleRegionBrightness, NotVisibleRegionBrightness);
	MID->SetScalarParameterValue(Names::FOW_EnableSceneGpuVisionSources, bEnableSceneGpuVisionSources ? 1.0f : 0.0f);
	MID->SetScalarParameterValue(Names::FOW_SceneGpuVisionSourceCount, static_cast<float>(SceneGpuVisionSourceCount));
	if (SceneGpuVisionSourceTexture)
	{
		MID->SetTextureParameterValue(Names::FOW_SceneGpuVisionSourceTexture, SceneGpuVisionSourceTexture);
	}
}

void AFogOfWar::Activate()
{
	if (!ensure(!bActivated))
	{
		return;
	}
	bActivated = true;

	checkf(IsValid(GridVolume), TEXT("Volume was not set for the FogOfWar Volume"));
	checkf(IsValid(PostProcessingMaterial), TEXT("PostProcessingMaterial must be set. GPU FogOfWar uses a single post-process material."));

	Initialize();

	UMinimapDataSubsystem* MinimapSubsystem = UMinimapDataSubsystem::Get();
	if (MinimapSubsystem)
	{
		MinimapSubsystem->SetVisionGridActive(false);
	}

	SceneGpuVisionSourceTexture = Names::CreateSceneGpuVisionDataTexture(this, FMath::Max(1, MaxSceneGpuVisionSources));
	PostProcessingMID = UMaterialInstanceDynamic::Create(PostProcessingMaterial, this);
	SetCommonMIDParameters(PostProcessingMID);
	if (SceneGpuVisionSourceTexture)
	{
		PostProcessingMID->SetTextureParameterValue(Names::FOW_SceneGpuVisionSourceTexture, SceneGpuVisionSourceTexture);
	}

	PostProcess->AddOrUpdateBlendable(PostProcessingMID);

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

	if (PropertyName == GET_MEMBER_NAME_CHECKED(AFogOfWar, GridVolume) ||
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
		if (PropertyName == GET_MEMBER_NAME_CHECKED(AFogOfWar, NotVisibleRegionBrightness))
		{
			if (IsValid(PostProcessingMID))
			{
				PostProcessingMID->SetScalarParameterValue(Names::FOW_NotVisibleRegionBrightness, NotVisibleRegionBrightness);
			}
			return;
		}
	}

	if (GetWorld() && !GetWorld()->IsGameWorld())
	{
		if (PropertyName == GET_MEMBER_NAME_CHECKED(AFogOfWar, GridVolume))
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

	UpdateSceneGpuVisionSourceTexture();
}

void AFogOfWar::Initialize()
{
	if (!IsValid(GridVolume))
	{
		GridSize = FVector2D::Zero();
		GridBottomLeftWorldLocation = FVector2D::Zero();

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
	if (UMinimapDataSubsystem* MinimapSubsystem = UMinimapDataSubsystem::Get())
	{
		MinimapSubsystem->SyncWorldBounds(GridBottomLeftWorldLocation, GridSize);
	}
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
