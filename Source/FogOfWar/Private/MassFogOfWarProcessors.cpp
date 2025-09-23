// Copyright Winyunq, 2025. All Rights Reserved.

#include "MassFogOfWarProcessors.h"
#include "MassFogOfWarFragments.h"
#include "MassCommonFragments.h"
#include "FogOfWar.h"
#include "MassExecutionContext.h"
#include "Kismet/GameplayStatics.h"
#include "Containers/StringView.h"
#include "MassRepresentationProcessor.h" // 包含 UMassVisibilityProcessor 的定义
#include "MassRepresentationFragments.h" // 包含 FMassVisibilityFragment 的定义

//----------------------------------------------------------------------//
// FFogOfWarMassHelpers
//----------------------------------------------------------------------//
void FFogOfWarMassHelpers::ProcessEntityChunk(FMassExecutionContext& Context, AFogOfWar* FogOfWar)
{
	const TConstArrayView<FTransformFragment> TransformList = Context.GetFragmentView<FTransformFragment>();
	const TConstArrayView<FMassVisionFragment> VisionList = Context.GetFragmentView<FMassVisionFragment>();
	const TArrayView<FMassPreviousVisionFragment> PreviousVisionList = Context.GetMutableFragmentView<FMassPreviousVisionFragment>();

	for (int32 EntityIndex = 0; EntityIndex < Context.GetNumEntities(); ++EntityIndex)
	{
		const FVector& Location = TransformList[EntityIndex].GetTransform().GetLocation();
		const float SightRadius = VisionList[EntityIndex].SightRadius;
		FMassPreviousVisionFragment& PreviousVisionFragment = PreviousVisionList[EntityIndex];

		// Reset previous vision contribution
		if (PreviousVisionFragment.PreviousVisionData.bHasCachedData)
		{
			for (int I = 0; I < PreviousVisionFragment.PreviousVisionData.LocalAreaTilesResolution; I++)
			{
				for (int J = 0; J < PreviousVisionFragment.PreviousVisionData.LocalAreaTilesResolution; J++)
				{
					if (PreviousVisionFragment.PreviousVisionData.GetLocalTileState({ I, J }) == ETileState::Visible)
					{
						FIntVector2 GlobalIJ = PreviousVisionFragment.PreviousVisionData.LocalToGlobal({ I, J });
						if (FogOfWar->IsGlobalIJValid(GlobalIJ))
						{
							FTile& GlobalTile = FogOfWar->GetGlobalTile(GlobalIJ);
							checkSlow(GlobalTile.VisibilityCounter > 0);
							GlobalTile.VisibilityCounter--;
						}
					}
				}
			}
			PreviousVisionFragment.PreviousVisionData.bHasCachedData = false;
		}

		// Create current VisionUnitData
		int LocalAreaTilesResolution = FMath::CeilToInt32(SightRadius * 2 / FogOfWar->GetTileSize()) + 1;
		TArray<ETileState> LocalAreaTilesStates;
		LocalAreaTilesStates.Init(ETileState::NotVisible, LocalAreaTilesResolution * LocalAreaTilesResolution);
		
		FVisionUnitData VisionUnitData = {
			.LocalAreaTilesResolution = LocalAreaTilesResolution,
			.GridSpaceRadius = SightRadius / FogOfWar->GetTileSize(),
			.LocalAreaTilesCachedStates = MoveTemp(LocalAreaTilesStates),
		};

		const FVector2f OriginGridLocation = FogOfWar->ConvertWorldSpaceLocationToGridSpace(FVector2D(Location));

		checkSlow(FogOfWar->ConvertGridLocationToTileIJ(OriginGridLocation + VisionUnitData.GridSpaceRadius).X - FogOfWar->ConvertGridLocationToTileIJ(OriginGridLocation - VisionUnitData.GridSpaceRadius).X + 1 <= VisionUnitData.LocalAreaTilesResolution);
		checkSlow(FogOfWar->ConvertGridLocationToTileIJ(OriginGridLocation + VisionUnitData.GridSpaceRadius).Y - FogOfWar->ConvertGridLocationToTileIJ(OriginGridLocation - VisionUnitData.GridSpaceRadius).Y + 1 <= VisionUnitData.LocalAreaTilesResolution);
		checkSlow(FogOfWar->ConvertGridLocationToTileIJ(OriginGridLocation + VisionUnitData.GridSpaceRadius).X - FogOfWar->ConvertGridLocationToTileIJ(OriginGridLocation - VisionUnitData.GridSpaceRadius).X + 1 + 2 > VisionUnitData.LocalAreaTilesResolution);
		checkSlow(FogOfWar->ConvertGridLocationToTileIJ(OriginGridLocation + VisionUnitData.GridSpaceRadius).Y - FogOfWar->ConvertGridLocationToTileIJ(OriginGridLocation - VisionUnitData.GridSpaceRadius).Y + 1 + 2 > VisionUnitData.LocalAreaTilesResolution);

		VisionUnitData.LocalAreaTilesCachedStates.Init(ETileState::Unknown, VisionUnitData.LocalAreaTilesCachedStates.Num());
		const FIntVector2 OriginGlobalIJ = FogOfWar->ConvertGridLocationToTileIJ(OriginGridLocation);
		
		if (!ensureMsgf(FogOfWar->IsGlobalIJValid(OriginGlobalIJ), TEXT("Vision actor is outside the grid")))
		{
			PreviousVisionFragment.PreviousVisionData = MoveTemp(VisionUnitData);
			return;
		}

		if (VisionUnitData.LocalAreaTilesResolution == 0)
		{
			PreviousVisionFragment.PreviousVisionData = MoveTemp(VisionUnitData);
			return;
		}

		VisionUnitData.CachedOriginGlobalIndex = FogOfWar->GetGlobalIndex(OriginGlobalIJ);
		VisionUnitData.LocalAreaCachedMinIJ = FogOfWar->ConvertGridLocationToTileIJ(OriginGridLocation - VisionUnitData.GridSpaceRadius);
		const FIntVector2 OriginLocalIJ = VisionUnitData.GlobalToLocal(OriginGlobalIJ);

		VisionUnitData.GetLocalTileState(OriginLocalIJ) = ETileState::Visible;

		const float GridSpaceRadiusSqr = FMath::Square(VisionUnitData.GridSpaceRadius);

		TArray<int> DDALocalIndexesStack;

		// going in spiral
		{
#if DO_GUARD_SLOW
			int SafetyIterations = VisionUnitData.LocalAreaTilesCachedStates.Num();
			TArray<bool> IsTileVisited;
			IsTileVisited.Init(false, VisionUnitData.LocalAreaTilesCachedStates.Num());
#endif

			enum class EDirection { Right, Up, Left, Down };
			const FIntVector2 DirectionDeltas[] = { {0, 1}, {1, 0}, {0, -1}, {-1, 0} };

			EDirection CurrentDirection = EDirection::Right;
			bool Clock = true;
			int CurrentStepSize = VisionUnitData.LocalAreaTilesResolution;
			int LeftToSpend = CurrentStepSize;
			FIntVector2 CurrentLocalIJ = FIntVector2(0, 0) - DirectionDeltas[static_cast<int>(CurrentDirection)];

			while (true)
			{
				checkSlow(LeftToSpend > 0);
				CurrentLocalIJ += DirectionDeltas[static_cast<int>(CurrentDirection)];
				LeftToSpend--;

				{
					checkSlow(VisionUnitData.IsLocalIJValid(CurrentLocalIJ));

#if DO_GUARD_SLOW
						SafetyIterations--;
						IsTileVisited[VisionUnitData.GetLocalIndex(CurrentLocalIJ)] = true;
#endif

					FIntVector2 GlobalIJ = VisionUnitData.LocalToGlobal(CurrentLocalIJ);

					if (FogOfWar->IsGlobalIJValid(GlobalIJ))
					{
						int DistToTileSqr = FMath::Square(OriginGlobalIJ.X - GlobalIJ.X) + FMath::Square(OriginGlobalIJ.Y - GlobalIJ.Y);
						if (DistToTileSqr <= GridSpaceRadiusSqr)
						{
							TArray<int> CurrentDDALocalIndexesStack;
							int LocalIndex = VisionUnitData.GetLocalIndex(CurrentLocalIJ);
							if (VisionUnitData.GetLocalTileState(LocalIndex) == ETileState::Unknown)
							{
								const FIntVector2 Direction = OriginLocalIJ - CurrentLocalIJ;
								checkSlow(FMath::Abs(Direction.X) + FMath::Abs(Direction.Y) != 0);
								const FIntVector2 DirectionSign = { Direction.X >= 0 ? 1 : -1, Direction.Y >= 0 ? 1 : -1 };
								const float S_x = FMath::Sqrt(FMath::Square(1.0) + FMath::Square(static_cast<float>(Direction.Y) / Direction.X));
								const float S_y = FMath::Sqrt(FMath::Square(1.0) + FMath::Square(static_cast<float>(Direction.X) / Direction.Y));
								float NextAccumulatedDxLength = 0.5 * S_x;
								float NextAccumulatedDyLength = 0.5 * S_y;

								bool bIsBlocking = false;
								const int DDASafetyIterations = FMath::Abs(Direction.X) + FMath::Abs(Direction.Y) + 1;
								checkSlow(DDASafetyIterations < 10000);
								int DDASafetyCounter;

								FIntVector2 CurrentDDALocalIJ = CurrentLocalIJ;
								int CurrentDDALocalIndex = LocalIndex;

								for (DDASafetyCounter = 0; DDASafetyCounter < DDASafetyIterations; DDASafetyCounter++)
								{
									CurrentDDALocalIndexesStack.Push(CurrentDDALocalIndex);
									if (CurrentDDALocalIJ == OriginLocalIJ) break;

									auto CurrentHeight = FogOfWar->GetGlobalTile(VisionUnitData.LocalToGlobal(CurrentDDALocalIJ)).Height;
									if (FogOfWar->IsBlockingVision(Location.Z, CurrentHeight))
									{
										bIsBlocking = true;
										break;
									}

									if (NextAccumulatedDxLength < NextAccumulatedDyLength)
									{
										NextAccumulatedDxLength += S_x;
										CurrentDDALocalIJ.X += DirectionSign.X;
									}
									else
									{
										NextAccumulatedDyLength += S_y;
										CurrentDDALocalIJ.Y += DirectionSign.Y;
									}
									checkSlow(VisionUnitData.IsLocalIJValid(CurrentDDALocalIJ));
									checkSlow(FogOfWar->IsGlobalIJValid(VisionUnitData.LocalToGlobal(CurrentDDALocalIJ)));
									CurrentDDALocalIndex = VisionUnitData.GetLocalIndex(CurrentDDALocalIJ);
								}
								checkSlow(DDASafetyCounter < DDASafetyIterations);

								if (bIsBlocking)
								{
									while (!CurrentDDALocalIndexesStack.IsEmpty())
									{
										int LocalIndexFromStack = CurrentDDALocalIndexesStack.Pop(EAllowShrinking::No);
										auto& TileState = VisionUnitData.GetLocalTileState(LocalIndexFromStack);
										if (TileState != ETileState::Visible) TileState = ETileState::NotVisible;
									}
								}
								else
								{
									while (!CurrentDDALocalIndexesStack.IsEmpty())
									{
										int LocalIndexFromStack = CurrentDDALocalIndexesStack.Pop(EAllowShrinking::No);
										VisionUnitData.GetLocalTileState(LocalIndexFromStack) = ETileState::Visible;
									}
								}
							}
							checkSlow(VisionUnitData.GetLocalTileState(CurrentLocalIJ) != ETileState::Unknown);
						}
					}
				}

				if (LeftToSpend == 0)
				{
					if (Clock)
					{
						if (CurrentStepSize == 1) break;
						CurrentStepSize--;
					}
					Clock ^= 1;
					CurrentDirection = static_cast<EDirection>((static_cast<int>(CurrentDirection) + 1) % 4);
					LeftToSpend = CurrentStepSize;
				}
			}

#if DO_GUARD_SLOW
			check(SafetyIterations == 0);
			for (auto bVisited : IsTileVisited) check(bVisited);
#endif
		}

		for (int I = 0; I < VisionUnitData.LocalAreaTilesResolution; I++)
		{
			for (int J = 0; J < VisionUnitData.LocalAreaTilesResolution; J++)
			{
				FIntVector2 GlobalIJ = VisionUnitData.LocalToGlobal({ I, J });
				if (FogOfWar->IsGlobalIJValid(GlobalIJ))
				{
					int DistToTileSqr = FMath::Square(OriginGlobalIJ.X - GlobalIJ.X) + FMath::Square(OriginGlobalIJ.Y - GlobalIJ.Y);
					if (DistToTileSqr <= GridSpaceRadiusSqr)
					{
						if (VisionUnitData.GetLocalTileState({ I, J }) == ETileState::Visible)
						{
							FTile& GlobalTile = FogOfWar->GetGlobalTile(GlobalIJ);
							GlobalTile.VisibilityCounter++;
						}
					}
				}
			}
		}

		VisionUnitData.bHasCachedData = true;
		PreviousVisionFragment.PreviousVisionData = MoveTemp(VisionUnitData);
	}
}

