// Copyright Winyunq, 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassSubsystemBase.h"
#include "MinimapDataSubsystem.generated.h"

class AFogOfWar;

/**
 * 代表小地图网格上的单个瓦片数据。
 * Represents a single tile on the minimap grid.
 */
USTRUCT()
struct FOGOFWAR_API FMinimapTile
{
	GENERATED_BODY()

	/** 当前瓦片内的单位数量 */
	UPROPERTY()
	int32 UnitCount = 0;

	/** 最后一个进入该瓦片的单位颜色 (用于调试或显示) */
	UPROPERTY()
	FLinearColor Color = FLinearColor::Black;

	/** 该瓦片内单位的最大视野半径 */
	UPROPERTY()
	float MaxSightRadius = 0.0f;

	/** 该瓦片内单位的最大图标尺寸 */
	UPROPERTY()
	float MaxIconSize = 0.0f;
};

/**
 * UMinimapDataSubsystem
 * 
 * 全局子系统，负责管理小地图和高精度迷雾网格的数据及坐标转换。
 * 它是所有网格计算的单一真理来源 (Single Source of Truth)。引用 AFogOfWar 在激活时注册的参数。
 */
UCLASS(Config = MassBattle, defaultconfig)
class FOGOFWAR_API UMinimapDataSubsystem : public UMassSubsystemBase
{
	GENERATED_BODY()

public:
	//~ Begin UMassSubsystemBase Interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End UMassSubsystemBase Interface

	/** 提供对着色器实例的静态直接访问，用于高性能代码路径。 */
	static FORCEINLINE UMinimapDataSubsystem* Get() { return SingletonInstance; }

	// Deprecated: UpdateVisionGridParameters removed for strict decoupling.

	/** 由小地图 UI 组件调用以设置其所需的分辨率。 */
	void SetMinimapResolution(const FIntPoint& NewResolution);

	/** 
	 * [Path B] 直接查询 MassBattleHashGrid 以更新小地图瓦片数据。
	 * 采用三层 LOD 遍历 (Map -> Block -> Cell) 以实现 O(Occupied) 性能。
	 */
	UFUNCTION(BlueprintCallable, Category = "FogOfWar|Minimap")
	void UpdateMinimapFromHashGrid(FVector CenterLocation, int32 BlockRadius = 8);

	/**
	 * 手动初始化小地图网格参数 (通常由 AMinimapVolume 等 Actor 调用)。
	 * @param InGridOrigin 世界坐标原点 (GridBottomLeft)
	 * @param InGridSize 世界空间总尺寸
	 * @param InResolution 纹理分辨率 (Tile Count)
	 */
	UFUNCTION(BlueprintCallable, Category = "FogOfWar|Minimap")
	void InitMinimapGrid(const FVector2D& InGridOrigin, const FVector2D& InGridSize, const FIntPoint& InResolution);

public:
	/** 标志位，指示子系统是否已接收到有效的网格参数并准备就绪。 */
	
	//~ Common Grid Properties (Shared by Vision and Minimap)
	
	// 世界空间下的网格总尺寸 (World Size)
	// 如果不使用 AFogOfWar，需手动配置此项以定义小地图覆盖范围。
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category="Minimap|Config")
	FVector2D GridSize = FVector2D(409600.0f, 409600.0f); // Default 4km x 4km

	// 网格左下角的世界坐标 (World Origin)
	// 通常为 -(GridSize / 2) 以使 (0,0) 为中心。
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category="Minimap|Config")
	FVector2D GridBottomLeftWorldLocation = FVector2D(-204800.0f, -204800.0f);
	
	//~ Vision Grid Properties (High-Resolution for Fog of War calculation)
	
	// 高精度视野网格的单瓦片尺寸
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category="Minimap|Config")
	float VisionTileSize = 100.0f;

	// 高精度视野网格的分辨率
	UPROPERTY(Transient)
	FIntPoint VisionGridResolution = FIntPoint::ZeroValue;
	
	//~ Minimap Grid Properties (Low-Resolution for UI)
	
	// 小地图网格的分辨率
    UPROPERTY(Transient)
    FIntPoint MinimapGridResolution = FIntPoint(256, 256);

	// 小地图单瓦片尺寸
	UPROPERTY(Transient)
	FVector2D MinimapTileSize = FVector2D::Zero();

	// 小地图瓦片数据数组
	TArray<FMinimapTile> MinimapTiles;
	
