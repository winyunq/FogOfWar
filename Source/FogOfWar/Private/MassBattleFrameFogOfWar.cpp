// Copyright Winyunq, 2025. All Rights Reserved.
// Commercial extension: see COMMERCIAL_FEATURE_LICENSE.md.

#include "MassBattleFrameFogOfWar.h"
#include "MassBattleFrameFogSceneViewExtension.h"

#include "Components/SceneComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Minimap/MapPackageProfilePaths.h"
#include "Minimap/MapRegion.h"
#include "Misc/ConfigCacheIni.h"
#include "SceneViewExtension.h"
#include "Subsystems/MassBattleFogRenderSubsystem.h"
#include "Subsystems/MassBattleHashGridSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogMassBattleFrameFog, Log, All);

namespace
{
	constexpr float SceneFogFallbackMapSizeUU = 65536.0f;
	const TCHAR* MapRegionSection = TEXT("MapRegion");

	FString GetSceneFogMapRegionPath(const UWorld* World)
	{
		return MassBattleMapProfilePaths::GetMapRegionIniPath(World);
	}

}

AMassBattleFrameFogOfWar::AMassBattleFrameFogOfWar()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;
}

void AMassBattleFrameFogOfWar::BeginPlay()
{
	Super::BeginPlay();
	ActivateMassBattleFrameFog();
}

void AMassBattleFrameFogOfWar::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DeactivateMassBattleFrameFog();
	Super::EndPlay(EndPlayReason);
}

void AMassBattleFrameFogOfWar::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bFogActive)
	{
		return;
	}

	ConsumeGpuMaskReadback();

	const double CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (FogUpdateRateHz > 0.0f && CurrentTime - LastParameterPushTime < 1.0 / FogUpdateRateHz)
	{
		return;
	}

	PushMassBattleFrameFogParameters();
}

void AMassBattleFrameFogOfWar::ActivateMassBattleFrameFog()
{
	if (bFogActive)
	{
		return;
	}

	if (!EnsureWorldVisibilityMask())
	{
		UE_LOG(LogMassBattleFrameFog, Fatal,
			TEXT("A placed MassBattleFrameFogOfWar actor requires a valid HashGrid-aligned visibility-mask layout; active fog never falls back to MassBattleFrame's unfiltered renderer."));
	}
	MaskReadbackMailbox = MakeShared<FMassBattleFrameFogMaskReadbackMailbox, ESPMode::ThreadSafe>();
	SceneViewExtension = FSceneViewExtensions::NewExtension<FMassBattleFrameFogSceneViewExtension>();
	if (!SceneViewExtension.IsValid())
	{
		UE_LOG(LogMassBattleFrameFog, Fatal,
			TEXT("A placed MassBattleFrameFogOfWar actor could not register its scene view extension; the active path has no compatibility renderer."));
	}

	bFogActive = true;
	bForceLogicMaskUpdate = true;
	LastLogicMaskPushTime = -TNumericLimits<double>::Max();
	PrimaryActorTick.SetTickFunctionEnable(true);
	ConfigureRenderFilter();
	PushMassBattleFrameFogParameters();
}

void AMassBattleFrameFogOfWar::DeactivateMassBattleFrameFog()
{
	bFogActive = false;
	bForceLogicMaskUpdate = true;
	LastLogicMaskPushTime = -TNumericLimits<double>::Max();
	PrimaryActorTick.SetTickFunctionEnable(false);
	ConfigureRenderFilter();

	if (SceneViewExtension.IsValid())
	{
		SceneViewExtension->Release_GameThread();
		SceneViewExtension.Reset();
	}
	MaskReadbackMailbox.Reset();
}

