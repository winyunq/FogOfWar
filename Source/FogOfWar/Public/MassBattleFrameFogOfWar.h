// Copyright Winyunq, 2025. All Rights Reserved.
// Commercial extension: see COMMERCIAL_FEATURE_LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MassBattleFrameFogOfWar.generated.h"

class FMassBattleFrameFogSceneViewExtension;
class FMassBattleFrameFogMaskReadbackMailbox;
class UTextureRenderTarget2D;

USTRUCT(BlueprintType)
struct FOGOFWAR_API FMassBattleFrameFogPerfStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "FogOfWar|MassBattleFrame Performance")
	float ParameterPushMs = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "FogOfWar|MassBattleFrame Performance")
	float ArrayUploadMs = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "FogOfWar|MassBattleFrame Performance")
	int32 SourceCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "FogOfWar|MassBattleFrame Performance")
	int32 BatchCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "FogOfWar|MassBattleFrame Performance")
	int32 ActiveProxyCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "FogOfWar|MassBattleFrame Performance")
	int32 NiagaraUploadElementCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "FogOfWar|MassBattleFrame Performance")
	int32 ParameterPushCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "FogOfWar|MassBattleFrame Performance")
	int32 MaskGeneration = 0;

	UPROPERTY(BlueprintReadOnly, Category = "FogOfWar|MassBattleFrame Performance")
	bool bVisibilityMaskReady = false;

	UPROPERTY(BlueprintReadOnly, Category = "FogOfWar|MassBattleFrame Performance")
	bool bUnitFilterActive = false;

	UPROPERTY(BlueprintReadOnly, Category = "FogOfWar|MassBattleFrame Performance")
	bool bSceneGpuPathActive = false;
};

