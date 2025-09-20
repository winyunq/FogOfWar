// Copyright Winyunq, 2025. All Rights Reserved.

#pragma once

#include "MassEntityTypes.h"
#include "MassCommonFragments.h"
#include "MassFogOfWarFragments.generated.h"

/**
 * @file MassFogOfWarFragments.h
 * @brief 定义了战争迷雾系统在Mass框架中使用的所有Fragments和Tags。
 * @details 这些数据结构是实体（Entity）与战争迷雾系统交互的基础。
 */

/**
 * @enum ETileState
 * @brief 表示单个瓦片（Tile）的可见性状态。
 * @details 用于在视野计算的缓存中记录每个瓦片是否可见。
 */
UENUM()
enum class ETileState : uint8
{
	/// @brief 状态未知，通常是初始状态。
	Unknown,
	/// @brief 不可见，在视野范围之外或被遮挡。
	NotVisible,
	/// @brief 可见，在视野范围之内且未被遮挡。
	Visible
};

/**
 * @struct FVisionUnitData
 * @brief 存储单个视野单位（Vision Unit）的内部缓存数据。
 * @details 这是一个核心优化结构体。它不直接存储在实体上，而是由AFogOfWar主控Actor持有和计算。
 * 它为每个视野单位缓存了一个局部的、以该单位为中心的视野计算结果，
 * 避免了在每次更新时都对整个全局网格进行操作，从而大幅提升性能。
 */
USTRUCT()
struct FOGOFWAR_API FVisionUnitData
{
	GENERATED_BODY()

	/// @brief 局部区域缓存网格的分辨率（边长）。
	UPROPERTY()
	int LocalAreaTilesResolution = 0;

	/// @brief 视野半径在网格空间中的大小。
	UPROPERTY()
	float GridSpaceRadius = 0.0f;

	/// @brief 局部区域缓存网格左上角在全局网格中的坐标(IJ)。
	UPROPERTY()
	FIntVector2 LocalAreaCachedMinIJ = FIntVector2::ZeroValue;

	/// @brief 局部区域内所有瓦片状态的缓存数组。
	UPROPERTY()
	TArray<ETileState> LocalAreaTilesCachedStates;

	/// @brief 缓存的原点在全局网格中的一维索引。
	UPROPERTY()
	int CachedOriginGlobalIndex = 0;

	/// @brief 标记此结构体是否已包含有效的缓存数据。
	UPROPERTY()
	bool bHasCachedData = false;

	/// @brief 检查是否已有缓存数据。
	FORCEINLINE bool HasCachedData() const { return bHasCachedData; }
	/// @brief 根据局部二维坐标获取一维索引。
	FORCEINLINE int GetLocalIndex(FIntVector2 IJ) const { return IJ.X * LocalAreaTilesResolution + IJ.Y; }
	/// @brief 根据局部一维索引获取二维坐标。
	FORCEINLINE FIntVector2 GetLocalIJ(int LocalIndex) { return { LocalIndex / LocalAreaTilesResolution, LocalIndex % LocalAreaTilesResolution }; }
	/// @brief 检查局部二维坐标是否有效。
	FORCEINLINE bool IsLocalIJValid(FIntVector2 IJ) { return (IJ.X >= 0) & (IJ.Y >= 0) & (IJ.X < LocalAreaTilesResolution) & (IJ.Y < LocalAreaTilesResolution); }
	/// @brief 根据局部一维索引获取瓦片状态。
	FORCEINLINE ETileState& GetLocalTileState(int LocalIndex) { return LocalAreaTilesCachedStates[LocalIndex]; }
	/// @brief 根据局部二维坐标获取瓦片状态。
	FORCEINLINE ETileState& GetLocalTileState(FIntVector2 IJ) { checkSlow(IsLocalIJValid(IJ)); return GetLocalTileState(GetLocalIndex(IJ)); }
	/// @brief 将局部二维坐标转换为全局二维坐标。
	FORCEINLINE FIntVector2 LocalToGlobal(FIntVector2 LocalIJ) const { return LocalAreaCachedMinIJ + LocalIJ; }
	/// @brief 将全局二维坐标转换为局部二维坐标。
	FORCEINLINE FIntVector2 GlobalToLocal(FIntVector2 GlobalIJ) const { return GlobalIJ - LocalAreaCachedMinIJ; }
};

