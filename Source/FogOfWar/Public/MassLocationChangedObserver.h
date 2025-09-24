// Copyright Winyunq, 2025. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "MassLocationChangedObserver.generated.h"

/**
 * Observes changes in the FTransformFragment and adds a FMassLocationChangedTag to the entity.
 * This triggers the UVisionProcessor to recalculate vision for the moved entity.
 */
UCLASS()
class FOGOFWAR_API UMassLocationChangedObserver : public UMassObserverProcessor
{
	GENERATED_BODY()

public:
	UMassLocationChangedObserver();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};