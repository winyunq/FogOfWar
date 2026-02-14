// Copyright Winyunq, 2025. All Rights Reserved.

#include "MassFogOfWarProcessors.h"
#include "MassFogOfWarFragments.h"
#include "MassCommonFragments.h"
#include "FogOfWar.h"
#include "Subsystems/MinimapDataSubsystem.h"
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
	// Subsystem is now accessed via its static Get() method.
	const float VisionTileSize = UMinimapDataSubsystem::Get()->VisionTileSize;
	
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
						const FIntPoint GlobalIJ = PreviousVisionFragment.PreviousVisionData.LocalToGlobal({ I, J });
						if (UMinimapDataSubsystem::IsVisionGridIJValid_Static(GlobalIJ))
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
		int LocalAreaTilesResolution = FMath::CeilToInt32(SightRadius * 2 / VisionTileSize) + 1;
		TArray<ETileState> LocalAreaTilesStates;
		LocalAreaTilesStates.Init(ETileState::NotVisible, LocalAreaTilesResolution * LocalAreaTilesResolution);
		
		FVisionUnitData VisionUnitData = {
			.LocalAreaTilesResolution = LocalAreaTilesResolution,
			.GridSpaceRadius = SightRadius / VisionTileSize,
			.LocalAreaTilesCachedStates = MoveTemp(LocalAreaTilesStates),
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
			UE_LOG(LogFogOfWar, Verbose, TEXT("Vision actor is outside the grid. Skipping."));
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

void UInitialVisionProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassVisionFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassPreviousVisionFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddTagRequirement<FMassVisionInitializedTag>(EMassFragmentPresence::None); // Run only on uninitialized entities
}

void UInitialVisionProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	/*if (!FogOfWarActor.Get())
	{
		FogOfWarActor = Cast<AFogOfWar>(UGameplayStatics::GetActorOfClass(GetWorld(), AFogOfWar::StaticClass()));
	}
	if (!FogOfWarActor.Get() || !FogOfWarActor->IsActivated() || !UMinimapDataSubsystem::Get() || !UMinimapDataSubsystem::Get()->bIsInitialized)
	{
		return;
	}

	EntityQuery.ForEachEntityChunk(Context, [this](FMassExecutionContext& Context)
	{
		const auto& Entities = Context.GetEntities();
		for (const FMassEntityHandle& Entity : Entities)
		{
			Context.Defer().AddTag<FMassVisionInitializedTag>(Entity);
		}
	});*/
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
    EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
    EntityQuery.AddRequirement<FMassVisionFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassPreviousVisionFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddTagRequirement<FMassLocationChangedTag>(EMassFragmentPresence::All); // Only process entities that have moved

	// --- 核心修复 ---
	// 只处理未被剔除的实体.
	// 即：实体不能有“被距离剔除”的Tag，也不能有“被视锥体剔除”的Tag。
	EntityQuery.AddTagRequirement<FMassVisibilityCulledByDistanceTag>(EMassFragmentPresence::None);
	EntityQuery.AddTagRequirement<FMassVisibilityCulledByFrustumTag>(EMassFragmentPresence::None);
}

void UVisionProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	//if (!FogOfWarActor.Get())
	//{
	//	FogOfWarActor = Cast<AFogOfWar>(UGameplayStatics::GetActorOfClass(GetWorld(), AFogOfWar::StaticClass()));
	//}
	//if (!FogOfWarActor.Get() || !FogOfWarActor->IsActivated() || !UMinimapDataSubsystem::Get() || !UMinimapDataSubsystem::Get()->bIsInitialized)
	//{
	//	return;
	//}

	//EntityQuery.ForEachEntityChunk(Context, [this](FMassExecutionContext& Context)
	//{
	//	FFogOfWarMassHelpers::ProcessEntityChunk(Context, FogOfWarActor.Get());

	//	// Remove location changed tag from all entities in the chunk
	//	const auto& Entities = Context.GetEntities();
	//	for (const FMassEntityHandle& Entity : Entities)
	//	{
	//		Context.Defer().RemoveTag<FMassLocationChangedTag>(Entity);
	//	}
	//});
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
}

void UDebugStressTestProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	/*if (!FogOfWarActor.Get())
	{
		FogOfWarActor = Cast<AFogOfWar>(UGameplayStatics::GetActorOfClass(GetWorld(), AFogOfWar::StaticClass()));
	}
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

	EntityQuery.ForEachEntityChunk(Context, [this, bForceVisionUpdate, bForceMinimapUpdate](FMassExecutionContext& Context)
	{
		const auto& Entities = Context.GetEntities();
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
	});*/
}

