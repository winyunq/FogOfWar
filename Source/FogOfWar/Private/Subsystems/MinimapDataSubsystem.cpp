// Copyright Winyunq, 2025. All Rights Reserved.

#include "Subsystems/MinimapDataSubsystem.h"
#include "FogOfWarMassBinding.h"
#include "MassBattleMinimapRegion.h" // Updated Actor-Driven Region
#include "Kismet/GameplayStatics.h"
#include "Subsystems/MassBattleHashGridSubsystem.h"
#include "MassEntitySubsystem.h"
#include "MassFogOfWarFragments.h"
#include "DrawDebugHelpers.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
	constexpr int32 DefaultMinimapResolution = 256;
	constexpr float DefaultVisionTileSize = 100.0f;

	FORCEINLINE void ValidateAndClampResolution(FIntPoint& Resolution)
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
}

// Define the static singleton instance pointer.
UMinimapDataSubsystem* UMinimapDataSubsystem::SingletonInstance = nullptr;

void UMinimapDataSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	SingletonInstance = this;

	// 被动初始化：如果场景中已存在 MinimapRegion，则自动提取参数
	TArray<AActor*> FoundRegions;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AMinimapRegion::StaticClass(), FoundRegions);
	if (FoundRegions.Num() > 0)
	{
		if (AMinimapRegion* Region = Cast<AMinimapRegion>(FoundRegions[0]))
		{
			const FVector Origin = Region->GetActorLocation();
			const FVector BoxExtent = Region->BoundsComponent->GetScaledBoxExtent();
			const FVector2D GridOrigin(Origin.X - BoxExtent.X, Origin.Y - BoxExtent.Y);
			const FVector2D GridSizeVal(BoxExtent.X * 2.f, BoxExtent.Y * 2.f);
			InitMinimapGrid(GridOrigin, GridSizeVal, Region->GridResolution);
		}
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
	MinimapGridResolution = NewResolution;
	ValidateAndClampResolution(MinimapGridResolution);
	MinimapTiles.SetNum(MinimapGridResolution.X * MinimapGridResolution.Y);

	// 如果主网格数据已存在，现在计算小地图瓦片尺寸
	if (GridSize.X > 0 && GridSize.Y > 0)
	{
		MinimapTileSize = FVector2D(GridSize.X / MinimapGridResolution.X, GridSize.Y / MinimapGridResolution.Y);
	}
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

void UMinimapDataSubsystem::SyncVisionGridParameters(const FVector2D& InGridOrigin, const FVector2D& InGridSize, float InVisionTileSize, const FIntPoint& InVisionResolution)
{
	bVisionGridActive = false;

	GridBottomLeftWorldLocation = InGridOrigin;
	GridSize = InGridSize;
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

	if (MinimapGridResolution.X > 0 && MinimapGridResolution.Y > 0 && GridSize.X > 0 && GridSize.Y > 0)
	{
		MinimapTileSize = FVector2D(GridSize.X / MinimapGridResolution.X, GridSize.Y / MinimapGridResolution.Y);
	}

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

bool UMinimapDataSubsystem::IsMinimapGridReady() const
{
	return
		MinimapTileSize.X > 0.0f &&
		MinimapTileSize.Y > 0.0f &&
		MinimapGridResolution.X > 0 &&
		MinimapGridResolution.Y > 0 &&
		MinimapTiles.Num() == MinimapGridResolution.X * MinimapGridResolution.Y;
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

void UMinimapDataSubsystem::InitMinimapGrid(const FVector2D& InGridOrigin, const FVector2D& InGridSize, const FIntPoint& InResolution)
{
	GridBottomLeftWorldLocation = InGridOrigin;
	GridSize = InGridSize;
	MinimapGridResolution = InResolution;
	
	// Ensure Resolution is valid to avoid division by zero
	ValidateAndClampResolution(MinimapGridResolution);

	MinimapTiles.SetNum(MinimapGridResolution.X * MinimapGridResolution.Y);

	if (GridSize.X > 0 && GridSize.Y > 0)
	{
		MinimapTileSize = FVector2D(GridSize.X / MinimapGridResolution.X, GridSize.Y / MinimapGridResolution.Y);
		UE_LOG(LogTemp, Log, TEXT("[MinimapDataSubsystem] Manually Initialized Grid. Origin:%s, Size:%s, Res:%s, TileSize:%s"), 
			*GridBottomLeftWorldLocation.ToString(), *GridSize.ToString(), *MinimapGridResolution.ToString(), *MinimapTileSize.ToString());

		// Sync with MassBattleHashGridSubsystem
		if (UMassBattleHashGridSubsystem* HashGrid = GetWorld()->GetSubsystem<UMassBattleHashGridSubsystem>())
		{
			// Align HashGrid Origin with Minimap Origin to ensure consistent spatial hashing
			// We only override X/Y as Z is usually handled separately or assumed valid.
			HashGrid->GridOrigin.X = GridBottomLeftWorldLocation.X;
			HashGrid->GridOrigin.Y = GridBottomLeftWorldLocation.Y;
			// HashGrid->GridOrigin.Z remains unchanged or defaults to 0.
			
			UE_LOG(LogTemp, Log, TEXT("[MinimapDataSubsystem] Synced MassBattleHashGrid Origin to: %s"), *HashGrid->GridOrigin.ToString());
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[MinimapDataSubsystem] InitMinimapGrid called with invalid GridSize!"));
	}
}

void UMinimapDataSubsystem::UpdateMinimapFromHashGrid(FVector CenterLocation, int32 BlockRadius)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("Minimap.UpdateFromHashGrid");

	if (!GetWorld()) return;
	const double TotalStartTime = FPlatformTime::Seconds();
	const bool bCollectStats = bEnableMinimapPerformanceStats;
	const bool bCollectDetailedStats = bCollectStats && bEnableDetailedMinimapPerformanceStats;
	FMinimapHashGridPerfStats Stats;
	
	// Zero Overhead Check: If Minimap hasn't been initialized (TileSize is Zero), do nothing.
	if (MinimapTileSize.X <= 0 || MinimapTileSize.Y <= 0)
	{
		return;
	}

	// 1. 获取必要的子系统
	UMassBattleHashGridSubsystem* HashGrid = UMassBattleHashGridSubsystem::GetPtr(GetWorld());
	UMassEntitySubsystem* EntitySubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	if (!HashGrid || !EntitySubsystem) return;
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();

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
		Tile.UnitCount = 0;
		Tile.MaxSightRadius = 0.0f;
		Tile.MaxIconSize = 0.0f;
		Tile.RepresentativeInfluence = -FLT_MAX;
		Tile.Color = FLinearColor::Transparent;
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
					
					// Fallback defaults come from MassBattle's own unit size fragments.
					const uint64 FragmentStartCycles = bCollectDetailedStats ? FPlatformTime::Cycles64() : 0;
					const FOW_TEAM_FRAGMENT* TeamFrag = EntityManager.GetFragmentDataPtr<FOW_TEAM_FRAGMENT>(AgentData.EntityHandle);
					Stats.FragmentDataPtrCalls++;
					FLinearColor IconColor = TeamFrag ? GetTeamColor(FOW_GET_TEAM_INDEX(*TeamFrag)) : DefaultTeamColor;
					float IconSize = DefaultMinimapUnitPixelRadius;

					if (const FMassMinimapRepresentationFragment* RepFrag = EntityManager.GetFragmentDataPtr<FMassMinimapRepresentationFragment>(AgentData.EntityHandle))
					{
						Stats.AgentsWithRepresentationFragment++;
						IconColor = RepFrag->IconColor;
						IconSize = RepFrag->IconSize;
					}
					Stats.FragmentDataPtrCalls++;

					float SightRadius = DefaultMassBattleSightRadius;
					if (const FMassVisionFragment* VisionFrag = EntityManager.GetFragmentDataPtr<FMassVisionFragment>(AgentData.EntityHandle))
					{
						Stats.AgentsWithVisionFragment++;
						SightRadius = VisionFrag->SightRadius;
					}
					Stats.FragmentDataPtrCalls++;

					MiniTile.MaxSightRadius = FMath::Max(MiniTile.MaxSightRadius, SightRadius);
					const float Influence = SightRadius;
					if (Influence >= MiniTile.RepresentativeInfluence)
					{
						MiniTile.RepresentativeInfluence = Influence;
						MiniTile.Color = IconColor;
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
		const double CurrentTime = GetWorld()->GetTimeSeconds();
		if (CurrentTime - LastHashGridPerfLogTime >= MinimapPerformanceLogInterval)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[MinimapPerf][HashGridRead] Total=%.3fms Clear=%.3fms TraverseAll=%.3fms TraverseEst=%.3fms EntityValid=%.3fms FragLookup=%.3fms Detailed=%d Blocks=%d ValidBlocks=%d OccCells=%d NonEmpty=%d Agents=%d InBounds=%d OOB=%d InvalidEnt=%d FragCalls=%d Rep=%d Vision=%d Writes=%d AgentCell=%s BlockDim=%s First=%s"),
				Stats.TotalMs,
				Stats.ClearTilesMs,
				Stats.TraverseHashGridMs,
				Stats.EstimatedTraversalAndProjectionMs,
				Stats.EntityValidationMs,
				Stats.FragmentLookupMs,
				bCollectDetailedStats ? 1 : 0,
				Stats.HashGridBlocks,
				Stats.ValidBlocks,
				Stats.OccupiedCells,
				Stats.NonEmptyCells,
				Stats.AgentsVisited,
				Stats.AgentsInBounds,
				Stats.SkippedOutOfBounds,
				Stats.InvalidEntities,
				Stats.FragmentDataPtrCalls,
				Stats.AgentsWithRepresentationFragment,
				Stats.AgentsWithVisionFragment,
				Stats.MinimapCellsWritten,
				*Stats.AgentCellSize.ToString(),
				*Stats.AgentBlockDimensions.ToString(),
				*FirstAgentLoc.ToString());
			LastHashGridPerfLogTime = CurrentTime;
		}
	}
}

void UMinimapDataSubsystem::RecordMinimapDrawPerfStats(const FMinimapDrawPerfStats& Stats)
{
	LastDrawPerfStats = Stats;
	if (!bEnableMinimapPerformanceStats || !GetWorld())
	{
		return;
	}

	const double CurrentTime = GetWorld()->GetTimeSeconds();
	if (CurrentTime - LastDrawPerfLogTime < MinimapPerformanceLogInterval)
	{
		return;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[MinimapPerf][Draw] Total=%.3fms Lock=%.3fms ScanTiles=%.3fms Upload=%.3fms DrawRT=%.3fms SourceTiles=%d ActiveTiles=%d EncodedUnits=%d VisionSources=%d UnitsRepresented=%d MaxStack=%d"),
		Stats.TotalMs,
		Stats.LockTexturesMs,
		Stats.ScanTilesMs,
		Stats.UploadTexturesMs,
		Stats.DrawRenderTargetMs,
		Stats.SourceTilesScanned,
		Stats.ActiveTiles,
		Stats.EncodedUnits,
		Stats.EncodedVisionSources,
		Stats.TotalUnitsRepresented,
		Stats.MaxUnitsInSingleTile);
	LastDrawPerfLogTime = CurrentTime;
}
