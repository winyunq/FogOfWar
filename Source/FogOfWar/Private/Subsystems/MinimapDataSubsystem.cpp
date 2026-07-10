// Copyright Winyunq, 2025. All Rights Reserved.

#include "Subsystems/MinimapDataSubsystem.h"
#include "FogOfWarMassBinding.h"
#include "Subsystems/MassBattleHashGridSubsystem.h"
#include "MassEntitySubsystem.h"
#include "MassFogOfWarFragments.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
	constexpr float DefaultMapRegionSizeUU = 65536.0f;
	constexpr int32 DefaultMinimapResolution = 256;
	constexpr float DefaultVisionTileSize = 100.0f;
	const TCHAR* MinimapDataMapRegionDirectoryName = TEXT("MapRegion");
	const TCHAR* MinimapDataMapRegionFileName = TEXT("MapRegion.ini");
	const TCHAR* MinimapDataMapRegionSectionName = TEXT("MapRegion");
	const TCHAR* MinimapPerformanceCsvRelativePath = TEXT("Logs/FogOfWar_MinimapPerf.csv");

	FORCEINLINE bool IsValidMinimapResolution(const FIntPoint& Resolution)
	{
		return Resolution.X > 0 && Resolution.Y > 0;
	}

	FORCEINLINE void ApplyFallbackMinimapResolution(FIntPoint& Resolution)
	{
		if (Resolution.X <= 0) Resolution.X = DefaultMinimapResolution;
		if (Resolution.Y <= 0) Resolution.Y = DefaultMinimapResolution;
	}

	FORCEINLINE float SecondsToMs(const double Seconds)
	{
		return static_cast<float>(Seconds * 1000.0);
	}

	FORCEINLINE float CyclesToMs(const uint64 Cycles)
	{
		return static_cast<float>(FPlatformTime::ToMilliseconds64(Cycles));
	}

	FString GetMinimapDataCleanMapName(const UWorld* World)
	{
		if (!World)
		{
			return TEXT("Default");
		}

		FString MapName = World->GetMapName();
		MapName.RemoveFromStart(World->StreamingLevelsPrefix);
		return MapName.IsEmpty() ? FString(TEXT("Default")) : MapName;
	}

	FString GetMinimapDataMapRegionIniPath(const UWorld* World)
	{
		return FPaths::ProjectConfigDir() / MinimapDataMapRegionDirectoryName / GetMinimapDataCleanMapName(World) / MinimapDataMapRegionFileName;
	}

	void ReadMapRegionIni(
		const UWorld* World,
		FVector2D& OutGridOrigin,
		FVector2D& OutGridSize)
	{
		OutGridOrigin = FVector2D(-DefaultMapRegionSizeUU * 0.5f, -DefaultMapRegionSizeUU * 0.5f);
		OutGridSize = FVector2D(DefaultMapRegionSizeUU, DefaultMapRegionSizeUU);

		FConfigFile IniFile;
		IniFile.Read(GetMinimapDataMapRegionIniPath(World));

		float OriginX = OutGridOrigin.X;
		float OriginY = OutGridOrigin.Y;
		float SizeX = OutGridSize.X;
		float SizeY = OutGridSize.Y;
		IniFile.GetFloat(MinimapDataMapRegionSectionName, TEXT("OriginX"), OriginX);
		IniFile.GetFloat(MinimapDataMapRegionSectionName, TEXT("OriginY"), OriginY);
		IniFile.GetFloat(MinimapDataMapRegionSectionName, TEXT("SizeX"), SizeX);
		IniFile.GetFloat(MinimapDataMapRegionSectionName, TEXT("SizeY"), SizeY);

		if (SizeX > 0.0f && SizeY > 0.0f)
		{
			OutGridOrigin = FVector2D(OriginX, OriginY);
			OutGridSize = FVector2D(SizeX, SizeY);
		}
	}

	bool IsEntitySelectedByOptionalRtsSubsystem(const ULocalPlayer* LocalPlayer, const FEntityHandle& EntityHandle)
	{
		if (!LocalPlayer)
		{
			return false;
		}

		UClass* SelectionSubsystemClass = FindObject<UClass>(nullptr, TEXT("/Script/RTSInputSystem.RTSSelectionSubsystem"));
		if (!SelectionSubsystemClass)
		{
			return false;
		}

		ULocalPlayerSubsystem* SelectionSubsystem = LocalPlayer->GetSubsystemBase(SelectionSubsystemClass);
		if (!SelectionSubsystem)
		{
			return false;
		}

		UFunction* IsEntitySelectedFunction = SelectionSubsystem->FindFunction(TEXT("IsEntitySelected"));
		if (!IsEntitySelectedFunction)
		{
			return false;
		}

		struct FIsEntitySelectedParams
		{
			FEntityHandle Handle;
			bool ReturnValue = false;
		};

		FIsEntitySelectedParams Params;
		Params.Handle = EntityHandle;
		SelectionSubsystem->ProcessEvent(IsEntitySelectedFunction, &Params);
		return Params.ReturnValue;
	}

}

