// Copyright Winyunq, 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassSubsystemBase.h"
#include "MinimapDataSubsystem.generated.h"

class AFogOfWar;

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
 * A global subsystem that holds and manages grid data and coordinate conversions for both the low-resolution Minimap and the high-resolution Vision grid.
 * This is the single source of truth for all grid calculations. It relies on the AFogOfWar actor to register its parameters upon activation.
 */
UCLASS()
class FOGOFWAR_API UMinimapDataSubsystem : public UMassSubsystemBase
{
	GENERATED_BODY()

public:
	//~ Begin UMassSubsystemBase Interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End UMassSubsystemBase Interface

	/** Provides direct, static access to the singleton instance of the subsystem for high-performance code paths. */
	static FORCEINLINE UMinimapDataSubsystem* Get() { return SingletonInstance; }

	/** Called by AFogOfWar when it has activated and its grid parameters are ready. */
	void UpdateVisionGridParameters(AFogOfWar* InFogOfWarActor);

	/** Called by the Minimap UI Widget to set its desired resolution. */
	void SetMinimapResolution(const FIntPoint& NewResolution);

public:
	/** Flag to indicate if the subsystem has received valid grid parameters and is ready for use. */
	UPROPERTY(BlueprintReadOnly, Category="Minimap")
	bool bIsInitialized = false;
	
	//~ Begin Common Grid Properties (Shared by Vision and Minimap)
	UPROPERTY(Transient)
	FVector2D GridSize = FVector2D::Zero();

	UPROPERTY(Transient)
	FVector2D GridBottomLeftWorldLocation = FVector2D::Zero();
	//~ End Common Grid Properties

	//~ Begin Vision Grid Properties (High-Resolution for Fog of War calculation)
	UPROPERTY(Transient)
	float Vision_TileSize = 0.0f;

	UPROPERTY(Transient)
	FIntPoint Vision_GridResolution = FIntPoint::ZeroValue;
	//~ End Vision Grid Properties

	//~ Begin Minimap Grid Properties (Low-Resolution for UI)
    UPROPERTY(Transient)
    FIntPoint Minimap_GridResolution = FIntPoint(256, 256);

	UPROPERTY(Transient)
	FVector2D Minimap_TileSize = FVector2D::Zero();

	TArray<FMinimapTile> MinimapTiles;
	//~ End Minimap Grid Properties

public:
	//~ Begin Static Vision Grid Conversion Functions
	static FORCEINLINE FVector2f ConvertWorldSpaceLocationToVisionGridSpace_Static(const FVector2D& WorldLocation);
	static FORCEINLINE FIntPoint ConvertVisionGridLocationToTileIJ_Static(const FVector2f& GridLocation);
	static FORCEINLINE FIntPoint ConvertWorldLocationToVisionTileIJ_Static(const FVector2D& WorldLocation);
	static FORCEINLINE FVector2D ConvertVisionTileIJToTileCenterWorldLocation_Static(const FIntPoint& IJ);
	static FORCEINLINE int32 GetVisionGridGlobalIndex_Static(FIntPoint IJ);
	static FORCEINLINE FIntPoint GetVisionGridTileIJ_Static(int32 GlobalIndex);
	static FORCEINLINE bool IsVisionGridIJValid_Static(FIntPoint IJ);
	//~ End Static Vision Grid Conversion Functions

	//~ Begin Static Minimap Grid Conversion Functions
	static FORCEINLINE FIntPoint ConvertWorldLocationToMinimapTileIJ_Static(const FVector2D& WorldLocation);
	static FORCEINLINE FVector2D ConvertMinimapTileIJToWorldLocation_Static(const FIntPoint& TileIJ);
	//~ End Static Minimap Grid Conversion Functions

private:
	// Private helpers for minimap conversions
	static FORCEINLINE FVector2f ConvertWorldSpaceLocationToMinimapGridSpace_Static(const FVector2D& WorldLocation);
	static FORCEINLINE FIntPoint ConvertMinimapGridLocationToTileIJ_Static(const FVector2f& GridLocation);

private:
	/** Singleton instance, set on Initialize and cleared on Deinitialize. */
	static UMinimapDataSubsystem* SingletonInstance;
};

