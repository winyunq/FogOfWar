// Copyright Winyunq, 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MassEntityTypes.h"
#include "Components/PostProcessComponent.h"
#include "MassFogOfWarFragments.h"
#include "MassRepresentationFragments.h" // For FMassVisibilityFragment
#include "MassRepresentationProcessor.h" // For UMassVisibilityProcessor
#include "MassLODFragments.h" // For LOD culling tags
#include "Subsystems/MinimapDataSubsystem.h"
#include "FogOfWar.generated.h"

/// @file FogOfWar.h
/// @brief 定义了战争迷雾系统的核心Actor AFogOfWar。

// 前向声明
class UBrushComponent;
class UTexture2D;
class AVolume;

/// 声明一个全局的日志分类，用于本模块的日志输出
DECLARE_LOG_CATEGORY_EXTERN(LogFogOfWar, Log, All)

/**
 * @class AFogOfWar
 * @brief 战争迷雾系统的核心管理器Actor。
 * @details 场景战争迷雾采用 GPU 圆形视野源后处理。CPU 只从 MassBattle HashGrid 收集并压缩
 * 对当前镜头有影响的视野源，然后将 (WorldX, WorldY, SightRadius) 数据上传给一个后处理材质。
 */
UCLASS(BlueprintType, Blueprintable)
class FOGOFWAR_API AFogOfWar : public AActor
{
	GENERATED_BODY()

public:
	AFogOfWar();

public:
	/** 旧 CPU tile 查询已裁剪；场景可见性由后处理材质按 GPU 圆形源实时判断。 */
	UFUNCTION(BlueprintCallable, meta=(DeprecatedFunction, DeprecationMessage="GPU 圆形战争迷雾不再维护 CPU tile 可见性查询。"))
	bool IsLocationVisible(FVector WorldLocation);

	/** 旧 FinalVisibilityTexture 管线已裁剪；保留函数仅避免蓝图引用立刻断裂。 */
	UFUNCTION(BlueprintPure, meta=(DeprecatedFunction, DeprecationMessage="使用 FOW_SceneGpuVisionSourceTexture 在后处理材质中按圆形源揭雾。"))
	UTexture* GetFinalVisibilityTexture();

	/**
	 * @brief       为动态材质实例（MID）设置通用的着色器参数。
	 * @details     将网格尺寸、分辨率等通用信息传递给指定的MID。
	 * @param       MID                            数据类型: UMaterialInstanceDynamic*
	 * @details     需要设置参数的动态材质实例。
	 */
	UFUNCTION(BlueprintCallable)
	void SetCommonMIDParameters(UMaterialInstanceDynamic* MID);

	/**
	 * @brief       手动激活战争迷雾系统。
	 * @details     开始计算和渲染战争迷雾。如果bAutoActivate为true，则会在BeginPlay时自动调用。
	 */
	UFUNCTION(BlueprintCallable)
	void Activate();

	/**
	 * @brief       检查战争迷雾系统当前是否已激活。
	 * @return      bool
	 * @retval      true 如果已激活。
	 */
	UFUNCTION(BlueprintCallable, Category = "FogOfWar")
	bool IsActivated() const { return bActivated; }

	/** 旧 tile 管线已裁剪，始终返回 0。 */
	UFUNCTION(BlueprintCallable, Category = "FogOfWar", meta=(DeprecatedFunction, DeprecationMessage="GPU 圆形战争迷雾不使用 TileSize。"))
	float GetTileSize() const { return 0.0f; }

public:
	//~ Begin UPROPERTY Configuration
	
	/// @brief 用于应用战争迷雾效果的后期处理组件。
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UPostProcessComponent> PostProcess;

	/// @brief 如果为true，系统将在BeginPlay时自动激活。
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bAutoActivate = true;

