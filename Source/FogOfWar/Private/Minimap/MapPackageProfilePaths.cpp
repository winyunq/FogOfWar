// Copyright Winyunq, 2026. All Rights Reserved.

#include "Minimap/MapPackageProfilePaths.h"

#include "Engine/World.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

namespace MassBattleMapProfilePaths
{
	FString GetCanonicalMapPackagePath(const UWorld* World)
	{
		if (!World)
		{
			return TEXT("/Default");
		}

		FString MapName = World->GetName();
		MapName.RemoveFromStart(World->StreamingLevelsPrefix);
		const FString PackageDirectory = FPackageName::GetLongPackagePath(
			World->GetPackage()->GetName());
		return PackageDirectory.IsEmpty() ? TEXT("/") + MapName : PackageDirectory / MapName;
	}

	FString GetConfigDirectory(const UWorld* World)
	{
		FString RelativePackagePath = GetCanonicalMapPackagePath(World);
		RelativePackagePath.RemoveFromStart(TEXT("/"));
		return FPaths::ProjectConfigDir() / TEXT("MapRegion") / RelativePackagePath;
	}

	FString GetMapRegionIniPath(const UWorld* World)
	{
		return GetConfigDirectory(World) / TEXT("MapRegion.ini");
	}

	FString GetMinimapColorsIniPath(const UWorld* World)
	{
		return GetConfigDirectory(World) / TEXT("MinimapColors.ini");
	}

	FString GetMinimapBackgroundIniPath(const UWorld* World)
	{
		return GetConfigDirectory(World) / TEXT("MinimapBackground.ini");
	}
}
