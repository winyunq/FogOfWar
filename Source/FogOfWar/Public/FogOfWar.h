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
class UTextureRenderTarget2D;
class AVolume;

/// 声明一个全局的日志分类，用于本模块的日志输出
DECLARE_LOG_CATEGORY_EXTERN(LogFogOfWar, Log, All)

/**
 * @struct FTile
 * @brief 代表战争迷雾网格中的单个瓦片（单元格）。
 * @details 存储了每个网格单元的核心数据。
 */
USTRUCT()
struct FOGOFWAR_API FTile
{
	GENERATED_BODY()

	/// @brief 瓦片中心点的地形高度（Z轴坐标）。
	/// @details 在初始化时通过射线检测计算得出，用于后续的视野遮挡判断。
	UPROPERTY()
	float Height = 0.0f;

	/// @brief 瓦片的可见性计数器。
	/// @details 每当有一个视野单位能看到此瓦片时，此计数器加1；当单位移开视野时，减1。
	/// 只要此值大于0，该瓦片就被认为是当前可见的。这种机制允许多个单位同时观察同一区域。
	UPROPERTY()
	int VisibilityCounter = 0;
};


/**
 * @class AFogOfWar
 * @brief 战争迷雾系统的核心管理器Actor。
 * @details 这是一个应在场景中全局唯一的Actor，负责管理整个战争迷雾系统的所有数据和操作。
 * 主要职责包括：
 * 1. 管理一个二维的FTile网格，存储地形高度和可见性状态。
 * 2. 提供接口（UpdateVisibilities, ResetCachedVisibilities）给Mass Processors，以响应单位的移动和生成/销毁。
 * 3. 执行核心的视野计算，使用DDA（数字微分分析器）算法进行高效的视线检查。
 * 4. 管理一个复杂的渲染管线，通过一系列RT（Render Target）和材质，生成最终平滑、带渐隐效果的战争迷雾纹理。
 * 5. 通过后期处理（Post-Process）将战争迷雾效果应用到游戏屏幕上。
 */
UCLASS(BlueprintType, Blueprintable)
class FOGOFWAR_API AFogOfWar : public AActor
{
	GENERATED_BODY()

public:
	AFogOfWar();

public:
	/**
	 * @brief       检查指定的世界坐标点当前是否可见。
	 * @param       WorldLocation                  数据类型: FVector
	 * @details     要检查的点的世界坐标。
	 * @return      bool
	 * @retval      true 如果该点在迷雾中是可见的。
	 * @retval      false 如果该点被迷雾遮挡。
	 */
	UFUNCTION(BlueprintCallable)
	bool IsLocationVisible(FVector WorldLocation);

	/**
	 * @brief       获取最终生成的、可用于UI或后期处理的战争迷雾纹理。
	 * @details     此纹理是经过了插值、超采样和平滑处理后的最终结果。
	 * @return      UTexture*
	 * @retval      指向FinalVisibilityTextureRenderTarget的指针。
	 */
	UFUNCTION(BlueprintPure)
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

	/**
	 * @brief       获取网格瓦片的大小。
	 * @return      float
	 * @retval      单个瓦片在世界空间中的边长（厘米）。
	 */	UFUNCTION(BlueprintCallable, Category = "FogOfWar")
	float GetTileSize() const { return TileSize; }

public:
	//~ Begin UPROPERTY Configuration
	
	/// @brief 用于向下扫描以确定地形高度的碰撞通道。
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TEnumAsByte<ECollisionChannel> HeightScanCollisionChannel = ECC_Camera;

	/// @brief 用于应用战争迷雾效果的后期处理组件。
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UPostProcessComponent> PostProcess;

	/// @brief 如果为true，系统将在BeginPlay时自动激活。
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bAutoActivate = true;

