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
	}

	// 根据配置添加小地图表示相关的Fragment和Tag
	if (bShouldBeRepresentedOnMinimap)
	{
		FMassMinimapRepresentationFragment& RepresentationFragment = BuildContext.AddFragment_GetRef<FMassMinimapRepresentationFragment>();
		RepresentationFragment.IconColor = MinimapIconColor;
		RepresentationFragment.IconSize = MinimapIconSize;

		if (bAlwaysVisibleOnMinimap)
		{
			BuildContext.AddTag<FMassMinimapVisibleTag>();
		}
	}
}
