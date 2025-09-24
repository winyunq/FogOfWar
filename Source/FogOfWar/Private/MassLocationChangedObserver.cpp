// Copyright Winyunq, 2025. All Rights Reserved.

#include "MassLocationChangedObserver.h"
#include "MassCommonFragments.h"
#include "MassFogOfWarFragments.h"
#include "MassExecutionContext.h"

UMassLocationChangedObserver::UMassLocationChangedObserver()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
}

void UMassLocationChangedObserver::ConfigureQueries()
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	// We only want to add the tag to entities that are actually vision providers.
	EntityQuery.AddTagRequirement<FMassVisionEntityTag>(EMassFragmentPresence::All);
}

void UMassLocationChangedObserver::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntityManager, Context, [this](FMassExecutionContext& Context)
	{
		const TArrayView<const FMassEntityHandle> Entities = Context.GetEntities();
		for (const FMassEntityHandle& Entity : Entities)
		{
			Context.Defer().AddTag<FMassLocationChangedTag>(Entity);
		}
	});
}