public:
	//~ Begin Static Vision Grid Conversion Functions
	// 静态视野网格转换函数
	
	static FORCEINLINE FVector2f ConvertWorldSpaceLocationToVisionGridSpace_Static(const FVector2D& WorldLocation);
	static FORCEINLINE FIntPoint ConvertVisionGridLocationToTileIJ_Static(const FVector2f& GridLocation);
	static FORCEINLINE FIntPoint ConvertWorldLocationToVisionTileIJ_Static(const FVector2D& WorldLocation);
	static FORCEINLINE FVector2D ConvertVisionTileIJToTileCenterWorldLocation_Static(const FIntPoint& IJ);
	static FORCEINLINE int32 GetVisionGridGlobalIndex_Static(FIntPoint IJ);
	static FORCEINLINE FIntPoint GetVisionGridTileIJ_Static(int32 GlobalIndex);
	static FORCEINLINE bool IsVisionGridIJValid_Static(FIntPoint IJ);
	
	//~ Begin Static Minimap Grid Conversion Functions
	// 静态小地图网格转换函数
	
	static FORCEINLINE FIntPoint ConvertWorldLocationToMinimapTileIJ_Static(const FVector2D& WorldLocation);
	static FORCEINLINE FVector2D ConvertMinimapTileIJToWorldLocation_Static(const FIntPoint& TileIJ);
	
private:
	// 小地图转换的私有辅助函数
	static FORCEINLINE FVector2f ConvertWorldSpaceLocationToMinimapGridSpace_Static(const FVector2D& WorldLocation);
	static FORCEINLINE FIntPoint ConvertMinimapGridLocationToTileIJ_Static(const FVector2f& GridLocation);

private:
	/** 单例实例指针 */
	static UMinimapDataSubsystem* SingletonInstance;
};

//~ Begin Inline Implementations of Static Functions

FORCEINLINE FVector2f UMinimapDataSubsystem::ConvertWorldSpaceLocationToVisionGridSpace_Static(const FVector2D& WorldLocation)
{
	check(SingletonInstance);
	return FVector2f((WorldLocation - SingletonInstance->GridBottomLeftWorldLocation) / SingletonInstance->VisionTileSize);
}

FORCEINLINE FIntPoint UMinimapDataSubsystem::ConvertVisionGridLocationToTileIJ_Static(const FVector2f& GridLocation)
{
	return FIntPoint(FMath::FloorToInt(GridLocation.X), FMath::FloorToInt(GridLocation.Y));
}

FORCEINLINE FIntPoint UMinimapDataSubsystem::ConvertWorldLocationToVisionTileIJ_Static(const FVector2D& WorldLocation)
{
	check(SingletonInstance);
	const FVector2f GridLocation = FVector2f((WorldLocation - SingletonInstance->GridBottomLeftWorldLocation) / SingletonInstance->VisionTileSize);
	return FIntPoint(FMath::FloorToInt(GridLocation.X), FMath::FloorToInt(GridLocation.Y));
}

FORCEINLINE FVector2D UMinimapDataSubsystem::ConvertVisionTileIJToTileCenterWorldLocation_Static(const FIntPoint& IJ)
{
	check(SingletonInstance);
	return SingletonInstance->GridBottomLeftWorldLocation + (FVector2D(IJ) + 0.5f) * SingletonInstance->VisionTileSize;
}

FORCEINLINE int32 UMinimapDataSubsystem::GetVisionGridGlobalIndex_Static(FIntPoint IJ)
{
	check(SingletonInstance);
	return IJ.X * SingletonInstance->VisionGridResolution.Y + IJ.Y;
}

FORCEINLINE FIntPoint UMinimapDataSubsystem::GetVisionGridTileIJ_Static(int32 GlobalIndex)
{
	check(SingletonInstance);
	return { GlobalIndex / SingletonInstance->VisionGridResolution.Y, GlobalIndex % SingletonInstance->VisionGridResolution.Y };
}

FORCEINLINE bool UMinimapDataSubsystem::IsVisionGridIJValid_Static(FIntPoint IJ)
{
	check(SingletonInstance);
	return IJ.X >= 0 && IJ.Y >= 0 && IJ.X < SingletonInstance->VisionGridResolution.X && IJ.Y < SingletonInstance->VisionGridResolution.Y;
}

FORCEINLINE FIntPoint UMinimapDataSubsystem::ConvertWorldLocationToMinimapTileIJ_Static(const FVector2D& WorldLocation)
{
	check(SingletonInstance);
	const FVector2f GridLocation = ConvertWorldSpaceLocationToMinimapGridSpace_Static(WorldLocation);
	return ConvertMinimapGridLocationToTileIJ_Static(GridLocation);
}

FORCEINLINE FVector2D UMinimapDataSubsystem::ConvertMinimapTileIJToWorldLocation_Static(const FIntPoint& TileIJ)
{
	check(SingletonInstance);
	return SingletonInstance->GridBottomLeftWorldLocation + (FVector2D(TileIJ) + 0.5f) * SingletonInstance->MinimapTileSize;
}

FORCEINLINE FVector2f UMinimapDataSubsystem::ConvertWorldSpaceLocationToMinimapGridSpace_Static(const FVector2D& WorldLocation)
{
	check(SingletonInstance);
	return FVector2f((WorldLocation - SingletonInstance->GridBottomLeftWorldLocation) / SingletonInstance->MinimapTileSize);
}

FORCEINLINE FIntPoint UMinimapDataSubsystem::ConvertMinimapGridLocationToTileIJ_Static(const FVector2f& GridLocation)
{
	return FIntPoint(FMath::FloorToInt(GridLocation.X), FMath::FloorToInt(GridLocation.Y));
}
