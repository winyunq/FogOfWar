#pragma once

#include "CoreMinimal.h"

class UWorld;

struct FOGOFWAR_API FFogOfWarMapBoundsConfig
{
	FVector2D GridOrigin = FVector2D::ZeroVector;
	FVector2D GridSize = FVector2D::ZeroVector;
	float MapOverflowUU = 0.0f;
	FIntPoint MinimapGridResolution = FIntPoint::ZeroValue;
	FVector2D HashGridCellSize = FVector2D::ZeroVector;
	FIntPoint HashGridResolution = FIntPoint::ZeroValue;

	bool IsValid() const;

	static FString GetConfigFilePath();
	static FString GetSectionName(const UWorld* World);
	static bool LoadForWorld(const UWorld* World, FFogOfWarMapBoundsConfig& OutConfig);
	static bool SaveForWorld(const UWorld* World, const FFogOfWarMapBoundsConfig& Config);
};
