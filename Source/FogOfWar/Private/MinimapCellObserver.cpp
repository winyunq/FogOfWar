// Copyright Winyunq, 2025. All Rights Reserved.

#include "MinimapCellObserver.h"
#include "FogOfWarMassBinding.h"
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
	EntityQuery.AddRequirement<FOW_LOCATION_FRAGMENT>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassPreviousMinimapCellFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddSubsystemRequirement<UMinimapDataSubsystem>(EMassFragmentAccess::ReadOnly);
	ProcessorRequirements.AddSubsystemRequirement<UMinimapDataSubsystem>(EMassFragmentAccess::ReadOnly);
}

void UMinimapCellObserver::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	const UMinimapDataSubsystem* MinimapSubsystem = Context.GetSubsystem<UMinimapDataSubsystem>();
	if (!MinimapSubsystem || !MinimapSubsystem->IsMinimapGridReady())
	{
		return;
	}

	EntityQuery.ForEachEntityChunk(Context, [this](FMassExecutionContext& Context)
	{
		const TConstArrayView<FOW_LOCATION_FRAGMENT> LocationList = Context.GetFragmentView<FOW_LOCATION_FRAGMENT>();
		const TConstArrayView<FMassPreviousMinimapCellFragment> PrevCellList = Context.GetFragmentView<FMassPreviousMinimapCellFragment>();

		for (int32 i = 0; i < Context.GetNumEntities(); ++i)
		{
			const FVector WorldLocation = FOW_GET_LOCATION(LocationList[i]);
			const FIntPoint& PrevCellCoords = PrevCellList[i].PrevCellCoords;

			const FIntPoint CurrentMinimapTileIJ = UMinimapDataSubsystem::ConvertWorldLocationToMinimapTileIJ_Static(FVector2D(WorldLocation));

			if (CurrentMinimapTileIJ != PrevCellCoords)
			{
				Context.Defer().AddTag<FMinimapCellChangedTag>(Context.GetEntity(i));
			}
		}
	});
}
