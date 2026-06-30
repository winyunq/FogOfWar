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
				"EnhancedInput",
				"MassAPI",
				"MassBattle",
				"MassBattleMinimap",
				"MassCommon",
				"MassCore",
				"MassEntity",
				"MassLOD",
				"MassMovement",
				"MassRepresentation",
				"MassSignals",
				"MassSpawner"
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"MassCommon",
				"RHI",
				"RenderCore",
				"UMG", // Needed for UUserWidget
				"InputCore", // Needed for EKeys
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
