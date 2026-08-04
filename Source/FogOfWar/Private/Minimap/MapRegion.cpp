#include "Minimap/MapRegion.h"

#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Minimap/MapPackageProfilePaths.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

namespace
{
	const TCHAR* MapRegionSectionName = TEXT("MapRegion");
	const TCHAR* MinimapColorsSectionName = TEXT("MinimapUnitColors");

	FString GetMapRegionIniPath(const UWorld* World)
	{
		return MassBattleMapProfilePaths::GetMapRegionIniPath(World);
	}

	FString GetMinimapColorsIniPath(const UWorld* World)
	{
		return MassBattleMapProfilePaths::GetMinimapColorsIniPath(World);
	}

	void SetMinimapColor(FConfigFile& IniFile, const FString& SectionName, const TCHAR* Key, const FLinearColor& Color)
	{
		IniFile.SetString(*SectionName, Key, *Color.ToString());
	}

	bool ExportDefaultMinimapColorsIfMissing(const UWorld* World)
	{
		const FString IniPath = GetMinimapColorsIniPath(World);
		if (FPaths::FileExists(IniPath))
		{
			return true;
		}

		static const FLinearColor DefaultTeamColor(0.7f, 0.7f, 0.7f, 1.0f);
		static const FLinearColor DefaultTeamColors[] = {
			FLinearColor(0.45f, 0.45f, 0.45f, 1.0f),
			FLinearColor(0.10f, 0.72f, 0.18f, 1.0f),
			FLinearColor(0.85f, 0.12f, 0.10f, 1.0f),
			FLinearColor(0.12f, 0.34f, 0.95f, 1.0f),
			FLinearColor(0.95f, 0.72f, 0.10f, 1.0f),
			FLinearColor(0.10f, 0.80f, 0.90f, 1.0f),
			FLinearColor(0.75f, 0.20f, 0.85f, 1.0f),
			FLinearColor(0.95f, 0.45f, 0.12f, 1.0f)
		};

		IFileManager::Get().MakeDirectory(*FPaths::GetPath(IniPath), true);

		FConfigFile IniFile;
		const FString SectionName = MinimapColorsSectionName;
		IniFile.SetString(*SectionName, TEXT("Version"), TEXT("1"));
		SetMinimapColor(IniFile, SectionName, TEXT("DefaultTeamColor"), DefaultTeamColor);
		IniFile.SetString(*SectionName, TEXT("TeamColorCount"), TEXT("8"));
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(DefaultTeamColors); ++Index)
		{
			SetMinimapColor(IniFile, SectionName, *FString::Printf(TEXT("TeamColor%d"), Index), DefaultTeamColors[Index]);
		}
		IniFile.SetString(*SectionName, TEXT("bNormalizeTeamColorDirection"), TEXT("True"));
		IniFile.SetFloat(*SectionName, TEXT("NormalUnitColorLength"), 0.5f);
		IniFile.SetFloat(*SectionName, TEXT("SelectedUnitColorLength"), 1.0f);
		SetMinimapColor(IniFile, SectionName, TEXT("CombatUnitColor"), FLinearColor::White);
		IniFile.SetString(*SectionName, TEXT("bEnableCombatColorFlash"), TEXT("False"));
		IniFile.SetFloat(*SectionName, TEXT("CombatColorFlashHz"), 3.0f);
		IniFile.SetFloat(*SectionName, TEXT("DefaultUnitPixelRadius"), 1.5f);

		const bool bSaved = IniFile.Write(IniPath);
		UE_LOG(LogTemp, Log, TEXT("[MapRegion] Export default minimap colors %s to %s"),
			bSaved ? TEXT("succeeded") : TEXT("failed"),
			*IniPath);
		return bSaved;
	}
}

AMapRegion::AMapRegion()
{
	PrimaryActorTick.bCanEverTick = false;

	RegionComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("MapRegion"));
	RootComponent = RegionComponent;
	RegionComponent->SetBoxExtent(FVector(0.5f, 0.5f, 0.5f));
	RegionComponent->ShapeColor = FColor::Green;
	RegionComponent->bDrawOnlyIfSelected = false;

	OverflowComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("OverflowRegion"));
	OverflowComponent->SetupAttachment(RootComponent);
	OverflowComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OverflowComponent->SetUsingAbsoluteScale(true);
	OverflowComponent->ShapeColor = FColor::Cyan;
	OverflowComponent->bDrawOnlyIfSelected = false;
}

