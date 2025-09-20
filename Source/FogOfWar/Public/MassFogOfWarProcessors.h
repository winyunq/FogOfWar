// Copyright Winyunq, 2025. All Rights Reserved.

#pragma once

#include "MassExecutionContext.h" // Required for FMassExecutionContext
#include "MassProcessor.h"
#include "MassRepresentationFragments.h" // For FMassVisibilityFragment
#include "Subsystems/MinimapDataSubsystem.h"
#include "MassFogOfWarProcessors.generated.h"

class AFogOfWar;

/**
 * @file MassFogOfWarProcessors.h
 * @brief 定义了驱动战争迷雾逻辑的Mass Processors。
 * @details 这些处理器负责查询实体并调用核心功能来更新战争迷雾状态。
 */

/**
 * @struct FFogOfWarMassHelpers
 * @brief 包含战争迷雾处理器共享的静态辅助函数。
 * @details 目的是将通用逻辑（如遍历实体块并调用AFogOfWar进行计算）提取出来，避免在多个Processor中重复代码。
 */
struct FOGOFWAR_API FFogOfWarMassHelpers
{
	/**
	 * @brief       处理单个实体块（Entity Chunk）中的所有实体。
	 * @details     遍历给定执行上下文（Context）中的所有实体，并为每个实体调用AFogOfWar主控Actor的视野更新函数。
	 *
	 * @param       Context                        数据类型: FMassExecutionContext&
	 * @details     Mass执行上下文，包含了当前正在处理的实体块信息。
	 * @param       FogOfWar                       数据类型: AFogOfWar*
	 * @details     指向场景中唯一的AFogOfWar主控Actor的指针。
	 */
	static void ProcessEntityChunk(FMassExecutionContext& Context, AFogOfWar* FogOfWar);
};

/**
 * @class UInitialVisionProcessor
 * @brief 为新生成的视野单位执行首次视野计算。
 * @details 此处理器在Mass处理流程的同步阶段（Sync）运行。
 * 它查询所有拥有视野能力（FMassVisionEntityTag）但尚未被初始化（无FMassVisionInitializedTag）的实体，
 * 为它们执行一次视野计算，并添加FMassVisionInitializedTag以防止重复计算。
 */
UCLASS()
class FOGOFWAR_API UInitialVisionProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UInitialVisionProcessor();

protected:
	/**
	 * @brief       初始化处理器。
	 * @details     在处理器开始运行时调用一次，用于获取对AFogOfWar主控Actor的引用。
	 *
	 * @param       Owner                          数据类型: UObject&
	 * @details     拥有此处理器的对象，通常是UMassSimulationSubsystem。
	 */
	virtual void Initialize(UObject& Owner) override;

	/**
	 * @brief       配置Mass实体查询。
	 * @details     设置处理器需要处理的实体原型（Archetype）。
	 *              查询条件为：包含FMassVisionEntityTag，但不包含FMassVisionInitializedTag。
	 */
	virtual void ConfigureQueries() override;

	/**
	 * @brief       执行处理器逻辑。
	 * @details     在每一帧（或根据设置的频率）对查询到的实体块执行此函数。
	 *              它会调用辅助函数ProcessEntityChunk来计算视野，并为处理过的实体添加FMassVisionInitializedTag。
	 *
	 * @param       EntityManager                  数据类型: FMassEntityManager&
	 * @details     Mass实体管理器，用于与实体系统交互。
	 * @param       Context                        数据类型: FMassExecutionContext&
	 * @details     当前的执行上下文。
	 */
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	/// @brief 指向场景中AFogOfWar主控Actor的指针，在Initialize时被缓存。
	TObjectPtr<AFogOfWar> FogOfWarActor;

	/// @brief 处理器使用的实体查询对象，在ConfigureQueries时被定义。
	FMassEntityQuery EntityQuery;
};

/**
 * @class UVisionProcessor
 * @brief 为位置发生变化的视野单位更新视野。
 * @details 此处理器在Mass处理流程的同步阶段（Sync）运行。
 * 它只查询那些位置发生了变化（拥有FMassLocationChangedTag）的视野提供者实体。
 * 这是系统的核心性能优化，确保只有移动中的单位才触发昂贵的视野更新计算。
 */
UCLASS()
class FOGOFWAR_API UVisionProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UVisionProcessor();

protected:
	/**
	 * @brief       初始化处理器。
	 * @details     在处理器开始运行时调用一次，用于获取对AFogOfWar主控Actor的引用。
	 *
	 * @param       Owner                          数据类型: UObject&
	 * @details     拥有此处理器的对象，通常是UMassSimulationSubsystem。
	 */
	virtual void Initialize(UObject& Owner) override;

	/**
	 * @brief       配置Mass实体查询。
	 * @details     设置处理器需要处理的实体原型（Archetype）。
	 *              查询条件为：包含FMassVisionEntityTag和FMassLocationChangedTag。
	 */
	virtual void ConfigureQueries() override;

	/**
	 * @brief       执行处理器逻辑。
	 * @details     在每一帧（或根据设置的频率）对查询到的实体块执行此函数。
	 *              它会调用辅助函数ProcessEntityChunk来更新移动单位的视野。
	 *
	 * @param       EntityManager                  数据类型: FMassEntityManager&
	 * @details     Mass实体管理器，用于与实体系统交互。
	 * @param       Context                        数据类型: FMassExecutionContext&
	 * @details     当前的执行上下文。
	 */
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	/// @brief 指向场景中AFogOfWar主控Actor的指针，在Initialize时被缓存。
	TObjectPtr<AFogOfWar> FogOfWarActor;

	/// @brief 处理器使用的实体查询对象，在ConfigureQueries时被定义。
	FMassEntityQuery EntityQuery;
};

/**
 * @class UDebugStressTestProcessor
 * @brief 【调试】强制为所有可见单位添加 FMassLocationChangedTag 以进行压力测试。
 * @details 当 AFogOfWar 的 bDebugStressTestIgnoreCache 为 true 时，此处理器会运行。
 * 它在 UVisionProcessor 之前执行，确保所有可见单位都能被后续的视野计算处理器捕获。
 */
UCLASS()
class FOGOFWAR_API UDebugStressTestProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UDebugStressTestProcessor();

protected:
	virtual void Initialize(UObject& Owner) override;
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	TObjectPtr<AFogOfWar> FogOfWarActor;
	FMassEntityQuery EntityQuery;
};

/**
 * @class UMinimapDataCollectorProcessor
 * @brief 收集小地图所需的数据并缓存到 UMinimapDataSubsystem 中。
 */
UCLASS()
class FOGOFWAR_API UMinimapDataCollectorProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMinimapDataCollectorProcessor();

protected:
	virtual void Initialize(UObject& Owner) override;
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	// 指向数据仓库的指针
	TObjectPtr<UMinimapDataSubsystem> MinimapDataSubsystem;

	// 查询1：获取所有视野源
	FMassEntityQuery VisionSourcesQuery;

	// 查询2：获取所有需要在小地图上表示的单位图标
	FMassEntityQuery RepresentationQuery;
};