void AMassBattleFrameFogOfWar::PushMassBattleFrameFogParameters()
{
	if (!bFogActive)
	{
		return;
	}

	const double StartSeconds = FPlatformTime::Seconds();
	if (!EnsureWorldVisibilityMask())
	{
		UE_LOG(LogMassBattleFrameFog, Fatal,
			TEXT("MassBattleFrameFogOfWar lost its required visibility-mask layout; active fog never falls back to an unfiltered full-population renderer."));
	}
	ConfigureRenderFilter();
	SetMassBattleFrameFogArrays();
	LastParameterPushTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	LastPerfStats.ParameterPushMs = static_cast<float>((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
	LastPerfStats.ParameterPushCount++;
	LastPerfStats.bSceneGpuPathActive = SceneViewExtension.IsValid();
	if (const UMassBattleFogRenderSubsystem* RenderFilter = GetWorld()
		? GetWorld()->GetSubsystem<UMassBattleFogRenderSubsystem>()
		: nullptr)
	{
		LastPerfStats.MaskGeneration = static_cast<int32>(RenderFilter->GetMaskGeneration());
		LastPerfStats.bVisibilityMaskReady = RenderFilter->IsMaskReady();
		LastPerfStats.bUnitFilterActive = RenderFilter->IsFilteringActive();
		LastPerfStats.ActiveProxyCount = RenderFilter->GetActiveProxyCount();
		LastPerfStats.NiagaraUploadElementCount = RenderFilter->GetUploadedElementCount();
	}
	else
	{
		LastPerfStats.MaskGeneration = 0;
		LastPerfStats.bVisibilityMaskReady = false;
		LastPerfStats.bUnitFilterActive = false;
		LastPerfStats.ActiveProxyCount = 0;
		LastPerfStats.NiagaraUploadElementCount = 0;
	}

	if (bEnablePerformanceStats && bLogPerformanceToOutputLog && GetWorld() && GetWorld()->GetTimeSeconds() - LastPerformanceLogTime >= PerformanceLogInterval)
	{
		UE_LOG(LogMassBattleFrameFog, Log,
			TEXT("[FogOfWarPerf][MassBattleFrameFog] ParameterPush=%.3fms ArrayUpload=%.3fms Sources=%d Batches=%d ActiveProxies=%d NiagaraElements=%d SceneGPU=%s Filter=%s MaskReady=%s MaskGen=%d SceneRateHz=%.3f LogicRateHz=%.3f LogicMask=%dx%d Radius=%.1f Team=%d"),
			LastPerfStats.ParameterPushMs,
			LastPerfStats.ArrayUploadMs,
			LastPerfStats.SourceCount,
			LastPerfStats.BatchCount,
			LastPerfStats.ActiveProxyCount,
			LastPerfStats.NiagaraUploadElementCount,
			LastPerfStats.bSceneGpuPathActive ? TEXT("yes") : TEXT("no"),
			LastPerfStats.bUnitFilterActive ? TEXT("yes") : TEXT("no"),
			LastPerfStats.bVisibilityMaskReady ? TEXT("yes") : TEXT("no"),
			LastPerfStats.MaskGeneration,
			FogUpdateRateHz,
			LogicMaskUpdateRateHz,
			WorldMaskDimensions.X,
			WorldMaskDimensions.Y,
			TemporaryVisionRadius,
			ViewingTeamIndex);
		LastPerformanceLogTime = GetWorld()->GetTimeSeconds();
	}
}

void AMassBattleFrameFogOfWar::SetMassBattleFrameFogArrays()
{
	const double StartSeconds = FPlatformTime::Seconds();
	LastPerfStats.SourceCount = 0;
	LastPerfStats.BatchCount = 0;

	UWorld* World = GetWorld();
	UMassBattleFogRenderSubsystem* RenderFilter = World ? World->GetSubsystem<UMassBattleFogRenderSubsystem>() : nullptr;

	// Sources were gathered only when requested, inside the replacement render
	// processor's existing entity traversal. This avoids the former second walk
	// over every renderer batch and uploads only friendly/allied XY+velocity values.
	TArray<FVector4f> VisionSourceSamples;
	uint32 VisionCollectionRevision = 0;
	double VisionSourceSampleWorldTimeSeconds = 0.0;
	bool bVisionSourcesMatchCurrentWindow = false;
	if (RenderFilter)
	{
		RenderFilter->CopyLatestVisionSources(
			VisionSourceSamples,
			VisionCollectionRevision,
			VisionSourceSampleWorldTimeSeconds);
		bVisionSourcesMatchCurrentWindow = VisionCollectionRevision == RenderFilter->GetVisionCollectionRevision();
		RenderFilter->RequestVisionSourceCollection();
	}

	LastPerfStats.SourceCount = VisionSourceSamples.Num();
	if (SceneViewExtension.IsValid())
	{
		const double UploadWorldTimeSeconds = World ? World->GetTimeSeconds() : 0.0;
		const bool bLogicMaskDue = bForceLogicMaskUpdate
			|| LogicMaskUpdateRateHz <= 0.0f
			|| UploadWorldTimeSeconds - LastLogicMaskPushTime >= 1.0 / LogicMaskUpdateRateHz;
		const bool bCanUploadMasks = bVisionSourcesMatchCurrentWindow
			&& bWorldMaskLayoutValid;
		const bool bUpdateLogicMask = bCanUploadMasks
			&& bLogicMaskDue
			&& WorldVisibilityMask != nullptr;

		FMassBattleFrameFogSceneUploadData SceneUpload;
		SceneUpload.VisionSourceSamples = MoveTemp(VisionSourceSamples);
		SceneUpload.VisionRadiusUU = FMath::Max(0.0f, TemporaryVisionRadius);
		SceneUpload.FogOpacity = FMath::Clamp(FogOpacity, 0.0f, 1.0f);
		SceneUpload.SceneProjectionPlaneZ = SceneFogProjectionPlaneZ;
		SceneUpload.MaxSourcePredictionSeconds = FMath::Max(0.0f, SceneFogSourcePredictionMaxSeconds);
		SceneUpload.SourceSampleWorldTimeSeconds = VisionSourceSampleWorldTimeSeconds;
		SceneUpload.UploadWorldTimeSeconds = UploadWorldTimeSeconds;
		SceneUpload.WorldMaskMin = WorldMaskMin;
		SceneUpload.WorldMaskSize = WorldMaskSize;
		SceneUpload.WorldMaskCellSize = WorldMaskCellSize;
		SceneUpload.WorldMaskDimensions = WorldMaskDimensions;
		SceneUpload.WorldMaskResource = bUpdateLogicMask
			? WorldVisibilityMask->GameThread_GetRenderTargetResource()
			: nullptr;
		SceneUpload.ReadbackMailbox = MaskReadbackMailbox;
		SceneUpload.bUpdateLogicMask = bUpdateLogicMask;
		SceneUpload.bEnabled = bFogActive;
		SceneUpload.bDebug = bFogDebug;
		SceneUpload.bDebugRevealAll = bDebugRevealAll;
		SceneViewExtension->Upload_GameThread(MoveTemp(SceneUpload));
		if (bUpdateLogicMask)
		{
			LastLogicMaskPushTime = UploadWorldTimeSeconds;
			bForceLogicMaskUpdate = false;
		}
	}

	LastPerfStats.ArrayUploadMs = static_cast<float>((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
}

void AMassBattleFrameFogOfWar::SetTemporaryVisionRadius(const float InRadius)
{
	TemporaryVisionRadius = FMath::Max(0.0f, InRadius);
	bForceLogicMaskUpdate = true;
	PushMassBattleFrameFogParameters();
}

void AMassBattleFrameFogOfWar::SetViewingTeamIndex(const int32 InTeamIndex)
{
	ViewingTeamIndex = FMath::Clamp(InTeamIndex, 0, 1023);
	bForceLogicMaskUpdate = true;
	PushMassBattleFrameFogParameters();
}

void AMassBattleFrameFogOfWar::SetAlliedTeamIndices(const TArray<int32>& InAlliedTeamIndices)
{
	AlliedTeamIndices.Reset(InAlliedTeamIndices.Num());
	for (const int32 TeamIndex : InAlliedTeamIndices)
	{
		AlliedTeamIndices.AddUnique(FMath::Clamp(TeamIndex, 0, 1023));
	}
	bForceLogicMaskUpdate = true;
	PushMassBattleFrameFogParameters();
}

void AMassBattleFrameFogOfWar::SetFogOpacity(const float InOpacity)
{
	FogOpacity = FMath::Clamp(InOpacity, 0.0f, 1.0f);
	PushMassBattleFrameFogParameters();
}

void AMassBattleFrameFogOfWar::SetFogDebug(const bool bInDebug)
{
	bFogDebug = bInDebug;
	PushMassBattleFrameFogParameters();
}

void AMassBattleFrameFogOfWar::SetDebugRevealAll(const bool bInRevealAll)
{
	bDebugRevealAll = bInRevealAll;
	bForceLogicMaskUpdate = true;
	PushMassBattleFrameFogParameters();
}

void AMassBattleFrameFogOfWar::ConfigureRenderFilter()
{
	UWorld* World = GetWorld();
	UMassBattleFogRenderSubsystem* RenderFilter = World ? World->GetSubsystem<UMassBattleFogRenderSubsystem>() : nullptr;
	if (!RenderFilter)
	{
		if (bFogActive)
		{
			UE_LOG(LogMassBattleFrameFog, Fatal,
				TEXT("A placed MassBattleFrameFogOfWar actor requires UMassBattleFogRenderSubsystem."));
		}
		return;
	}

	if (bFogActive && !bWorldMaskLayoutValid)
	{
		UE_LOG(LogMassBattleFrameFog, Fatal,
			TEXT("Active MassBattleFrameFogOfWar has no valid visibility-mask layout."));
	}
	RenderFilter->Configure(
		bFogActive,
		bDebugRevealAll,
		ViewingTeamIndex,
		AlliedTeamIndices,
		TemporaryVisionRadius,
		FogOpacity,
		WorldMaskMin,
		WorldMaskCellSize,
		WorldMaskDimensions,
		CameraRenderCullMin,
		CameraRenderCullMax,
		AttackRevealDuration,
		UnitVisibilityConvergenceRateHz,
		UnitVisibilityRemovalDelay);
}

bool AMassBattleFrameFogOfWar::ResolveWorldMaskLayout()
{
	UWorld* World = GetWorld();
	const UMassBattleHashGridSubsystem* HashGrid = World ? World->GetSubsystem<UMassBattleHashGridSubsystem>() : nullptr;
	if (!HashGrid)
	{
		return false;
	}

	FVector2D RegionMin(-SceneFogFallbackMapSizeUU * 0.5, -SceneFogFallbackMapSizeUU * 0.5);
	FVector2D RegionMax(SceneFogFallbackMapSizeUU * 0.5, SceneFogFallbackMapSizeUU * 0.5);
	bool bFoundWorldRegion = false;
	for (TActorIterator<AMapRegion> It(World); It; ++It)
	{
		const AMapRegion* Region = *It;
		if (!Region || !Region->RegionComponent)
		{
			continue;
		}
		const FBoxSphereBounds Bounds = Region->RegionComponent->Bounds;
		if (Bounds.BoxExtent.X > 0.0 && Bounds.BoxExtent.Y > 0.0)
		{
			RegionMin = FVector2D(Bounds.Origin.X - Bounds.BoxExtent.X, Bounds.Origin.Y - Bounds.BoxExtent.Y);
			RegionMax = FVector2D(Bounds.Origin.X + Bounds.BoxExtent.X, Bounds.Origin.Y + Bounds.BoxExtent.Y);
			bFoundWorldRegion = true;
			break;
		}
	}

	if (!bFoundWorldRegion)
	{
		FConfigFile Config;
		Config.Read(GetSceneFogMapRegionPath(World));
		float OriginX = static_cast<float>(RegionMin.X);
		float OriginY = static_cast<float>(RegionMin.Y);
		float SizeX = SceneFogFallbackMapSizeUU;
		float SizeY = SceneFogFallbackMapSizeUU;
		Config.GetFloat(MapRegionSection, TEXT("OriginX"), OriginX);
		Config.GetFloat(MapRegionSection, TEXT("OriginY"), OriginY);
		Config.GetFloat(MapRegionSection, TEXT("SizeX"), SizeX);
		Config.GetFloat(MapRegionSection, TEXT("SizeY"), SizeY);
		RegionMin = FVector2D(OriginX, OriginY);
		RegionMax = RegionMin + FVector2D(FMath::Max(1.0f, SizeX), FMath::Max(1.0f, SizeY));
	}

	const FVector2D GridOrigin(HashGrid->GridOrigin.X, HashGrid->GridOrigin.Y);
	const FVector2D CellSize(
		FMath::Max(1.0, HashGrid->AgentCellSize.X),
		FMath::Max(1.0, HashGrid->AgentCellSize.Y));

	FVector2D RequiredMin = RegionMin;
	FVector2D RequiredMax = RegionMax;
	{
		TArray<FVector2D, TInlineAllocator<4>> GroundCorners;
		APlayerController* PlayerController = World->GetFirstPlayerController();
		int32 ViewportWidth = 0;
		int32 ViewportHeight = 0;
		if (PlayerController)
		{
			PlayerController->GetViewportSize(ViewportWidth, ViewportHeight);
		}
		if (PlayerController && ViewportWidth > 0 && ViewportHeight > 0)
		{
			const FVector2D ScreenCorners[4] =
			{
				FVector2D(0.0, 0.0),
				FVector2D(static_cast<double>(ViewportWidth), 0.0),
				FVector2D(0.0, static_cast<double>(ViewportHeight)),
				FVector2D(static_cast<double>(ViewportWidth), static_cast<double>(ViewportHeight))
			};
			for (const FVector2D& ScreenCorner : ScreenCorners)
			{
				FVector RayOrigin;
				FVector RayDirection;
				if (!PlayerController->DeprojectScreenPositionToWorld(
					static_cast<float>(ScreenCorner.X),
					static_cast<float>(ScreenCorner.Y),
					RayOrigin,
					RayDirection)
					|| FMath::Abs(RayDirection.Z) <= UE_SMALL_NUMBER)
				{
					continue;
				}
				const double RayDistance = (SceneFogProjectionPlaneZ - RayOrigin.Z) / RayDirection.Z;
				if (RayDistance > 0.0)
				{
					const FVector GroundPoint = RayOrigin + RayDirection * RayDistance;
					GroundCorners.Add(FVector2D(GroundPoint.X, GroundPoint.Y));
				}
			}
		}

		bool bHasUsableGroundFootprint = false;
		if (GroundCorners.Num() >= 2)
		{
			RequiredMin = GroundCorners[0];
			RequiredMax = GroundCorners[0];
			for (const FVector2D& Corner : GroundCorners)
			{
				RequiredMin.X = FMath::Min(RequiredMin.X, Corner.X);
				RequiredMin.Y = FMath::Min(RequiredMin.Y, Corner.Y);
				RequiredMax.X = FMath::Max(RequiredMax.X, Corner.X);
				RequiredMax.Y = FMath::Max(RequiredMax.Y, Corner.Y);
			}
			bHasUsableGroundFootprint = RequiredMax.X - RequiredMin.X > 1.0
				&& RequiredMax.Y - RequiredMin.Y > 1.0;
		}
		if (!bHasUsableGroundFootprint)
		{
			const FVector CameraLocation = PlayerController && PlayerController->PlayerCameraManager
				? PlayerController->PlayerCameraManager->GetCameraLocation()
				: FVector::ZeroVector;
			const FVector2D HalfExtent(FMath::Max(1.0f, CameraMaskFallbackHalfExtentUU));
			RequiredMin = FVector2D(CameraLocation.X, CameraLocation.Y) - HalfExtent;
			RequiredMax = FVector2D(CameraLocation.X, CameraLocation.Y) + HalfExtent;
		}

		// One HashGrid cell is the complete spatial broad-phase guard. The former
		// multi-cell + 1200 uu visual padding kept off-screen friendly units in the
		// heavy Active query and traded frame time for smoother transitions.
		const double RenderCullMargin = FMath::Max(CellSize.X, CellSize.Y);
		const double CullRecenterGuard = FMath::Max(
			FMath::Max(CellSize.X, CellSize.Y),
			RenderCullMargin * 0.5);
		const bool bExistingCullInsideRegion = bCameraRenderCullInitialized
			&& CameraRenderCullMin.X >= RegionMin.X
			&& CameraRenderCullMin.Y >= RegionMin.Y
			&& CameraRenderCullMax.X <= RegionMax.X
			&& CameraRenderCullMax.Y <= RegionMax.Y;
		const bool bFootprintInsideCullGuard = bExistingCullInsideRegion
			&& RequiredMin.X >= CameraRenderCullMin.X + CullRecenterGuard
			&& RequiredMin.Y >= CameraRenderCullMin.Y + CullRecenterGuard
			&& RequiredMax.X <= CameraRenderCullMax.X - CullRecenterGuard
			&& RequiredMax.Y <= CameraRenderCullMax.Y - CullRecenterGuard;
		if (!bFootprintInsideCullGuard)
		{
			// Snap the broad-phase window to HashGrid cells and retain it until the
			// camera crosses an inner guard. Sub-cell camera motion must not trigger
			// a serial HashGrid + visibility refresh every 24 Hz update.
			const FVector2D DesiredCullMin = RequiredMin - FVector2D(RenderCullMargin);
			const FVector2D DesiredCullMax = RequiredMax + FVector2D(RenderCullMargin);
			CameraRenderCullMin.X = GridOrigin.X
				+ FMath::FloorToDouble((DesiredCullMin.X - GridOrigin.X) / CellSize.X) * CellSize.X;
			CameraRenderCullMin.Y = GridOrigin.Y
				+ FMath::FloorToDouble((DesiredCullMin.Y - GridOrigin.Y) / CellSize.Y) * CellSize.Y;
			CameraRenderCullMax.X = GridOrigin.X
				+ FMath::CeilToDouble((DesiredCullMax.X - GridOrigin.X) / CellSize.X) * CellSize.X;
			CameraRenderCullMax.Y = GridOrigin.Y
				+ FMath::CeilToDouble((DesiredCullMax.Y - GridOrigin.Y) / CellSize.Y) * CellSize.Y;
			CameraRenderCullMin.X = FMath::Max(CameraRenderCullMin.X, RegionMin.X);
			CameraRenderCullMin.Y = FMath::Max(CameraRenderCullMin.Y, RegionMin.Y);
			CameraRenderCullMax.X = FMath::Min(CameraRenderCullMax.X, RegionMax.X);
			CameraRenderCullMax.Y = FMath::Min(CameraRenderCullMax.Y, RegionMax.Y);
			bCameraRenderCullInitialized = true;
		}

		const double Margin = FMath::Max(0.0f, TemporaryVisionRadius) + RenderCullMargin;
		RequiredMin -= FVector2D(Margin);
		RequiredMax += FVector2D(Margin);
		RequiredMin.X = FMath::Max(RequiredMin.X, RegionMin.X);
		RequiredMin.Y = FMath::Max(RequiredMin.Y, RegionMin.Y);
		RequiredMax.X = FMath::Min(RequiredMax.X, RegionMax.X);
		RequiredMax.Y = FMath::Min(RequiredMax.Y, RegionMax.Y);
	}

	const int64 RequiredMinCellX = FMath::FloorToInt64((RequiredMin.X - GridOrigin.X) / CellSize.X);
	const int64 RequiredMinCellY = FMath::FloorToInt64((RequiredMin.Y - GridOrigin.Y) / CellSize.Y);
	const int64 RequiredMaxCellX = FMath::CeilToInt64((RequiredMax.X - GridOrigin.X) / CellSize.X);
	const int64 RequiredMaxCellY = FMath::CeilToInt64((RequiredMax.Y - GridOrigin.Y) / CellSize.Y);
	const int64 RequiredWidth = RequiredMaxCellX - RequiredMinCellX;
	const int64 RequiredHeight = RequiredMaxCellY - RequiredMinCellY;
	if (RequiredWidth <= 0 || RequiredHeight <= 0)
	{
		return false;
	}

	int64 MinCellX = RequiredMinCellX;
	int64 MinCellY = RequiredMinCellY;
	int64 Width = RequiredWidth;
	int64 Height = RequiredHeight;
	{
		const int64 GuardCells = FMath::Max(1, CameraMaskGuardBandCells);
		const int64 DesiredWidth = FMath::RoundUpToPowerOfTwo(
			static_cast<uint64>(FMath::Max<int64>(1, RequiredWidth + GuardCells * 2)));
		const int64 DesiredHeight = FMath::RoundUpToPowerOfTwo(
			static_cast<uint64>(FMath::Max<int64>(1, RequiredHeight + GuardCells * 2)));

		const bool bCurrentLayoutCompatible = WorldMaskDimensions.X > 0
			&& WorldMaskDimensions.Y > 0
			&& WorldMaskCellSize.Equals(CellSize, UE_SMALL_NUMBER);
		if (bCurrentLayoutCompatible)
		{
			const int64 CurrentMinCellX = FMath::RoundToInt64((WorldMaskMin.X - GridOrigin.X) / CellSize.X);
			const int64 CurrentMinCellY = FMath::RoundToInt64((WorldMaskMin.Y - GridOrigin.Y) / CellSize.Y);
			const int64 InnerMaxCellX = CurrentMinCellX + WorldMaskDimensions.X - GuardCells;
			const int64 InnerMaxCellY = CurrentMinCellY + WorldMaskDimensions.Y - GuardCells;
			const bool bInsideGuardBand = RequiredMinCellX >= CurrentMinCellX + GuardCells
				&& RequiredMinCellY >= CurrentMinCellY + GuardCells
				&& RequiredMaxCellX <= InnerMaxCellX
				&& RequiredMaxCellY <= InnerMaxCellY;
			if (bInsideGuardBand)
			{
				return true;
			}
			Width = FMath::Max<int64>(WorldMaskDimensions.X, DesiredWidth);
			Height = FMath::Max<int64>(WorldMaskDimensions.Y, DesiredHeight);
		}
		else
		{
			Width = DesiredWidth;
			Height = DesiredHeight;
		}

		const int64 RequiredCenterCellX = (RequiredMinCellX + RequiredMaxCellX) / 2;
		const int64 RequiredCenterCellY = (RequiredMinCellY + RequiredMaxCellY) / 2;
		MinCellX = RequiredCenterCellX - Width / 2;
		MinCellY = RequiredCenterCellY - Height / 2;
	}

	const int64 CellCount = Width * Height;
	if (Width <= 0 || Height <= 0
		|| Width > MaxWorldMaskDimension || Height > MaxWorldMaskDimension
		|| CellCount <= 0 || CellCount > MaxWorldMaskCells)
	{
		UE_LOG(LogMassBattleFrameFog, Fatal,
			TEXT("World visibility mask requires %lldx%lld HashGrid cells (%lld total); safety limits are %d per dimension and %d total. Active fog has no unfiltered fallback."),
			Width, Height, CellCount, MaxWorldMaskDimension, MaxWorldMaskCells);
		return false;
	}

	WorldMaskCellSize = CellSize;
	WorldMaskDimensions = FIntPoint(static_cast<int32>(Width), static_cast<int32>(Height));
	WorldMaskMin = GridOrigin + FVector2D(MinCellX * CellSize.X, MinCellY * CellSize.Y);
	WorldMaskSize = FVector2D(Width * CellSize.X, Height * CellSize.Y);
	return true;
}

bool AMassBattleFrameFogOfWar::EnsureWorldVisibilityMask()
{
	const FIntPoint PreviousDimensions = WorldMaskDimensions;
	const FVector2D PreviousCellSize = WorldMaskCellSize;
	const FVector2D PreviousWorldMin = WorldMaskMin;
	const FVector2D PreviousWorldSize = WorldMaskSize;
	bWorldMaskLayoutValid = ResolveWorldMaskLayout();
	if (!bWorldMaskLayoutValid)
	{
		return false;
	}

	if (!WorldVisibilityMask)
	{
		WorldVisibilityMask = NewObject<UTextureRenderTarget2D>(this, TEXT("MassBattleFogWorldVisibilityMask"), RF_Transient);
	}
	if (!WorldVisibilityMask)
	{
		bWorldMaskLayoutValid = false;
		return false;
	}

	const bool bNeedsInitialization = WorldVisibilityMask->SizeX != WorldMaskDimensions.X
		|| WorldVisibilityMask->SizeY != WorldMaskDimensions.Y
		|| PreviousDimensions != WorldMaskDimensions
		|| PreviousCellSize != WorldMaskCellSize;
	if (bNeedsInitialization)
	{
		WorldVisibilityMask->ClearColor = FLinearColor::Black;
		WorldVisibilityMask->bForceLinearGamma = true;
		WorldVisibilityMask->AddressX = TA_Clamp;
		WorldVisibilityMask->AddressY = TA_Clamp;
		WorldVisibilityMask->Filter = TF_Nearest;
		// UE 5.8's UTextureRenderTarget2D whitelist uses PF_G8 for a single
		// 8-bit UNORM render-target channel (PF_R8 is buffer-only here).
		WorldVisibilityMask->InitCustomFormat(WorldMaskDimensions.X, WorldMaskDimensions.Y, PF_G8, false);
		WorldVisibilityMask->UpdateResourceImmediate(true);
	}

	const bool bWorldLayoutChanged = PreviousDimensions != WorldMaskDimensions
		|| PreviousCellSize != WorldMaskCellSize
		|| PreviousWorldMin != WorldMaskMin
		|| PreviousWorldSize != WorldMaskSize;
	bForceLogicMaskUpdate |= bNeedsInitialization || bWorldLayoutChanged;
	return true;
}

void AMassBattleFrameFogOfWar::ConsumeGpuMaskReadback()
{
	if (!MaskReadbackMailbox.IsValid())
	{
		return;
	}

	FMassBattleFrameFogMaskReadbackData ReadbackData;
	if (!MaskReadbackMailbox->Consume_GameThread(ReadbackData))
	{
		return;
	}
	if (ReadbackData.Dimensions != WorldMaskDimensions
		|| ReadbackData.WorldMin != WorldMaskMin
		|| ReadbackData.CellSize != WorldMaskCellSize)
	{
		// A map/layout change can complete an older asynchronous pair after the
		// actor has already rebuilt its HashGrid-aligned render target.
		return;
	}

	UWorld* World = GetWorld();
	if (UMassBattleFogRenderSubsystem* RenderFilter = World ? World->GetSubsystem<UMassBattleFogRenderSubsystem>() : nullptr)
	{
		RenderFilter->UpdateGpuVisibilityStates(
			MoveTemp(ReadbackData.VisibilityStates),
			ReadbackData.Dimensions,
			ReadbackData.WorldMin,
			ReadbackData.CellSize);
	}
}