void AMapRegion::BeginPlay()
{
	Super::BeginPlay();
	UpdateVisuals();
	ExportMapRegion();
}

void AMapRegion::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateVisuals();
	ExportMapRegion();
}

#if WITH_EDITOR
void AMapRegion::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdateVisuals();
	ExportMapRegion();
	NotifyDataUpdated();
}
#endif

void AMapRegion::UpdateVisuals()
{
	if (!RegionComponent || !OverflowComponent)
	{
		return;
	}

	OverflowComponent->SetUsingAbsoluteScale(true);
	OverflowComponent->SetWorldScale3D(FVector::OneVector);

	const FVector ScaledMapExtent = RegionComponent->GetScaledBoxExtent();
	OverflowComponent->SetBoxExtent(FVector(
		ScaledMapExtent.X + MapOverflowUU,
		ScaledMapExtent.Y + MapOverflowUU,
		ScaledMapExtent.Z + 10.0f));
}

bool AMapRegion::BuildMapRegionValues(
	FVector2D& OutGridOrigin,
	FVector2D& OutGridSize,
	float& OutMapOverflowUU) const
{
	if (!RegionComponent)
	{
		return false;
	}

	const FVector RegionCenter = RegionComponent->GetComponentLocation();
	const FVector RegionExtent = RegionComponent->GetScaledBoxExtent();
	if (RegionExtent.X <= 0.0f || RegionExtent.Y <= 0.0f)
	{
		return false;
	}

	OutGridOrigin = FVector2D(RegionCenter.X - RegionExtent.X, RegionCenter.Y - RegionExtent.Y);
	OutGridSize = FVector2D(RegionExtent.X * 2.0f, RegionExtent.Y * 2.0f);
	OutMapOverflowUU = MapOverflowUU;

	return true;
}

bool AMapRegion::ExportMapRegion() const
{
	FVector2D GridOrigin = FVector2D::ZeroVector;
	FVector2D GridSize = FVector2D::ZeroVector;
	float OverflowUU = 0.0f;
	if (!BuildMapRegionValues(GridOrigin, GridSize, OverflowUU))
	{
		return false;
	}

	FConfigFile IniFile;
	const FString IniPath = GetMapRegionIniPath(GetWorld());
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(IniPath), true);

	const FString SectionName = MapRegionSectionName;
	IniFile.SetString(*SectionName, TEXT("Version"), TEXT("1"));
	IniFile.SetFloat(*SectionName, TEXT("OriginX"), GridOrigin.X);
	IniFile.SetFloat(*SectionName, TEXT("OriginY"), GridOrigin.Y);
	IniFile.SetFloat(*SectionName, TEXT("SizeX"), GridSize.X);
	IniFile.SetFloat(*SectionName, TEXT("SizeY"), GridSize.Y);
	IniFile.SetFloat(*SectionName, TEXT("CenterX"), GridOrigin.X + GridSize.X * 0.5f);
	IniFile.SetFloat(*SectionName, TEXT("CenterY"), GridOrigin.Y + GridSize.Y * 0.5f);
	IniFile.SetFloat(*SectionName, TEXT("ExtentX"), GridSize.X * 0.5f);
	IniFile.SetFloat(*SectionName, TEXT("ExtentY"), GridSize.Y * 0.5f);
	IniFile.SetFloat(*SectionName, TEXT("MapOverflowUU"), OverflowUU);

	const bool bSaved = IniFile.Write(IniPath);
	ExportDefaultMinimapColorsIfMissing(GetWorld());
	UE_LOG(LogTemp, Log, TEXT("[MapRegion] Export %s to %s: Origin=%s Size=%s"),
		bSaved ? TEXT("succeeded") : TEXT("failed"),
		*IniPath,
		*GridOrigin.ToString(),
		*GridSize.ToString());
	return bSaved;
}
