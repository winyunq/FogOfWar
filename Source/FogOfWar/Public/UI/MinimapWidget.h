// Copyright Winyunq, 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MassEntityTypes.h"
#include "RTSCamera.h"
#include "Components/Button.h" // Added for UButton
#include "MinimapWidget.generated.h"

class UImage;
class UButton; // Forward declaration for UButton
class UTextureRenderTarget2D;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UMinimapDataSubsystem;
class AFogOfWar;

// 声明委托
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMinimapDrag, const FVector&, WorldLocationDelta);

/**
 * UMinimapWidget
 * 
 * 高度自动化、易于使用的小地图控件。
 * 自动从主战争迷雾Actor同步坐标系，自动绑定UI中的Image控件，并提供清晰的点击、拖动、释放事件回调。
 */
UCLASS()
class FOGOFWAR_API UMinimapWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 从场景中的AFogOfWar Actor自动初始化坐标系和边界 */
	UFUNCTION(BlueprintCallable, Category = "Minimap")
	bool InitializeFromWorldFogOfWar();

	/** 将小地图上的UV坐标转换为世界坐标 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Minimap")
	FVector ConvertMinimapUVToWorldLocation(const FVector2D& UVPosition) const;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// 重写鼠标事件以捕获点击和拖动
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	// 核心更新函数
	void UpdateMinimapTexture();

	// 根据单位数量选择不同的绘制路径
	void DrawInMassSize();  // 大于阈值时，使用缓存绘制
	void DrawInLessSize();  // 小于等于阈值时，直接查询绘制

	UFUNCTION() void OnMinimapButtonPressed(); // Added for UButton
	UFUNCTION() void OnMinimapButtonReleased(); // Added for UButton

	// 将相机移动到小地图上的鼠标点击/悬停位置
	void JumpToLocationUnderMouse();

public:
	// --- 事件回调 (Event Callbacks) ---

	/** 当鼠标在小地图上拖动时广播。返回拖动的世界坐标增量。*/
	UPROPERTY(BlueprintAssignable, Category = "Minimap|Events")
	FOnMinimapDrag OnMinimapDragged;

protected:
	// --- 蓝图可配置属性 (Config) ---

	/** 用于绘制小地图的父材质 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap|Appearance")
	TObjectPtr<UMaterialInterface> MinimapMaterial;

	/** 小地图纹理的分辨率 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap|Performance")
	FIntPoint TextureResolution = FIntPoint(256, 256);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap|Performance", meta = (ClampMin = "0"))
	int32 DirectQueryThreshold = 1024;

	/** 小地图更新的频率（秒）。0表示每帧更新。*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap|Performance", meta = (ClampMin = "0.0"))
	float UpdateInterval = 1.0f;
	/// 单位数量
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap|Performance", meta = (ClampMin = "0.0"))
	int32 MaxUnits = 8192;
	// --- 自动绑定和内部状态 (Internal State) ---

	/** 【自动绑定】请在UMG编辑器中，将您的Image控件命名为'MinimapImage' */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> MinimapImage;

	/** 【自动绑定】请在UMG编辑器中，将您的Button控件命名为'MinimapButton'，并放置在MinimapImage上方 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> MinimapButton; // Added for UButton

	/** 渲染目标（RT）*/
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Minimap|Internal")
	TObjectPtr<UTextureRenderTarget2D> MinimapRenderTarget;

	/** 动态材质实例 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Minimap|Internal")
	TObjectPtr<UMaterialInstanceDynamic> MinimapMaterialInstance;

	/** [数据纹理] 缓存视野单位数据的动态纹理 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Minimap|Internal")
	TObjectPtr<UTexture2D> VisionDataTexture;

	/** [数据纹理] 缓存图标单位数据的动态纹理 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Minimap|Internal")
	TObjectPtr<UTexture2D> IconDataTexture;

	/** [数据纹理] 缓存图标颜色数据的动态纹理 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Minimap|Internal")
	TObjectPtr<UTexture2D> IconColorTexture;

	/** 指向数据仓库的指针 */
	UPROPERTY(Transient)
	TObjectPtr<UMinimapDataSubsystem> MinimapDataSubsystem;

	/** 指向主战争迷雾Actor的指针 */
	UPROPERTY(Transient)
	TObjectPtr<AFogOfWar> FogOfWarActor;

	UPROPERTY(Transient)
	TObjectPtr<class URTSCamera> RTSCameraComponent;

	/** Query for counting entities with a minimap representation. Configured once on initialization. */
	FMassEntityQuery CountQuery;

	/** Query for drawing entities directly to the minimap. Configured once on initialization. */
	FMassEntityQuery DrawQuery;

	// Tick更新频率控制器
	float TimeSinceLastUpdate = 0.0f;

	// 拖动状态
	bool bIsDragging = false;
	FVector2D LastMousePosition = FVector2D::ZeroVector;

	// 按钮按住状态
	bool bIsMinimapButtonHeld = false; 

	// -- Internal Debug --
	bool bIsSuccessfullyInitialized = false;
};