// Copyright Winyunq, 2025. All Rights Reserved.

#include "MinimapCellObserver.h"
#include "Subsystems/MinimapDataSubsystem.h"
#include "MassCommonFragments.h"
#include "MassFogOfWarFragments.h"
#include "MassExecutionContext.h"

UMinimapCellObserver::UMinimapCellObserver()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
}

void UMinimapCellObserver::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassPreviousMinimapCellFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassMinimapRepresentationFragment>(EMassFragmentAccess::ReadOnly);
}

void UMinimapCellObserver::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	if (!MinimapDataSubsystem)
	{
		MinimapDataSubsystem = GetWorld()->GetSubsystem<UMinimapDataSubsystem>();
	}
	if (!MinimapDataSubsystem)
	{
		return;
	}

	EntityQuery.ForEachEntityChunk(Context, [this](FMassExecutionContext& Context)
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
