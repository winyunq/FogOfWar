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
				"MassEntity", // Moved to Public
				"MassMovement", // Moved to Public
				"MassSpawner", // For UMassEntityTraitBase
				"MassRepresentation", // Needed for FMassVisibilityFragment
				"MassSignals", // Often used in modern Mass development
				"MassLOD", // Required for LOD-based culling tags
				"MassBattleMinimap", // Added for RTS/Mass minimap integration
				"EnhancedInput", // Required because RTSCamera.h includes InputMappingContext.h
				"MassCommon", // For Mass types
				"MassEntity", 
				"MassMovement", 
				"MassSpawner", 
				"MassRepresentation", 
				"MassSignals", 
				"MassLOD", 
				"MassBattle", // Direct Dependency
				"OpenRTSCamera", 
				"EnhancedInput", 
				// ... add other public dependencies that you statically link with here ...
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
