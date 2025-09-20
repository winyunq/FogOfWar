// Copyright Winyunq, 2025. All Rights Reserved.

#pragma once

#include "MassEntityTraitBase.h"
#include "MassFogOfWarFragments.h"
#include "MassVisionTrait.generated.h"

/**
 * @file MassVisionTrait.h
 * @brief 定义了为Mass实体添加视野能力的Trait。
 */

/**
 * @class UMassVisionTrait
 * @brief 为实体添加视野能力的Mass Trait。
 * @details 该Trait用于在Mass原型编辑器中，为实体模板添加战争迷雾系统所需的各种组件。
 * 它会将FMassVisionFragment（视野参数）、FMassPreviousVisionFragment（用于清除旧视野）、
 * FMassVisionEntityTag（标记为视野提供者）和FMassVisibleEntityTag（标记为可被看见）添加到实体上。
 */
UCLASS(meta = (DisplayName = "Winyunq|视野和迷雾"))
class FOGOFWAR_API UMassVisionTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

public:
	/**
	 * @brief       在构建实体模板时修改模板。
	 * @details     此函数是Trait的核心，在Mass实体模板创建时被调用。
	 *              它负责向模板添加所有必要的Fragments和Tags。
	 *
	 * @param       BuildContext                   数据类型: FMassEntityTemplateBuildContext&
	 * @details     实体模板的构建上下文，用于添加Fragments, Tags等。
	 * @param       World                          数据类型: const UWorld&
	 * @details     当前世界的引用。
	 */
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;

protected:
	/// @brief 视野参数
	/// @details 在Mass原型编辑器中配置的视野参数。
	/// 这些参数将被作为 FMassVisionFragment 添加到实体上。
	UPROPERTY(EditAnywhere, Category = "Fog of War")
	FMassVisionFragment VisionParams;
};
