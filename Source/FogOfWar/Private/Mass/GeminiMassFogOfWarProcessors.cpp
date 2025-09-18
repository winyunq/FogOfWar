// Copyright Winyunq, 2025. All Rights Reserved.

#include "Mass/GeminiMassFogOfWarProcessors.h"
#include "Mass/GeminiMassFogOfWarFragments.h"
#include "MassCommonFragments.h" // Explicitly added for FMassPreviousLocationFragment
#include "GeminiFogOfWar.h"
#include "MassExecutionContext.h"
#include "Kismet/GameplayStatics.h"
#include "Containers/StringView.h"

//----------------------------------------------------------------------//
//  UGeminiMovementDetectionProcessor
//----------------------------------------------------------------------//
UGeminiMovementDetectionProcessor::UGeminiMovementDetectionProcessor()
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
	// ExecutionOrder.ExecuteInGroup = UE_MUTABLE_ARRAY_TO_STRING_VIEW("FogOfWar");
	// ExecutionOrder.ExecuteAfter.Add(UE_MUTABLE_ARRAY_TO_STRING_VIEW("MassSimulationLODProcessor"));
}

void UGeminiMovementDetectionProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	// EntityQuery.AddRequirement<FMassPreviousLocationFragment>(EMassFragmentAccess::ReadWrite);
    EntityQuery.AddTagRequirement<FGeminiMassVisionEntityTag>(EMassFragmentPresence::All);
	EntityQuery.AddTagRequirement<FGeminiMassStationaryTag>(EMassFragmentPresence::None); // Ignore stationary entities
}

void UGeminiMovementDetectionProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntityManager, Context, [this](FMassExecutionContext& Context)
	{
		const TConstArrayView<FTransformFragment> TransformList = Context.GetFragmentView<FTransformFragment>();
		// const TArrayView<FMassPreviousLocationFragment> PreviousLocationList = Context.GetFragmentView<FMassPreviousLocationFragment>();

		for (int32 EntityIndex = 0; EntityIndex < Context.GetNumEntities(); ++EntityIndex)
		{
			const FVector& Location = TransformList[EntityIndex].GetTransform().GetLocation();
			// FVector& PreviousLocation = PreviousLocationList[EntityIndex].Location;

			// Compare current location with previous location (with a tolerance)
			// if (!Location.Equals(PreviousLocation, 1.0f))
			// {
				// Context.AddTag<FGeminiMassLocationChangedTag>(Context.GetEntity(EntityIndex));
				// PreviousLocation = Location;
			// }
		}
	});
}

//----------------------------------------------------------------------//
//  UGeminiVisionProcessor
//----------------------------------------------------------------------//
UGeminiVisionProcessor::UGeminiVisionProcessor()
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
	// ExecutionOrder.ExecuteInGroup = UE_MUTABLE_ARRAY_TO_STRING_VIEW("FogOfWar");
	// ExecutionOrder.ExecuteAfter.Add(UE_MUTABLE_ARRAY_TO_STRING_VIEW("UGeminiMovementDetectionProcessor"));
}

void UGeminiVisionProcessor::ConfigureQueries()
{
    EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
    EntityQuery.AddRequirement<FGeminiMassVisionFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FGeminiMassPreviousVisionFragment>(EMassFragmentAccess::ReadWrite); // Added for previous vision data
	EntityQuery.AddTagRequirement<FGeminiMassLocationChangedTag>(EMassFragmentPresence::All); // Only process entities that have moved
}

void UGeminiVisionProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	AGeminiFogOfWar* GeminiFogOfWar = Cast<AGeminiFogOfWar>(UGameplayStatics::GetActorOfClass(GetWorld(), AGeminiFogOfWar::StaticClass()));
	if (!IsValid(GeminiFogOfWar) || !GeminiFogOfWar->IsActivated())
	{
		return;
	}