	/// @brief 定义战争迷雾生效范围的体积（Volume）。
	/// @details 系统将根据此Volume的边界和TileSize来确定网格的分辨率。
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly)
	TObjectPtr<AVolume> GridVolume = nullptr;

	/// @brief 单个网格瓦片的大小（世界单位，厘米）。
	/// @details 这是战争迷雾计算的精度基础。值越小，精度越高，但性能和内存开销越大。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = 0.0f, UIMin = 0.0f))
	float TileSize = 100.0f;

	/// @brief 判定视野被遮挡所需的高度差阈值。
	/// @details 当观察者视线路径上的某个瓦片高度超过（观察者高度 + 路径距离 * 斜率 + 此阈值）时，视线被阻挡。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = 0.0f, UIMin = 0.0f))
	float VisionBlockingDeltaHeightThreshold = 200.0f;

	/// @brief 吸收新视野快照所需的大致时间（秒）。
	/// @details 控制视野从不可见到可见时的平滑过渡时间。值越大，过渡越平滑，但视野更新有延迟感。
	UPROPERTY(EditAnywhere, meta = (ClampMin = 0.0f, UIMin = 0.0f))
	float ApproximateSecondsToAbsorbNewSnapshot = 0.1f;

	/// @brief 最小可见度阈值。
	/// @details 在纹理中，任何低于此值的像素将被视为完全不可见（0）。用于消除插值产生的微弱“鬼影”。
	UPROPERTY(EditAnywhere, meta = (ClampMin = 0.0f, UIMin = 0.0f, ClampMax = 1.0f, UIMax = 1.0f))
	float MinimalVisibility = 0.1f;

	/// @brief 非可见区域的亮度。
	/// @details 在后期处理材质中，用于控制完全被迷雾覆盖区域的最终显示亮度。
	UPROPERTY(EditAnywhere, meta = (ClampMin = 0.0f, UIMin = 0.0f, ClampMax = 1.0f, UIMax = 1.0f))
	float NotVisibleRegionBrightness = 0.1f;

	/// @brief 用于在时间上平滑视野变化的插值材质。
	UPROPERTY(EditAnywhere, Category = "FogOfWar|Materials")
	TObjectPtr<UMaterialInterface> InterpolationMaterial;

	/// @brief 在插值之后应用的额外处理材质。
	UPROPERTY(EditAnywhere, Category = "FogOfWar|Materials")
	TObjectPtr<UMaterialInterface> AfterInterpolationMaterial;

	/// @brief 用于将低分辨率视野纹理提升至最终分辨率的超采样材质。
	UPROPERTY(EditAnywhere, Category = "FogOfWar|Materials")
	TObjectPtr<UMaterialInterface> SuperSamplingMaterial;

	/// @brief 应用于全屏的后期处理材质，最终将迷雾效果渲染到屏幕上。
	UPROPERTY(EditAnywhere, Category = "FogOfWar|Materials")
	TObjectPtr<UMaterialInterface> PostProcessingMaterial;

//#if WITH_EDITORONLY_DATA
	/// @brief 【调试】压力测试模式，忽略所有缓存，强制每帧重新计算所有单位的视野。
	UPROPERTY(EditAnywhere, Category = "FogOfWar|Debug")
	bool bDebugStressTestIgnoreCache = false;

	/// @brief 【调试】压力测试模式，强制每帧更新所有单位的小地图数据。
	UPROPERTY(EditAnywhere, Category = "FogOfWar|Debug")
	bool bDebugStressTestMinimap = false;

	/// @brief 【调试】在调试高度图纹理时使用最近邻过滤，而不是线性过滤。
	UPROPERTY(EditAnywhere, Category = "FogOfWar|Debug")
	bool bDebugFilterNearest = false;

	/// @brief 【调试】高度图纹理中代表的最低高度。
	UPROPERTY(EditAnywhere, Category = "FogOfWar|Debug")
	float DebugHeightmapLowestZ = -1000.0f;

	/// @brief 【调试】高度图纹理中代表的最高高度。
	UPROPERTY(EditAnywhere, Category = "FogOfWar|Debug")
	float DebugHeightmapHightestZ = 1000.0f;
