#include "Minimap/MinimapRangeConfig.h"

AMinimapRangeConfig::AMinimapRangeConfig()
{
	PrimaryActorTick.bCanEverTick = false;

	BoundsComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("MapBounds"));
	RootComponent = BoundsComponent;
	BoundsComponent->SetBoxExtent(FVector(0.5f, 0.5f, 0.5f));
	BoundsComponent->ShapeColor = FColor::Green;
	BoundsComponent->bDrawOnlyIfSelected = false;

	OverflowComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("OverflowBounds"));
	OverflowComponent->SetupAttachment(RootComponent);
	OverflowComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OverflowComponent->SetUsingAbsoluteScale(true);
	OverflowComponent->ShapeColor = FColor::Cyan;
	OverflowComponent->bDrawOnlyIfSelected = false;
}

void AMinimapRangeConfig::BeginPlay()
{
	Super::BeginPlay();
	UpdateVisuals();
}

void AMinimapRangeConfig::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (BoundsComponent)
	{
		BoundsComponent->SetBoxExtent(FVector(0.5f, 0.5f, 0.5f));
	}

	UpdateVisuals();
}

#if WITH_EDITOR
void AMinimapRangeConfig::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdateVisuals();
}
#endif

void AMinimapRangeConfig::UpdateVisuals()
{
	if (!BoundsComponent || !OverflowComponent)
	{
		return;
	}

	OverflowComponent->SetUsingAbsoluteScale(true);
	OverflowComponent->SetWorldScale3D(FVector::OneVector);

	const FVector ScaledMapExtent = BoundsComponent->GetScaledBoxExtent();
	OverflowComponent->SetBoxExtent(FVector(
		ScaledMapExtent.X + MapOverflowUU,
		ScaledMapExtent.Y + MapOverflowUU,
		ScaledMapExtent.Z + 10.0f));
}
