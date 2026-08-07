// Copyright Winyunq, 2025. All Rights Reserved.

#include "UI/MassBattleFrameMinimapWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/BoxComponent.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Minimap/MapPackageProfilePaths.h"
#include "Minimap/MapRegion.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Subsystems/MassBattleFogRenderSubsystem.h"
#include "TimerManager.h"
#include "UI/MassBattleFrameMinimapSlate.h"

namespace
{
	constexpr float FallbackMapSizeUU = 65536.0f;
	constexpr int32 TeamIdLookupSize = 1 << 10;
	const TCHAR* MassBattleMinimapMapRegionSection = TEXT("MapRegion");
	const TCHAR* MinimapColorSection = TEXT("MinimapUnitColors");
	const TCHAR* MinimapBackgroundSection = TEXT("MinimapBackground");

	FString GetMapRegionPath(const UWorld* World)
	{
		return MassBattleMapProfilePaths::GetMapRegionIniPath(World);
	}

	FString GetMinimapColorPath(const UWorld* World)
	{
		return MassBattleMapProfilePaths::GetMinimapColorsIniPath(World);
	}

	FString GetMinimapBackgroundPath(const UWorld* World)
	{
		return MassBattleMapProfilePaths::GetMinimapBackgroundIniPath(World);
	}

	bool ReadMinimapColor(const FConfigFile& IniFile, const TCHAR* Key, FLinearColor& InOutColor)
	{
		FString Value;
		if (!IniFile.GetString(MinimapColorSection, Key, Value))
		{
			return false;
		}

		FLinearColor ParsedColor;
		if (!ParsedColor.InitFromString(Value))
		{
			return false;
		}

		InOutColor = ParsedColor;
		return true;
	}
}

UMassBattleFrameMinimapWidget::UMassBattleFrameMinimapWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	TeamColors.Init(DefaultTeamColor, TeamIdLookupSize);
}

#if WITH_EDITOR
const FText UMassBattleFrameMinimapWidget::GetPaletteCategory()
{
	return NSLOCTEXT("FogOfWar", "MassBattleFrameMinimapPaletteCategory", "Fog of War");
}
#endif

TSharedRef<SWidget> UMassBattleFrameMinimapWidget::RebuildWidget()
{
	if (!RenderData.IsValid())
	{
		RenderData = MakeShared<FMassBattleMinimapRenderData, ESPMode::ThreadSafe>();
	}
	return SAssignNew(RuntimeSlateWidget, SMassBattleFrameMinimap)
		.RenderData(RenderData);
}

void UMassBattleFrameMinimapWidget::ReleaseSlateResources(const bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	RuntimeSlateWidget.Reset();
	if (RenderData.IsValid())
	{
		RenderData->Release_GameThread();
		RenderData.Reset();
	}
}

void UMassBattleFrameMinimapWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ApplyBaseMapTextureFromConfig();
	InitializeMassBattleFrameMinimap();
}

void UMassBattleFrameMinimapWidget::NativeDestruct()
{
	StopUpdateTimer();
	bIsSuccessfullyInitialized = false;
	Super::NativeDestruct();
}

bool UMassBattleFrameMinimapWidget::InitializeMassBattleFrameMinimap()
{
	const bool bHasMapRegion = ResolveMapRegion();
	LoadTeamColorsFromConfig();
	if (!RenderData.IsValid())
	{
		RenderData = MakeShared<FMassBattleMinimapRenderData, ESPMode::ThreadSafe>();
	}
	bIsSuccessfullyInitialized = bHasMapRegion && RenderData.IsValid();
	RestartUpdateTimer();
	return bIsSuccessfullyInitialized && PushMassBattleFrameMinimapFrame();
}

