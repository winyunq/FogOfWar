// Copyright Epic Games, Inc. All Rights Reserved.
// Winyunq commercial integration: see COMMERCIAL_FEATURE_LICENSE.md.

#include "FogOfWarModule.h"

#include "MassBattleFogAgentRenderProcessor.h"
#include "MassEntitySettings.h"
#include "MassProcessingPhaseManager.h"
#include "Processors/MassBattleAgentRenderProcessor.h"
#include "Engine/Engine.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "FFogOfWarModule"
DEFINE_LOG_CATEGORY(LogFogOfWar);

namespace UE::FogOfWar::Private
{
	constexpr const TCHAR* AutoRegisterPropertyName = TEXT("bAutoRegisterWithProcessingPhases");
	constexpr const TCHAR* OriginalRenderSection = TEXT("/Script/MassBattle.MassBattleAgentRenderProcessor");
	constexpr const TCHAR* FogRenderSection = TEXT("/Script/FogOfWar.MassBattleFogAgentRenderProcessor");

	bool ReadDesiredRegistrationValue(
		const FConfigFile& PluginMassConfig,
		const TCHAR* ConfigSection)
	{
		bool bAutoRegister = false;
		if (!PluginMassConfig.GetBool(ConfigSection, AutoRegisterPropertyName, bAutoRegister))
		{
			UE_LOG(
				LogFogOfWar,
				Fatal,
				TEXT("FogOfWar DefaultMass.ini is missing required setting %s.%s."),
				ConfigSection,
				AutoRegisterPropertyName);
		}
		return bAutoRegister;
	}

	void ApplyProcessorCDORegistrationValue(
		const TCHAR* ConfigSection,
		UClass* ProcessorClass,
		const bool bAutoRegister)
	{
		UObject* ProcessorCDO = ProcessorClass ? ProcessorClass->GetDefaultObject() : nullptr;
		FBoolProperty* AutoRegisterProperty = ProcessorClass
			? FindFProperty<FBoolProperty>(ProcessorClass, AutoRegisterPropertyName)
			: nullptr;
		if (!ProcessorCDO || !AutoRegisterProperty)
		{
			UE_LOG(LogFogOfWar, Fatal, TEXT("FogOfWar could not apply processor registration config for %s."), ConfigSection);
		}

		AutoRegisterProperty->SetPropertyValue_InContainer(ProcessorCDO, bAutoRegister);
	}

	bool IsProcessorClassInPhaseList(
		const TConstArrayView<FMassProcessingPhaseConfig> PhaseConfigs,
		const UClass* ProcessorClass)
	{
		for (const FMassProcessingPhaseConfig& PhaseConfig : PhaseConfigs)
		{
			for (const UMassProcessor* ProcessorCDO : PhaseConfig.ProcessorCDOs)
			{
				if (ProcessorCDO && ProcessorCDO->GetClass() == ProcessorClass)
				{
					return true;
				}
			}
		}
		return false;
	}
}

void FFogOfWarModule::StartupModule()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("FogOfWar"));
	if (Plugin.IsValid() && !AllShaderSourceDirectoryMappings().Contains(TEXT("/Plugin/FogOfWar")))
	{
		AddShaderSourceDirectoryMapping(
			TEXT("/Plugin/FogOfWar"),
			FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders")));
	}

	// FogOfWar loads in PostConfigInit. Put its ownership rules into the live
	// Mass config cache now, before UMassEntitySettings constructs processor
	// CDOs. This is deterministic even when MassBattle's plugin config was
	// mounted after FogOfWar's config and would otherwise win the scalar merge.
	InjectProcessorRegistrationConfig();

	// The delegate is intentionally retained as a post-build audit. UE 5.8
	// broadcasts this delegate in reverse registration order, so mutating only
	// here is too late when UMassEntitySettings registered after this module.
	if (GEngine)
	{
		AuditProcessorPipeline();
	}
	else
	{
		PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddRaw(
			this,
			&FFogOfWarModule::AuditProcessorPipeline);
	}
}

void FFogOfWarModule::ShutdownModule()
{
	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);
		PostEngineInitHandle.Reset();
	}
}