// Define the static singleton instance pointer.
UMinimapDataSubsystem* UMinimapDataSubsystem::SingletonInstance = nullptr;

void UMinimapDataSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	SingletonInstance = this;
	RebuildTeamDisplayColorCache();
	if (!ApplyMinimapGridFromMapRegion())
	{
		UE_LOG(LogTemp, Warning, TEXT("[MinimapDataSubsystem] Failed to initialize minimap bounds from current-level MapRegion ini."));
	}
}

void UMinimapDataSubsystem::Deinitialize()
{
	SingletonInstance = nullptr;
	Super::Deinitialize();
}

// Deprecated: UpdateVisionGridParameters removed for strict decoupling.

void UMinimapDataSubsystem::SetMinimapResolution(const FIntPoint& NewResolution)
{
	bMinimapResolutionExplicitlySet = IsValidMinimapResolution(NewResolution);
	MinimapGridResolution = bMinimapResolutionExplicitlySet ? NewResolution : FIntPoint::ZeroValue;
	MinimapTileSize = FVector2D::ZeroVector;
	MinimapTiles.Reset();

	ApplyMinimapGridFromMapRegion();
}

bool UMinimapDataSubsystem::ApplyMinimapGridFromCurrentBounds()
{
	if (GridSize.X <= 0.0f || GridSize.Y <= 0.0f)
	{
		MinimapTileSize = FVector2D::ZeroVector;
		MinimapTiles.Reset();
		return false;
	}

	if (!IsValidMinimapResolution(MinimapGridResolution))
	{
		ApplyFallbackMinimapResolution(MinimapGridResolution);
	}

	MinimapTileSize = FVector2D(GridSize.X / MinimapGridResolution.X, GridSize.Y / MinimapGridResolution.Y);
	MinimapTiles.SetNum(MinimapGridResolution.X * MinimapGridResolution.Y);
	return true;
}

bool UMinimapDataSubsystem::ApplyMinimapGridFromMapRegion()
{
	ReadMapRegionIni(GetWorld(), GridBottomLeftWorldLocation, GridSize);
	MinimapGridResolution = bMinimapResolutionExplicitlySet ? MinimapGridResolution : FIntPoint::ZeroValue;

	if (UMassBattleHashGridSubsystem* HashGrid = UMassBattleHashGridSubsystem::GetPtr(GetWorld()))
	{
		if (HashGrid->AgentGrid.Num() == 0)
		{
			HashGrid->GridOrigin.X = GridBottomLeftWorldLocation.X;
			HashGrid->GridOrigin.Y = GridBottomLeftWorldLocation.Y;
		}
	}

	const bool bApplied = ApplyMinimapGridFromCurrentBounds();
	UE_LOG(LogTemp, Log, TEXT("[MinimapDataSubsystem] Initialized minimap bounds from MapRegion ini: Origin=%s Size=%s Resolution=%s"),
		*GridBottomLeftWorldLocation.ToString(), *GridSize.ToString(), *MinimapGridResolution.ToString());
	return bApplied;
}


void UMinimapDataSubsystem::SyncFogOfWarRuntimeOptions(float InVisionBlockingDeltaHeightThreshold, float InVisionUpdateWorldDistanceThreshold, bool bInDebugStressTestIgnoreCache, bool bInDebugStressTestMinimap)
{
	VisionBlockingDeltaHeightThreshold = InVisionBlockingDeltaHeightThreshold;
	VisionUpdateWorldDistanceThreshold = InVisionUpdateWorldDistanceThreshold;
	bDebugStressTestIgnoreCache = bInDebugStressTestIgnoreCache;
	bDebugStressTestMinimap = bInDebugStressTestMinimap;
}

FLinearColor UMinimapDataSubsystem::GetTeamColor(int32 TeamIndex) const
{
	return TeamColors.IsValidIndex(TeamIndex) ? TeamColors[TeamIndex] : DefaultTeamColor;
}

FLinearColor UMinimapDataSubsystem::BuildCachedTeamDisplayColor(const FLinearColor& TeamColor, float TargetLength) const
{
	const FVector3f RawRgb(
		FMath::Max(0.0f, TeamColor.R),
		FMath::Max(0.0f, TeamColor.G),
		FMath::Max(0.0f, TeamColor.B));
	const float RawLength = RawRgb.Size();
	const float Scale = bNormalizeTeamColorDirection && RawLength > KINDA_SMALL_NUMBER
		? TargetLength / RawLength
		: TargetLength;
	return FLinearColor(
		FMath::Clamp(RawRgb.X * Scale, 0.0f, 1.0f),
		FMath::Clamp(RawRgb.Y * Scale, 0.0f, 1.0f),
		FMath::Clamp(RawRgb.Z * Scale, 0.0f, 1.0f),
		TeamColor.A);
}

