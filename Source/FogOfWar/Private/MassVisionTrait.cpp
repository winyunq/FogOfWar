// Copyright Winyunq, 2025. All Rights Reserved.

#include "MassVisionTrait.h"
#include "MassEntityTemplateRegistry.h"

void UMassVisionTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	// Add the vision fragment with the parameters set in the editor
	BuildContext.AddFragment_GetRef<FMassVisionFragment>() = VisionParams;

	// Add the tag to identify this entity as a vision provider
	BuildContext.AddTag<FMassVisionEntityTag>();

	// Add the fragment needed to store the previous frame's vision data
	BuildContext.AddFragment<FMassPreviousVisionFragment>();
	
	// Add the tag to mark this entity as something that can be seen by others
	BuildContext.AddTag<FMassVisibleEntityTag>();
}
