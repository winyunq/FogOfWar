// Copyright Winyunq, 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassSubsystemBase.h"
#include "MinimapDataSubsystem.generated.h"

/**
 * Represents a single tile on the minimap grid.
 */
USTRUCT()
struct FOGOFWAR_API FMinimapTile
{
	GENERATED_BODY()

	/** How many units are currently in this tile. */
	UPROPERTY()
	int32 UnitCount = 0;

	/** Color of the last unit that entered this tile. */
	UPROPERTY()
	FLinearColor Color = FLinearColor::Black;

	/** The maximum sight radius of any unit that has been in this tile. */
	UPROPERTY()
	float MaxSightRadius = 0.0f;
};

/**
 * UMinimapDataSubsystem
 * 
 * A global subsystem that holds the grid data for the minimap.
 * This data is written to by Mass processors and read by the UMinimapWidget.
 */
UCLASS()
class FOGOFWAR_API UMinimapDataSubsystem : public UMassSubsystemBase
{
	GENERATED_BODY()

public:
    /** The resolution of the minimap grid. */
    UPROPERTY(Transient)
    FIntPoint GridResolution = FIntPoint(256, 256);

	/** The core data store for the minimap, representing a grid of tiles. */
	TArray<FMinimapTile> MinimapTiles;

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
};