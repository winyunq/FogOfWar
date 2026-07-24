// Copyright Winyunq, 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "MassBattleFogVisionSourceFragment.generated.h"

/** Per-unit-type rendering policy; none of these options grants terrain vision. */
UENUM(BlueprintType)
enum class EMassBattleFogVisibilityPolicy : uint8
{
	/** Hidden in deep fog; attack exposure only reveals the attacking entity. */
	Standard UMETA(DisplayName = "Standard"),

	/** Always submitted inside the camera window, but rendered as fog-visible/dark outside true vision. */
	AlwaysFogVisible UMETA(DisplayName = "Always Visible In Fog"),

	/** StarCraft-style building memory: freeze the last truly visible render snapshot. */
	RememberLastSeen UMETA(DisplayName = "Remember Last Seen (Building)")
};

/**
 * Optional per-unit-type vision policy for MassBattleFrame templates.
 *
 * Add this to FMassBattleTemplate::ConstSharedFragments. Because it is a const
 * shared fragment, one value is stored per archetype/config rather than once
 * per entity. When omitted, enabled friendly and allied units use the
 * controller's default vision radius.
 *
 * This contains authored type policy only; the current state still comes
 * from the shared world mask and is never stored as a per-frame fragment.
 */
USTRUCT(BlueprintType)
struct FOGOFWAR_API FMassBattleFogVisionSourceFragment : public FMassConstSharedFragment
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|Vision")
	bool bProvidesVision = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|Visibility")
	EMassBattleFogVisibilityPolicy VisibilityPolicy = EMassBattleFogVisibilityPolicy::Standard;
};

/**
 * Mutable per-entity snapshot required only by RememberLastSeen unit types.
 * Add it to FMassBattleTemplate::Fragments for building archetypes. It is read
 * and written only by the final-visible replacement query; no cache walk exists.
 */
USTRUCT(BlueprintType)
struct FOGOFWAR_API FMassBattleFogLastSeenFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FogOfWar|Last Seen")
	bool bHasSnapshot = false;

	UPROPERTY(Transient)
	bool bShowingSnapshot = false;

	UPROPERTY(Transient)
	FVector SnapshotLocation = FVector::ZeroVector;

	UPROPERTY(Transient)
	FQuat4f SnapshotRotation = FQuat4f::Identity;

	UPROPERTY(Transient)
	FVector3f SnapshotScale = FVector3f::OneVector;

	UPROPERTY(Transient)
	FVector4f SnapshotDynamicParams0 = FVector4f::Zero();

	UPROPERTY(Transient)
	FVector3f SnapshotHealthBar = FVector3f::ZeroVector;

	UPROPERTY(Transient)
	int32 SnapshotLOD = 0;

	UPROPERTY(Transient)
	int32 SnapshotStyle = 0;
};
