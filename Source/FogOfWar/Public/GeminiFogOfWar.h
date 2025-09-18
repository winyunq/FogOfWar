// Copyright Winyunq, 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MassEntityTypes.h" // For Mass framework integration
#include "Components/PostProcessComponent.h" // For rendering
#include "Mass/GeminiMassFogOfWarFragments.h" // For Mass fragments and tags
#include "GeminiFogOfWar.generated.h"

class UBrushComponent; // Original FogOfWar.h uses AVolume* GridVolume, so UBrushComponent is needed for its bounds.
class UTexture2D;
class UTextureRenderTarget2D;
class AVolume; // For GridVolume

DECLARE_LOG_CATEGORY_EXTERN(LogGeminiFogOfWar, Log, All)

// Internal struct for a single tile in the grid
USTRUCT()
struct FOGOFWAR_API FTile
{
	GENERATED_BODY()

	UPROPERTY()
	float Height = 0.0f;

	UPROPERTY()
	int VisibilityCounter = 0;
};


UCLASS(BlueprintType, Blueprintable)
class FOGOFWAR_API AGeminiFogOfWar : public AActor
{
	GENERATED_BODY()

public:
	AGeminiFogOfWar();

public:
	UFUNCTION(BlueprintCallable)
	bool IsLocationVisible(FVector WorldLocation);

	UFUNCTION(BlueprintPure)
	UTexture* GetFinalVisibilityTexture();

	UFUNCTION(BlueprintCallable)
	void SetCommonMIDParameters(UMaterialInstanceDynamic* MID);

	UFUNCTION(BlueprintCallable)
	void Activate();

	UFUNCTION(BlueprintCallable, Category = "FogOfWar")
	bool IsActivated() const { return bActivated; }

	UFUNCTION(BlueprintCallable, Category = "FogOfWar")
	float GetTileSize() const { return TileSize; }

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TEnumAsByte<ECollisionChannel> HeightScanCollisionChannel = ECC_Camera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UPostProcessComponent> PostProcess;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bAutoActivate = true;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly)
	TObjectPtr<AVolume> GridVolume = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = 0.0f, UIMin = 0.0f))
	float TileSize = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = 0.0f, UIMin = 0.0f))
	float VisionBlockingDeltaHeightThreshold = 200.0f;

	// The more the value, the less the impact of the new snapshot on the "history" will be and the smoother the transition will be.
	UPROPERTY(EditAnywhere, meta = (ClampMin = 0.0f, UIMin = 0.0f))
	float ApproximateSecondsToAbsorbNewSnapshot = 0.1f;

	// All pixels with visibility less than this value will be zeroed out.
	UPROPERTY(EditAnywhere, meta = (ClampMin = 0.0f, UIMin = 0.0f, ClampMax = 1.0f, UIMax = 1.0f))
	float MinimalVisibility = 0.1f;

	UPROPERTY(EditAnywhere, meta = (ClampMin = 0.0f, UIMin = 0.0f, ClampMax = 1.0f, UIMax = 1.0f))
	float NotVisibleRegionBrightness = 0.1f;

	UPROPERTY(EditAnywhere, Category = "FogOfWar|Materials")
	TObjectPtr<UMaterialInterface> InterpolationMaterial;

	UPROPERTY(EditAnywhere, Category = "FogOfWar|Materials")
	TObjectPtr<UMaterialInterface> AfterInterpolationMaterial;

	UPROPERTY(EditAnywhere, Category = "FogOfWar|Materials")
	TObjectPtr<UMaterialInterface> SuperSamplingMaterial;

	UPROPERTY(EditAnywhere, Category = "FogOfWar|Materials")
	TObjectPtr<UMaterialInterface> PostProcessingMaterial;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = "FogOfWar|Debug")
	bool bDebugStressTestIgnoreCache = false;

	UPROPERTY(EditAnywhere, Category = "FogOfWar|Debug")
	bool bDebugFilterNearest = false;

	UPROPERTY(EditAnywhere, Category = "FogOfWar|Debug")
	float DebugHeightmapLowestZ = -1000.0f;

	UPROPERTY(EditAnywhere, Category = "FogOfWar|Debug")
	float DebugHeightmapHightestZ = 1000.0f;
#endif

protected:
	virtual void BeginPlay() override;

