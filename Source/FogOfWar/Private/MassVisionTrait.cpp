// Copyright Winyunq, 2025. All Rights Reserved.

#include "MassVisionTrait.h"
#include "MassEntityTemplateRegistry.h"
#include "MassRepresentationFragments.h" // For FMassRepresentationFragment

void UMassVisionTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	BuildContext.AddFragment_GetRef<FMassPreviousVisionFragment>();

	// 根据配置添加视野相关的Fragment和Tag
	if (SightRadius > 0.0f)
	{
		BuildContext.AddTag<FMassVisionEntityTag>();
		FMassVisionFragment& VisionFragment = BuildContext.AddFragment_GetRef<FMassVisionFragment>();
		VisionFragment.SightRadius = SightRadius;

		// 【核心修改】单位诞生时，即标记为“已改变”，以便更新器在第一帧处理它
		BuildContext.AddTag<FMassLocationChangedTag>();
	}

	// 根据配置添加小地图表示相关的Fragment和Tag
	if (bShouldBeRepresentedOnMinimap)
	{
		FMassMinimapRepresentationFragment& RepresentationFragment = BuildContext.AddFragment_GetRef<FMassMinimapRepresentationFragment>();
		RepresentationFragment.IconColor = MinimapIconColor;
		RepresentationFragment.IconSize = MinimapIconSize;
		
		BuildContext.AddFragment<FMassPreviousMinimapCellFragment>(); // Add fragment for the observer

		// 【核心修改】单位诞生时，即标记为“已改变”，以便更新器在第一帧处理它
		BuildContext.AddTag<FMinimapCellChangedTag>();

		if (bAlwaysVisibleOnMinimap)
		{
			BuildContext.AddTag<FMassMinimapVisibleTag>();
		}
	}
}
