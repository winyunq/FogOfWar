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
UCLASS(BlueprintType, Blueprintable , meta = (DisplayName = "Winyunq|视野和迷雾"))
class FOGOFWAR_API UMassVisionTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

public:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;

protected:
	// --- 视野属性 (Vision Properties) ---
	/** 该单位的视野半径（用于计算战争迷雾）。设为0则不提供视野。*/
	UPROPERTY(EditAnywhere, Category = "Vision", meta = (ClampMin = "0.0"))
	float SightRadius = 1024.0f;

	// --- 小地图表示属性 (Minimap Representation Properties) ---
	/** 是否在小地图上显示该单位的图标。*/
	UPROPERTY(EditAnywhere, Category = "Minimap")
	bool bShouldBeRepresentedOnMinimap = true;

	/** 图标的颜色。*/
	UPROPERTY(EditAnywhere, Category = "Minimap", meta = (EditCondition = "bShouldBeRepresentedOnMinimap"))
	FLinearColor MinimapIconColor = FLinearColor::Green;

	/** 图标的尺寸（小地图像素，半径）。*/
	UPROPERTY(EditAnywhere, Category = "Minimap", meta = (EditCondition = "bShouldBeRepresentedOnMinimap", ClampMin = "0.0"))
	float MinimapIconSize = 0.5;

	/** （高级）是否让该单位的图标无视战争迷雾，始终在小地图上可见？（例如任务单位）*/
	UPROPERTY(EditAnywhere, Category = "Minimap", meta = (EditCondition = "bShouldBeRepresentedOnMinimap"))
	bool bAlwaysVisibleOnMinimap = false;
};
