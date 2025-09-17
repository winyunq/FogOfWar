// Copyright Winyunq, 2025. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "GeminiMassFogOfWarProcessors.generated.h"

/**
 * This processor detects entities that have moved and tags them for recalculation.
 */
UCLASS()
class FOGOFWAR_API UGeminiMovementDetectionProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UGeminiMovementDetectionProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};

/**
 * This processor updates the fog of war texture based on the vision of entities that have moved.
 */
UCLASS()
class FOGOFWAR_API UGeminiVisionProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UGeminiVisionProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};
