// Copyright Winyunq, 2025. All Rights Reserved.

#include "MassBattleFrameFogOfWar.h"

#include "Components/SceneComponent.h"
#include "HAL/PlatformTime.h"
#include "NiagaraComponent.h"
#include "NiagaraDataChannel.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

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
		return IsValid(FogNiagaraComponent);
	}

	if (!IsValid(FogNiagaraSystem))
	{
		UE_LOG(LogMassBattleFrameFog, Error, TEXT("AMassBattleFrameFogOfWar requires FogNiagaraSystem; CPU fallback is intentionally disabled."));
		return false;
	}

	FogNiagaraComponent = NewObject<UNiagaraComponent>(this, TEXT("FogNiagaraComponent"));
	if (!FogNiagaraComponent)
	{
		return false;
	}

	FogNiagaraComponent->SetupAttachment(SceneRoot);
	FogNiagaraComponent->SetAsset(FogNiagaraSystem);
	FogNiagaraComponent->RegisterComponent();
	FogNiagaraComponent->Activate(true);
	bFogActive = true;
	PrimaryActorTick.SetTickFunctionEnable(true);
	PushMassBattleFrameFogParameters();
	return true;
}

void AMassBattleFrameFogOfWar::DeactivateMassBattleFrameFog()
{
	bFogActive = false;
	PrimaryActorTick.SetTickFunctionEnable(false);

	if (IsValid(FogNiagaraComponent))
	{
		FogNiagaraComponent->Deactivate();
		FogNiagaraComponent->DestroyComponent();
	}
	FogNiagaraComponent = nullptr;
}

void AMassBattleFrameFogOfWar::PushMassBattleFrameFogParameters()
{
	if (!bFogActive || !IsValid(FogNiagaraComponent))
	{
		return;
	}

	const double StartSeconds = FPlatformTime::Seconds();
	SetNiagaraParameters();
	LastParameterPushTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	LastPerfStats.ParameterPushMs = static_cast<float>((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
	LastPerfStats.ParameterPushCount++;
	LastPerfStats.bNiagaraPathActive = true;

	if (bEnablePerformanceStats && bLogPerformanceToOutputLog && GetWorld() && GetWorld()->GetTimeSeconds() - LastPerformanceLogTime >= PerformanceLogInterval)
	{
		UE_LOG(LogMassBattleFrameFog, Log,
			TEXT("[FogOfWarPerf][MassBattleFrameFog] NiagaraParameterPush=%.3fms UpdateRateHz=%.3f Radius=%.1f Team=%d"),
			LastPerfStats.ParameterPushMs,
			FogUpdateRateHz,
			TemporaryVisionRadius,
			ViewingTeamIndex);
		LastPerformanceLogTime = GetWorld()->GetTimeSeconds();
	}
}

void AMassBattleFrameFogOfWar::SetNiagaraParameters()
{
	FogNiagaraComponent->SetVariableFloat(VisionRadiusParameter, FMath::Max(0.0f, TemporaryVisionRadius));
	FogNiagaraComponent->SetVariableInt(ViewingTeamParameter, FMath::Clamp(ViewingTeamIndex, 0, 1023));
	FogNiagaraComponent->SetVariableFloat(FogOpacityParameter, FMath::Clamp(FogOpacity, 0.0f, 1.0f));
	FogNiagaraComponent->SetVariableBool(FogDebugParameter, bFogDebug);
	FogNiagaraComponent->SetVariableFloat(FogUpdateRateParameter, FMath::Max(0.0f, FogUpdateRateHz));
	FogNiagaraComponent->SetVariableBool(FogEnabledParameter, bFogActive);

	if (IsValid(VisionDataChannel))
	{
		FogNiagaraComponent->SetVariableObject(VisionDataChannelParameter, VisionDataChannel);
	}
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
