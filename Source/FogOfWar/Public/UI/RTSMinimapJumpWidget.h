// Copyright Winyunq, 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RTSMinimapJumpWidget.generated.h"

class UActorComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMinimapWorldLocation, const FVector&, WorldLocation);

/**
 * URTSMinimapJumpWidget
 *
 * 覆盖在小地图之上的自包含透明跳转控件。
 * 负责提供可命中的透明 surface、渲染相机视锥体，并把点击/拖动转换为世界坐标跳转请求。
 */
UCLASS(BlueprintType, Blueprintable, meta=(DisplayName="RTS Minimap Jump Widget"))
class FOGOFWAR_API URTSMinimapJumpWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	URTSMinimapJumpWidget(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintCallable, Category = "Minimap|Jump")
	void InitializeJumpWidget();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	// --- Input Handling ---
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	/** 将世界坐标转换为 Widget 局部坐标 (用于绘制) */
	FVector2D ConvertWorldToWidgetLocal(const FVector2D& WorldPos, const FVector2D& WidgetSize) const;

	/** 将 Widget 局部坐标转换为世界坐标 (用于点击) */
	FVector2D ConvertWidgetLocalToWorld(const FVector2D& LocalPos, const FVector2D& WidgetSize) const;

	void BindRTSCameraFrustumUpdates();
	void HandleMinimapFrustumUpdated();
	bool GetCurrentBounds(FVector& OutOrigin, FVector& OutExtent) const;
	UActorComponent* FindRTSCameraJumpComponent() const;
	bool TryJumpToWorldLocation(const FVector& WorldLocation);
	void RequestWorldLocation(const FVector2D& WorldPos);

protected:
	/** 是否绘制当前相机视锥线。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap|Jump")
	bool bDrawCameraFrustum = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap|Jump", meta = (EditCondition = "bDrawCameraFrustum"))
	FLinearColor FrustumLineColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap|Jump", meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bDrawCameraFrustum"))
	float FrustumLineThickness = 2.0f;

	/** 点击/拖动时自动寻找 RTS 相机组件并调用 jumpTo。关闭后只广播 OnWorldLocationRequested。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap|Jump")
	bool bAutoJumpToRTSCamera = true;

private:
	TWeakObjectPtr<UActorComponent> CachedJumpComponent;

public:
	/** 点击或拖动小地图时广播世界坐标；默认也会尝试自动调用 RTS 相机 jumpTo。 */
	UPROPERTY(BlueprintAssignable, Category = "Minimap|Jump")
	FOnMinimapWorldLocation OnWorldLocationRequested;

	/** 是否正在拖动小地图 */
	bool bIsDragging = false;
};
