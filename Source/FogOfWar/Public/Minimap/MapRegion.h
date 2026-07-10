#pragma once

#include "CoreMinimal.h"
#include "Components/BoxComponent.h"
#include "GameFramework/Actor.h"
#include "MapRegion.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMapRegionDataUpdated);

UCLASS(Blueprintable, BlueprintType, meta = (DisplayName = "MapRegion"))
class FOGOFWAR_API AMapRegion : public AActor
{
	GENERATED_BODY()

public:
	AMapRegion();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MapRegion", meta = (DisplayName = "地图区域", ToolTip = "定义当前关卡的小地图与 RTS 相机共享区域。"))
	TObjectPtr<UBoxComponent> RegionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MapRegion", meta = (DisplayName = "溢出区域", ToolTip = "显示包含缓冲区后的相机边界区域。"))
	TObjectPtr<UBoxComponent> OverflowComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MapRegion", meta = (ClampMin = "0.0", DisplayName = "边界溢出长度 (UU)", ToolTip = "在地图区域之外允许相机看到的缓冲区宽度。单位为厘米(UU)。"))
	float MapOverflowUU = 512.0f;

	UPROPERTY(BlueprintAssignable, Category = "MapRegion|Events", meta = (DisplayName = "数据更新回调", ToolTip = "当 MapRegion 数据发生变化时触发。"))
	FOnMapRegionDataUpdated OnDataUpdated;

	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UFUNCTION(BlueprintCallable, Category = "MapRegion")
	void NotifyDataUpdated() { OnDataUpdated.Broadcast(); }

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "MapRegion")
	bool ExportMapRegion() const;

private:
	bool BuildMapRegionValues(
		FVector2D& OutGridOrigin,
		FVector2D& OutGridSize,
		float& OutMapOverflowUU) const;
	void UpdateVisuals();
};