#if WITH_EDITOR
	UFUNCTION(CallInEditor, Category = "FogOfWar", DisplayName = "RefreshVolume")
	void RefreshVolumeInEditor();

	virtual bool CanEditChange(const FProperty* InProperty) const override;

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	virtual void Tick(float DeltaSeconds) override;

public:
	void Initialize();

	void ResetCachedVisibilities(FVisionUnitData& VisionUnitData);

	void UpdateVisibilities(const FVector3d& OriginWorldLocation, FVisionUnitData& VisionUnitData);

	void CalculateTileHeight(FTile& Tile, FIntVector2 TileIJ);

	UTexture2D* CreateSnapshotTexture();

	UTextureRenderTarget2D* CreateRenderTarget();

#if WITH_EDITORONLY_DATA
	void WriteHeightmapDataToTexture(UTexture2D* Texture);
#endif

	void WriteVisionDataToTexture(UTexture2D* Texture);

	FORCEINLINE int GetGlobalIndex(FIntVector2 IJ) const { return IJ.X * GridResolution.Y + IJ.Y; }

	FORCEINLINE FIntVector2 GetTileIJ(int GlobalIndex) { return { GlobalIndex / GridResolution.Y, GlobalIndex % GridResolution.Y }; }

	FORCEINLINE FTile& GetGlobalTile(int GlobalIndex) { return Tiles[GlobalIndex]; }

	FORCEINLINE FTile& GetGlobalTile(FIntVector2 IJ) { checkSlow(IsGlobalIJValid(IJ)); return GetGlobalTile(GetGlobalIndex(IJ)); }

	FORCEINLINE bool IsGlobalIJValid(FIntVector2 IJ) { return (IJ.X >= 0) & (IJ.Y >= 0) & (IJ.X < GridResolution.X) & (IJ.Y < GridResolution.Y); }

	FORCEINLINE FVector2f ConvertWorldSpaceLocationToGridSpace(const FVector2D& WorldLocation);

	FORCEINLINE FVector2D ConvertTileIJToTileCenterWorldLocation(const FIntVector2& IJ);

	FORCEINLINE FIntVector2 ConvertGridLocationToTileIJ(const FVector2f& GridLocation);

	FORCEINLINE FIntVector2 ConvertWorldLocationToTileIJ(const FVector2D& WorldLocation);

	FORCEINLINE bool IsBlockingVision(float ObserverHeight, float PotentialObstacleHeight);

	FORCEINLINE void ExecuteDDAVisibilityCheck(float ObserverHeight, FIntVector2 LocalIJ, FIntVector2 OriginLocalIJ, FVisionUnitData& VisionUnitData);

public:
	UPROPERTY(VisibleInstanceOnly)
	FVector2D GridSize = FVector2D::Zero();

	UPROPERTY(VisibleInstanceOnly)
	FIntVector2 GridResolution = {};

	UPROPERTY(VisibleInstanceOnly)
	FVector2D GridBottomLeftWorldLocation = FVector2D::Zero();

#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleInstanceOnly, Category = "FogOfWar|Textures")
	TObjectPtr<UTexture2D> HeightmapTexture = nullptr;
#endif

	UPROPERTY(VisibleInstanceOnly, Category = "FogOfWar|Textures")
	TObjectPtr<UTexture2D> SnapshotTexture = nullptr;

	UPROPERTY(VisibleInstanceOnly, Category = "FogOfWar|Textures")
	TObjectPtr<UTextureRenderTarget2D> VisibilityTextureRenderTarget = nullptr;

	UPROPERTY(VisibleInstanceOnly, Category = "FogOfWar|Textures")
	TObjectPtr<UTextureRenderTarget2D> PreFinalVisibilityTextureRenderTarget = nullptr;

	UPROPERTY(VisibleInstanceOnly, Category = "FogOfWar|Textures")
	TObjectPtr<UTextureRenderTarget2D> FinalVisibilityTextureRenderTarget = nullptr;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> InterpolationMID;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> AfterInterpolationMID;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> SuperSamplingMID;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> PostProcessingMID;

	TArray<FTile> Tiles;

	TArray<uint8> TextureDataBuffer;

	// this is to avoid recursion overhead and this is not a local variable to avoid allocations overhead
	TArray<int> DDALocalIndexesStack;

	bool bFirstTick = true;

	bool bActivated = false;
};