void UMinimapDataSubsystem::RebuildTeamDisplayColorCache()
{
	NormalTeamDisplayColors.Reset(TeamColors.Num());
	SelectedTeamDisplayColors.Reset(TeamColors.Num());
	TeamDisplayColorsByState.Reset(TeamColors.Num() * 2);

	for (const FLinearColor& TeamColor : TeamColors)
	{
		const FLinearColor NormalColor = BuildCachedTeamDisplayColor(TeamColor, NormalUnitColorLength);
		const FLinearColor SelectedColor = BuildCachedTeamDisplayColor(TeamColor, SelectedUnitColorLength);
		NormalTeamDisplayColors.Add(NormalColor);
		SelectedTeamDisplayColors.Add(SelectedColor);
		TeamDisplayColorsByState.Add(NormalColor);
		TeamDisplayColorsByState.Add(SelectedColor);
	}

	DefaultNormalTeamDisplayColor = BuildCachedTeamDisplayColor(DefaultTeamColor, NormalUnitColorLength);
	DefaultSelectedTeamDisplayColor = BuildCachedTeamDisplayColor(DefaultTeamColor, SelectedUnitColorLength);
	DefaultTeamDisplayColorsByState.Reset(2);
	DefaultTeamDisplayColorsByState.Add(DefaultNormalTeamDisplayColor);
	DefaultTeamDisplayColorsByState.Add(DefaultSelectedTeamDisplayColor);
}

void UMinimapDataSubsystem::SyncMinimapDisplayOptions(
	const FLinearColor& InDefaultTeamColor,
	const TArray<FLinearColor>& InTeamColors,
	bool bInNormalizeTeamColorDirection,
	float InNormalUnitColorLength,
	float InSelectedUnitColorLength,
	const FLinearColor& InCombatUnitColor,
	bool bInEnableCombatColorFlash,
	float InCombatColorFlashHz,
	float InDefaultUnitPixelRadius)
{
	DefaultTeamColor = InDefaultTeamColor;
	TeamColors = InTeamColors;
	bNormalizeTeamColorDirection = bInNormalizeTeamColorDirection;
	NormalUnitColorLength = FMath::Max(0.0f, InNormalUnitColorLength);
	SelectedUnitColorLength = FMath::Max(0.0f, InSelectedUnitColorLength);
	CombatUnitColor = InCombatUnitColor;
	bEnableCombatColorFlash = bInEnableCombatColorFlash;
	CombatColorFlashHz = FMath::Max(0.01f, InCombatColorFlashHz);
	DefaultMinimapUnitPixelRadius = FMath::Max(0.0f, InDefaultUnitPixelRadius);
	RebuildTeamDisplayColorCache();
}

void UMinimapDataSubsystem::SyncVisionGridParameters(const FVector2D&, const FVector2D&, float InVisionTileSize, const FIntPoint& InVisionResolution)
{
	bVisionGridActive = false;

	ReadMapRegionIni(GetWorld(), GridBottomLeftWorldLocation, GridSize);
	// Fallback to the plugin's historical default tile size (100 cm) to keep behavior
	// predictable when callers pass an invalid value, while preserving reasonable density.
	const float SafeVisionTileSize = InVisionTileSize > 0.0f ? InVisionTileSize : DefaultVisionTileSize;
	VisionTileSize = SafeVisionTileSize;

	VisionGridResolution = InVisionResolution;
	if (VisionGridResolution.X <= 0)
	{
		VisionGridResolution.X = FMath::Max(1, FMath::CeilToInt32(GridSize.X / SafeVisionTileSize));
	}
	if (VisionGridResolution.Y <= 0)
	{
		VisionGridResolution.Y = FMath::Max(1, FMath::CeilToInt32(GridSize.Y / SafeVisionTileSize));
	}

	ApplyMinimapGridFromCurrentBounds();

	const int32 NumVisionTiles = VisionGridResolution.X * VisionGridResolution.Y;
	if (GridSize.X > 0.0f && GridSize.Y > 0.0f && SafeVisionTileSize > 0.0f && NumVisionTiles > 0)
	{
		VisionTiles.SetNum(NumVisionTiles);
		for (FTile& Tile : VisionTiles)
		{
			Tile = FTile();
		}
	}
	else
	{
		VisionTiles.Reset();
	}
}

