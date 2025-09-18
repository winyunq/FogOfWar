// Copyright Winyunq, 2025. All Rights Reserved.

#include "MassFogOfWarProcessors.h"
#include "MassFogOfWarFragments.h"
#include "MassCommonFragments.h"
#include "FogOfWar.h"
#include "MassExecutionContext.h"
#include "Kismet/GameplayStatics.h"
#include "Containers/StringView.h"

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