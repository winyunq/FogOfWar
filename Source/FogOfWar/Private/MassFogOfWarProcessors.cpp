// Copyright Winyunq, 2025. All Rights Reserved.

#include "MassFogOfWarProcessors.h"
#include "FogOfWarMassBinding.h"
#include "MassFogOfWarFragments.h"
#include "MassCommonFragments.h"
#include "Subsystems/MinimapDataSubsystem.h"
#include "MassExecutionContext.h"
#include "MassCommands.h"
#include "Containers/StringView.h"
#include "MassRepresentationProcessor.h" // 包含 UMassVisibilityProcessor 的定义
#include "MassRepresentationFragments.h" // 包含 FMassVisibilityFragment 的定义
#include "Fragments/PrimaryType.h"

//----------------------------------------------------------------------//
// FFogOfWarMassHelpers
//----------------------------------------------------------------------//
void FFogOfWarMassHelpers::ProcessEntityChunk(FMassExecutionContext& Context, UMinimapDataSubsystem& MinimapSubsystem)
{
	const float VisionTileSize = MinimapSubsystem.VisionTileSize;
	
	const TConstArrayView<FOW_LOCATION_FRAGMENT> LocationList = Context.GetFragmentView<FOW_LOCATION_FRAGMENT>();
	const TConstArrayView<FMassVisionFragment> VisionList = Context.GetFragmentView<FMassVisionFragment>();
	const TArrayView<FMassPreviousVisionFragment> PreviousVisionList = Context.GetMutableFragmentView<FMassPreviousVisionFragment>();

	for (int32 EntityIndex = 0; EntityIndex < Context.GetNumEntities(); ++EntityIndex)
	{
		const FVector Location = FOW_GET_LOCATION(LocationList[EntityIndex]);
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
						const FIntPoint GlobalIJ = PreviousVisionFragment.PreviousVisionData.LocalToGlobal({ I, J });
						if (UMinimapDataSubsystem::IsVisionGridIJValid_Static(GlobalIJ))
						{
							FTile& GlobalTile = MinimapSubsystem.GetVisionTile(GlobalIJ);
							checkSlow(GlobalTile.VisibilityCounter > 0);
							GlobalTile.VisibilityCounter--;
						}
					}
				}
			}
			PreviousVisionFragment.PreviousVisionData.bHasCachedData = false;
		}

		if (SightRadius <= 0.0f)
		{
			PreviousVisionFragment.PreviousVisionData = FVisionUnitData();
			continue;
		}

		// Create current VisionUnitData
		int LocalAreaTilesResolution = FMath::CeilToInt32(SightRadius * 2 / VisionTileSize) + 1;
		TArray<ETileState> LocalAreaTilesStates;
		LocalAreaTilesStates.Init(ETileState::NotVisible, LocalAreaTilesResolution * LocalAreaTilesResolution);
		
		FVisionUnitData VisionUnitData = {
			.LocalAreaTilesResolution = LocalAreaTilesResolution,
			.GridSpaceRadius = SightRadius / VisionTileSize,
			.LocalAreaTilesCachedStates = MoveTemp(LocalAreaTilesStates),
			.CachedOriginWorldLocation = Location,
		};

		const FVector2f OriginGridLocation = UMinimapDataSubsystem::ConvertWorldSpaceLocationToVisionGridSpace_Static(FVector2D(Location));
		const FIntPoint OriginGridLocationRounded = UMinimapDataSubsystem::ConvertVisionGridLocationToTileIJ_Static(OriginGridLocation + VisionUnitData.GridSpaceRadius);
		const FIntPoint OriginGridLocationRounded2 = UMinimapDataSubsystem::ConvertVisionGridLocationToTileIJ_Static(OriginGridLocation - VisionUnitData.GridSpaceRadius);

		checkSlow(OriginGridLocationRounded.X - OriginGridLocationRounded2.X + 1 <= VisionUnitData.LocalAreaTilesResolution);
		checkSlow(OriginGridLocationRounded.Y - OriginGridLocationRounded2.Y + 1 <= VisionUnitData.LocalAreaTilesResolution);
		checkSlow(OriginGridLocationRounded.X - OriginGridLocationRounded2.X + 1 + 2 > VisionUnitData.LocalAreaTilesResolution);
		checkSlow(OriginGridLocationRounded.Y - OriginGridLocationRounded2.Y + 1 + 2 > VisionUnitData.LocalAreaTilesResolution);

		VisionUnitData.LocalAreaTilesCachedStates.Init(ETileState::Unknown, VisionUnitData.LocalAreaTilesCachedStates.Num());
		const FIntPoint OriginGlobalIJ = UMinimapDataSubsystem::ConvertVisionGridLocationToTileIJ_Static(OriginGridLocation);
		
		if (!UMinimapDataSubsystem::IsVisionGridIJValid_Static(OriginGlobalIJ))
		{
			UE_LOG(LogTemp, Verbose, TEXT("Vision entity is outside the FogOfWar grid. Skipping."));
			PreviousVisionFragment.PreviousVisionData = MoveTemp(VisionUnitData);
			continue; // Use continue to skip this entity and proceed with the next in the chunk
		}

		if (VisionUnitData.LocalAreaTilesResolution == 0)
		{
			PreviousVisionFragment.PreviousVisionData = MoveTemp(VisionUnitData);
			return;
		}

		VisionUnitData.CachedOriginGlobalIndex = UMinimapDataSubsystem::GetVisionGridGlobalIndex_Static(OriginGlobalIJ);
		VisionUnitData.LocalAreaCachedMinIJ = UMinimapDataSubsystem::ConvertVisionGridLocationToTileIJ_Static(OriginGridLocation - VisionUnitData.GridSpaceRadius);
		const FIntPoint OriginLocalIJ = VisionUnitData.GlobalToLocal(OriginGlobalIJ);

		VisionUnitData.GetLocalTileState(OriginLocalIJ) = ETileState::Visible;

		const float GridSpaceRadiusSqr = FMath::Square(VisionUnitData.GridSpaceRadius);

		// going in spiral
		{
#if DO_GUARD_SLOW
			int SafetyIterations = VisionUnitData.LocalAreaTilesCachedStates.Num();
			TArray<bool> IsTileVisited;
			IsTileVisited.Init(false, VisionUnitData.LocalAreaTilesCachedStates.Num());
#endif

			enum class EDirection { Right, Up, Left, Down };
			const FIntPoint DirectionDeltas[] = { {0, 1}, {1, 0}, {0, -1}, {-1, 0} };

			EDirection CurrentDirection = EDirection::Right;
			bool Clock = true;
			int CurrentStepSize = VisionUnitData.LocalAreaTilesResolution;
			int LeftToSpend = CurrentStepSize;
			FIntPoint CurrentLocalIJ = FIntPoint(0, 0) - DirectionDeltas[static_cast<int>(CurrentDirection)];

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

					const FIntPoint GlobalIJ = VisionUnitData.LocalToGlobal(CurrentLocalIJ);

					if (UMinimapDataSubsystem::IsVisionGridIJValid_Static(GlobalIJ))
					{
						int DistToTileSqr = FMath::Square(OriginGlobalIJ.X - GlobalIJ.X) + FMath::Square(OriginGlobalIJ.Y - GlobalIJ.Y);
						if (DistToTileSqr <= GridSpaceRadiusSqr)
						{
							TArray<int> CurrentDDALocalIndexesStack;
							int LocalIndex = VisionUnitData.GetLocalIndex(CurrentLocalIJ);
							if (VisionUnitData.GetLocalTileState(LocalIndex) == ETileState::Unknown)
							{
								const FIntPoint Direction = OriginLocalIJ - CurrentLocalIJ;
								checkSlow(FMath::Abs(Direction.X) + FMath::Abs(Direction.Y) != 0);
								const FIntPoint DirectionSign = { Direction.X >= 0 ? 1 : -1, Direction.Y >= 0 ? 1 : -1 };
								const float S_x = FMath::Sqrt(FMath::Square(1.0) + FMath::Square(static_cast<float>(Direction.Y) / Direction.X));
								const float S_y = FMath::Sqrt(FMath::Square(1.0) + FMath::Square(static_cast<float>(Direction.X) / Direction.Y));
								float NextAccumulatedDxLength = 0.5 * S_x;
								float NextAccumulatedDyLength = 0.5 * S_y;

								bool bIsBlocking = false;
								const int DDASafetyIterations = FMath::Abs(Direction.X) + FMath::Abs(Direction.Y) + 1;
								checkSlow(DDASafetyIterations < 10000);
								int DDASafetyCounter;

								FIntPoint CurrentDDALocalIJ = CurrentLocalIJ;
								int CurrentDDALocalIndex = LocalIndex;

								for (DDASafetyCounter = 0; DDASafetyCounter < DDASafetyIterations; DDASafetyCounter++)
								{
									CurrentDDALocalIndexesStack.Push(CurrentDDALocalIndex);
									if (CurrentDDALocalIJ == OriginLocalIJ) break;

									const float CurrentHeight = MinimapSubsystem.GetVisionTile(VisionUnitData.LocalToGlobal(CurrentDDALocalIJ)).Height;
									if (MinimapSubsystem.IsBlockingVision(Location.Z, CurrentHeight))
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
									checkSlow(UMinimapDataSubsystem::IsVisionGridIJValid_Static(VisionUnitData.LocalToGlobal(CurrentDDALocalIJ)));
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
				const FIntPoint GlobalIJ = VisionUnitData.LocalToGlobal({ I, J });
				if (UMinimapDataSubsystem::IsVisionGridIJValid_Static(GlobalIJ))
				{
					int DistToTileSqr = FMath::Square(OriginGlobalIJ.X - GlobalIJ.X) + FMath::Square(OriginGlobalIJ.Y - GlobalIJ.Y);
					if (DistToTileSqr <= GridSpaceRadiusSqr)
					{
						if (VisionUnitData.GetLocalTileState({ I, J }) == ETileState::Visible)
						{
							FTile& GlobalTile = MinimapSubsystem.GetVisionTile(GlobalIJ);
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

void UInitialVisionProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FOW_LOCATION_FRAGMENT>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassVisionFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassPreviousVisionFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddTagRequirement<FMassVisionEntityTag>(EMassFragmentPresence::All);
	EntityQuery.AddTagRequirement<FMassVisionInitializedTag>(EMassFragmentPresence::None); // Run only on uninitialized entities
	EntityQuery.AddSubsystemRequirement<UMinimapDataSubsystem>(EMassFragmentAccess::ReadWrite);
	ProcessorRequirements.AddSubsystemRequirement<UMinimapDataSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UInitialVisionProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UMinimapDataSubsystem* MinimapSubsystem = Context.GetMutableSubsystem<UMinimapDataSubsystem>();
	if (!MinimapSubsystem || !MinimapSubsystem->bVisionGridActive || !MinimapSubsystem->IsVisionGridReady())
	{
		return;
	}

	EntityQuery.ForEachEntityChunk(Context, [MinimapSubsystem](FMassExecutionContext& Context)
	{
		FFogOfWarMassHelpers::ProcessEntityChunk(Context, *MinimapSubsystem);

		const TArrayView<const FMassEntityHandle> Entities = Context.GetEntities();
		for (const FMassEntityHandle& Entity : Entities)
		{
			Context.Defer().AddTag<FMassVisionInitializedTag>(Entity);
		}
	});
}


//----------------------------------------------------------------------
//  UMassBattleFogOfWarBootstrapProcessor
//----------------------------------------------------------------------
UMassBattleFogOfWarBootstrapProcessor::UMassBattleFogOfWarBootstrapProcessor()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
	ExecutionOrder.ExecuteBefore.Add(UInitialVisionProcessor::StaticClass()->GetFName());
}

void UMassBattleFogOfWarBootstrapProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FOW_LOCATION_FRAGMENT>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FOW_TEAM_FRAGMENT>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassVisionFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::None);
	EntityQuery.AddTagRequirement<FAgentTag>(EMassFragmentPresence::All);
	EntityQuery.AddSubsystemRequirement<UMinimapDataSubsystem>(EMassFragmentAccess::ReadOnly);
	ProcessorRequirements.AddSubsystemRequirement<UMinimapDataSubsystem>(EMassFragmentAccess::ReadOnly);
}

void UMassBattleFogOfWarBootstrapProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	const UMinimapDataSubsystem* MinimapSubsystem = Context.GetSubsystem<UMinimapDataSubsystem>();
	if (!MinimapSubsystem || !MinimapSubsystem->bAutoBindMassBattleAgents)
	{
		return;
	}

	const float DefaultSightRadius = MinimapSubsystem->DefaultMassBattleSightRadius;

	EntityQuery.ForEachEntityChunk(Context, [DefaultSightRadius](FMassExecutionContext& Context)
	{
		const TArrayView<const FMassEntityHandle> Entities = Context.GetEntities();

		const bool bHasPreviousVision = Context.DoesArchetypeHaveFragment<FMassPreviousVisionFragment>();
		const bool bHasVisionEntityTag = Context.DoesArchetypeHaveTag<FMassVisionEntityTag>();
		const bool bHasVisibleEntityTag = Context.DoesArchetypeHaveTag<FMassVisibleEntityTag>();

		for (int32 EntityIndex = 0; EntityIndex < Context.GetNumEntities(); ++EntityIndex)
		{
			const FMassEntityHandle Entity = Entities[EntityIndex];

			FMassVisionFragment VisionFragment;
			VisionFragment.SightRadius = DefaultSightRadius;
			Context.Defer().PushCommand<FMassCommandAddFragmentInstances>(Entity, VisionFragment);

			if (!bHasPreviousVision)
			{
				Context.Defer().PushCommand<FMassCommandAddFragmentInstances>(Entity, FMassPreviousVisionFragment());
			}

			if (!bHasVisionEntityTag)
			{
				Context.Defer().AddTag<FMassVisionEntityTag>(Entity);
			}
			if (!bHasVisibleEntityTag)
			{
				Context.Defer().AddTag<FMassVisibleEntityTag>(Entity);
			}
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
}

void UVisionProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
    EntityQuery.AddRequirement<FOW_LOCATION_FRAGMENT>(EMassFragmentAccess::ReadOnly);
    EntityQuery.AddRequirement<FMassVisionFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassPreviousVisionFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddTagRequirement<FMassVisionEntityTag>(EMassFragmentPresence::All);
	EntityQuery.AddTagRequirement<FMassLocationChangedTag>(EMassFragmentPresence::All); // Only process entities that have moved
	EntityQuery.AddSubsystemRequirement<UMinimapDataSubsystem>(EMassFragmentAccess::ReadWrite);
	ProcessorRequirements.AddSubsystemRequirement<UMinimapDataSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UVisionProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UMinimapDataSubsystem* MinimapSubsystem = Context.GetMutableSubsystem<UMinimapDataSubsystem>();
	if (!MinimapSubsystem || !MinimapSubsystem->bVisionGridActive || !MinimapSubsystem->IsVisionGridReady())
	{
		return;
	}

	EntityQuery.ForEachEntityChunk(Context, [MinimapSubsystem](FMassExecutionContext& Context)
	{
		FFogOfWarMassHelpers::ProcessEntityChunk(Context, *MinimapSubsystem);

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

void UDebugStressTestProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FMassVisionFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddTagRequirement<FMassVisionEntityTag>(EMassFragmentPresence::All);
	EntityQuery.AddSubsystemRequirement<UMinimapDataSubsystem>(EMassFragmentAccess::ReadOnly);
	ProcessorRequirements.AddSubsystemRequirement<UMinimapDataSubsystem>(EMassFragmentAccess::ReadOnly);
}

void UDebugStressTestProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	const UMinimapDataSubsystem* MinimapSubsystem = Context.GetSubsystem<UMinimapDataSubsystem>();
	if (!MinimapSubsystem || !MinimapSubsystem->bVisionGridActive)
	{
		return;
	}

	const bool bForceVisionUpdate = MinimapSubsystem->bDebugStressTestIgnoreCache;
	if (!bForceVisionUpdate)
	{
		return;
	}

	EntityQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& Context)
	{
		const auto& Entities = Context.GetEntities();
		for (const FMassEntityHandle& Entity : Entities)
		{
			Context.Defer().AddTag<FMassLocationChangedTag>(Entity);
		}
	});
}