void UMinimapDataSubsystem::SyncWorldBounds(const FVector2D&, const FVector2D&)
{
	bVisionGridActive = false;
	ApplyMinimapGridFromMapRegion();
}

void UMinimapDataSubsystem::SetVisionGridActive(bool bInActive)
{
	bVisionGridActive = bInActive && IsVisionGridReady();
}

bool UMinimapDataSubsystem::IsVisionGridReady() const
{
	return
		VisionTileSize > 0.0f &&
		VisionGridResolution.X > 0 &&
		VisionGridResolution.Y > 0 &&
		VisionTiles.Num() == VisionGridResolution.X * VisionGridResolution.Y;
}

bool UMinimapDataSubsystem::IsLocationVisible(const FVector& WorldLocation) const
{
	const FIntPoint TileIJ = ConvertWorldLocationToVisionTileIJ_Static(FVector2D(WorldLocation));
	if (!IsVisionGridIJValid_Static(TileIJ))
	{
		return false;
	}

	return GetVisionTile(TileIJ).VisibilityCounter > 0;
}

FTile& UMinimapDataSubsystem::GetVisionTile(int32 GlobalIndex)
{
	return VisionTiles[GlobalIndex];
}

const FTile& UMinimapDataSubsystem::GetVisionTile(int32 GlobalIndex) const
{
	return VisionTiles[GlobalIndex];
}

FTile& UMinimapDataSubsystem::GetVisionTile(FIntPoint IJ)
{
	checkSlow(IsVisionGridIJValid_Static(IJ));
	return GetVisionTile(GetVisionGridGlobalIndex_Static(IJ));
}

const FTile& UMinimapDataSubsystem::GetVisionTile(FIntPoint IJ) const
{
	checkSlow(IsVisionGridIJValid_Static(IJ));
	return GetVisionTile(GetVisionGridGlobalIndex_Static(IJ));
}

bool UMinimapDataSubsystem::IsBlockingVision(float ObserverHeight, float PotentialObstacleHeight) const
{
	return PotentialObstacleHeight - ObserverHeight > VisionBlockingDeltaHeightThreshold;
}

void UMinimapDataSubsystem::InitMinimapGrid(const FVector2D&, const FVector2D&, const FIntPoint& InResolution)
{
	bMinimapResolutionExplicitlySet = IsValidMinimapResolution(InResolution);
	MinimapGridResolution = bMinimapResolutionExplicitlySet ? InResolution : FIntPoint::ZeroValue;
	
	if (!ApplyMinimapGridFromMapRegion())
	{
		UE_LOG(LogTemp, Warning, TEXT("[MinimapDataSubsystem] InitMinimapGrid failed to apply MapRegion bounds."));
	}

	UE_LOG(LogTemp, Log, TEXT("[MinimapDataSubsystem] Initialized Grid from MapRegion. Origin:%s, Size:%s, Res:%s, TileSize:%s"),
		*GridBottomLeftWorldLocation.ToString(), *GridSize.ToString(), *MinimapGridResolution.ToString(), *MinimapTileSize.ToString());

	if (UMassBattleHashGridSubsystem* HashGrid = GetWorld()->GetSubsystem<UMassBattleHashGridSubsystem>())
	{
		// Sync with MassBattleHashGridSubsystem
		// Align HashGrid Origin with Minimap Origin to ensure consistent spatial hashing.
		HashGrid->GridOrigin.X = GridBottomLeftWorldLocation.X;
		HashGrid->GridOrigin.Y = GridBottomLeftWorldLocation.Y;
		UE_LOG(LogTemp, Log, TEXT("[MinimapDataSubsystem] Synced MassBattleHashGrid Origin to: %s"), *HashGrid->GridOrigin.ToString());
	}
}

