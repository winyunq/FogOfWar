// Copyright Epic Games, Inc. All Rights Reserved.

#include "FogOfWarModule.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

#define LOCTEXT_NAMESPACE "FFogOfWarModule"

void FFogOfWarModule::StartupModule()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("FogOfWar"));
	if (Plugin.IsValid() && !AllShaderSourceDirectoryMappings().Contains(TEXT("/Plugin/FogOfWar")))
	{
		AddShaderSourceDirectoryMapping(
			TEXT("/Plugin/FogOfWar"),
			FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders")));
	}
}

void FFogOfWarModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FFogOfWarModule, FogOfWar)
