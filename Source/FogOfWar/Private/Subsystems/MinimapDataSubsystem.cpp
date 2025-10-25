// Copyright Winyunq, 2025. All Rights Reserved.

#include "Subsystems/MinimapDataSubsystem.h"
#include "FogOfWar.h"

// Define the static singleton instance pointer.
UMinimapDataSubsystem* UMinimapDataSubsystem::SingletonInstance = nullptr;

void UMinimapDataSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	SingletonInstance = this;
	bIsInitialized = false;
}

void UMinimapDataSubsystem::Deinitialize()
{
	SingletonInstance = nullptr;
	Super::Deinitialize();
}

void UMinimapDataSubsystem::UpdateVisionGridParameters(AFogOfWar* InFogOfWarActor)
{
	if (!InFogOfWarActor || !InFogOfWarActor->IsActivated())
	{
		bIsInitialized = false;
		return;
	}

	// Copy common properties
	GridSize = InFogOfWarActor->GridSize;
	GridBottomLeftWorldLocation = InFogOfWarActor->GridBottomLeftWorldLocation;

	// Initialize Vision Grid properties
	Vision_TileSize = InFogOfWarActor->GetTileSize();
	Vision_GridResolution = InFogOfWarActor->GridResolution;

	// If the minimap resolution has already been set, we can complete initialization
	if (Minimap_GridResolution.X > 0 && Minimap_GridResolution.Y > 0 && GridSize.X > 0 && GridSize.Y > 0)
	{
		Minimap_TileSize = FVector2D(GridSize.X / Minimap_GridResolution.X, GridSize.Y / Minimap_GridResolution.Y);
	}

	// The system is ready once it has valid vision grid parameters
	if (Vision_GridResolution.X > 0 && Vision_TileSize > 0.0f)
	{
		bIsInitialized = true;
	}
}

void UMinimapDataSubsystem::SetMinimapResolution(const FIntPoint& NewResolution)
{
	Minimap_GridResolution = NewResolution;
	MinimapTiles.SetNum(Minimap_GridResolution.X * Minimap_GridResolution.Y);

	// If the main grid data is already available, calculate the minimap tile size now
	if (bIsInitialized && GridSize.X > 0 && GridSize.Y > 0)
	{
		Minimap_TileSize = FVector2D(GridSize.X / Minimap_GridResolution.X, GridSize.Y / Minimap_GridResolution.Y);
	}
}