void UMinimapDataSubsystem::UpdateMinimapFromHashGrid(FVector CenterLocation, int32 BlockRadius)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("Minimap.UpdateFromHashGrid");

	if (!GetWorld()) return;
	const double TotalStartTime = FPlatformTime::Seconds();
	bDrawCombatColorThisUpdate = true;
	if (bEnableCombatColorFlash)
	{
		const float CombatPhase = FMath::Fmod(GetWorld()->GetTimeSeconds() * CombatColorFlashHz, 1.0f);
		bDrawCombatColorThisUpdate = CombatPhase < 0.5f;
	}

	const bool bCollectStats = bEnableMinimapPerformanceStats;
	const bool bCollectDetailedStats = bCollectStats && bEnableDetailedMinimapPerformanceStats;
	FMinimapHashGridPerfStats Stats;
	
	// 1. 获取必要的子系统
	UMassBattleHashGridSubsystem* HashGrid = UMassBattleHashGridSubsystem::GetPtr(GetWorld());
	UMassEntitySubsystem* EntitySubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	if (!HashGrid || !EntitySubsystem) return;
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	const ULocalPlayer* LocalPlayer = nullptr;
	if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* PlayerController = World->GetFirstPlayerController())
		{
			LocalPlayer = PlayerController->GetLocalPlayer();
		}
	}
	Stats.HashGridBlocks = HashGrid->AgentGrid.Num();
	Stats.AgentCellSize = HashGrid->AgentCellSize;
	Stats.AgentBlockDimensions = HashGrid->AgentBlockDimensionsCache;

	// 2. 清空并重置小地图数据
	// ResetAllTiles is not exposed, so we iterate. 
	// Optimally we should use a TArray::Init or Memzero if struct is simple, but we have colors/float.
	// For 256x256 (65k), a loop is acceptable.
	const int32 TotalTiles = MinimapTiles.Num();
	if (TotalTiles == 0) return;

	// ParallelFor could be used here for clearing if needed, but simple loop is safer for now.
	const double ClearStartTime = FPlatformTime::Seconds();
	for (FMinimapTile& Tile : MinimapTiles)
	{
		Tile.PreviousColor = Tile.Color;
		Tile.UnitCount = 0;
		Tile.MaxSightRadius = 0.0f;
		Tile.MaxIconSize = 0.0f;
		Tile.RepresentativeInfluence = -FLT_MAX;
		Tile.Color = FLinearColor::Transparent;
		Tile.bHasSelectedUnit = false;
		Tile.bHasCombatUnit = false;
	}
	Stats.ClearTilesMs = SecondsToMs(FPlatformTime::Seconds() - ClearStartTime);

	// 3. 准备坐标转换参数 (Cache for performance)
	const FVector2D GridOrigin = GridBottomLeftWorldLocation;
	const FVector2D GridSizeVal = GridSize;
	const FIntPoint MapRes = MinimapGridResolution;
	const FVector2D TileSize = MinimapTileSize;
	
	FVector FirstAgentLoc = FVector::ZeroVector;

	// 4. LOD2 - 遍历所有活跃的 Block (Active Blocks)
	const double TraverseStartTime = FPlatformTime::Seconds();
	for (auto It = HashGrid->AgentGrid.CreateConstIterator(); It; ++It)
	{
		const FIntVector& BlockCoord = It.Key();
		const TSharedPtr<FAgentGridBlock>& Block = It.Value();

		if (!Block.IsValid()) continue;
		Stats.ValidBlocks++;

		const FIntVector BlockBaseGlobalCellCoord = BlockCoord * HashGrid->AgentBlockDimensionsCache;

		// 5. LOD1 - 遍历 Block 内的活跃 Cell (Occupied Cells)
		for (TConstSetBitIterator<> CellIt(Block->OccupiedCells.OccupiedCellBitArray); CellIt; ++CellIt)
		{
			Stats.OccupiedCells++;
			const int32 CellIndex = CellIt.GetIndex();
			const FHashGridAgentCell& Cell = Block->Cells[CellIndex];

			if (Cell.Agents.Num() == 0) continue;
			Stats.NonEmptyCells++;

			// ... (Coord calc same as before)
			const int32 DimX = HashGrid->AgentBlockDimensionsCache.X;
			const int32 DimY = HashGrid->AgentBlockDimensionsCache.Y;
			const int32 Z = CellIndex / (DimX * DimY);
			const int32 RemAfterZ = CellIndex % (DimX * DimY);
			const int32 Y = RemAfterZ / DimX;
			const int32 X = RemAfterZ % DimX;

			const FIntVector CellGlobalCoord = BlockBaseGlobalCellCoord + FIntVector(X, Y, Z);
			// Fix: AgentCoordToLocation returns Center, so we use it directly as the cell center reference.
			const FVector CellCenterWorld = HashGrid->AgentCoordToLocation(CellGlobalCoord);

			// 6. LOD0 - 遍历 Cell 内的 Agent
			for (const FAgentGridData& AgentData : Cell.Agents)
			{
				Stats.AgentsVisited++;
				const FVector AgentWorldPos = CellCenterWorld + AgentData.GetRelativeLocation();

				if (Stats.AgentsVisited == 1) FirstAgentLoc = AgentWorldPos;

				const float RelX = AgentWorldPos.X - GridOrigin.X;
				const float RelY = AgentWorldPos.Y - GridOrigin.Y;

				if (RelX < 0 || RelY < 0 || RelX >= GridSizeVal.X || RelY >= GridSizeVal.Y) 
				{
					Stats.SkippedOutOfBounds++;
					continue;
				}
				Stats.AgentsInBounds++;

				const int32 TileX = FMath::FloorToInt(RelX / TileSize.X);
				const int32 TileY = FMath::FloorToInt(RelY / TileSize.Y);

				if (TileX >= 0 && TileX < MapRes.X && TileY >= 0 && TileY < MapRes.Y)
				{
					const int32 TileIndex = TileX * MapRes.Y + TileY;

					// HashGrid 不与 Mass Entity 生命周期同步，Entity 可能已被销毁
					// 必须在访问任何 Fragment 前检查，否则触发 IsEntityValid 断言崩溃
					Stats.EntityValidationChecks++;
					const uint64 ValidationStartCycles = bCollectDetailedStats ? FPlatformTime::Cycles64() : 0;
					const bool bEntityValid = EntityManager.IsEntityValid(AgentData.EntityHandle);
					if (bCollectDetailedStats)
					{
						Stats.EntityValidationMs += CyclesToMs(FPlatformTime::Cycles64() - ValidationStartCycles);
					}
					if (!bEntityValid)
					{
						Stats.InvalidEntities++;
						continue;
					}

					FMinimapTile& MiniTile = MinimapTiles[TileIndex];
					MiniTile.UnitCount++;
					Stats.MinimapCellsWritten++;
					const bool bSelected = IsEntitySelectedByOptionalRtsSubsystem(LocalPlayer, AgentData.EntityHandle);
					const bool bInCombat = AgentData.bIsAttacker != 0;
					const int32 SelectedBit = static_cast<int32>(bSelected);
					const int32 CombatBit = static_cast<int32>(bInCombat && bDrawCombatColorThisUpdate);
					MiniTile.bHasSelectedUnit |= bSelected;
					MiniTile.bHasCombatUnit |= bInCombat;
					
					// Fallback defaults come from MassBattle's own unit size fragments.
					const uint64 FragmentStartCycles = bCollectDetailedStats ? FPlatformTime::Cycles64() : 0;
					const FOW_TEAM_FRAGMENT* TeamFrag = EntityManager.GetFragmentDataPtr<FOW_TEAM_FRAGMENT>(AgentData.EntityHandle);
					Stats.FragmentDataPtrCalls++;
					const int32 TeamIndex = TeamFrag ? FOW_GET_TEAM_INDEX(*TeamFrag) : INDEX_NONE;
					const float IconSize = DefaultMinimapUnitPixelRadius;

					float SightRadius = DefaultMassBattleSightRadius;
					if (const FMassVisionFragment* VisionFrag = EntityManager.GetFragmentDataPtr<FMassVisionFragment>(AgentData.EntityHandle))
					{
						Stats.AgentsWithVisionFragment++;
						SightRadius = VisionFrag->SightRadius;
					}
					Stats.FragmentDataPtrCalls++;

					MiniTile.MaxSightRadius = FMath::Max(MiniTile.MaxSightRadius, SightRadius);
					const float Influence = SightRadius + (bSelected ? 1000000.0f : 0.0f) + (bInCombat ? 100000.0f : 0.0f);
					if (Influence >= MiniTile.RepresentativeInfluence)
					{
						MiniTile.RepresentativeInfluence = Influence;
						const int32 ColorIndex = (TeamIndex << 1) | SelectedBit;
						const FLinearColor TeamStateColor = TeamDisplayColorsByState.IsValidIndex(ColorIndex)
							? TeamDisplayColorsByState[ColorIndex]
							: DefaultTeamDisplayColorsByState[SelectedBit];
						if (CombatBit != 0 && bDrawCombatColorThisUpdate)
						{
							MiniTile.Color = CombatUnitColor;
						}
						else
						{
							MiniTile.Color = TeamStateColor;
						}
						MiniTile.MaxIconSize = FMath::Max(0.0f, IconSize);
					}
					if (bCollectDetailedStats)
					{
						Stats.FragmentLookupMs += CyclesToMs(FPlatformTime::Cycles64() - FragmentStartCycles);
					}
				}
			}
		}
	}
	Stats.TraverseHashGridMs = SecondsToMs(FPlatformTime::Seconds() - TraverseStartTime);
	Stats.EstimatedTraversalAndProjectionMs = FMath::Max(0.0f, Stats.TraverseHashGridMs - Stats.EntityValidationMs - Stats.FragmentLookupMs);
	Stats.TotalMs = SecondsToMs(FPlatformTime::Seconds() - TotalStartTime);
	LastHashGridPerfStats = Stats;
	if (bCollectStats)
	{
		RecordHashGridPerfStats(Stats);
	}
}

