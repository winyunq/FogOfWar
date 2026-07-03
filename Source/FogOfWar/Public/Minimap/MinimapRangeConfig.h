#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/BoxComponent.h"
#include "MinimapRangeConfig.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMinimapRangeConfigDataUpdated);

/**
 * Defines the world-space bounds and optional resolution policy for the FogOfWar minimap.
 */
UCLASS(Blueprintable, BlueprintType, meta = (DisplayName = "小地图范围配置"))
class FOGOFWAR_API AMinimapRangeConfig : public AActor
{
	GENERATED_BODY()

public:
	AMinimapRangeConfig();

	/** The logical boundaries of the map. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Minimap", meta = (DisplayName = "逻辑地图包围盒", ToolTip = "定义地图的可移动和逻辑边界。"))
	TObjectPtr<UBoxComponent> BoundsComponent;

	/** Visualizer for the overflow boundary. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Minimap", meta = (DisplayName = "视野溢出包围盒", ToolTip = "显示包含缓冲区后的实际视野限制范围。"))
	TObjectPtr<UBoxComponent> OverflowComponent;

	/** Visual buffer outside logical map, in Unreal units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Range Config", meta = (ClampMin = "0.0", DisplayName = "边界溢出长度 (UU)", ToolTip = "在逻辑地图边缘之外允许相机看到的缓冲区宽度。单位为厘米(UU)。"))
	float MapOverflowUU = 512.0f;

	/** Optional minimap grid resolution. Set either axis to 0 to let FogOfWar derive resolution from the MassBattle HashGrid cell size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Range Config", meta = (DisplayName = "小地图网格分辨率", ToolTip = "定义小地图网格精度；任一轴为 0 时按 HashGrid cell 自动推导。"))
	FIntPoint GridResolution = FIntPoint(2048, 2048);

	/** Delegate triggered when minimap data changes. */
	UPROPERTY(BlueprintAssignable, Category = "Minimap|Events", meta = (DisplayName = "数据更新回调", ToolTip = "当小地图数据发生变化时触发，供 UI 重画使用。"))
	FOnMinimapRangeConfigDataUpdated OnDataUpdated;

	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UFUNCTION(BlueprintCallable, Category = "Minimap")
	void NotifyDataUpdated() { OnDataUpdated.Broadcast(); }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Minimap")
	bool HasExplicitGridResolution() const { return GridResolution.X > 0 && GridResolution.Y > 0; }

	/** Writes this actor's bounds into Config/FogOfWarMapBounds.ini so runtime plugins can share bounds without referencing this Actor. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Minimap|Map Bounds")
	bool ExportMapBoundsConfig() const;

private:
	void UpdateVisuals();
};
