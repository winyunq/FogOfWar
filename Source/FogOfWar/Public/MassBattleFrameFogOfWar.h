// Copyright Winyunq, 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MassBattleFrameFogOfWar.generated.h"

class FMassBattleFrameFogSceneViewExtension;

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
	int32 ParameterPushCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "FogOfWar|MassBattleFrame Performance")
	bool bSceneGpuPathActive = false;
};

/**
 * Standalone MassBattleFrame scene fog controller.
 *
 * The actor consumes the existing MassBattleFrame render-batch arrays. The
 * scene result is produced by a scene-view GPU pass that borrows the
 * minimap's instanced circle rasterization and inverse composite.
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
	bool ActivateMassBattleFrameFog();

	UFUNCTION(BlueprintCallable, Category = "FogOfWar|MassBattleFrame")
	void DeactivateMassBattleFrameFog();

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
	void SetFogOpacity(float InOpacity);

	UFUNCTION(BlueprintCallable, Category = "FogOfWar|MassBattleFrame")
	void SetFogDebug(bool bInDebug);

	UFUNCTION(BlueprintCallable, Category = "FogOfWar|MassBattleFrame|Debug")
	void SetDebugRevealAll(bool bInRevealAll);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FogOfWar|MassBattleFrame")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Temporary/default radius until a per-agent fog-radius attribute is introduced. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|MassBattleFrame|Parameters", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float TemporaryVisionRadius = 1024.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|MassBattleFrame|Parameters", meta = (ClampMin = "0", ClampMax = "1023", UIMin = "0", UIMax = "1023"))
	int32 ViewingTeamIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|MassBattleFrame|Parameters", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float FogOpacity = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|MassBattleFrame|Parameters")
	bool bFogDebug = false;

	/** Debug-only override: every uploaded unit reveals the scene, ignoring team and hidden filters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|MassBattleFrame|Debug")
	bool bDebugRevealAll = false;

	/** 0 means update every engine tick; positive values explicitly cap the GPU data update rate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|MassBattleFrame|Parameters", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "Hz"))
	float FogUpdateRateHz = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|MassBattleFrame|Parameters")
	bool bAutoActivate = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|MassBattleFrame|Performance")
	bool bEnablePerformanceStats = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|MassBattleFrame|Performance")
	bool bLogPerformanceToOutputLog = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|MassBattleFrame|Performance")
	float PerformanceLogInterval = 2.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "FogOfWar|MassBattleFrame|Performance")
	FMassBattleFrameFogPerfStats LastPerfStats;

private:
	void SetMassBattleFrameFogArrays();

	TSharedPtr<FMassBattleFrameFogSceneViewExtension, ESPMode::ThreadSafe> SceneViewExtension;

	bool bFogActive = false;
	double LastParameterPushTime = -TNumericLimits<double>::Max();
	double LastPerformanceLogTime = -TNumericLimits<double>::Max();
};
