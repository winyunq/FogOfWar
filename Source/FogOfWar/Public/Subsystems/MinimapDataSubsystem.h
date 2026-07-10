// Copyright Winyunq, 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Mass/ExternalSubsystemTraits.h"
#include "MassSubsystemBase.h"
#include "MinimapDataSubsystem.generated.h"

/** One tile in the legacy gameplay/scene visibility grid. */
USTRUCT()
struct FOGOFWAR_API FTile
{
	GENERATED_BODY()

	UPROPERTY()
	float Height = 0.0f;

	UPROPERTY()
	int32 VisibilityCounter = 0;
};

/**
 * Storage for the legacy gameplay visibility grid.
 *
 * The class name is retained for serialized compatibility. The Mass Battle
 * minimap does not read this subsystem; it reads renderer batches directly.
 */
UCLASS(Config = MassBattle, defaultconfig)
class FOGOFWAR_API UMinimapDataSubsystem : public UMassSubsystemBase
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	static FORCEINLINE UMinimapDataSubsystem* Get() { return SingletonInstance; }

	void SyncFogOfWarRuntimeOptions(
		float InVisionBlockingDeltaHeightThreshold,
		float InVisionUpdateWorldDistanceThreshold,
		bool bInDebugStressTestIgnoreCache);

	UFUNCTION(BlueprintCallable, Category = "FogOfWar|Vision")
	void SyncVisionGridParameters(
		const FVector2D& InGridOrigin,
		const FVector2D& InGridSize,
		float InVisionTileSize,
		const FIntPoint& InVisionResolution);

	void SetVisionGridActive(bool bInActive);
	bool IsVisionGridReady() const;
	bool IsLocationVisible(const FVector& WorldLocation) const;
	FTile& GetVisionTile(int32 GlobalIndex);
	const FTile& GetVisionTile(int32 GlobalIndex) const;
	FTile& GetVisionTile(FIntPoint IJ);
	const FTile& GetVisionTile(FIntPoint IJ) const;
	bool IsBlockingVision(float ObserverHeight, float PotentialObstacleHeight) const;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|Vision")
	FVector2D GridSize = FVector2D::ZeroVector;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|Vision")
	FVector2D GridBottomLeftWorldLocation = FVector2D::ZeroVector;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|Vision")
	float VisionTileSize = 100.0f;

	UPROPERTY(Transient)
	FIntPoint VisionGridResolution = FIntPoint::ZeroValue;

	UPROPERTY(Transient)
	bool bVisionGridActive = false;

	UPROPERTY(Transient)
	float VisionBlockingDeltaHeightThreshold = 200.0f;

	UPROPERTY(Transient)
	float VisionUpdateWorldDistanceThreshold = 0.0f;

	UPROPERTY(Transient)
	bool bDebugStressTestIgnoreCache = false;

	TArray<FTile> VisionTiles;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|MassBattle")
	bool bAutoBindMassBattleAgents = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|MassBattle", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DefaultMassBattleSightRadius = 1024.0f;

	static FORCEINLINE FVector2f ConvertWorldSpaceLocationToVisionGridSpace_Static(const FVector2D& WorldLocation);
	static FORCEINLINE FIntPoint ConvertVisionGridLocationToTileIJ_Static(const FVector2f& GridLocation);
	static FORCEINLINE FIntPoint ConvertWorldLocationToVisionTileIJ_Static(const FVector2D& WorldLocation);
	static FORCEINLINE FVector2D ConvertVisionTileIJToTileCenterWorldLocation_Static(const FIntPoint& IJ);
	static FORCEINLINE int32 GetVisionGridGlobalIndex_Static(FIntPoint IJ);
	static FORCEINLINE FIntPoint GetVisionGridTileIJ_Static(int32 GlobalIndex);
	static FORCEINLINE bool IsVisionGridIJValid_Static(FIntPoint IJ);

private:
	static UMinimapDataSubsystem* SingletonInstance;
};

template<>
struct TMassExternalSubsystemTraits<UMinimapDataSubsystem> final
{
	enum
	{
		GameThreadOnly = false
	};
};

FORCEINLINE FVector2f UMinimapDataSubsystem::ConvertWorldSpaceLocationToVisionGridSpace_Static(const FVector2D& WorldLocation)
{
	check(SingletonInstance);
	return FVector2f((WorldLocation - SingletonInstance->GridBottomLeftWorldLocation) / SingletonInstance->VisionTileSize);
}

FORCEINLINE FIntPoint UMinimapDataSubsystem::ConvertVisionGridLocationToTileIJ_Static(const FVector2f& GridLocation)
{
	return FIntPoint(FMath::FloorToInt(GridLocation.X), FMath::FloorToInt(GridLocation.Y));
}

FORCEINLINE FIntPoint UMinimapDataSubsystem::ConvertWorldLocationToVisionTileIJ_Static(const FVector2D& WorldLocation)
{
	check(SingletonInstance);
	return ConvertVisionGridLocationToTileIJ_Static(
		FVector2f((WorldLocation - SingletonInstance->GridBottomLeftWorldLocation) / SingletonInstance->VisionTileSize));
}

FORCEINLINE FVector2D UMinimapDataSubsystem::ConvertVisionTileIJToTileCenterWorldLocation_Static(const FIntPoint& IJ)
{
	check(SingletonInstance);
	return SingletonInstance->GridBottomLeftWorldLocation + (FVector2D(IJ) + 0.5f) * SingletonInstance->VisionTileSize;
}

FORCEINLINE int32 UMinimapDataSubsystem::GetVisionGridGlobalIndex_Static(FIntPoint IJ)
{
	check(SingletonInstance);
	return IJ.X * SingletonInstance->VisionGridResolution.Y + IJ.Y;
}

FORCEINLINE FIntPoint UMinimapDataSubsystem::GetVisionGridTileIJ_Static(int32 GlobalIndex)
{
	check(SingletonInstance);
	return { GlobalIndex / SingletonInstance->VisionGridResolution.Y, GlobalIndex % SingletonInstance->VisionGridResolution.Y };
}

FORCEINLINE bool UMinimapDataSubsystem::IsVisionGridIJValid_Static(FIntPoint IJ)
{
	check(SingletonInstance);
	return IJ.X >= 0 && IJ.Y >= 0 &&
		IJ.X < SingletonInstance->VisionGridResolution.X &&
		IJ.Y < SingletonInstance->VisionGridResolution.Y;
}