void UMinimapDataSubsystem::RecordMinimapDrawPerfStats(const FMinimapDrawPerfStats& Stats)
{
	LastDrawPerfStats = Stats;
	if (!bEnableMinimapPerformanceStats || !GetWorld())
	{
		return;
	}

	DrawPerfAccum.TotalMs += Stats.TotalMs;
	DrawPerfAccum.LockTexturesMs += Stats.LockTexturesMs;
	DrawPerfAccum.ScanTilesMs += Stats.ScanTilesMs;
	DrawPerfAccum.UploadTexturesMs += Stats.UploadTexturesMs;
	DrawPerfAccum.DrawRenderTargetMs += Stats.DrawRenderTargetMs;
	DrawPerfAccum.SourceTilesScanned += Stats.SourceTilesScanned;
	DrawPerfAccum.ActiveTiles += Stats.ActiveTiles;
	DrawPerfAccum.EncodedUnits += Stats.EncodedUnits;
	DrawPerfAccum.EncodedVisionSources += Stats.EncodedVisionSources;
	DrawPerfAccum.TotalUnitsRepresented += Stats.TotalUnitsRepresented;
	DrawPerfAccum.MaxUnitsInSingleTile = FMath::Max(DrawPerfAccum.MaxUnitsInSingleTile, Stats.MaxUnitsInSingleTile);
	DrawPerfSampleCount++;

	const double CurrentTime = GetWorld()->GetTimeSeconds();
	if (CurrentTime - LastDrawPerfLogTime >= MinimapPerformanceLogInterval)
	{
		FlushDrawPerfStats(CurrentTime);
	}
}