//----------------------------------------------------------------------//
//  UInitialVisionProcessor
//----------------------------------------------------------------------//
UInitialVisionProcessor::UInitialVisionProcessor()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
}

void UInitialVisionProcessor::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);
	FogOfWarActor = Cast<AFogOfWar>(UGameplayStatics::GetActorOfClass(GetWorld(), AFogOfWar::StaticClass()));
}

void UInitialVisionProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassVisionFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassPreviousVisionFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddTagRequirement<FMassVisionInitializedTag>(EMassFragmentPresence::None); // Run only on uninitialized entities
	EntityQuery.RegisterWithProcessor(*this);
}

void UInitialVisionProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	if (!FogOfWarActor.Get() || !FogOfWarActor->IsActivated())
	{
		return;
	}

	EntityQuery.ForEachEntityChunk(EntityManager, Context, [this](FMassExecutionContext& Context)
	{
		FFogOfWarMassHelpers::ProcessEntityChunk(Context, FogOfWarActor.Get());

		// Add initialized tag to all entities in the chunk
		const TArrayView<const FMassEntityHandle> Entities = Context.GetEntities();
		for (const FMassEntityHandle& Entity : Entities)
		{
			Context.Defer().AddTag<FMassVisionInitializedTag>(Entity);
		}
	});
}



