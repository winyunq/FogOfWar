#include "Minimap/MinimapRangeConfig.h"

#include "Minimap/MapBoundsConfig.h"
#include "Subsystems/MassBattleHashGridSubsystem.h"

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

bool AMinimapRangeConfig::ExportMapBoundsConfig() const
{
	if (!BoundsComponent)
	{
		return false;
	}

	const FVector RegionCenter = BoundsComponent->GetComponentLocation();
	const FVector RegionExtent = BoundsComponent->GetScaledBoxExtent();
	if (RegionExtent.X <= 0.0f || RegionExtent.Y <= 0.0f)
	{
		return false;
	}

	FFogOfWarMapBoundsConfig Config;
	Config.GridOrigin = FVector2D(RegionCenter.X - RegionExtent.X, RegionCenter.Y - RegionExtent.Y);
	Config.GridSize = FVector2D(RegionExtent.X * 2.0f, RegionExtent.Y * 2.0f);
	Config.MapOverflowUU = MapOverflowUU;
	Config.MinimapGridResolution = HasExplicitGridResolution() ? GridResolution : FIntPoint::ZeroValue;

	if (const UWorld* World = GetWorld())
	{
		if (const UMassBattleHashGridSubsystem* HashGrid = UMassBattleHashGridSubsystem::GetPtr(World))
		{
			Config.HashGridCellSize = FVector2D(FMath::Abs(HashGrid->AgentCellSize.X), FMath::Abs(HashGrid->AgentCellSize.Y));
			if (!HasExplicitGridResolution() && Config.HashGridCellSize.X > 0.0f && Config.HashGridCellSize.Y > 0.0f)
			{
				Config.MinimapGridResolution = FIntPoint(
					FMath::Max(1, FMath::CeilToInt32(Config.GridSize.X / Config.HashGridCellSize.X)),
					FMath::Max(1, FMath::CeilToInt32(Config.GridSize.Y / Config.HashGridCellSize.Y)));
			}
		}
	}

	const bool bSaved = FFogOfWarMapBoundsConfig::SaveForWorld(GetWorld(), Config);
	UE_LOG(LogTemp, Log, TEXT("[MinimapRangeConfig] Export %s to %s: Origin=%s Size=%s Resolution=%s"),
		bSaved ? TEXT("succeeded") : TEXT("failed"),
		*FFogOfWarMapBoundsConfig::GetConfigFilePath(),
		*Config.GridOrigin.ToString(),
		*Config.GridSize.ToString(),
		*Config.MinimapGridResolution.ToString());
	return bSaved;
}
