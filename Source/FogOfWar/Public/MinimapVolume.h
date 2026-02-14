// Copyright Winyunq, 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MinimapVolume.generated.h"

class UBoxComponent;

/**
 * A dedicated volume actor to configure the Minimap world bounds and resolution for the current level.
 * Visualizes the coverage area in the editor.
 */
UCLASS()
class FOGOFWAR_API AMinimapVolume : public AActor
{
	GENERATED_BODY()
	
public:	
	AMinimapVolume();

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

public:
	// --- Configuration ---

	/** The resolution of the minimap grid (e.g., 256x256). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap Config")
	FIntPoint GridResolution = FIntPoint(256, 256);

	/** Adjust this box to define the world area covered by the minimap. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Minimap Config")
	TObjectPtr<UBoxComponent> BoundsComponent;

	/** If true, draws a debug grid in the editor to visualize tile size. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap Debug")
	bool bDrawDebugGrid = true;
	
	// --- Integration ---

	/** 
	 * If true, automatically adds the 'OpenRTSCamera#CameraBounds' tag to this actor.
	 * This allows the RTS Camera to automatically use this volume as its movement boundary.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Integration")
	bool bUseAsRTSCameraBounds = true;

};