// #endif
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

	/**
	 * @brief       根据缓存的视野数据重置（减少）瓦片的可见性计数。
	 * @details     当一个单位移动或消失时，需要先“擦除”它上一帧的视野贡献。此函数即用于此目的。
	 * @param       VisionUnitData                 数据类型: FVisionUnitData&
	 * @details     包含单位上一帧视野信息的缓存数据。
	 */
	void ResetCachedVisibilities(FVisionUnitData& VisionUnitData);

	/**
	 * @brief       为一个单位更新其视野，并更新瓦片的可见性计数。
	 * @details     这是由Mass Processor调用的核心函数。它会计算指定单位的新视野，并更新全局的可见性计数器。
	 * @param       OriginWorldLocation            数据类型: const FVector3d&
	 * @details     视野单位当前的世界坐标。
	 * @param       VisionUnitData                 数据类型: FVisionUnitData&
	 * @details     用于接收新计算出的视野缓存数据。
	 */
	void UpdateVisibilities(const FVector3d& OriginWorldLocation, FVisionUnitData& VisionUnitData);

	/**
	 * @brief       计算单个瓦片的地形高度。
	 * @details     通过从空中向下发射射线来确定瓦片中心点 的Z坐标。
	 * @param       Tile                           数据类型: FTile&
	 * @details     要计算高度的瓦片对象。
	 * @param       TileIJ                         数据类型: FIntVector2
	 * @details     瓦片的二维网格坐标。
	 */
	void CalculateTileHeight(FTile& Tile, FIntPoint TileIJ);

	/**
	 * @brief       创建一个用于存储当前帧可见性格子快照的2D纹理。
	 * @return      UTexture2D*
	 */
	UTexture2D* CreateSnapshotTexture();

	/**
	 * @brief       创建一个通用的渲染目标（Render Target）2D纹理。
	 * @return      UTextureRenderTarget2D*
	 */
	UTextureRenderTarget2D* CreateRenderTarget();

#if WITH_EDITORONLY_DATA
	/**
	 * @brief       【调试】将高度图数据写入纹理，用于可视化调试。
	 * @param       Texture                        数据类型: UTexture2D*
	 * @details     要写入数据的目标纹理。
	 */
	void WriteHeightmapDataToTexture(UTexture2D* Texture);
#endif

	/**
	 * @brief       将当前帧的可见性数据（基于VisibilityCounter）写入快照纹理。
	 * @param       Texture                        数据类型: UTexture2D*
	 * @details     要写入数据的快照纹理。
	 */
	void WriteVisionDataToTexture(UTexture2D* Texture);
	//~ End Core Logic Functions

	//~ Begin Inline Helper Functions

	/// @brief 将二维网格坐标转换为一维数组索引。
	FORCEINLINE int GetGlobalIndex(FIntPoint IJ) const { return IJ.X * GridResolution.Y + IJ.Y; }

	/// @brief 将一维数组索引转换为二维网格坐标。
	FORCEINLINE FIntPoint GetTileIJ(int GlobalIndex) const { return { GlobalIndex / GridResolution.Y, GlobalIndex % GridResolution.Y }; }

	/// @brief 根据一维索引获取瓦片对象引用。
	FORCEINLINE FTile& GetGlobalTile(int GlobalIndex) { return Tiles[GlobalIndex]; }

	/// @brief 根据二维坐标获取瓦片对象引用。
	FORCEINLINE FTile& GetGlobalTile(FIntPoint IJ) { checkSlow(UMinimapDataSubsystem::IsVisionGridIJValid_Static(IJ)); return GetGlobalTile(GetGlobalIndex(IJ)); }

	/// @brief 检查一个潜在的障碍物高度是否足以阻挡来自观察者的视线。
	FORCEINLINE bool IsBlockingVision(float ObserverHeight, float PotentialObstacleHeight);

	/**
	 * @brief       执行DDA（数字微分分析器）算法进行视线检查。
	 * @details     从原点出发，沿直线路径遍历网格瓦片，检查是否有地形遮挡。
	 * @param       ObserverHeight                 数据类型: float
	 * @details     观察者的高度。
	 * @param       LocalIJ                        数据类型: FIntVector2
	 * @details     当前检查的目标瓦片的局部坐标。
	 * @param       OriginLocalIJ                  数据类型: FIntVector2
	 * @details     观察者所在瓦片的局部坐标。
	 * @param       VisionUnitData                 数据类型: FVisionUnitData&
	 * @details     用于存储和更新视野结果的缓存数据。
	 */
	FORCEINLINE void ExecuteDDAVisibilityCheck(float ObserverHeight, FIntVector2 LocalIJ, FIntVector2 OriginLocalIJ, FVisionUnitData& VisionUnitData);
	//~ End Inline Helper Functions

