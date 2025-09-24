// Copyright Winyunq, 2025. All Rights Reserved.

#pragma once

#include "MassExecutionContext.h" // Required for FMassExecutionContext
#include "MassProcessor.h"
#include "Subsystems/MinimapDataSubsystem.h"
#include "MassMinimapProcessors.generated.h"

class AFogOfWar;

/**
 * @class UMinimapAddProcessor
 * @brief 观察新实体的创建，并将其添加到 UMinimapDataSubsystem 中。
 */
UCLASS()
class FOGOFWAR_API UMinimapAddProcessor : public UMassObserverProcessor
{
	GENERATED_BODY()

public:
	UMinimapAddProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};


/**
 * @class UMinimapUpdateProcessor
 * @brief 收集小地图所需的数据并缓存到 UMinimapDataSubsystem 中。
 */
UCLASS()
class FOGOFWAR_API UMinimapUpdateProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMinimapUpdateProcessor();

protected:
	virtual void Initialize(UObject& Owner) override;
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	// 指向数据仓库的指针
	TObjectPtr<UMinimapDataSubsystem> MinimapDataSubsystem;

	// 查询：获取所有需要在小地图上表示的单位图标
	FMassEntityQuery RepresentationQuery;
};

/**
 * @class UMinimapObserverProcessor
 * @brief [OPTIMIZATION] 观察实体位置变化，并在实体跨越小地图格子时添加Tag。
 */
UCLASS()
class FOGOFWAR_API UMinimapObserverProcessor : public UMassObserverProcessor
{
	GENERATED_BODY()

public:
	UMinimapObserverProcessor();

protected:
	virtual void Initialize(UObject& Owner) override;
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	TObjectPtr<AFogOfWar> FogOfWarActor;
	FMassEntityQuery EntityQuery;
};
