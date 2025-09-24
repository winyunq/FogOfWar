// Copyright Winyunq, 2025. All Rights Reserved.

#include "Subsystems/MinimapDataSubsystem.h"
#include "FogOfWar.h"
#include "Kismet/GameplayStatics.h"

void UMinimapDataSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    // The MinimapTiles array is now initialized in InitializeFromWidget,
    // once the widget provides all necessary data.
}

void UMinimapDataSubsystem::Deinitialize()
{
    MinimapTiles.Empty();
    Super::Deinitialize();
}

void UMinimapDataSubsystem::InitializeFromWidget(AFogOfWar* InFogOfWarActor, const FIntPoint& NewResolution)
{
    // --- Step 1: Validate all inputs ---
    if (!InFogOfWarActor)
    {
        return;
    }
    if (NewResolution.X <= 0 || NewResolution.Y <= 0)
    {
        return;
    }
    // Ensure the Fog of War actor itself is activated and has valid grid data.
    if (!InFogOfWarActor->IsActivated() || InFogOfWarActor->GridSize.IsZero())
    {
        // This can happen if called too early. The caller (e.g., MinimapWidget) should ensure AFogOfWar is ready.
        return;
    }

    // --- Step 2: Set Resolution and allocate array ---
    GridResolution = NewResolution;
    MinimapTiles.SetNum(GridResolution.X * GridResolution.Y);

    // --- Step 3: Set coordinate system properties ---
    GridSize = InFogOfWarActor->GridSize;
    GridBottomLeftWorldLocation = InFogOfWarActor->GridBottomLeftWorldLocation;

    // --- Step 4: Calculate final derived data (tile size) ---
    if (GridResolution.X > 0 && GridResolution.Y > 0)
    {
        MinimapTileSize.X = GridSize.X / GridResolution.X;
        MinimapTileSize.Y = GridSize.Y / GridResolution.Y;
    }
}