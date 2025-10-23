// Copyright Winyunq, 2025. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "MinimapCellObserver.generated.h"

/**
 * Observes entities with minimap representation and tags them with FMinimapCellChangedTag
 * if they have moved to a new minimap grid cell.
 */
UCLASS()
class FOGOFWAR_API UMinimapCellObserver : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMinimapCellObserver();

protected:
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
	class UMinimapDataSubsystem* MinimapDataSubsystem;
};