/*
	EntityQuery.ForEachEntityChunk(EntityManager, Context, [this, &GeminiFogOfWar](FMassExecutionContext& Context)
	{
		const TConstArrayView<FTransformFragment> TransformList = Context.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FGeminiMassVisionFragment> VisionList = Context.GetFragmentView<FGeminiMassVisionFragment>();
		const TArrayView<FGeminiMassPreviousVisionFragment> PreviousVisionList = Context.GetMutableFragmentView<FGeminiMassPreviousVisionFragment>();

		for (int32 EntityIndex = 0; EntityIndex < Context.GetNumEntities(); ++EntityIndex)
		{
			const FVector& Location = TransformList[EntityIndex].GetTransform().GetLocation();
			const float SightRadius = VisionList[EntityIndex].SightRadius;
			FGeminiMassPreviousVisionFragment& PreviousVisionFragment = PreviousVisionList[EntityIndex];

			// Reset previous vision contribution (logic moved from AGeminiFogOfWar::ResetCachedVisibilities)
			if (PreviousVisionFragment.PreviousVisionData.bHasCachedData)
			{
				for (int I = 0; I < PreviousVisionFragment.PreviousVisionData.LocalAreaTilesResolution; I++)
				{
					for (int J = 0; J < PreviousVisionFragment.PreviousVisionData.LocalAreaTilesResolution; J++)
					{
						if (PreviousVisionFragment.PreviousVisionData.GetLocalTileState({ I, J }) == ETileState::Visible)
						{
							FIntVector2 GlobalIJ = PreviousVisionFragment.PreviousVisionData.LocalToGlobal({ I, J });
							if (GeminiFogOfWar->IsGlobalIJValid(GlobalIJ)) // Check if GlobalIJ is valid before accessing Tiles
							{
								FTile& GlobalTile = GeminiFogOfWar->GetGlobalTile(GlobalIJ);
								checkSlow(GlobalTile.VisibilityCounter > 0);
								GlobalTile.VisibilityCounter--;
							}
						}
					}
				}
				PreviousVisionFragment.PreviousVisionData.bHasCachedData = false;
			}

			// Create current VisionUnitData
			int LocalAreaTilesResolution = FMath::CeilToInt32(SightRadius * 2 / GeminiFogOfWar->GetTileSize()) + 1;
			TArray<ETileState> LocalAreaTilesStates;
			LocalAreaTilesStates.Init(ETileState::NotVisible, LocalAreaTilesResolution * LocalAreaTilesResolution);
			
			AGeminiFogOfWar::FVisionUnitData VisionUnitData = {
				.LocalAreaTilesResolution = LocalAreaTilesResolution,
				.GridSpaceRadius = SightRadius / GeminiFogOfWar->GetTileSize(),
				.LocalAreaTilesCachedStates = MoveTemp(LocalAreaTilesStates),
			};

			// Core logic moved from AGeminiFogOfWar::UpdateVisibilities
			const FVector2f OriginGridLocation = GeminiFogOfWar->ConvertWorldSpaceLocationToGridSpace(FVector2D(Location));

			// check that we have allocated enough local area cached tiles to fit the radius. THIS IS A MUST!
			checkSlow(GeminiFogOfWar->ConvertGridLocationToTileIJ(OriginGridLocation + VisionUnitData.GridSpaceRadius).X - GeminiFogOfWar->ConvertGridLocationToTileIJ(OriginGridLocation - VisionUnitData.GridSpaceRadius).X + 1 <= VisionUnitData.LocalAreaTilesResolution);
			checkSlow(GeminiFogOfWar->ConvertGridLocationToTileIJ(OriginGridLocation + VisionUnitData.GridSpaceRadius).Y - GeminiFogOfWar->ConvertGridLocationToTileIJ(OriginGridLocation - VisionUnitData.GridSpaceRadius).Y + 1 <= VisionUnitData.LocalAreaTilesResolution);
			// check that we have allocated not too much local area cached tiles to fit the radius. this is not a must, but saves memory
			checkSlow(GeminiFogOfWar->ConvertGridLocationToTileIJ(OriginGridLocation + VisionUnitData.GridSpaceRadius).X - GeminiFogOfWar->ConvertGridLocationToTileIJ(OriginGridLocation - VisionUnitData.GridSpaceRadius).X + 1 + 2 > VisionUnitData.LocalAreaTilesResolution);
			checkSlow(GeminiFogOfWar->ConvertGridLocationToTileIJ(OriginGridLocation + VisionUnitData.GridSpaceRadius).Y - GeminiFogOfWar->ConvertGridLocationToTileIJ(OriginGridLocation - VisionUnitData.GridSpaceRadius).Y + 1 + 2 > VisionUnitData.LocalAreaTilesResolution);

			VisionUnitData.LocalAreaTilesCachedStates.Init(ETileState::Unknown, VisionUnitData.LocalAreaTilesCachedStates.Num());
			const FIntVector2 OriginGlobalIJ = GeminiFogOfWar->ConvertGridLocationToTileIJ(OriginGridLocation);
			// if the vision unit is outside the grid, we ignore it (normally this shouldn\'t happen)
			if (!ensureMsgf(GeminiFogOfWar->IsGlobalIJValid(OriginGlobalIJ), TEXT("Vision actor is outside the grid")))
			{
				// Store current vision data as previous for the next frame, even if invalid, to avoid re-processing
				PreviousVisionFragment.PreviousVisionData = MoveTemp(VisionUnitData);
				Context.RemoveTag<FGeminiMassLocationChangedTag>(Context.GetEntity(EntityIndex));
				continue;
			}

			if (VisionUnitData.LocalAreaTilesResolution == 0)
			{
				// Store current vision data as previous for the next frame, even if invalid, to avoid re-processing
				PreviousVisionFragment.PreviousVisionData = MoveTemp(VisionUnitData);
				Context.RemoveTag<FGeminiMassLocationChangedTag>(Context.GetEntity(EntityIndex));
				continue;
			}

			VisionUnitData.CachedOriginGlobalIndex = GeminiFogOfWar->GetGlobalIndex(OriginGlobalIJ);
			// the \"bottom-left\" tile of the local area in the global grid space
			VisionUnitData.LocalAreaCachedMinIJ = GeminiFogOfWar->ConvertGridLocationToTileIJ(OriginGridLocation - VisionUnitData.GridSpaceRadius);
			const FIntVector2 OriginLocalIJ = VisionUnitData.GlobalToLocal(OriginGlobalIJ);

			// we see the tile we\'re currently on
			VisionUnitData.GetLocalTileState(OriginLocalIJ) = ETileState::Visible;

			const float GridSpaceRadiusSqr = FMath::Square(VisionUnitData.GridSpaceRadius);

			// this is to avoid recursion overhead and this is not a local variable to avoid allocations overhead
			TArray<int> DDALocalIndexesStack;

			// going in spiral (spooky code)
			{
#if DO_GUARD_SLOW
				int SafetyIterations = VisionUnitData.LocalAreaTilesCachedStates.Num();
				TArray<bool> IsTileVisited;
				IsTileVisited.Init(false, VisionUnitData.LocalAreaTilesCachedStates.Num());
#endif

				// in the order of spiral traversal
				enum class EDirection
				{
					Right,
					Up,
					Left,
					Down,
				};
				const FIntVector2 DirectionDeltas[] = {
					{0, 1},
					{1, 0},
					{0, -1},
					{-1, 0},
				};

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

						if (GeminiFogOfWar->IsGlobalIJValid(GlobalIJ))
						{
							// the distance between bottom-left corners of the tiles is the same as the distance between their centers, no need to add 0.5
							int DistToTileSqr = FMath::Square(OriginGlobalIJ.X - GlobalIJ.X) + FMath::Square(OriginGlobalIJ.Y - GlobalIJ.Y);
							if (DistToTileSqr <= GridSpaceRadiusSqr)
							{
								// ExecuteDDAVisibilityCheck logic moved here
								TArray<int> CurrentDDALocalIndexesStack;
								int LocalIndex = VisionUnitData.GetLocalIndex(CurrentLocalIJ);
								if (VisionUnitData.GetLocalTileState(LocalIndex) == ETileState::Unknown)
								{
									const FIntVector2 Direction = OriginLocalIJ - CurrentLocalIJ;
									checkSlow(FMath::Abs(Direction.X) + FMath::Abs(Direction.Y) != 0);
									const FIntVector2 DirectionSign = {
										Direction.X >= 0 ? 1 : -1,
										Direction.Y >= 0 ? 1 : -1
									};
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

										if (CurrentDDALocalIJ == OriginLocalIJ)
										{
											break;
										}

										auto CurrentHeight = GeminiFogOfWar->GetGlobalTile(VisionUnitData.LocalToGlobal(CurrentDDALocalIJ)).Height;
										if (GeminiFogOfWar->IsBlockingVision(Location.Z, CurrentHeight))
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
										checkSlow(GeminiFogOfWar->IsGlobalIJValid(VisionUnitData.LocalToGlobal(CurrentDDALocalIJ)));

										CurrentDDALocalIndex = VisionUnitData.GetLocalIndex(CurrentDDALocalIJ);
									}

									checkSlow(DDASafetyCounter < DDASafetyIterations);

									if (bIsBlocking)
									{
										while (!CurrentDDALocalIndexesStack.IsEmpty())
										{
											int LocalIndexFromStack = CurrentDDALocalIndexesStack.Pop(false);
											auto& TileState = VisionUnitData.GetLocalTileState(LocalIndexFromStack);
											if (TileState != ETileState::Visible)
											{
												TileState = ETileState::NotVisible;
											}
										}
									}
									else
									{
										while (!CurrentDDALocalIndexesStack.IsEmpty())
										{
											int LocalIndexFromStack = CurrentDDALocalIndexesStack.Pop(false);
											auto& TileState = VisionUnitData.GetLocalTileState(LocalIndexFromStack);
											TileState = ETileState::Visible;
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
							if (CurrentStepSize == 1)
							{
								break;
							}
							CurrentStepSize--;
						}
						Clock ^= 1;
						CurrentDirection = static_cast<EDirection>((static_cast<int>(CurrentDirection) + 1) % 4);
						LeftToSpend = CurrentStepSize;
					}
				}

#if DO_GUARD_SLOW
				check(SafetyIterations == 0);

				for (auto bVisited : IsTileVisited)
				{
					check(bVisited);
				}
#endif
			}

			for (int I = 0; I < VisionUnitData.LocalAreaTilesResolution; I++)
			{
				for (int J = 0; J < VisionUnitData.LocalAreaTilesResolution; J++)
				{
					FIntVector2 GlobalIJ = VisionUnitData.LocalToGlobal({ I, J });

					if (GeminiFogOfWar->IsGlobalIJValid(GlobalIJ))
					{
						// the distance between bottom-left corners of the tiles is the same as the distance between their centers, no need to add 0.5
						int DistToTileSqr = FMath::Square(OriginGlobalIJ.X - GlobalIJ.X) + FMath::Square(OriginGlobalIJ.Y - GlobalIJ.Y);
						if (DistToTileSqr <= GridSpaceRadiusSqr)
						{
							if (VisionUnitData.GetLocalTileState({ I, J }) == ETileState::Visible)
							{
								FTile& GlobalTile = GeminiFogOfWar->GetGlobalTile(GlobalIJ);
								GlobalTile.VisibilityCounter++;
							}
						}
					}
				}
			}

			VisionUnitData.bHasCachedData = true;

			// Store current vision data as previous for the next frame
			PreviousVisionFragment.PreviousVisionData = MoveTemp(VisionUnitData);

			// Context.RemoveTag<FGeminiMassLocationChangedTag>(Context.GetEntity(EntityIndex));
		}
	});*/
}
