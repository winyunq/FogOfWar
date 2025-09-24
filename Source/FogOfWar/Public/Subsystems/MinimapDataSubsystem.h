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

	/** The maximum icon size of any unit in this tile. */
	UPROPERTY()
	float MaxIconSize = 0.0f;
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

	/** The size of the grid in world units. Copied from AFogOfWar on initialization. */
	UPROPERTY(Transient)
	FVector2D GridSize = FVector2D::Zero();

	/** The bottom-left corner of the grid in world space. Copied from AFogOfWar on initialization. */
	UPROPERTY(Transient)
	FVector2D GridBottomLeftWorldLocation = FVector2D::Zero();

	/** The calculated size of a single minimap tile in world units. */
	UPROPERTY(Transient)
	FVector2D MinimapTileSize = FVector2D::Zero();

	/** The core data store for the minimap, representing a grid of tiles. */
	TArray<FMinimapTile> MinimapTiles;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Initializes the entire subsystem from the widget. This is the main entry point for setup. */
	void InitializeFromWidget(class AFogOfWar* InFogOfWarActor, const FIntPoint& NewResolution);

	/**
	 * @brief Converts a world location to the corresponding minimap tile coordinate.
	 * @param WorldLocation The 2D world location to convert.
	 * @return The integer coordinate (X, Y) of the tile on the minimap grid.
	 */
	FORCEINLINE FIntPoint ConvertWorldLocationToMinimapTileIJ(const FVector2D& WorldLocation)
	{
		const FVector2f GridSpaceLocation = ConvertWorldSpaceLocationToMinimapGridSpace(WorldLocation);
		return ConvertMinimapGridLocationToTileIJ(GridSpaceLocation);
	}

	/**
	 * @brief Converts a minimap tile IJ coordinate to its corresponding world space 2D location (center of the tile).
	 */
	FORCEINLINE FVector2D ConvertMinimapTileIJToWorldLocation(const FIntPoint& TileIJ) const
	{
		return FVector2D(
			GridBottomLeftWorldLocation.X + MinimapTileSize.X * TileIJ.X + MinimapTileSize.X / 2.0f,
			GridBottomLeftWorldLocation.Y + MinimapTileSize.Y * TileIJ.Y + MinimapTileSize.Y / 2.0f
		);
	}

private:
	/**
	 * @brief Converts a world space 2D location to a grid space 2D float coordinate.
	 */
	FORCEINLINE FVector2f ConvertWorldSpaceLocationToMinimapGridSpace(const FVector2D& WorldLocation)
	{
		// Ensure MinimapTileSize is not zero to avoid division by zero.
		if (MinimapTileSize.X == 0.0f || MinimapTileSize.Y == 0.0f)
		{
			return FVector2f(-1.f, -1.f);
		}
		return FVector2f(
			static_cast<float>((WorldLocation.X - GridBottomLeftWorldLocation.X) / MinimapTileSize.X),
			static_cast<float>((WorldLocation.Y - GridBottomLeftWorldLocation.Y) / MinimapTileSize.Y)
		);
	}

	/**
	 * @brief Converts a grid space 2D float coordinate to its corresponding tile IJ coordinate.
	 */
	FORCEINLINE FIntPoint ConvertMinimapGridLocationToTileIJ(const FVector2f& GridLocation)
	{
		return FIntPoint(
			FMath::FloorToInt(GridLocation.X),
			FMath::FloorToInt(GridLocation.Y)
		);
	}
};
