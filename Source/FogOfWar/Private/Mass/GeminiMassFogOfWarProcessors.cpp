// Copyright Winyunq, 2025. All Rights Reserved.

#include "Mass/GeminiMassFogOfWarProcessors.h"
#include "Mass/GeminiMassFogOfWarFragments.h"
#include "Public/GeminiFogOfWar.h"
#include "MassExecutionContext.h"
#include "Kismet/GameplayStatics.h"

//----------------------------------------------------------------------//
//  UGeminiMovementDetectionProcessor
//----------------------------------------------------------------------//
UGeminiMovementDetectionProcessor::UGeminiMovementDetectionProcessor()
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
	ExecutionOrder.ExecuteInGroup = UE_MUTABLE_ARRAY_TO_STRING_VIEW("FogOfWar");
	ExecutionOrder.ExecuteAfter.Add(UE_MUTABLE_ARRAY_TO_STRING_VIEW("MassSimulationLODProcessor"));
}

void UGeminiMovementDetectionProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassPreviousLocationFragment>(EMassFragmentAccess::ReadWrite);
    EntityQuery.AddTagRequirement<FGeminiMassVisionEntityTag>(EMassFragmentPresence::All);
	EntityQuery.AddTagRequirement<FGeminiMassStationaryTag>(EMassFragmentPresence::None); // Ignore stationary entities
}

void UGeminiMovementDetectionProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntityManager, Context, [this](FMassExecutionContext& Context)
	{
		const TConstArrayView<FTransformFragment> TransformList = Context.GetFragmentView<FTransformFragment>();
		const TArrayView<FMassPreviousLocationFragment> PreviousLocationList = Context.GetFragmentView<FMassPreviousLocationFragment>();

		for (int32 EntityIndex = 0; EntityIndex < Context.GetNumEntities(); ++EntityIndex)
		{
			const FVector& CurrentLocation = TransformList[EntityIndex].GetTransform().GetLocation();
			FVector& PreviousLocation = PreviousLocationList[EntityIndex].Location;

			// Compare current location with previous location (with a tolerance)
			if (!CurrentLocation.Equals(PreviousLocation, 1.0f))
			{
				Context.AddTag<FGeminiMassLocationChangedTag>(Context.GetEntity(EntityIndex));
				PreviousLocation = CurrentLocation;
			}
		}
	});
}

//----------------------------------------------------------------------//
//  UGeminiVisionProcessor
//----------------------------------------------------------------------//
UGeminiVisionProcessor::UGeminiVisionProcessor()
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
	ExecutionOrder.ExecuteInGroup = UE_MUTABLE_ARRAY_TO_STRING_VIEW("FogOfWar");
	ExecutionOrder.ExecuteAfter.Add(UE_MUTABLE_ARRAY_TO_STRING_VIEW("UGeminiMovementDetectionProcessor"));
}

void UGeminiVisionProcessor::ConfigureQueries()
{
    EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
    EntityQuery.AddRequirement<FGeminiMassVisionFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddTagRequirement<FGeminiMassLocationChangedTag>(EMassFragmentPresence::All); // Only process entities that have moved
}

void UGeminiVisionProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	AGeminiFogOfWar* GeminiFogOfWar = Cast<AGeminiFogOfWar>(UGameplayStatics::GetActorOfClass(GetWorld(), AGeminiFogOfWar::StaticClass()));
	if (!IsValid(GeminiFogOfWar) || !GeminiFogOfWar->IsActivated())
	{
		return;
	}

	EntityQuery.ForEachEntityChunk(EntityManager, Context, [this, &GeminiFogOfWar](FMassExecutionContext& Context)
	{
		const TConstArrayView<FTransformFragment> TransformList = Context.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FGeminiMassVisionFragment> VisionList = Context.GetFragmentView<FGeminiMassVisionFragment>();

		for (int32 EntityIndex = 0; EntityIndex < Context.GetNumEntities(); ++EntityIndex)
		{
			const FVector& Location = TransformList[EntityIndex].GetTransform().GetLocation();
			const float SightRadius = VisionList[EntityIndex].SightRadius;

			// Create a temporary VisionUnitData on the stack
			int LocalAreaTilesResolution = FMath::CeilToInt32(SightRadius * 2 / GeminiFogOfWar->GetTileSize()) + 1;
			TArray<AGeminiFogOfWar::FVisionUnitData::TileState> LocalAreaTilesStates;
			LocalAreaTilesStates.Init(AGeminiFogOfWar::FVisionUnitData::TileState::NotVisible, LocalAreaTilesResolution * LocalAreaTilesResolution);
			
			AGeminiFogOfWar::FVisionUnitData VisionUnitData = {
				.LocalAreaTilesResolution = LocalAreaTilesResolution,
				.GridSpaceRadius = SightRadius / GeminiFogOfWar->GetTileSize(),
				.LocalAreaTilesCachedStates = MoveTemp(LocalAreaTilesStates),
			};

			// Call the UpdateVisibilities function on the main fog of war actor
			GeminiFogOfWar->UpdateVisibilities(Location, VisionUnitData);

			// Remove the tag so it's not processed again until it moves
			Context.RemoveTag<FGeminiMassLocationChangedTag>(Context.GetEntity(EntityIndex));
		}
	});
}