void FFogOfWarModule::InjectProcessorRegistrationConfig()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("FogOfWar"));
	if (!Plugin.IsValid())
	{
		UE_LOG(LogFogOfWar, Fatal, TEXT("FogOfWar plugin descriptor was unavailable while applying Mass processor config."));
	}

	FConfigFile PluginMassConfig;
	PluginMassConfig.Read(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Config/DefaultMass.ini")));

	const bool bOriginalRender = UE::FogOfWar::Private::ReadDesiredRegistrationValue(
		PluginMassConfig, UE::FogOfWar::Private::OriginalRenderSection);
	const bool bFogRender = UE::FogOfWar::Private::ReadDesiredRegistrationValue(
		PluginMassConfig, UE::FogOfWar::Private::FogRenderSection);

	FString MassIniFilename;
	if (!FConfigCacheIni::LoadGlobalIniFile(MassIniFilename, TEXT("Mass")) || !GConfig)
	{
		UE_LOG(LogFogOfWar, Fatal, TEXT("FogOfWar could not load the global Mass config cache."));
	}

	GConfig->SetBool(
		UE::FogOfWar::Private::OriginalRenderSection,
		UE::FogOfWar::Private::AutoRegisterPropertyName,
		bOriginalRender,
		MassIniFilename);
	GConfig->SetBool(
		UE::FogOfWar::Private::FogRenderSection,
		UE::FogOfWar::Private::AutoRegisterPropertyName,
		bFogRender,
		MassIniFilename);

	UE_LOG(
		LogFogOfWar,
		Log,
		TEXT("Injected Mass processor ownership before CDO construction: OriginalRender=%s FogRender=%s."),
		bOriginalRender ? TEXT("True") : TEXT("False"),
		bFogRender ? TEXT("True") : TEXT("False"));
}

void FFogOfWarModule::AuditProcessorPipeline()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("FogOfWar"));
	if (!Plugin.IsValid())
	{
		UE_LOG(LogFogOfWar, Fatal, TEXT("FogOfWar plugin descriptor was unavailable while auditing the Mass processor pipeline."));
	}

	FConfigFile PluginMassConfig;
	PluginMassConfig.Read(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Config/DefaultMass.ini")));
	const bool bOriginalRender = UE::FogOfWar::Private::ReadDesiredRegistrationValue(
		PluginMassConfig, UE::FogOfWar::Private::OriginalRenderSection);
	const bool bFogRender = UE::FogOfWar::Private::ReadDesiredRegistrationValue(
		PluginMassConfig, UE::FogOfWar::Private::FogRenderSection);

	// Also covers CDOs that another module happened to instantiate before the
	// early cache injection. If the phase list is already frozen, the audit
	// below reports a fatal ownership violation instead of running either a
	// fallback or a dual-writer pipeline.
	UE::FogOfWar::Private::ApplyProcessorCDORegistrationValue(
		UE::FogOfWar::Private::OriginalRenderSection,
		UMassBattleAgentRenderProcessor::StaticClass(),
		bOriginalRender);
	UE::FogOfWar::Private::ApplyProcessorCDORegistrationValue(
		UE::FogOfWar::Private::FogRenderSection,
		UMassBattleFogAgentRenderProcessor::StaticClass(),
		bFogRender);

	UMassEntitySettings* MassSettings = GetMutableDefault<UMassEntitySettings>();
	if (!MassSettings)
	{
		UE_LOG(LogFogOfWar, Fatal, TEXT("FogOfWar could not audit UMassEntitySettings."));
	}

	const TConstArrayView<FMassProcessingPhaseConfig> PhaseConfigs = MassSettings->GetProcessingPhasesConfig();
	const bool bHasOriginalRender = UE::FogOfWar::Private::IsProcessorClassInPhaseList(
		PhaseConfigs, UMassBattleAgentRenderProcessor::StaticClass());
	const bool bHasFogRender = UE::FogOfWar::Private::IsProcessorClassInPhaseList(
		PhaseConfigs, UMassBattleFogAgentRenderProcessor::StaticClass());

	// The replacement owns only MassBattleFrame's VAT/Actor state stage. The
	// independent MassBattleISKM processor remains a downstream backend and
	// consumes the same final visible work set through its own subsystem.
	const bool bPipelineOwned = !bHasOriginalRender && bHasFogRender;

	if (bPipelineOwned)
	{
		UE_LOG(LogFogOfWar, Log, TEXT("Mass render pipeline audit passed: FogOfWar exclusively owns the MBF render state stage; independent MassBattleISKM remains a downstream backend."));
	}
	else
	{
		UE_LOG(
			LogFogOfWar,
			Fatal,
			TEXT("Mass render pipeline ownership is invalid (OriginalRender=%s FogRender=%s). No compatibility or dual-writer mode is supported."),
			bHasOriginalRender ? TEXT("Present") : TEXT("Absent"),
			bHasFogRender ? TEXT("Present") : TEXT("Absent"));
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FFogOfWarModule, FogOfWar)