//----------------------------------------------------------------------//
//  UVisionProcessor
//----------------------------------------------------------------------//
UVisionProcessor::UVisionProcessor()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
	ExecutionOrder.ExecuteAfter.Add(UInitialVisionProcessor::StaticClass()->GetFName()); // Ensure initial vision runs first
	// ExecutionOrder.ExecuteAfter.Add(UMassVisibilityProcessor::StaticClass()->GetFName()); // Ensure vision update runs after visibility processor
}

void UVisionProcessor::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);
	FogOfWarActor = Cast<AFogOfWar>(UGameplayStatics::GetActorOfClass(GetWorld(), AFogOfWar::StaticClass()));
}

void UVisionProcessor::ConfigureQueries()
{
    EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
    EntityQuery.AddRequirement<FMassVisionFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassPreviousVisionFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddTagRequirement<FMassLocationChangedTag>(EMassFragmentPresence::All); // Only process entities that have moved

	// --- 核心修复 ---
	// 只处理未被剔除的实体.
	// 即：实体不能有“被距离剔除”的Tag，也不能有“被视锥体剔除”的Tag。
	EntityQuery.AddTagRequirement<FMassVisibilityCulledByDistanceTag>(EMassFragmentPresence::None);
	EntityQuery.AddTagRequirement<FMassVisibilityCulledByFrustumTag>(EMassFragmentPresence::None);

	EntityQuery.RegisterWithProcessor(*this);
}

void UVisionProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	if (!FogOfWarActor.Get() || !FogOfWarActor->IsActivated())
	{
		return;
	}

	EntityQuery.ForEachEntityChunk(EntityManager, Context, [this](FMassExecutionContext& Context)
	{
		FFogOfWarMassHelpers::ProcessEntityChunk(Context, FogOfWarActor.Get());

		// Remove location changed tag from all entities in the chunk
		const TArrayView<const FMassEntityHandle> Entities = Context.GetEntities();
		for (const FMassEntityHandle& Entity : Entities)
		{
			Context.Defer().RemoveTag<FMassLocationChangedTag>(Entity);
		}
	});
}

//----------------------------------------------------------------------//
//  UDebugStressTestProcessor
//----------------------------------------------------------------------//
UDebugStressTestProcessor::UDebugStressTestProcessor()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
	// 必须在 UVisionProcessor 之前运行
	ExecutionOrder.ExecuteBefore.Add(UVisionProcessor::StaticClass()->GetFName());
}

void UDebugStressTestProcessor::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);
	FogOfWarActor = Cast<AFogOfWar>(UGameplayStatics::GetActorOfClass(GetWorld(), AFogOfWar::StaticClass()));
}

void UDebugStressTestProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FMassVisionFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddTagRequirement<FMassVisionEntityTag>(EMassFragmentPresence::All);
	// 我们需要查询所有可见单位，无论它们是否已改变位置
	// EntityQuery.AddConstSharedRequirement<FMassVisibilityFragment>(EMassFragmentPresence::All);
	EntityQuery.RegisterWithProcessor(*this);
}

void UDebugStressTestProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	if (!FogOfWarActor.Get() || !FogOfWarActor->IsActivated())
	{
		return;
	}

	const bool bForceVisionUpdate = FogOfWarActor->bDebugStressTestIgnoreCache;
	const bool bForceMinimapUpdate = FogOfWarActor->bDebugStressTestMinimap;

	if (!bForceVisionUpdate && !bForceMinimapUpdate)
	{
		return;
	}

	EntityQuery.ForEachEntityChunk(EntityManager, Context, [this, bForceVisionUpdate, bForceMinimapUpdate](FMassExecutionContext& Context)
	{
		const TArrayView<const FMassEntityHandle> Entities = Context.GetEntities();
		for (const FMassEntityHandle& Entity : Entities)
		{
			if (bForceVisionUpdate)
			{
				Context.Defer().AddTag<FMassLocationChangedTag>(Entity);
			}
			if (bForceMinimapUpdate)
			{
				Context.Defer().AddTag<FMinimapCellChangedTag>(Entity);
			}
		}
	});
}

//----------------------------------------------------------------------//
//  UMinimapAddProcessor
//----------------------------------------------------------------------//
UMinimapAddProcessor::UMinimapAddProcessor()
{
	ObservedType = FMassMinimapRepresentationFragment::StaticStruct();
	Operation = EMassObservedOperation::Add;
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
}

void UMinimapAddProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassMinimapRepresentationFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.RegisterWithProcessor(*this);
}

void UMinimapAddProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UMinimapDataSubsystem* MinimapDataSubsystem = GetWorld()->GetSubsystem<UMinimapDataSubsystem>();
	if (!MinimapDataSubsystem)
	{
		return;
	}

	EntityQuery.ForEachEntityChunk(EntityManager, Context, [MinimapDataSubsystem](FMassExecutionContext& Context)
	{
		const auto& LocationList = Context.GetFragmentView<FTransformFragment>();
		const auto& RepList = Context.GetFragmentView<FMassMinimapRepresentationFragment>();
		
		for (int32 i = 0; i < Context.GetNumEntities(); ++i)
		{
			const FMassEntityHandle Entity = Context.GetEntity(i);
			const FVector& Location = LocationList[i].GetTransform().GetLocation();
			const auto& RepFragment = RepList[i];

			MinimapDataSubsystem->IconLocations.Add(Entity, FVector4(Location.X, Location.Y, RepFragment.IconSize, RepFragment.Intensity));
			
		}
	});
}


//----------------------------------------------------------------------//
//  UMinimapUpdateProcessor
//----------------------------------------------------------------------//

UMinimapUpdateProcessor::UMinimapUpdateProcessor()
	: RepresentationQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
	// 在观察者之后运行，确保Tag已经被正确更新
	ExecutionOrder.ExecuteAfter.Add(UMinimapObserverProcessor::StaticClass()->GetFName());
}

void UMinimapUpdateProcessor::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);
	MinimapDataSubsystem = GetWorld()->GetSubsystem<UMinimapDataSubsystem>();
}

