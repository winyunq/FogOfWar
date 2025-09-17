// Copyright Winyunq, 2025. All Rights Reserved.

#pragma once

#include "MassEntityTypes.h"
#include "MassCommonFragments.h"
#include "GeminiMassFogOfWarFragments.generated.h"

/**
 * Tag to identify entities that can be revealed by vision.
 */
USTRUCT()
struct FOGOFWAR_API FGeminiMassVisibleEntityTag : public FMassTag
{
	GENERATED_BODY()
};

/**
 * Tag to identify entities that provide vision.
 */
USTRUCT()
struct FOGOFWAR_API FGeminiMassVisionEntityTag : public FMassTag
{
	GENERATED_BODY()
};

/**
 * Tag for entities that do not move, allowing for significant optimization.
 */
USTRUCT()
struct FOGOFWAR_API FGeminiMassStationaryTag : public FMassTag
{
	GENERATED_BODY()
};

/**
 * Tag for entities that should always be visible on the minimap, regardless of fog.
 */
USTRUCT()
struct FOGOFWAR_API FGeminiMassMinimapVisibleTag : public FMassTag
{
	GENERATED_BODY()
};

/**
 * A temporary tag added to entities whose location has changed since the last frame.
 * This tag will be the primary driver for triggering recalculations.
 */
USTRUCT()
struct FOGOFWAR_API FGeminiMassLocationChangedTag : public FMassTag
{
	GENERATED_BODY()
};

/**
 * Fragment to store vision-related data for an entity.
 */
USTRUCT()
struct FOGOFWAR_API FGeminiMassVisionFragment : public FMassFragment
{
	GENERATED_BODY()

	// Radius of the vision circle.
	UPROPERTY(EditAnywhere, Category = "Fog of War", meta = (ClampMin = 0.0f, UIMin = 0.0f))
	float SightRadius = 1000.0f;
};