//~ Begin Inline Implementations of Static Functions

FORCEINLINE FVector2f UMinimapDataSubsystem::ConvertWorldSpaceLocationToVisionGridSpace_Static(const FVector2D& WorldLocation)
{
	check(SingletonInstance);
	return FVector2f((WorldLocation - SingletonInstance->GridBottomLeftWorldLocation) / SingletonInstance->Vision_TileSize);
}

FORCEINLINE FIntPoint UMinimapDataSubsystem::ConvertVisionGridLocationToTileIJ_Static(const FVector2f& GridLocation)
{
	return FIntPoint(FMath::FloorToInt(GridLocation.X), FMath::FloorToInt(GridLocation.Y));
}

FORCEINLINE FIntPoint UMinimapDataSubsystem::ConvertWorldLocationToVisionTileIJ_Static(const FVector2D& WorldLocation)
{
	check(SingletonInstance);
	const FVector2f GridLocation = FVector2f((WorldLocation - SingletonInstance->GridBottomLeftWorldLocation) / SingletonInstance->Vision_TileSize);
	return FIntPoint(FMath::FloorToInt(GridLocation.X), FMath::FloorToInt(GridLocation.Y));
}

FORCEINLINE FVector2D UMinimapDataSubsystem::ConvertVisionTileIJToTileCenterWorldLocation_Static(const FIntPoint& IJ)
{
	check(SingletonInstance);
	return SingletonInstance->GridBottomLeftWorldLocation + (FVector2D(IJ) + 0.5f) * SingletonInstance->Vision_TileSize;
}

FORCEINLINE int32 UMinimapDataSubsystem::GetVisionGridGlobalIndex_Static(FIntPoint IJ)
{
	check(SingletonInstance);
	return IJ.X * SingletonInstance->Vision_GridResolution.Y + IJ.Y;
}

FORCEINLINE FIntPoint UMinimapDataSubsystem::GetVisionGridTileIJ_Static(int32 GlobalIndex)
{
	check(SingletonInstance);
	return { GlobalIndex / SingletonInstance->Vision_GridResolution.Y, GlobalIndex % SingletonInstance->Vision_GridResolution.Y };
}

FORCEINLINE bool UMinimapDataSubsystem::IsVisionGridIJValid_Static(FIntPoint IJ)
{
	check(SingletonInstance);
	return IJ.X >= 0 && IJ.Y >= 0 && IJ.X < SingletonInstance->Vision_GridResolution.X && IJ.Y < SingletonInstance->Vision_GridResolution.Y;
}

FORCEINLINE FIntPoint UMinimapDataSubsystem::ConvertWorldLocationToMinimapTileIJ_Static(const FVector2D& WorldLocation)
{
	check(SingletonInstance);
	const FVector2f GridLocation = ConvertWorldSpaceLocationToMinimapGridSpace_Static(WorldLocation);
	return ConvertMinimapGridLocationToTileIJ_Static(GridLocation);
}

FORCEINLINE FVector2D UMinimapDataSubsystem::ConvertMinimapTileIJToWorldLocation_Static(const FIntPoint& TileIJ)
{
	check(SingletonInstance);
	return SingletonInstance->GridBottomLeftWorldLocation + (FVector2D(TileIJ) + 0.5f) * SingletonInstance->Minimap_TileSize;
}

FORCEINLINE FVector2f UMinimapDataSubsystem::ConvertWorldSpaceLocationToMinimapGridSpace_Static(const FVector2D& WorldLocation)
{
	check(SingletonInstance);
	return FVector2f((WorldLocation - SingletonInstance->GridBottomLeftWorldLocation) / SingletonInstance->Minimap_TileSize);
}

FORCEINLINE FIntPoint UMinimapDataSubsystem::ConvertMinimapGridLocationToTileIJ_Static(const FVector2f& GridLocation)
{
	return FIntPoint(FMath::FloorToInt(GridLocation.X), FMath::FloorToInt(GridLocation.Y));
}