	/// @brief 定义战争迷雾生效范围的体积（Volume）。
	/// @details 系统读取此 Volume 的世界范围，作为材质参数和 HashGrid fallback 查询范围。
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly)
	TObjectPtr<AVolume> GridVolume = nullptr;

	/// @brief 非可见区域的亮度。
	/// @details 在后期处理材质中，用于控制完全被迷雾覆盖区域的最终显示亮度。
	UPROPERTY(EditAnywhere, meta = (ClampMin = 0.0f, UIMin = 0.0f, ClampMax = 1.0f, UIMax = 1.0f))
	float NotVisibleRegionBrightness = 0.1f;

	/// @brief 唯一的场景战争迷雾后处理材质。材质读取圆形视野源并在 GPU 上逐像素揭雾。
	UPROPERTY(EditAnywhere, Category = "FogOfWar|Materials")
	TObjectPtr<UMaterialInterface> PostProcessingMaterial;

	/// @brief 为场景后处理材质提供逐像素 GPU 揭雾源。它独立于小地图战争迷雾。
	UPROPERTY(EditAnywhere, Category = "FogOfWar|Scene GPU")
	bool bEnableSceneGpuVisionSources = true;

	/// @brief 传给场景后处理材质的最大视野源数量。
	UPROPERTY(EditAnywhere, Category = "FogOfWar|Scene GPU", meta = (ClampMin = "1", UIMin = "1"))
	int32 MaxSceneGpuVisionSources = 4096;

	/// @brief 只收集当前玩家镜头地面投影范围附近的视野源；最终揭雾仍由 GPU 按圆形半径判断。
	UPROPERTY(EditAnywhere, Category = "FogOfWar|Scene GPU")
	bool bCullSceneGpuVisionSourcesToCamera = true;

	/// @brief 镜头投影范围外额外保留的世界距离，避免边缘闪烁。
	UPROPERTY(EditAnywhere, Category = "FogOfWar|Scene GPU", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SceneGpuVisionCullPadding = 2048.0f;

	/// @brief HashGrid 查询外扩半径。应不小于项目中最大的场景揭雾半径，以捕捉镜头外覆盖到镜头内的视野源。
	UPROPERTY(EditAnywhere, Category = "FogOfWar|Scene GPU", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SceneGpuVisionSourceSearchPadding = 12000.0f;

	/// @brief 场景 GPU 视野源查询的 Z 半范围，避免在 3D HashGrid 中扫描无关高度层。
	UPROPERTY(EditAnywhere, Category = "FogOfWar|Scene GPU", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SceneGpuVisionQueryZHalfRange = 10000.0f;

	//~ End UPROPERTY Configuration

protected:
	virtual void BeginPlay() override;

#if WITH_EDITOR
	/// @brief 在编辑器中手动刷新Volume范围，重新计算网格。
	UFUNCTION(CallInEditor, Category = "FogOfWar", DisplayName = "RefreshVolume")
	void RefreshVolumeInEditor();

	virtual bool CanEditChange(const FProperty* InProperty) const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	virtual void Tick(float DeltaSeconds) override;

public:
	//~ Begin Core Logic Functions
	
	/**
	 * @brief       初始化战争迷雾系统。
	 * @details     在激活时调用，负责创建网格、扫描地形高度、创建所有纹理和材质实例。
	 */
	void Initialize();

	/** 更新场景后处理专用的 Mass 视野源纹理。 */
	void UpdateSceneGpuVisionSourceTexture();

	/** 优先读取 RTSCamera 已计算的地面四点；失败时退回 PlayerController 屏幕角反投影。 */
	bool TryGetCameraGroundFrustum(FVector2D OutFrustumPoints[4]) const;

	/** 从四点计算当前玩家镜头地面查询窗口，只用于 HashGrid broad-phase，不代表迷雾形状。 */
	static bool BuildGroundBoundsFromFrustum(const FVector2D FrustumPoints[4], FBox2D& OutBounds);
	//~ End Core Logic Functions

public:
	//~ Begin Internal State Properties

	/// @brief 网格在世界空间中的尺寸（宽和高）。
	UPROPERTY(VisibleInstanceOnly)
	FVector2D GridSize = FVector2D::Zero();

	/// @brief 网格左下角在世界空间中的2D坐标。作为所有坐标转换的基准。
	UPROPERTY(VisibleInstanceOnly)
	FVector2D GridBottomLeftWorldLocation = FVector2D::Zero();

	/// @brief PostProcessingMaterial的动态实例。
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> PostProcessingMID;

	/// @brief 场景后处理材质读取的视野源数据纹理；每个 texel 为 (WorldX, WorldY, SightRadius, Reserved)。
	UPROPERTY(VisibleInstanceOnly, Category = "FogOfWar|Textures")
	TObjectPtr<UTexture2D> SceneGpuVisionSourceTexture = nullptr;

	/// @brief 避免每帧重复分配的场景视野源上传缓冲。
	TArray<FLinearColor> SceneGpuVisionSourceDataBuffer;

	/// @brief 当前已写入 SceneGpuVisionSourceTexture 的视野源数量。
	int32 SceneGpuVisionSourceCount = 0;

	/// @brief 标记系统是否已激活。
	bool bActivated = false;
};
