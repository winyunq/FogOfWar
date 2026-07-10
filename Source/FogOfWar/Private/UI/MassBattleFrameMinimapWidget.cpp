// Copyright Winyunq, 2025. All Rights Reserved.

#include "UI/MassBattleFrameMinimapWidget.h"

#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Fragments/RenderBatchData.h"
#include "HAL/PlatformTime.h"
#include "Minimap/MapRegion.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Renderers/MassBattleAgentRenderer.h"
#include "Subsystems/MassBattleSubsystem.h"
#include "TimerManager.h"
#include "UI/MassBattleFrameMinimapSlate.h"

namespace
{
	constexpr float FallbackMapSizeUU = 65536.0f;
	constexpr int32 TeamIdLookupSize = 1 << 10;
	const TCHAR* MapRegionSection = TEXT("MapRegion");
	const TCHAR* MinimapColorSection = TEXT("MinimapUnitColors");

	FString GetMapName(const UWorld* World)
	{
		if (!World)
		{
			return TEXT("Default");
		}

		FString MapName = World->GetMapName();
		MapName.RemoveFromStart(World->StreamingLevelsPrefix);
		return MapName.IsEmpty() ? FString(TEXT("Default")) : MapName;
	}

	FString GetMapRegionPath(const UWorld* World)
	{
		return FPaths::ProjectConfigDir() / TEXT("MapRegion") / GetMapName(World) / TEXT("MapRegion.ini");
	}

	FString GetMinimapColorPath(const UWorld* World)
	{
		return FPaths::ProjectConfigDir() / TEXT("MapRegion") / GetMapName(World) / TEXT("MinimapColors.ini");
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
	UMassBattleSubsystem* MassBattleSubsystem = World ? World->GetSubsystem<UMassBattleSubsystem>() : nullptr;
	if (!MassBattleSubsystem || !RenderData.IsValid())
	{
		return false;
	}

	FMassBattleMinimapUploadData UploadData;
	int32 AppendedBatchCount = 0;

	// Batch-level bulk concatenation only. TArray::Append copies each contiguous block;
	// there is no per-agent loop, projection, filtering, or repacking on the CPU.
	for (const TPair<int32, TObjectPtr<AMassBattleAgentRenderer>>& RendererPair : MassBattleSubsystem->AgentRenderers)
	{
		const AMassBattleAgentRenderer* Renderer = RendererPair.Value;
		if (!IsValid(Renderer))
		{
			continue;
		}

		for (const TPair<int32, FAgentRenderBatchData>& BatchPair : Renderer->SpawnedRenderBatches)
		{
			const FAgentRenderBatchData& Batch = BatchPair.Value;
			UploadData.Locations.Append(Batch.LocationArray);
			UploadData.DynamicParams0.Append(Batch.DynamicParams0_Array);
			UploadData.IsHidden.Append(Batch.IsHiddenArray);
			++AppendedBatchCount;
		}
	}

	const FVector MapCenter3D = MapRegionTransform.GetLocation();
	const FVector2D MapMin(MapCenter3D.X - MapWorldSize.X * 0.5f, MapCenter3D.Y - MapWorldSize.Y * 0.5f);
	UploadData.TeamColors = TeamColors;
	UploadData.MapMin = FVector2f(MapMin);
	UploadData.MapSize = FVector2f(MapWorldSize);
	UploadData.LogicalResolution = MinimapResolution;
	UploadData.VisionRadiusUU = VisionRadiusUU;
	UploadData.UnitRadiusUU = UnitRadiusUU;
	UploadData.FogOpacity = FogDarkenOpacity;
	UploadData.ViewingTeamIndex = static_cast<uint32>(FMath::Max(ViewingTeamIndex, 0));
	const int32 UploadedAgentCount = FMath::Min(
		UploadData.Locations.Num(),
		FMath::Min(UploadData.DynamicParams0.Num(), UploadData.IsHidden.Num()));
	const uint64 UploadBytes =
		static_cast<uint64>(UploadData.Locations.Num()) * sizeof(FVector)
		+ static_cast<uint64>(UploadData.DynamicParams0.Num()) * sizeof(FVector4f)
		+ static_cast<uint64>(UploadData.IsHidden.Num()) * sizeof(bool)
		+ static_cast<uint64>(UploadData.TeamColors.Num()) * sizeof(FLinearColor);

	// Ownership of the already-merged contiguous blocks moves to the render command.
	// The render thread performs one raw upload per array; it never asks the CPU to project agents.
	RenderData->Upload_GameThread(MoveTemp(UploadData));

	LastPerfStats.CpuAgentTraversalCount = 0;
	LastPerfStats.ParameterPushMs = static_cast<float>((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
	UE_LOG(LogTemp, Display,
		TEXT("MassBattleMinimapPerf GT: Agents=%d Batches=%d BulkMergeAndSchedule=%.3fms UploadBytes=%llu"),
		UploadedAgentCount,
		AppendedBatchCount,
		LastPerfStats.ParameterPushMs,
		static_cast<unsigned long long>(UploadBytes));
	return true;
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
	Config.GetFloat(MapRegionSection, TEXT("OriginX"), OriginX);
	Config.GetFloat(MapRegionSection, TEXT("OriginY"), OriginY);
	Config.GetFloat(MapRegionSection, TEXT("SizeX"), SizeX);
	Config.GetFloat(MapRegionSection, TEXT("SizeY"), SizeY);

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
