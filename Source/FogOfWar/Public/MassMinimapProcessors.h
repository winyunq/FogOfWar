// Copyright Winyunq, 2025. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "MassMinimapProcessors.generated.h"

class AFogOfWar;
class UMinimapDataSubsystem;

/**
 * @class UMinimapAddProcessor
 * @brief Observes when a minimap representation is added to an entity, incrementing the unit count on the corresponding tile.
 */
UCLASS()
class FOGOFWAR_API UMinimapAddProcessor : public UMassObserverProcessor
{
	GENERATED_BODY()

public:
	UMinimapAddProcessor();

protected:
	virtual void Initialize(UObject& Owner) override;
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	TObjectPtr<AFogOfWar> FogOfWarActor;
	TObjectPtr<UMinimapDataSubsystem> MinimapDataSubsystem;
	FMassEntityQuery EntityQuery;
};

/**
 * @class UMinimapRemoveProcessor
 * @brief Observes when a minimap representation is removed from an entity, decrementing the unit count on the corresponding tile.
 */
UCLASS()
class FOGOFWAR_API UMinimapRemoveProcessor : public UMassObserverProcessor
{
	GENERATED_BODY()

public:
	UMinimapRemoveProcessor();

protected:
	virtual void Initialize(UObject& Owner) override;
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	TObjectPtr<UMinimapDataSubsystem> MinimapDataSubsystem;
	FMassEntityQuery EntityQuery;
};

/**
 * @class UMinimapUpdateProcessor
 * @brief Observes when an entity's transform changes, updating the tile grid by decrementing the old tile and incrementing the new tile.
 */
UCLASS()
class FOGOFWAR_API UMinimapUpdateProcessor : public UMassObserverProcessor
{
	GENERATED_BODY()

public:
	UMinimapUpdateProcessor();

protected:
	virtual void Initialize(UObject& Owner) override;
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	TObjectPtr<AFogOfWar> FogOfWarActor;
	TObjectPtr<UMinimapDataSubsystem> MinimapDataSubsystem;
	FMassEntityQuery EntityQuery;
};
