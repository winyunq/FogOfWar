// Copyright Winyunq, 2025. All Rights Reserved.

#pragma once

#include "MassEntityTypes.h"
#include "MassCommonFragments.h"
#include "GeminiMassFogOfWarFragments.generated.h"

// Internal struct to hold vision data for a single unit
UENUM()
enum class ETileState : uint8
{
	Unknown,
	NotVisible,
	Visible
};

USTRUCT()
struct FOGOFWAR_API FVisionUnitData
{
	GENERATED_BODY()

	UPROPERTY()
	int LocalAreaTilesResolution = 0;

	UPROPERTY()
	float GridSpaceRadius = 0.0f;

	UPROPERTY()
	FIntVector2 LocalAreaCachedMinIJ;

	UPROPERTY()
	TArray<ETileState> LocalAreaTilesCachedStates;

	UPROPERTY()
	int CachedOriginGlobalIndex = 0;

	UPROPERTY()
	bool bHasCachedData = false;

	FORCEINLINE bool HasCachedData() const { return bHasCachedData; }
	FORCEINLINE int GetLocalIndex(FIntVector2 IJ) const { return IJ.X * LocalAreaTilesResolution + IJ.Y; }
	FORCEINLINE FIntVector2 GetLocalIJ(int LocalIndex) { return { LocalIndex / LocalAreaTilesResolution, LocalIndex % LocalAreaTilesResolution }; }
	FORCEINLINE bool IsLocalIJValid(FIntVector2 IJ) { return (IJ.X >= 0) & (IJ.Y >= 0) & (IJ.X < LocalAreaTilesResolution) & (IJ.Y < LocalAreaTilesResolution); }
	FORCEINLINE ETileState& GetLocalTileState(int LocalIndex) { return LocalAreaTilesCachedStates[LocalIndex]; }
	FORCEINLINE ETileState& GetLocalTileState(FIntVector2 IJ) { checkSlow(IsLocalIJValid(IJ)); return GetLocalTileState(GetLocalIndex(IJ)); }
	FORCEINLINE FIntVector2 LocalToGlobal(FIntVector2 LocalIJ) const { return LocalAreaCachedMinIJ + LocalIJ; }
	FORCEINLINE FIntVector2 GlobalToLocal(FIntVector2 GlobalIJ) const { return GlobalIJ - LocalAreaCachedMinIJ; }
};

/**
 * @struct FGeminiMassVisibleEntityTag
 * @brief 标记可被视野系统揭示的实体。
 * @details 拥有此标签的Mass实体表示它们可以被战争迷雾系统检测到并显示其可见性状态。
 */
USTRUCT()
struct FOGOFWAR_API FGeminiMassVisibleEntityTag : public FMassTag
{
	GENERATED_BODY()
};

/**
 * @struct FGeminiMassVisionEntityTag
 * @brief 标记提供视野的实体。
 * @details 拥有此标签的Mass实体表示它们能够向战争迷雾系统提供视野，揭示周围区域。
 */
USTRUCT()
struct FOGOFWAR_API FGeminiMassVisionEntityTag : public FMassTag
{
	GENERATED_BODY()
};

/**
 * @struct FGeminiMassStationaryTag
 * @brief 标记不移动的实体。
 * @details 拥有此标签的Mass实体被视为静态的，其视野信息在初始化后会被缓存，从而实现显著的性能优化。
 */
USTRUCT()
struct FOGOFWAR_API FGeminiMassStationaryTag : public FMassTag
{
	GENERATED_BODY()
};

/**
 * @struct FGeminiMassMinimapVisibleTag
 * @brief 标记始终在小地图上可见的实体。
 * @details 拥有此标签的Mass实体无论战争迷雾状态如何，都将在小地图上显示。
 */
USTRUCT()
struct FOGOFWAR_API FGeminiMassMinimapVisibleTag : public FMassTag
{
	GENERATED_BODY()
};

/**
 * @struct FGeminiMassLocationChangedTag
 * @brief 标记自上一帧以来位置发生变化的实体。
 * @details 这是一个临时标签，用于驱动视野重新计算，确保只有移动的实体才触发更新。
 */
USTRUCT()
struct FOGOFWAR_API FGeminiMassLocationChangedTag : public FMassTag
{
	GENERATED_BODY()
};

/**
 * @struct FGeminiMassVisionFragment
 * @brief 存储实体的视野相关数据。
 * @details 此Fragment包含实体提供视野所需的各种参数，例如视野半径。
 */
USTRUCT()
struct FOGOFWAR_API FGeminiMassVisionFragment : public FMassFragment
{
	GENERATED_BODY()

	/// @brief 视野圆的半径。
	/// @details 定义了实体能够揭示周围区域的距离。
	UPROPERTY(EditAnywhere, Category = "Fog of War", meta = (ClampMin = 0.0f, UIMin = 0.0f))
	float SightRadius = 1000.0f;
};

/**
 * @struct FGeminiMassPreviousVisionFragment
 * @brief 存储实体上一帧的视野数据。
 * @details 用于在计算新视野之前，清除实体在上一帧对战争迷雾的贡献，解决视野残留问题。
 */
USTRUCT()
struct FOGOFWAR_API FGeminiMassPreviousVisionFragment : public FMassFragment
{
	GENERATED_BODY()

	/// @brief 上一帧的视野单元数据。
	/// @details 包含上一帧视野范围内的瓦片状态和相关信息。
	UPROPERTY()
	FVisionUnitData PreviousVisionData;
};
