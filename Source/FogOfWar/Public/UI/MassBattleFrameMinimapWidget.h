// Copyright Winyunq, 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MassBattleFrameMinimapWidget.generated.h"

class FMassBattleMinimapRenderData;
class SMassBattleFrameMinimap;

USTRUCT(BlueprintType)
struct FOGOFWAR_API FMassBattleFrameMinimapPerfStats
{
	GENERATED_BODY()

	/** CPU time spent copying the requested compact snapshot and scheduling its GPU upload. */
	UPROPERTY(BlueprintReadOnly, Category = "FogOfWar|MassBattleFrame Minimap")
	float ParameterPushMs = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "FogOfWar|MassBattleFrame Minimap")
	int32 CpuAgentTraversalCount = 0;
};

/**
 * Layout and configuration anchor for the MassBattle GPU minimap.
 *
 * This widget owns no capture, material, persistent render target, unit query, or fog query.
 * It contributes one custom GPU draw at its Slate/UMG geometry.
 */
UCLASS(BlueprintType, Blueprintable, Config = MassBattle, meta = (DisplayName = "Mass Battle Frame Minimap"))
class FOGOFWAR_API UMassBattleFrameMinimapWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UMassBattleFrameMinimapWidget(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintCallable, Category = "FogOfWar|MassBattleFrame Minimap")
	bool InitializeMassBattleFrameMinimap();

	UFUNCTION(BlueprintCallable, Category = "FogOfWar|MassBattleFrame Minimap")
	bool PushMassBattleFrameMinimapFrame();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "FogOfWar|MassBattleFrame Minimap")
	FMassBattleFrameMinimapPerfStats GetLastPerformanceStats() const { return LastPerfStats; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "FogOfWar|MassBattleFrame Minimap")
	bool IsMassBattleFrameMinimapInitialized() const { return bIsSuccessfullyInitialized; }

	UFUNCTION(BlueprintCallable, Category = "FogOfWar|MassBattleFrame Minimap", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "Hz"))
	void SetUpdateRateHz(float InUpdateRateHz);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "FogOfWar|MassBattleFrame Minimap")
	float GetUpdateRateHz() const { return UpdateRateHz; }

	UFUNCTION(BlueprintCallable, Category = "FogOfWar|MassBattleFrame Minimap")
	void SetMinimapResolution(int32 InResolution);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "FogOfWar|MassBattleFrame Minimap")
	int32 GetMinimapResolution() const { return MinimapResolution; }

	UFUNCTION(BlueprintCallable, Category = "FogOfWar|MassBattleFrame Minimap")
	void SetFogDarkenOpacity(float InOpacity);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "FogOfWar|MassBattleFrame Minimap")
	float GetFogDarkenOpacity() const { return FogDarkenOpacity; }

	UFUNCTION(BlueprintCallable, Category = "FogOfWar|MassBattleFrame Minimap")
	void SetVisionRadiusUU(float InRadiusUU);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "FogOfWar|MassBattleFrame Minimap")
	float GetVisionRadiusUU() const { return VisionRadiusUU; }

	UFUNCTION(BlueprintCallable, Category = "FogOfWar|MassBattleFrame Minimap")
	void SetUnitRadiusUU(float InRadiusUU);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "FogOfWar|MassBattleFrame Minimap")
	float GetUnitRadiusUU() const { return UnitRadiusUU; }

	/** Reads configured nation colors into the caller-owned canonical palette. */
	void LoadConfiguredTeamColors(TArray<FLinearColor>& OutTeamColors) const;
	/** The one render submission path for a complete TeamId-to-color palette. */
	void CommitTeamColors(TConstArrayView<FLinearColor> InTeamColors);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

	/** Logical cells per map axis; it never changes widget layout size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassBattleFrame Minimap|Raster", meta = (ClampMin = "16", ClampMax = "2048", UIMin = "64", UIMax = "1024", DisplayName = "Logical Resolution"))
	int32 MinimapResolution = 256;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassBattleFrame Minimap|Fog", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm", DisplayName = "Vision Radius"))
	float VisionRadiusUU = 4000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassBattleFrame Minimap|Units", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm", DisplayName = "Unit Radius"))
	float UnitRadiusUU = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassBattleFrame Minimap|Fog", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", DisplayName = "Fog Opacity"))
	float FogDarkenOpacity = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MassBattleFrame Minimap|Performance", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "Hz", DisplayName = "Update Rate"))
	float UpdateRateHz = 3.0f;

	/** Used for every Team ID that has no explicit TeamColorN entry in MinimapColors.ini. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassBattleFrame Minimap|Team")
	FLinearColor DefaultTeamColor = FLinearColor(0.7f, 0.7f, 0.7f, 1.0f);

	/** Stable color used while a unit is in MassBattle's sampled hit-reaction state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassBattleFrame Minimap|Feedback")
	FLinearColor CombatUnitColor = FLinearColor::White;

	/** Legacy shader ABI values; normal markers are no longer normalized or darkened. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "MassBattleFrame Minimap|Feedback")
	bool bNormalizeTeamColorDirection = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "MassBattleFrame Minimap|Feedback")
	float NormalUnitColorLength = 0.58f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "MassBattleFrame Minimap|Feedback")
	float SelectedUnitColorLength = 1.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MassBattleFrame Minimap|Performance")
	FMassBattleFrameMinimapPerfStats LastPerfStats;

private:
	bool ResolveMapRegion();
	void ApplyBaseMapTextureFromConfig();
	void HandleUpdateTimer();
	void RestartUpdateTimer();
	void StopUpdateTimer();

	FTransform MapRegionTransform = FTransform::Identity;
	FVector2D MapWorldSize = FVector2D(65536.0f, 65536.0f);
	FTimerHandle UpdateTimerHandle;
	TSharedPtr<SMassBattleFrameMinimap> RuntimeSlateWidget;
	TSharedPtr<FMassBattleMinimapRenderData, ESPMode::ThreadSafe> RenderData;

	bool bIsSuccessfullyInitialized = false;
};