void UMinimapDataSubsystem::RecordHashGridPerfStats(const FMinimapHashGridPerfStats& Stats)
{
	HashGridPerfAccum.TotalMs += Stats.TotalMs;
	HashGridPerfAccum.ClearTilesMs += Stats.ClearTilesMs;
	HashGridPerfAccum.TraverseHashGridMs += Stats.TraverseHashGridMs;
	HashGridPerfAccum.EstimatedTraversalAndProjectionMs += Stats.EstimatedTraversalAndProjectionMs;
	HashGridPerfAccum.EntityValidationMs += Stats.EntityValidationMs;
	HashGridPerfAccum.FragmentLookupMs += Stats.FragmentLookupMs;
	HashGridPerfAccum.HashGridBlocks += Stats.HashGridBlocks;
	HashGridPerfAccum.ValidBlocks += Stats.ValidBlocks;
	HashGridPerfAccum.OccupiedCells += Stats.OccupiedCells;
	HashGridPerfAccum.NonEmptyCells += Stats.NonEmptyCells;
	HashGridPerfAccum.AgentsVisited += Stats.AgentsVisited;
	HashGridPerfAccum.AgentsInBounds += Stats.AgentsInBounds;
	HashGridPerfAccum.SkippedOutOfBounds += Stats.SkippedOutOfBounds;
	HashGridPerfAccum.EntityValidationChecks += Stats.EntityValidationChecks;
	HashGridPerfAccum.InvalidEntities += Stats.InvalidEntities;
	HashGridPerfAccum.FragmentDataPtrCalls += Stats.FragmentDataPtrCalls;
	HashGridPerfAccum.AgentsWithRepresentationFragment += Stats.AgentsWithRepresentationFragment;
	HashGridPerfAccum.AgentsWithVisionFragment += Stats.AgentsWithVisionFragment;
	HashGridPerfAccum.MinimapCellsWritten += Stats.MinimapCellsWritten;
	HashGridPerfAccum.AgentCellSize = Stats.AgentCellSize;
	HashGridPerfAccum.AgentBlockDimensions = Stats.AgentBlockDimensions;
	HashGridPerfSampleCount++;

	if (!GetWorld())
	{
		return;
	}

	const double CurrentTime = GetWorld()->GetTimeSeconds();
	if (CurrentTime - LastHashGridPerfLogTime >= MinimapPerformanceLogInterval)
	{
		FlushHashGridPerfStats(CurrentTime);
	}
}