bool UMassBattleFrameMinimapWidget::PushMassBattleFrameMinimapFrame()
{
	if (!bIsSuccessfullyInitialized)
	{
		return false;
	}

	const double StartSeconds = FPlatformTime::Seconds();
	UWorld* World = GetWorld();
	UMassBattleFogRenderSubsystem* RenderFilter = World ? World->GetSubsystem<UMassBattleFogRenderSubsystem>() : nullptr;
	if (!RenderData.IsValid() || !RenderFilter)
	{
		return false;
	}

	FMassBattleMinimapUploadData UploadData;

	RenderFilter->ConfigureStandaloneMinimapTeams(ViewingTeamIndex, AlliedTeamIndices);

	int32 FriendlySourceCount = 0;
	RenderFilter->CopyLatestMinimapSnapshot(
		UploadData.Units,
		FriendlySourceCount,
		UploadData.FogVisibleMarkers);
	UploadData.UnitCount = UploadData.Units.Num();
	UploadData.VisionSourceCount = FMath::Clamp(FriendlySourceCount, 0, UploadData.UnitCount);
	RenderFilter->RequestMinimapSnapshotCollection();

	// Attack exposure adds only the attacking unit marker. It never writes the
	// minimap visibility stencil and therefore cannot reveal nearby terrain/units.
	TArray<FVector4f> AttackRevealMarkers;
	RenderFilter->CollectAttackRevealMarkers(
		World ? World->GetTimeSeconds() : 0.0,
		AttackRevealMarkers);
	UploadData.FogVisibleMarkers.Append(MoveTemp(AttackRevealMarkers));
	const bool bSceneFogActive = RenderFilter->IsConfiguredActive();
	if (bSceneFogActive)
	{
		VisionRadiusUU = RenderFilter->GetVisionRadiusUU();
	}

	const FVector MapCenter3D = MapRegionTransform.GetLocation();
	const FVector2D MapMin(MapCenter3D.X - MapWorldSize.X * 0.5f, MapCenter3D.Y - MapWorldSize.Y * 0.5f);
	UploadData.TeamColors = TeamColors;
	UploadData.MapMin = FVector2f(MapMin);
	UploadData.MapSize = FVector2f(MapWorldSize);
	UploadData.LogicalResolution = MinimapResolution;
	UploadData.VisionRadiusUU = VisionRadiusUU;
	UploadData.UnitRadiusUU = UnitRadiusUU;
	// Minimap readability is independent from the stronger scene-fog treatment.
	// Both views still consume the same visibility state; only presentation opacity differs.
	UploadData.FogOpacity = FogDarkenOpacity;
	// Ownership of the compact snapshot moves to the render command. The render
	// thread uploads persistent buffers and performs all map projection/filtering.
	RenderData->Upload_GameThread(MoveTemp(UploadData));

	LastPerfStats.CpuAgentTraversalCount = 0;
	LastPerfStats.ParameterPushMs = static_cast<float>((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
	return true;
}

void UMassBattleFrameMinimapWidget::ApplyBaseMapTextureFromConfig()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UWidgetTree* ParentTree = GetTypedOuter<UWidgetTree>();
	UUserWidget* ParentWidget = ParentTree
		? Cast<UUserWidget>(ParentTree->GetOuter())
		: nullptr;
	if (!ParentWidget || !ParentWidget->WidgetTree)
	{
		UE_LOG(LogTemp, Error,
			TEXT("MassBattle minimap: cannot resolve the owning widget tree for [%s]."),
			*MassBattleMapProfilePaths::GetCanonicalMapPackagePath(World));
		return;
	}

	UImage* BaseMapImage = ParentWidget->WidgetTree->FindWidget<UImage>(TEXT("ImageBaseMap"));
	if (!BaseMapImage)
	{
		UE_LOG(LogTemp, Error,
			TEXT("MassBattle minimap: owning widget has no ImageBaseMap for [%s]."),
			*MassBattleMapProfilePaths::GetCanonicalMapPackagePath(World));
		return;
	}

	// The designer brush is only a preview/default. Clear it before resolving the
	// exact-map profile so a map with no background never inherits another
	// theater's texture.
	BaseMapImage->SetBrushFromTexture(nullptr, false);

	const FString IniPath = GetMinimapBackgroundPath(World);
	if (!FPaths::FileExists(IniPath))
	{
		UE_LOG(LogTemp, Log,
			TEXT("MassBattle minimap: exact map [%s] has no background profile; background cleared. Expected [%s]."),
			*MassBattleMapProfilePaths::GetCanonicalMapPackagePath(World),
			*IniPath);
		return;
	}

	FConfigFile IniFile;
	IniFile.Read(IniPath);
	const FString CanonicalMapPath =
		MassBattleMapProfilePaths::GetCanonicalMapPackagePath(World);
	FString ProfileMapPath;
	if (!IniFile.GetString(
			MinimapBackgroundSection,
			TEXT("MapPackagePath"),
			ProfileMapPath)
		|| !ProfileMapPath.Equals(CanonicalMapPath, ESearchCase::IgnoreCase))
	{
		UE_LOG(LogTemp, Error,
			TEXT("MassBattle minimap: profile [%s] does not declare exact map [%s]; refusing cross-map background fallback."),
			*IniPath,
			*CanonicalMapPath);
		return;
	}

	FString TexturePath;
	if (!IniFile.GetString(
			MinimapBackgroundSection,
			TEXT("Texture"),
			TexturePath)
		|| TexturePath.IsEmpty())
	{
		return;
	}

	UTexture2D* BaseMapTexture = Cast<UTexture2D>(FSoftObjectPath(TexturePath).TryLoad());
	if (!BaseMapTexture)
	{
		UE_LOG(LogTemp, Error,
			TEXT("MassBattle minimap: failed to load background texture [%s] for [%s]."),
			*TexturePath,
			*CanonicalMapPath);
		return;
	}

	BaseMapImage->SetBrushFromTexture(BaseMapTexture, false);
	UE_LOG(LogTemp, Log,
		TEXT("MassBattle minimap: bound exact map [%s] to background [%s]."),
		*CanonicalMapPath,
		*TexturePath);
}

void UMassBattleFrameMinimapWidget::LoadTeamColorsFromConfig()
{
	FConfigFile IniFile;
	const FString IniPath = GetMinimapColorPath(GetWorld());
	if (FPaths::FileExists(IniPath))
	{
		IniFile.Read(IniPath);
		ReadMinimapColor(IniFile, TEXT("DefaultTeamColor"), DefaultTeamColor);
	}

	// DynamicParams0.W contributes only ten Team-ID bits. Filling the complete lookup
	// makes every missing TeamColorN entry resolve to the configured default color.
	TeamColors.Init(DefaultTeamColor, TeamIdLookupSize);
	if (!FPaths::FileExists(IniPath))
	{
		return;
	}

	int32 TeamColorCount = 0;
	if (!IniFile.GetInt(MinimapColorSection, TEXT("TeamColorCount"), TeamColorCount))
	{
		for (int32 Index = 0; Index < TeamIdLookupSize; ++Index)
		{
			FString UnusedValue;
			if (IniFile.GetString(MinimapColorSection, *FString::Printf(TEXT("TeamColor%d"), Index), UnusedValue))
			{
				TeamColorCount = Index + 1;
			}
		}
	}

	TeamColorCount = FMath::Clamp(TeamColorCount, 0, TeamIdLookupSize);
	for (int32 Index = 0; Index < TeamColorCount; ++Index)
	{
		FLinearColor TeamColor = DefaultTeamColor;
		if (ReadMinimapColor(IniFile, *FString::Printf(TEXT("TeamColor%d"), Index), TeamColor))
		{
			TeamColors[Index] = TeamColor;
		}
	}
}

void UMassBattleFrameMinimapWidget::SetUpdateRateHz(const float InUpdateRateHz)
{
	UpdateRateHz = FMath::Max(0.0f, InUpdateRateHz);
	RestartUpdateTimer();
}

void UMassBattleFrameMinimapWidget::SetMinimapResolution(const int32 InResolution)
{
	MinimapResolution = FMath::Clamp(InResolution, 16, 2048);
	PushMassBattleFrameMinimapFrame();
}

void UMassBattleFrameMinimapWidget::SetFogDarkenOpacity(const float InOpacity)
{
	FogDarkenOpacity = FMath::Clamp(InOpacity, 0.0f, 1.0f);
	PushMassBattleFrameMinimapFrame();
}

void UMassBattleFrameMinimapWidget::SetVisionRadiusUU(const float InRadiusUU)
{
	VisionRadiusUU = FMath::Max(0.0f, InRadiusUU);
	PushMassBattleFrameMinimapFrame();
}

void UMassBattleFrameMinimapWidget::SetUnitRadiusUU(const float InRadiusUU)
{
	UnitRadiusUU = FMath::Max(0.0f, InRadiusUU);
	PushMassBattleFrameMinimapFrame();
}

void UMassBattleFrameMinimapWidget::SetViewingTeamIndex(const int32 InTeamIndex)
{
	ViewingTeamIndex = FMath::Clamp(InTeamIndex, 0, TeamIdLookupSize - 1);
	PushMassBattleFrameMinimapFrame();
}

void UMassBattleFrameMinimapWidget::SetAlliedTeamIndices(const TArray<int32>& InAlliedTeamIndices)
{
	AlliedTeamIndices.Reset(InAlliedTeamIndices.Num());
	for (const int32 TeamIndex : InAlliedTeamIndices)
	{
		AlliedTeamIndices.AddUnique(FMath::Clamp(TeamIndex, 0, TeamIdLookupSize - 1));
	}
	PushMassBattleFrameMinimapFrame();
}

bool UMassBattleFrameMinimapWidget::ResolveMapRegion()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	for (TActorIterator<AMapRegion> It(World); It; ++It)
	{
		const AMapRegion* Region = *It;
		if (!Region || !Region->RegionComponent)
		{
			continue;
		}

		const FVector Extent = Region->RegionComponent->GetScaledBoxExtent();
		if (Extent.X > 0.0f && Extent.Y > 0.0f)
		{
			MapRegionTransform = Region->RegionComponent->GetComponentTransform();
			MapWorldSize = FVector2D(Extent.X * 2.0f, Extent.Y * 2.0f);
			return true;
		}
	}

	FConfigFile Config;
	Config.Read(GetMapRegionPath(World));
	float OriginX = -FallbackMapSizeUU * 0.5f;
	float OriginY = -FallbackMapSizeUU * 0.5f;
	float SizeX = FallbackMapSizeUU;
	float SizeY = FallbackMapSizeUU;
	Config.GetFloat(MassBattleMinimapMapRegionSection, TEXT("OriginX"), OriginX);
	Config.GetFloat(MassBattleMinimapMapRegionSection, TEXT("OriginY"), OriginY);
	Config.GetFloat(MassBattleMinimapMapRegionSection, TEXT("SizeX"), SizeX);
	Config.GetFloat(MassBattleMinimapMapRegionSection, TEXT("SizeY"), SizeY);

	MapRegionTransform = FTransform(FRotator::ZeroRotator, FVector(OriginX + SizeX * 0.5f, OriginY + SizeY * 0.5f, 0.0f));
	MapWorldSize = FVector2D(FMath::Max(1.0f, SizeX), FMath::Max(1.0f, SizeY));
	return true;
}

void UMassBattleFrameMinimapWidget::RestartUpdateTimer()
{
	StopUpdateTimer();
	UWorld* World = GetWorld();
	if (!World || UpdateRateHz <= 0.0f || !bIsSuccessfullyInitialized)
	{
		return;
	}

	World->GetTimerManager().SetTimer(
		UpdateTimerHandle,
		this,
		&UMassBattleFrameMinimapWidget::HandleUpdateTimer,
		1.0f / UpdateRateHz,
		true);
}

void UMassBattleFrameMinimapWidget::HandleUpdateTimer()
{
	PushMassBattleFrameMinimapFrame();
}

void UMassBattleFrameMinimapWidget::StopUpdateTimer()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(UpdateTimerHandle);
	}
}
