// Copyright Winyunq, 2025. All Rights Reserved.

#include "MinimapCellObserver.h"
#include "Subsystems/MinimapDataSubsystem.h"
#include "MassCommonFragments.h"
#include "MassFogOfWarFragments.h"
#include "MassExecutionContext.h"

UMinimapCellObserver::UMinimapCellObserver()
	: EntityQuery(*this)
{
	// This processor now runs every frame on its queried entities.
	Operation = EMassObservedOperation::MAX;
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
}

void UMinimapCellObserver::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);
	MinimapDataSubsystem = GetWorld()->GetSubsystem<UMinimapDataSubsystem>();
}

void UMinimapCellObserver::ConfigureQueries()
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassPreviousMinimapCellFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassMinimapRepresentationFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.RegisterWithProcessor(*this);
}

void UMinimapCellObserver::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	if (!MinimapDataSubsystem)
	{
		return;
	}

	EntityQuery.ForEachEntityChunk(EntityManager, Context, [this](FMassExecutionContext& Context)
	{
		const TConstArrayView<FTransformFragment> LocationList = Context.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FMassPreviousMinimapCellFragment> PrevCellList = Context.GetFragmentView<FMassPreviousMinimapCellFragment>();

		for (int32 i = 0; i < Context.GetNumEntities(); ++i)
		{
			const FVector& WorldLocation = LocationList[i].GetTransform().GetLocation();
			const FIntPoint& PrevCellCoords = PrevCellList[i].PrevCellCoords;

			const FIntPoint CurrentMinimapTileIJ = MinimapDataSubsystem->ConvertWorldLocationToMinimapTileIJ(FVector2D(WorldLocation));

			if (CurrentMinimapTileIJ != PrevCellCoords)
			{
				Context.Defer().AddTag<FMinimapCellChangedTag>(Context.GetEntity(i));
			}
		}
	});
}