void UMinimapDataSubsystem::FlushHashGridPerfStats(double CurrentTime)
{
	if (HashGridPerfSampleCount <= 0)
	{
		return;
	}

	const float InvSamples = 1.0f / static_cast<float>(HashGridPerfSampleCount);
	const FString CsvColumns = FString::Printf(
		TEXT("%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%s,%s"),
		HashGridPerfSampleCount,
		HashGridPerfAccum.TotalMs * InvSamples,
		HashGridPerfAccum.ClearTilesMs * InvSamples,
		HashGridPerfAccum.TraverseHashGridMs * InvSamples,
		HashGridPerfAccum.EstimatedTraversalAndProjectionMs * InvSamples,
		HashGridPerfAccum.EntityValidationMs * InvSamples,
		HashGridPerfAccum.FragmentLookupMs * InvSamples,
		HashGridPerfAccum.HashGridBlocks * InvSamples,
		HashGridPerfAccum.ValidBlocks * InvSamples,
		HashGridPerfAccum.OccupiedCells * InvSamples,
		HashGridPerfAccum.NonEmptyCells * InvSamples,
		HashGridPerfAccum.AgentsVisited * InvSamples,
		HashGridPerfAccum.AgentsInBounds * InvSamples,
		HashGridPerfAccum.SkippedOutOfBounds * InvSamples,
		HashGridPerfAccum.InvalidEntities * InvSamples,
		HashGridPerfAccum.FragmentDataPtrCalls * InvSamples,
		HashGridPerfAccum.AgentsWithRepresentationFragment * InvSamples,
		HashGridPerfAccum.AgentsWithVisionFragment * InvSamples,
		HashGridPerfAccum.MinimapCellsWritten * InvSamples,
		*HashGridPerfAccum.AgentCellSize.ToString(),
		*HashGridPerfAccum.AgentBlockDimensions.ToString());

	if (bLogMinimapPerformanceToOutputLog)
	{
		UE_LOG(LogTemp, Log, TEXT("[FogOfWarPerf][MinimapHashGridAvg] %s"), *CsvColumns);
	}
	AppendPerformanceCsvLine(TEXT("MinimapHashGridAvg"), CsvColumns);

	HashGridPerfAccum = FMinimapHashGridPerfStats();
	HashGridPerfSampleCount = 0;
	LastHashGridPerfLogTime = CurrentTime;
}

void UMinimapDataSubsystem::FlushDrawPerfStats(double CurrentTime)
{
	if (DrawPerfSampleCount <= 0)
	{
		return;
	}

	const float InvSamples = 1.0f / static_cast<float>(DrawPerfSampleCount);
	const FString CsvColumns = FString::Printf(
		TEXT("%d,%.3f,%.3f,%.3f,%.3f,%.3f,0.000,%.1f,%.1f,%.1f,%.1f,%.1f,%d,0,0,0,0,0,0,,"),
		DrawPerfSampleCount,
		DrawPerfAccum.TotalMs * InvSamples,
		DrawPerfAccum.LockTexturesMs * InvSamples,
		DrawPerfAccum.ScanTilesMs * InvSamples,
		DrawPerfAccum.UploadTexturesMs * InvSamples,
		DrawPerfAccum.DrawRenderTargetMs * InvSamples,
		DrawPerfAccum.SourceTilesScanned * InvSamples,
		DrawPerfAccum.ActiveTiles * InvSamples,
		DrawPerfAccum.EncodedUnits * InvSamples,
		DrawPerfAccum.EncodedVisionSources * InvSamples,
		DrawPerfAccum.TotalUnitsRepresented * InvSamples,
		DrawPerfAccum.MaxUnitsInSingleTile);

	if (bLogMinimapPerformanceToOutputLog)
	{
		UE_LOG(LogTemp, Log, TEXT("[FogOfWarPerf][MinimapDrawAvg] %s"), *CsvColumns);
	}
	AppendPerformanceCsvLine(TEXT("MinimapDrawAvg"), CsvColumns);

	DrawPerfAccum = FMinimapDrawPerfStats();
	DrawPerfSampleCount = 0;
	LastDrawPerfLogTime = CurrentTime;
}

void UMinimapDataSubsystem::AppendPerformanceCsvLine(const FString& Channel, const FString& CsvColumns) const
{
	if (!bWriteMinimapPerformanceCsv || !GetWorld())
	{
		return;
	}

	const FString FilePath = FPaths::ProjectSavedDir() / MinimapPerformanceCsvRelativePath;
	const bool bNeedsHeader = !FPaths::FileExists(FilePath);
	FString Output;
	if (bNeedsHeader)
	{
		Output += TEXT("WorldTime,Channel,Samples,AvgTotalMs,AvgClearOrLockMs,AvgTraverseOrScanMs,AvgTraverseEstOrUploadMs,AvgEntityValidOrDrawRTMs,AvgFragLookupMs,AvgBlocksOrSourceTiles,AvgValidBlocksOrActiveTiles,AvgOccCellsOrEncodedUnits,AvgNonEmptyCellsOrVisionSources,AvgAgentsOrUnitsRepresented,AvgInBoundsOrMaxStack,AvgOutOfBounds,AvgInvalidEntities,AvgFragmentCalls,AvgRepFragments,AvgVisionFragments,AvgWrites,ExtraA,ExtraB\n");
	}
	Output += FString::Printf(TEXT("%.3f,%s,%s\n"), GetWorld()->GetTimeSeconds(), *Channel, *CsvColumns);
	FFileHelper::SaveStringToFile(Output, *FilePath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
}
