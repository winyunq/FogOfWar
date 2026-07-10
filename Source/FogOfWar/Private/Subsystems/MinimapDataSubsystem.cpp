// Copyright Winyunq, 2025. All Rights Reserved.

#include "Subsystems/MinimapDataSubsystem.h"

#include "Engine/World.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

namespace
{
	constexpr float DefaultMapRegionSizeUU = 65536.0f;
	constexpr float DefaultVisionTileSize = 100.0f;
	const TCHAR* MinimapDataMapRegionSection = TEXT("MapRegion");

	FString GetMinimapDataCleanMapName(const UWorld* World)
	{
		if (!World)
		{
			return TEXT("Default");
		}

		FString MapName = World->GetMapName();
		MapName.RemoveFromStart(World->StreamingLevelsPrefix);
		return MapName.IsEmpty() ? FString(TEXT("Default")) : MapName;
	}

	FString GetMinimapDataMapRegionIniPath(const UWorld* World)
	{
		return FPaths::ProjectConfigDir() / TEXT("MapRegion") / GetMinimapDataCleanMapName(World) / TEXT("MapRegion.ini");
	}

	void ReadMapRegionIni(const UWorld* World, FVector2D& OutGridOrigin, FVector2D& OutGridSize)
	{
		OutGridOrigin = FVector2D(-DefaultMapRegionSizeUU * 0.5f, -DefaultMapRegionSizeUU * 0.5f);
		OutGridSize = FVector2D(DefaultMapRegionSizeUU, DefaultMapRegionSizeUU);

		FConfigFile IniFile;
		IniFile.Read(GetMinimapDataMapRegionIniPath(World));

		float OriginX = OutGridOrigin.X;
		float OriginY = OutGridOrigin.Y;
		float SizeX = OutGridSize.X;
		float SizeY = OutGridSize.Y;
		IniFile.GetFloat(MinimapDataMapRegionSection, TEXT("OriginX"), OriginX);
		IniFile.GetFloat(MinimapDataMapRegionSection, TEXT("OriginY"), OriginY);
		IniFile.GetFloat(MinimapDataMapRegionSection, TEXT("SizeX"), SizeX);
		IniFile.GetFloat(MinimapDataMapRegionSection, TEXT("SizeY"), SizeY);

		if (SizeX > 0.0f && SizeY > 0.0f)
		{
			OutGridOrigin = FVector2D(OriginX, OriginY);
			OutGridSize = FVector2D(SizeX, SizeY);
		}
	}
}

UMinimapDataSubsystem* UMinimapDataSubsystem::SingletonInstance = nullptr;

void UMinimapDataSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	SingletonInstance = this;
}

void UMinimapDataSubsystem::Deinitialize()
{
	if (SingletonInstance == this)
	{
		SingletonInstance = nullptr;
	}
	Super::Deinitialize();
}

void UMinimapDataSubsystem::SyncFogOfWarRuntimeOptions(
	float InVisionBlockingDeltaHeightThreshold,
	float InVisionUpdateWorldDistanceThreshold,
	bool bInDebugStressTestIgnoreCache)
{
	VisionBlockingDeltaHeightThreshold = InVisionBlockingDeltaHeightThreshold;
	VisionUpdateWorldDistanceThreshold = InVisionUpdateWorldDistanceThreshold;
	bDebugStressTestIgnoreCache = bInDebugStressTestIgnoreCache;
}

void UMinimapDataSubsystem::SyncVisionGridParameters(
	const FVector2D&,
	const FVector2D&,
	float InVisionTileSize,
	const FIntPoint& InVisionResolution)
{
	bVisionGridActive = false;
	ReadMapRegionIni(GetWorld(), GridBottomLeftWorldLocation, GridSize);

	VisionTileSize = InVisionTileSize > 0.0f ? InVisionTileSize : DefaultVisionTileSize;
	VisionGridResolution = InVisionResolution;
	if (VisionGridResolution.X <= 0)
	{
		VisionGridResolution.X = FMath::Max(1, FMath::CeilToInt32(GridSize.X / VisionTileSize));
	}
	if (VisionGridResolution.Y <= 0)
	{
		VisionGridResolution.Y = FMath::Max(1, FMath::CeilToInt32(GridSize.Y / VisionTileSize));
	}

	const int64 NumVisionTiles64 = static_cast<int64>(VisionGridResolution.X) * VisionGridResolution.Y;
	if (GridSize.X > 0.0f && GridSize.Y > 0.0f && NumVisionTiles64 > 0 && NumVisionTiles64 <= MAX_int32)
	{
		VisionTiles.SetNum(static_cast<int32>(NumVisionTiles64));
		for (FTile& Tile : VisionTiles)
		{
			Tile = FTile();
		}
	}
	else
	{
		VisionTiles.Reset();
	}
}

void UMinimapDataSubsystem::SetVisionGridActive(bool bInActive)
{
	bVisionGridActive = bInActive && IsVisionGridReady();
}

bool UMinimapDataSubsystem::IsVisionGridReady() const
{
	return VisionTileSize > 0.0f &&
		VisionGridResolution.X > 0 &&
		VisionGridResolution.Y > 0 &&
		VisionTiles.Num() == VisionGridResolution.X * VisionGridResolution.Y;
}

bool UMinimapDataSubsystem::IsLocationVisible(const FVector& WorldLocation) const
{
	const FIntPoint TileIJ = ConvertWorldLocationToVisionTileIJ_Static(FVector2D(WorldLocation));
	return IsVisionGridIJValid_Static(TileIJ) && GetVisionTile(TileIJ).VisibilityCounter > 0;
}

FTile& UMinimapDataSubsystem::GetVisionTile(int32 GlobalIndex)
{
	return VisionTiles[GlobalIndex];
}

const FTile& UMinimapDataSubsystem::GetVisionTile(int32 GlobalIndex) const
{
	return VisionTiles[GlobalIndex];
}

FTile& UMinimapDataSubsystem::GetVisionTile(FIntPoint IJ)
{
	checkSlow(IsVisionGridIJValid_Static(IJ));
	return GetVisionTile(GetVisionGridGlobalIndex_Static(IJ));
}

const FTile& UMinimapDataSubsystem::GetVisionTile(FIntPoint IJ) const
{
	checkSlow(IsVisionGridIJValid_Static(IJ));
	return GetVisionTile(GetVisionGridGlobalIndex_Static(IJ));
}

bool UMinimapDataSubsystem::IsBlockingVision(float ObserverHeight, float PotentialObstacleHeight) const
{
	return PotentialObstacleHeight - ObserverHeight > VisionBlockingDeltaHeightThreshold;
}
