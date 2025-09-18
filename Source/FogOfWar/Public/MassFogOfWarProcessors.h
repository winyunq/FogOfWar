// Copyright Winyunq, 2025. All Rights Reserved.

#pragma once

#include "MassExecutionContext.h" // Required for FMassExecutionContext
#include "MassProcessor.h"
#include "MassFogOfWarProcessors.generated.h"

class AFogOfWar;

/**
 * @struct FFogOfWarMassHelpers
 * @brief A struct containing static helper functions for Fog of War processors.
 */
struct FOGOFWAR_API FFogOfWarMassHelpers
{
	static void ProcessEntityChunk(FMassExecutionContext& Context, AFogOfWar* FogOfWar);
};

/**
 * @brief Calculates initial vision for newly created entities.
 */
UCLASS()
class FOGOFWAR_API UInitialVisionProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UInitialVisionProcessor();

protected:
	virtual void Initialize(UObject& Owner) override;
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	TObjectPtr<AFogOfWar> FogOfWarActor;
	FMassEntityQuery EntityQuery;
};

/**
 * @brief Updates vision for entities that have moved.
 * @details This processor only runs on entities tagged with FMassLocationChangedTag.
 */
UCLASS()
class FOGOFWAR_API UVisionProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UVisionProcessor();

protected:
	virtual void Initialize(UObject& Owner) override;
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	TObjectPtr<AFogOfWar> FogOfWarActor;
	FMassEntityQuery EntityQuery;
};