/**
 * Standalone MassBattleFrame scene fog controller.
 *
 * It requests compact friendly-source snapshots from the replacement MBF
 * processor, composites scene fog through a camera view extension, maintains
 * a camera-local GPU state map, and publishes the asynchronous cell states
 * used to reject Niagara submissions.
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "Mass Battle Frame Fog Of War"))
class FOGOFWAR_API AMassBattleFrameFogOfWar : public AActor
{
	GENERATED_BODY()

public:
	AMassBattleFrameFogOfWar();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "FogOfWar|MassBattleFrame")
	void PushMassBattleFrameFogParameters();

	UFUNCTION(BlueprintPure, Category = "FogOfWar|MassBattleFrame")
	FMassBattleFrameFogPerfStats GetLastMassBattleFrameFogPerfStats() const
	{
		return LastPerfStats;
	}

	UFUNCTION(BlueprintCallable, Category = "FogOfWar|MassBattleFrame")
	void SetTemporaryVisionRadius(float InRadius);

	UFUNCTION(BlueprintCallable, Category = "FogOfWar|MassBattleFrame")
	void SetViewingTeamIndex(int32 InTeamIndex);

	UFUNCTION(BlueprintCallable, Category = "FogOfWar|MassBattleFrame")
	void SetAlliedTeamIndices(const TArray<int32>& InAlliedTeamIndices);

	UFUNCTION(BlueprintCallable, Category = "FogOfWar|MassBattleFrame")
	void SetFogOpacity(float InOpacity);

	UFUNCTION(BlueprintCallable, Category = "FogOfWar|MassBattleFrame")
	void SetFogDebug(bool bInDebug);

	UFUNCTION(BlueprintCallable, Category = "FogOfWar|MassBattleFrame|Debug")
	void SetDebugRevealAll(bool bInRevealAll);

	/** HashGrid-aligned R8 logic mask. GPU rasterization writes only 0 hidden or 3 true vision. */
	UFUNCTION(BlueprintPure, Category = "FogOfWar|MassBattleFrame")
	UTextureRenderTarget2D* GetWorldVisibilityMask() const { return WorldVisibilityMask; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FogOfWar|MassBattleFrame")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Unified scene/minimap/render-filter radius. One radius keeps the cache compact and deterministic. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|MassBattleFrame|Parameters", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float TemporaryVisionRadius = 1024.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|MassBattleFrame|Parameters", meta = (ClampMin = "0", ClampMax = "1023", UIMin = "0", UIMax = "1023"))
	int32 ViewingTeamIndex = 1;

	/** Manual additions merged with allies resolved from URTSDiplomacySubsystem. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|MassBattleFrame|Team")
	TArray<int32> AlliedTeamIndices;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|MassBattleFrame|Parameters", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float FogOpacity = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|MassBattleFrame|Parameters")
	bool bFogDebug = false;

	/** Debug-only override: clears the persistent state map to fully visible. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|MassBattleFrame|Debug")
	bool bDebugRevealAll = false;

	/**
	 * 0 means update every engine tick. Vision sources and the high-resolution
	 * scene mask default to 24 Hz.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|MassBattleFrame|Parameters", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "Hz"))
	float FogUpdateRateHz = 24.0f;

	/**
	 * HashGrid-aligned unit-state mask frequency. Unit admission/removal does not
	 * need the scene mask's visual cadence, so the default is deliberately 3 Hz.
	 * 0 updates it on every scene-mask upload.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|MassBattleFrame|Visibility Filter", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "Hz"))
	float LogicMaskUpdateRateHz = 3.0f;

	/** Seconds an enemy remains renderable after beginning an attack outside current vision. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|MassBattleFrame|Visibility Filter", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "s"))
	float AttackRevealDuration = 2.0f;

	/**
	 * Rate at which the visible-unit set performs a full removal/convergence pass.
	 * Newly visible units are still admitted on every fresh fog-mask generation.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|MassBattleFrame|Visibility Filter", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "Hz"))
	float UnitVisibilityConvergenceRateHz = 3.0f;

	/** Minimum time an admitted unit remains present after first failing visibility. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|MassBattleFrame|Visibility Filter", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "s"))
	float UnitVisibilityRemovalDelay = 0.333333f;

	/** Ground plane shared by camera-footprint calculation and screen visibility projection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|MassBattleFrame|Visibility Filter", meta = (Units = "cm"))
	float SceneFogProjectionPlaneZ = 0.0f;

	/**
	 * Maximum GPU extrapolation time for a 24 Hz vision-source snapshot. This
	 * removes stair-step motion without collecting or uploading sources again.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|MassBattleFrame|Visibility Filter", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "s"))
	float SceneFogSourcePredictionMaxSeconds = 0.125f;

	/** Fallback camera half extent when a view ray cannot intersect the ground plane. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|MassBattleFrame|Visibility Filter", meta = (ClampMin = "1.0", UIMin = "1.0", Units = "cm"))
	float CameraMaskFallbackHalfExtentUU = 16000.0f;

	/** Recenter only after the required camera footprint crosses this inner guard band. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|MassBattleFrame|Visibility Filter", meta = (ClampMin = "1", UIMin = "1"))
	int32 CameraMaskGuardBandCells = 8;

	/** Hard safety cap. Exceeding it is fatal; active fog never falls back to the unfiltered renderer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|MassBattleFrame|Visibility Filter", meta = (ClampMin = "1", UIMin = "1"))
	int32 MaxWorldMaskDimension = 4096;

	/** Hard safety cap for the R8 readback/dilation workload; one cell is exactly one MBF Agent HashGrid cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|MassBattleFrame|Visibility Filter", meta = (ClampMin = "1", UIMin = "1"))
	int32 MaxWorldMaskCells = 2097152;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|MassBattleFrame|Performance")
	bool bEnablePerformanceStats = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|MassBattleFrame|Performance")
	bool bLogPerformanceToOutputLog = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|MassBattleFrame|Performance")
	float PerformanceLogInterval = 2.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "FogOfWar|MassBattleFrame|Performance")
	FMassBattleFrameFogPerfStats LastPerfStats;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "FogOfWar|MassBattleFrame|Visibility Filter")
	TObjectPtr<UTextureRenderTarget2D> WorldVisibilityMask;

private:
	void ActivateMassBattleFrameFog();
	void DeactivateMassBattleFrameFog();
	void ConfigureRenderFilter();
	bool ResolveWorldMaskLayout();
	bool EnsureWorldVisibilityMask();
	void ConsumeGpuMaskReadback();
	void SetMassBattleFrameFogArrays();

	TSharedPtr<FMassBattleFrameFogSceneViewExtension, ESPMode::ThreadSafe> SceneViewExtension;
	TSharedPtr<FMassBattleFrameFogMaskReadbackMailbox, ESPMode::ThreadSafe> MaskReadbackMailbox;

	FVector2D WorldMaskMin = FVector2D::ZeroVector;
	FVector2D WorldMaskSize = FVector2D(1.0, 1.0);
	FVector2D WorldMaskCellSize = FVector2D(1.0, 1.0);
	FIntPoint WorldMaskDimensions = FIntPoint::ZeroValue;
	FVector2D CameraRenderCullMin = FVector2D(-HALF_WORLD_MAX, -HALF_WORLD_MAX);
	FVector2D CameraRenderCullMax = FVector2D(HALF_WORLD_MAX, HALF_WORLD_MAX);
	bool bCameraRenderCullInitialized = false;
	bool bWorldMaskLayoutValid = false;
	bool bForceLogicMaskUpdate = true;

	bool bFogActive = false;
	double LastParameterPushTime = -TNumericLimits<double>::Max();
	double LastLogicMaskPushTime = -TNumericLimits<double>::Max();
	double LastPerformanceLogTime = -TNumericLimits<double>::Max();
};
