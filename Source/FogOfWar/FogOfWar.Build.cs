// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class FogOfWar : ModuleRules
{
	public FogOfWar(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"MassAPI",
				"MassBattle",
				"MassCommon",
				"MassCore",
				"MassEntity",
				"MassLOD",
				"MassMovement",
				"MassSignals",
				"MassSpawner"
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Projects",
				"RenderCore",
				"RHI",
				"Renderer",
				"Slate",
				"SlateCore",
				"RTSInputSystem",
				"MassBattleRTSDiplomacy",
				"MassBattleISKM",
				"MassCommon",
				"UMG", // Needed for UUserWidget
				"Niagara",
				// ... add private dependencies that you statically link with here ...	
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}
