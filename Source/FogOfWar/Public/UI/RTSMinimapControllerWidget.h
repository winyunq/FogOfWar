// Copyright Winyunq, 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Subsystems/MinimapDataSubsystem.h" // Needed for FMinimapTile logic if accessed, but mostly for Grid params
#include "RTSMinimapControllerWidget.generated.h"

class URTSCamera;

/**
 * URTSMinimapControllerWidget
 * 
 * 一个完全独立的、覆盖在小地图之上的透明控制器。
 * 负责渲染相机视锥体（FOV）并处理点击跳转逻辑。
 * 它拥有自己的独立更新频率（通常为每帧），且通过缓存网格参数来解耦对 Subsystem 的依赖。
 */
UCLASS()
class FOGOFWAR_API URTSMinimapControllerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	URTSMinimapControllerWidget(const FObjectInitializer& ObjectInitializer);

	/** 
	 * 手动初始化控制器。建议在 NativeConstruct 后调用，或者由外部管理类调用。
	 * 尝试获取 RTSCamera 和 MinimapDataSubsystem 的参数。
	 */
	UFUNCTION(BlueprintCallable, Category = "Minimap|Controller")
	void InitializeController();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	// --- Input Handling ---
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	/** 尝试查找当前玩家的 RTSCamera 组件 */
	void FindRTSCamera();

	/** 将世界坐标转换为 Widget 局部坐标 (用于绘制) */
	FVector2D ConvertWorldToWidgetLocal(const FVector2D& WorldPos, const FVector2D& WidgetSize) const;

	/** 将 Widget 局部坐标转换为世界坐标 (用于点击) */
	FVector2D ConvertWidgetLocalToWorld(const FVector2D& LocalPos, const FVector2D& WidgetSize) const;

protected:
	
	/** 缓存的网格左下角世界坐标 (从 Subsystem 读取一次) */
	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Cache")
	FVector2D CachedGridBottomLeft = FVector2D::ZeroVector;

	/** 缓存的网格世界尺寸 (从 Subsystem 读取一次) */
	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Cache")
	FVector2D CachedGridSize = FVector2D(1.0f, 1.0f); // Default to non-zero to avoid div/0

	/** 缓存的 RTCamera 引用 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Minimap|Cache")
	TObjectPtr<URTSCamera> CachedRTSCamera;

	/** 是否正在拖动小地图 */
	bool bIsDragging = false;
};
