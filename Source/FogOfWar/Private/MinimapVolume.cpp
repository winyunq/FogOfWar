// Copyright Winyunq, 2025. All Rights Reserved.

#include "MinimapVolume.h"
#include "Components/BoxComponent.h"
#include "Subsystems/MinimapDataSubsystem.h"
#include "DrawDebugHelpers.h"

AMinimapVolume::AMinimapVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	BoundsComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("BoundsComponent"));
	RootComponent = BoundsComponent;
	
	// Default to a base size of 1x1x1 (Extent=0.5), so Scale directly equals Size in UU
	// e.g. Scale 65536 = 65536uu
	BoundsComponent->SetBoxExtent(FVector(0.5f, 0.5f, 0.5f));
	BoundsComponent->SetRelativeScale3D(FVector(256.0f, 256.0f, 256.0f)); // Default visible size 256uu
	BoundsComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AMinimapVolume::BeginPlay()
{
	Super::BeginPlay();

	UMinimapDataSubsystem* MinimapSubsystem = UMinimapDataSubsystem::Get();
	if (MinimapSubsystem && BoundsComponent)
	{
		const FVector Origin = BoundsComponent->GetComponentLocation();
		const FVector BoxExtent = BoundsComponent->GetScaledBoxExtent();
		
		// Minimap Grid Origin is usually Bottom-Left (relative to XY plane)
		// GridBottomLeft = Center.XY - Extent.XY
		const FVector2D GridOrigin(Origin.X - BoxExtent.X, Origin.Y - BoxExtent.Y);
		const FVector2D GridSize(BoxExtent.X * 2.f, BoxExtent.Y * 2.f);

		MinimapSubsystem->InitMinimapGrid(GridOrigin, GridSize, GridResolution);
	}
}

void AMinimapVolume::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

#if WITH_EDITOR
	if (bUseAsRTSCameraBounds)
	{
		Tags.AddUnique(FName("OpenRTSCamera#CameraBounds"));
	}
	else
	{
		Tags.Remove(FName("OpenRTSCamera#CameraBounds"));
	}

	if (bDrawDebugGrid && BoundsComponent)
	{
		// Draw basic bounds
		DrawDebugBox(GetWorld(), BoundsComponent->GetComponentLocation(), BoundsComponent->GetScaledBoxExtent(), FColor::Green, false, -1.f, 0, 5.f);

		// Visualize Grid Density if possible
		if (GridResolution.X > 0 && GridResolution.Y > 0)
		{
			const FVector Origin = BoundsComponent->GetComponentLocation();
			const FVector BoxExtent = BoundsComponent->GetScaledBoxExtent();
			const FVector2D GridSize(BoxExtent.X * 2.f, BoxExtent.Y * 2.f);
			const float TileSizeX = GridSize.X / GridResolution.X;
			const float TileSizeY = GridSize.Y / GridResolution.Y;

			// Draw a few sample lines (e.g., center cross)
			const float Z = Origin.Z;
			
			// Center X Line
			DrawDebugLine(GetWorld(), 
				FVector(Origin.X, Origin.Y - BoxExtent.Y, Z), 
				FVector(Origin.X, Origin.Y + BoxExtent.Y, Z), 
				FColor::Green, false, -1.f, 0, 2.f);

			// Center Y Line
			DrawDebugLine(GetWorld(), 
				FVector(Origin.X - BoxExtent.X, Origin.Y, Z), 
				FVector(Origin.X + BoxExtent.X, Origin.Y, Z), 
				FColor::Green, false, -1.f, 0, 2.f);

			// Draw Corner (Bottom-Left Tile)
			const FVector BL = FVector(Origin.X - BoxExtent.X, Origin.Y - BoxExtent.Y, Z);
			DrawDebugBox(GetWorld(), BL + FVector(TileSizeX*0.5f, TileSizeY*0.5f, 0), FVector(TileSizeX*0.5f, TileSizeY*0.5f, 10.f), FColor::Cyan, false, -1.f, 0, 2.f);
		}
	}
#endif
}
