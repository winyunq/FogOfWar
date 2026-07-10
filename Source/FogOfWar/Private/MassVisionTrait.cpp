// Copyright Winyunq, 2025. All Rights Reserved.

#include "MassVisionTrait.h"
#include "FogOfWarMassBinding.h"
#include "MassEntityTemplateRegistry.h"

void UMassVisionTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
#if !FOW_USE_MASSBATTLE_BINDING
	BuildContext.AddFragment_GetRef<FFogOfWarLocationFragment>();
	BuildContext.AddFragment_GetRef<FFogOfWarTeamFragment>();
#endif

	BuildContext.AddFragment_GetRef<FMassPreviousVisionFragment>();

	// 根据配置添加视野相关的Fragment和Tag
	if (SightRadius > 0.0f)
	{
		BuildContext.AddTag<FMassVisionEntityTag>();
		FMassVisionFragment& VisionFragment = BuildContext.AddFragment_GetRef<FMassVisionFragment>();
		VisionFragment.SightRadius = SightRadius;
	}

}