void UMinimapUpdateProcessor::ConfigureQueries()
{
    // 只查询那些需要在小地图上表示，并且其格子位置已发生变化的单位
    RepresentationQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
    RepresentationQuery.AddRequirement<FMassMinimapRepresentationFragment>(EMassFragmentAccess::ReadOnly);
    RepresentationQuery.AddRequirement<FMassVisionFragment>(EMassFragmentAccess::ReadOnly);
	RepresentationQuery.AddTagRequirement<FMinimapCellChangedTag>(EMassFragmentPresence::All); // 核心优化
    RepresentationQuery.RegisterWithProcessor(*this);
}

void UMinimapUpdateProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	if (!MinimapDataSubsystem)
	{
		return;
	}

	RepresentationQuery.ForEachEntityChunk(EntityManager, Context, [this, &Context](FMassExecutionContext& ChunkContext)
	{
		const auto& LocationList = ChunkContext.GetFragmentView<FTransformFragment>();
		const auto& RepList = ChunkContext.GetFragmentView<FMassMinimapRepresentationFragment>();
		const auto& VisionList = ChunkContext.GetFragmentView<FMassVisionFragment>();
		
		for (int32 i = 0; i < ChunkContext.GetNumEntities(); ++i)
		{
			const FMassEntityHandle Entity = ChunkContext.GetEntity(i);
			const FVector& Location = LocationList[i].GetTransform().GetLocation();
			const auto& RepFragment = RepList[i];
			const auto& VisionFragment = VisionList[i];

			// 更新或添加数据到TMap
			MinimapDataSubsystem->IconLocations.Add(Entity, FVector4(Location.X, Location.Y, RepFragment.IconSize, RepFragment.Intensity));
			
			MinimapDataSubsystem->VisionSources.Add(Entity, FVector4(Location.X, Location.Y, 0.0f, VisionFragment.SightRadius));

			// 处理完后移除Tag，避免重复处理
			ChunkContext.Defer().RemoveTag<FMinimapCellChangedTag>(Entity);
		}
	});
}




//----------------------------------------------------------------------//
//  UMinimapObserverProcessor
//----------------------------------------------------------------------//
UMinimapObserverProcessor::UMinimapObserverProcessor()
{
	ObservedType = FTransformFragment::StaticStruct();
	Operation = EMassObservedOperation::MAX;
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
}

void UMinimapObserverProcessor::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);
	FogOfWarActor = Cast<AFogOfWar>(UGameplayStatics::GetActorOfClass(GetWorld(), AFogOfWar::StaticClass()));
}

void UMinimapObserverProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassPreviousMinimapCellFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.RegisterWithProcessor(*this);
}

void UMinimapObserverProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	if (!FogOfWarActor)
	{
		return;
	}

	EntityQuery.ForEachEntityChunk(EntityManager, Context, [this, &Context](FMassExecutionContext& ChunkContext)
	{
		const TConstArrayView<FTransformFragment> Locations = ChunkContext.GetFragmentView<FTransformFragment>();
		const TArrayView<FMassPreviousMinimapCellFragment> PrevCells = ChunkContext.GetMutableFragmentView<FMassPreviousMinimapCellFragment>();

		for (int32 i = 0; i < ChunkContext.GetNumEntities(); ++i)
		{
			const FVector& WorldLocation = Locations[i].GetTransform().GetLocation();
			FMassPreviousMinimapCellFragment& PrevCellFragment = PrevCells[i];

			// Convert world location to grid location, then to tile IJ coordinates.
			// This logic must be consistent with how the FOW grid is structured.
			const FVector2f GridLocation = FogOfWarActor->ConvertWorldSpaceLocationToGridSpace(FVector2D(WorldLocation.X, WorldLocation.Y));
			const FIntVector2 CurrentCellVec = FogOfWarActor->ConvertGridLocationToTileIJ(GridLocation);
			const FIntPoint CurrentCell(CurrentCellVec.X, CurrentCellVec.Y);

			if (CurrentCell != PrevCellFragment.PrevCellCoords)
			{
				// The entity has moved to a new cell. Add a tag to mark it as "dirty"
				// for the UMinimapDataCollectorProcessor to process later.
				Context.Defer().AddTag<FMinimapCellChangedTag>(ChunkContext.GetEntity(i));
				
				// Update the last known cell coordinate.
				PrevCellFragment.PrevCellCoords = CurrentCell;
			}
		}
	});
}