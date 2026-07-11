// Copyright Winyunq, 2025. All Rights Reserved.

#include "MassBattleFrameFogOfWar.h"
#include "MassBattleFrameFogSceneViewExtension.h"

#include "Components/SceneComponent.h"
#include "HAL/PlatformTime.h"
#include "Fragments/RenderBatchData.h"
#include "SceneViewExtension.h"
#include "Renderers/MassBattleAgentRenderer.h"
#include "Subsystems/MassBattleSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogMassBattleFrameFog, Log, All);

AMassBattleFrameFogOfWar::AMassBattleFrameFogOfWar()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;
}

void AMassBattleFrameFogOfWar::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoActivate)
	{
		ActivateMassBattleFrameFog();
	}
}

void AMassBattleFrameFogOfWar::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DeactivateMassBattleFrameFog();
	Super::EndPlay(EndPlayReason);
}

void AMassBattleFrameFogOfWar::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bFogActive)
	{
		return;
	}

	const double CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (FogUpdateRateHz > 0.0f && CurrentTime - LastParameterPushTime < 1.0 / FogUpdateRateHz)
	{
		return;
	}

	PushMassBattleFrameFogParameters();
}

bool AMassBattleFrameFogOfWar::ActivateMassBattleFrameFog()
{
	if (bFogActive)
	{
		return SceneViewExtension.IsValid();
	}

	SceneViewExtension = FSceneViewExtensions::NewExtension<FMassBattleFrameFogSceneViewExtension>();
	if (!SceneViewExtension.IsValid())
	{
		UE_LOG(LogMassBattleFrameFog, Error, TEXT("Could not register the MassBattleFrame scene fog view extension."));
		return false;
	}

	bFogActive = true;
	PrimaryActorTick.SetTickFunctionEnable(true);
	PushMassBattleFrameFogParameters();
	return true;
}

void AMassBattleFrameFogOfWar::DeactivateMassBattleFrameFog()
{
	bFogActive = false;
	PrimaryActorTick.SetTickFunctionEnable(false);

	if (SceneViewExtension.IsValid())
	{
		SceneViewExtension->Release_GameThread();
		SceneViewExtension.Reset();
	}
}

void AMassBattleFrameFogOfWar::PushMassBattleFrameFogParameters()
{
	if (!bFogActive)
	{
		return;
	}

	const double StartSeconds = FPlatformTime::Seconds();
	SetMassBattleFrameFogArrays();
	LastParameterPushTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	LastPerfStats.ParameterPushMs = static_cast<float>((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
	LastPerfStats.ParameterPushCount++;
	LastPerfStats.bSceneGpuPathActive = SceneViewExtension.IsValid();

	if (bEnablePerformanceStats && bLogPerformanceToOutputLog && GetWorld() && GetWorld()->GetTimeSeconds() - LastPerformanceLogTime >= PerformanceLogInterval)
	{
		UE_LOG(LogMassBattleFrameFog, Log,
			TEXT("[FogOfWarPerf][MassBattleFrameFog] ParameterPush=%.3fms ArrayUpload=%.3fms Sources=%d Batches=%d SceneGPU=%s UpdateRateHz=%.3f Radius=%.1f Team=%d"),
			LastPerfStats.ParameterPushMs,
			LastPerfStats.ArrayUploadMs,
			LastPerfStats.SourceCount,
			LastPerfStats.BatchCount,
			LastPerfStats.bSceneGpuPathActive ? TEXT("yes") : TEXT("no"),
			FogUpdateRateHz,
			TemporaryVisionRadius,
			ViewingTeamIndex);
		LastPerformanceLogTime = GetWorld()->GetTimeSeconds();
	}
}

void AMassBattleFrameFogOfWar::SetMassBattleFrameFogArrays()
{
	const double StartSeconds = FPlatformTime::Seconds();
	LastPerfStats.SourceCount = 0;
	LastPerfStats.BatchCount = 0;

	UWorld* World = GetWorld();
	UMassBattleSubsystem* MassBattleSubsystem = World ? World->GetSubsystem<UMassBattleSubsystem>() : nullptr;

	// Copy the already-contiguous Mass Battle Frame arrays in batch blocks.
	// There is no entity query, per-agent branch, projection, or HashGrid walk here.
	TArray<FVector> Locations;
	TArray<FVector4f> DynamicParams;
	TArray<bool> IsHidden;

	if (MassBattleSubsystem)
	{
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
				Locations.Append(Batch.LocationArray);
				DynamicParams.Append(Batch.DynamicParams0_Array);
				IsHidden.Append(Batch.IsHiddenArray);
				++LastPerfStats.BatchCount;
			}
		}
	}

	LastPerfStats.SourceCount = Locations.Num();
	if (SceneViewExtension.IsValid())
	{
		FMassBattleFrameFogSceneUploadData SceneUpload;
		SceneUpload.Locations = Locations;
		SceneUpload.DynamicParams0 = DynamicParams;
		SceneUpload.IsHidden = IsHidden;
		SceneUpload.VisionRadiusUU = FMath::Max(0.0f, TemporaryVisionRadius);
		SceneUpload.FogOpacity = FMath::Clamp(FogOpacity, 0.0f, 1.0f);
		SceneUpload.ViewingTeamIndex = static_cast<uint32>(FMath::Clamp(ViewingTeamIndex, 0, 1023));
		SceneUpload.bEnabled = bFogActive;
		SceneUpload.bDebug = bFogDebug;
		SceneUpload.bDebugRevealAll = bDebugRevealAll;
		SceneViewExtension->Upload_GameThread(MoveTemp(SceneUpload));
	}

	LastPerfStats.ArrayUploadMs = static_cast<float>((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
}

void AMassBattleFrameFogOfWar::SetTemporaryVisionRadius(const float InRadius)
{
	TemporaryVisionRadius = FMath::Max(0.0f, InRadius);
	PushMassBattleFrameFogParameters();
}

void AMassBattleFrameFogOfWar::SetViewingTeamIndex(const int32 InTeamIndex)
{
	ViewingTeamIndex = FMath::Clamp(InTeamIndex, 0, 1023);
	PushMassBattleFrameFogParameters();
}

void AMassBattleFrameFogOfWar::SetFogOpacity(const float InOpacity)
{
	FogOpacity = FMath::Clamp(InOpacity, 0.0f, 1.0f);
	PushMassBattleFrameFogParameters();
}

void AMassBattleFrameFogOfWar::SetFogDebug(const bool bInDebug)
{
	bFogDebug = bInDebug;
	PushMassBattleFrameFogParameters();
}

void AMassBattleFrameFogOfWar::SetDebugRevealAll(const bool bInRevealAll)
{
	bDebugRevealAll = bInRevealAll;
	PushMassBattleFrameFogParameters();
}
