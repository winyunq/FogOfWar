// Copyright Winyunq, 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * Exact-package-path resource resolver shared by the map-region exporter and
 * runtime minimap. A level's short name is never a configuration key.
 */
namespace MassBattleMapProfilePaths
{
	FOGOFWAR_API FString GetCanonicalMapPackagePath(const UWorld* World);
	FOGOFWAR_API FString GetConfigDirectory(const UWorld* World);
	FOGOFWAR_API FString GetMapRegionIniPath(const UWorld* World);
	FOGOFWAR_API FString GetMinimapColorsIniPath(const UWorld* World);
	FOGOFWAR_API FString GetMinimapBackgroundIniPath(const UWorld* World);
}