/**
 * @struct FMassVisibleEntityTag
 * @brief 标记一个实体是“可见的”或“可被揭示的”。
 * @details 拥有此标签的实体，其自身的模型或图标可以被其他视野提供者揭示在战争迷雾中。
 * 它本身不提供视野。例如，一个普通的士兵单位。
 */
USTRUCT()
struct FOGOFWAR_API FMassVisibleEntityTag : public FMassTag
{
	GENERATED_BODY()
};

/**
 * @struct FMassVisionEntityTag
 * @brief 标记一个实体是“视野提供者”。
 * @details 拥有此标签的实体能够主动揭示其周围的战争迷雾区域。例如，一个侦察兵或一个岗哨。
 */
USTRUCT()
struct FOGOFWAR_API FMassVisionEntityTag : public FMassTag
{
	GENERATED_BODY()
};

/**
 * @struct FMassStationaryTag
 * @brief 标记一个实体是固定不动的。
 * @details 拥有此标签的实体被视为静态。系统会在初始化时计算一次其视野，然后缓存结果。
 * 只要该实体不被销毁，其视野贡献将不再重复计算，这是针对建筑等静态单位的关键性能优化。
 */
USTRUCT()
struct FOGOFWAR_API FMassStationaryTag : public FMassTag
{
	GENERATED_BODY()
};

/**
 * @struct FMassMinimapVisibleTag
 * @brief 标记一个实体在小地图上始终可见。
 * @details 拥有此标签的实体，其图标会无视战争迷雾的状态，始终在小地图上显示。
 * 通常用于任务目标或重要建筑。
 */
USTRUCT()
struct FOGOFWAR_API FMassMinimapVisibleTag : public FMassTag
{
	GENERATED_BODY()
};

/**
 * @struct FMassLocationChangedTag
 * @brief 标记一个实体的位置自上一帧起已发生改变。
 * @details 这是一个由Mass框架的观察器（Observer）动态添加和移除的临时标签。
 * 视野更新处理器（UVisionProcessor）只查询带有此标签的实体，从而确保只为移动中的单位重新计算视野。
 */
USTRUCT()
struct FOGOFWAR_API FMassLocationChangedTag : public FMassTag
{
	GENERATED_BODY()
};

/**
 * @struct FMassVisionInitializedTag
 * @brief 标记一个视野提供者已经完成了首次视野计算。
 * @details 这是一个内部状态标签，用于确保每个视野单位的初始视野计算只执行一次，
 * 防止在连续的帧中重复执行昂贵的初始化逻辑。
 */
USTRUCT()
struct FOGOFWAR_API FMassVisionInitializedTag : public FMassTag
{
	GENERATED_BODY()
};

/**
 * @struct FMassVisionFragment
 * @brief 存储视野提供者的核心参数。
 * @details 此Fragment包含了实体作为视野提供者所需的数据，例如视野半径。
 * 这些数据通常由 UMassVisionTrait 在原型编辑器中配置。
 */
USTRUCT()
struct FOGOFWAR_API FMassVisionFragment : public FMassFragment
{
	GENERATED_BODY()

	/// @brief 视野半径（单位：厘米）。
	/// @details 定义了该实体能够揭示周围区域的最大距离。
	UPROPERTY(EditAnywhere, Category = "Fog of War", meta = (ClampMin = 0.0f, UIMin = 0.0f))
	float SightRadius = 1000.0f;
};

/**
 * @struct FMassPreviousVisionFragment
 * @brief 存储实体在上一帧的视野缓存数据。
 * @details 当一个单位移动时，为了正确更新战争迷雾，系统需要先“擦除”它上一帧的视野贡献，然后再应用新一帧的视野。
 * 此Fragment就用于存储上一帧的FVisionUnitData，以便处理器能够执行“擦除”操作。
 */
USTRUCT()
struct FOGOFWAR_API FMassPreviousVisionFragment : public FMassFragment
{
	GENERATED_BODY()

	/// @brief 上一帧的视野单元缓存数据。
	/// @details 包含了上一帧视野范围内的瓦片状态和相关信息，用于清除旧视野。
	UPROPERTY()
	FVisionUnitData PreviousVisionData;
};
