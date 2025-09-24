// Copyright Winyunq, 2025. All Rights Reserved.

#include "Subsystems/MinimapDataSubsystem.h"

void UMinimapDataSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    MinimapTiles.SetNum(GridResolution.X * GridResolution.Y);
}