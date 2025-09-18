// Copyright Winyunq, 2025. All Rights Reserved.

#pragma once

#include "MassEntityTraitBase.h"
#include "MassFogOfWarFragments.h" // Include fragments that the trait will use
#include "MassVisionTrait.generated.h"

/**
 * A Mass Trait to add vision capabilities to an entity.
 * This allows for easy configuration of vision properties in the editor.
 */
UCLASS(meta = (DisplayName = "视野和迷雾 (Vision & Fog)"))
class FOGOFWAR_API UMassVisionTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

public:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;

protected:
	/** Vision parameters for this entity, editable in the archetype editor. */
	UPROPERTY(EditAnywhere, Category = "Fog of War")
	FMassVisionFragment VisionParams;
};