public:
	//~ Begin Internal State Properties

	/// @brief 网格在世界空间中的尺寸（宽和高）。
	UPROPERTY(VisibleInstanceOnly)
	FVector2D GridSize = FVector2D::Zero();

	/// @brief 网格的分辨率（X和Y方向上的瓦片数量）。
	UPROPERTY(VisibleInstanceOnly)
	FIntPoint GridResolution = {};

	/// @brief 网格左下角在世界空间中的2D坐标。作为所有坐标转换的基准。
	UPROPERTY(VisibleInstanceOnly)
	FVector2D GridBottomLeftWorldLocation = FVector2D::Zero();

#if WITH_EDITORONLY_DATA
	/// @brief 【调试】用于可视化地形高度图的纹理。
	UPROPERTY(VisibleInstanceOnly, Category = "FogOfWar|Textures")
	TObjectPtr<UTexture2D> HeightmapTexture = nullptr;
#endif

	/// @brief 存储当前帧原始可见性数据的快照纹理。
	/// @details 值为1代表可见，0代表不可见。这是一个临时的、未经平滑处理的纹理。
	UPROPERTY(VisibleInstanceOnly, Category = "FogOfWar|Textures")
	TObjectPtr<UTexture2D> SnapshotTexture = nullptr;

	/// @brief 渲染管线第一阶段的RT。将SnapshotTexture的内容绘制到这里。
	UPROPERTY(VisibleInstanceOnly, Category = "FogOfWar|Textures")
	TObjectPtr<UTextureRenderTarget2D> VisibilityTextureRenderTarget = nullptr;

	/// @brief 渲染管线第二阶段的RT。存储经过时间插值（temporal interpolation）后的结果，实现平滑过渡。
	UPROPERTY(VisibleInstanceOnly, Category = "FogOfWar|Textures")
	TObjectPtr<UTextureRenderTarget2D> PreFinalVisibilityTextureRenderTarget = nullptr;

	/// @brief 渲染管线最终阶段的RT。存储经过超采样和最终调整后的高分辨率纹理，可供后期处理或UI使用。
	UPROPERTY(VisibleInstanceOnly, Category = "FogOfWar|Textures")
	TObjectPtr<UTextureRenderTarget2D> FinalVisibilityTextureRenderTarget = nullptr;

	/// @brief InterpolationMaterial的动态实例。
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> InterpolationMID;

	/// @brief AfterInterpolationMaterial的动态实例。
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> AfterInterpolationMID;

	/// @brief SuperSamplingMaterial的动态实例。
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> SuperSamplingMID;

	/// @brief PostProcessingMaterial的动态实例。
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> PostProcessingMID;

	/// @brief 存储所有瓦片（FTile）的核心数据数组。
	TArray<FTile> Tiles;

	/// @brief 用于将可见性数据写入纹理的共享缓冲区，避免重复分配内存。
	TArray<uint8> TextureDataBuffer;

	/// @brief DDA算法使用的栈，用于避免递归并减少内存分配开销。
	TArray<int> DDALocalIndexesStack;

	/// @brief 标记是否是第一次Tick。用于执行一些只需要在首次更新时进行的操作。
	bool bFirstTick = true;

	/// @brief 标记系统是否已激活。
	bool bActivated = false;
};