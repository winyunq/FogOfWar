// Copyright Winyunq, 2025. All Rights Reserved.

#include "Subsystems/MinimapDataSubsystem.h"
#include "MassBattleMinimapRegion.h" // Updated Actor-Driven Region
#include "Kismet/GameplayStatics.h"
#include "Subsystems/MassBattleHashGridSubsystem.h"
#include "MassEntitySubsystem.h"
#include "MassFogOfWarFragments.h"
#include "DrawDebugHelpers.h"

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
	MinimapTiles.SetNum(MinimapGridResolution.X * MinimapGridResolution.Y);

	// 如果主网格数据已存在，现在计算小地图瓦片尺寸
	if (GridSize.X > 0 && GridSize.Y > 0)
	{
		MinimapTileSize = FVector2D(GridSize.X / MinimapGridResolution.X, GridSize.Y / MinimapGridResolution.Y);
	}
}

void UMinimapDataSubsystem::InitMinimapGrid(const FVector2D& InGridOrigin, const FVector2D& InGridSize, const FIntPoint& InResolution)
{
	GridBottomLeftWorldLocation = InGridOrigin;
	GridSize = InGridSize;
	MinimapGridResolution = InResolution;
	
	// Ensure Resolution is valid to avoid division by zero
	if (MinimapGridResolution.X <= 0) MinimapGridResolution.X = 256;
	if (MinimapGridResolution.Y <= 0) MinimapGridResolution.Y = 256;

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
	if (!GetWorld()) return;
	
	// Zero Overhead Check: If Minimap hasn't been initialized (TileSize is Zero), do nothing.
	if (MinimapTileSize.X <= 0 || MinimapTileSize.Y <= 0)
	{
		return;
	}

	// 1. 获取必要的子系统
	UMassBattleHashGridSubsystem* HashGrid = UMassBattleHashGridSubsystem::GetPtr(GetWorld());
	UMassEntitySubsystem* EntitySubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	if (!HashGrid || !EntitySubsystem) return;

	// 2. 清空并重置小地图数据
	// ResetAllTiles is not exposed, so we iterate. 
	// Optimally we should use a TArray::Init or Memzero if struct is simple, but we have colors/float.
	// For 256x256 (65k), a loop is acceptable.
	const int32 TotalTiles = MinimapTiles.Num();
	if (TotalTiles == 0) return;

	// ParallelFor could be used here for clearing if needed, but simple loop is safer for now.
	for (FMinimapTile& Tile : MinimapTiles)
	{
		Tile.UnitCount = 0;
		Tile.MaxSightRadius = 0.0f;
		Tile.MaxIconSize = 0.0f;
		Tile.Color = FLinearColor::Transparent;
	}

	// 3. 准备坐标转换参数 (Cache for performance)
	const FVector2D GridOrigin = GridBottomLeftWorldLocation;
	const FVector2D GridSizeVal = GridSize;
	const FIntPoint MapRes = MinimapGridResolution;
	const FVector2D TileSize = MinimapTileSize;
	
	// Debug Stats
	int32 ActiveBlocks = 0;
	int32 ActiveCells = 0;
	int32 TotalAgentsFound = 0;
	int32 AgentsWithFragment = 0;
	int32 SkippedOutOfBounds = 0;
	FVector FirstAgentLoc = FVector::ZeroVector;

	// 4. LOD2 - 遍历所有活跃的 Block (Active Blocks)
	for (auto It = HashGrid->AgentGrid.CreateConstIterator(); It; ++It)
	{
		ActiveBlocks++;
		const FIntVector& BlockCoord = It.Key();
		const TSharedPtr<FAgentGridBlock>& Block = It.Value();

		if (!Block.IsValid()) continue;

		const FIntVector BlockBaseGlobalCellCoord = BlockCoord * HashGrid->AgentBlockDimensionsCache;

		// 5. LOD1 - 遍历 Block 内的活跃 Cell (Occupied Cells)
		for (TConstSetBitIterator<> CellIt(Block->OccupiedCells.OccupiedCellBitArray); CellIt; ++CellIt)
		{
			ActiveCells++;
			const int32 CellIndex = CellIt.GetIndex();
			const FHashGridAgentCell& Cell = Block->Cells[CellIndex];

			if (Cell.Agents.Num() == 0) continue;

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
				TotalAgentsFound++;
				const FVector AgentWorldPos = CellCenterWorld + FVector(AgentData.RelativeLocation);

				if (TotalAgentsFound == 1) FirstAgentLoc = AgentWorldPos;

				const float RelX = AgentWorldPos.X - GridOrigin.X;
				const float RelY = AgentWorldPos.Y - GridOrigin.Y;

				if (RelX < 0 || RelY < 0 || RelX >= GridSizeVal.X || RelY >= GridSizeVal.Y) 
				{
					SkippedOutOfBounds++;
					continue;
				}

				const int32 TileX = FMath::FloorToInt(RelX / TileSize.X);
				const int32 TileY = FMath::FloorToInt(RelY / TileSize.Y);

				if (TileX >= 0 && TileX < MapRes.X && TileY >= 0 && TileY < MapRes.Y)
				{
					const int32 TileIndex = TileY * MapRes.X + TileX;
					FMinimapTile& MiniTile = MinimapTiles[TileIndex];
					MiniTile.UnitCount++;

					FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
					
					// Fallback Defaults
					FLinearColor IconColor = FLinearColor::White;
					float IconSize = 250.0f; // Visible default size

					if (const FMassMinimapRepresentationFragment* RepFrag = EntityManager.GetFragmentDataPtr<FMassMinimapRepresentationFragment>(AgentData.EntityHandle))
					{
						AgentsWithFragment++;
						IconColor = RepFrag->IconColor;
						IconSize = RepFrag->IconSize;
					}
					
					MiniTile.Color = IconColor;
					MiniTile.MaxIconSize = FMath::Max(MiniTile.MaxIconSize, IconSize);

					if (const FMassVisionFragment* VisionFrag = EntityManager.GetFragmentDataPtr<FMassVisionFragment>(AgentData.EntityHandle))
					{
						MiniTile.MaxSightRadius = FMath::Max(MiniTile.MaxSightRadius, VisionFrag->SightRadius);
					}
				}
			}
		}
	}
	
	// Explicitly Log Stats every second
	static double LastLogTime = 0.0f;
	const double CurrentTime = GetWorld()->GetTimeSeconds();
	if (CurrentTime - LastLogTime > 2.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MinimapDataSubsystem] Blocks: %d, Cells: %d, AgentsFound: %d, Skipped(OOB): %d, FirstAgent: %s"), 
			ActiveBlocks, ActiveCells, TotalAgentsFound, SkippedOutOfBounds, *FirstAgentLoc.ToString());
		LastLogTime = CurrentTime;
	}
}
