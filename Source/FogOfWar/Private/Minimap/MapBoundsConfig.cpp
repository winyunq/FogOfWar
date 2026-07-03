#include "Minimap/MapBoundsConfig.h"

#include "Engine/World.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

namespace
{
	const TCHAR* MapBoundsDefaultSection = TEXT("MapBounds.Default");
	const TCHAR* MapBoundsIniFileName = TEXT("FogOfWarMapBounds.ini");

	FString GetCleanMapName(const UWorld* World)
	{
		if (!World)
		{
			return TEXT("Default");
		}

		FString MapName = World->GetMapName();
		MapName.RemoveFromStart(World->StreamingLevelsPrefix);
		return MapName.IsEmpty() ? FString(TEXT("Default")) : MapName;
	}

	bool LoadSection(const FConfigFile& ConfigFile, const TCHAR* SectionName, FFogOfWarMapBoundsConfig& OutConfig)
	{
		float OriginX = 0.0f;
		float OriginY = 0.0f;
		float SizeX = 0.0f;
		float SizeY = 0.0f;
		if (!ConfigFile.GetFloat(SectionName, TEXT("OriginX"), OriginX) ||
			!ConfigFile.GetFloat(SectionName, TEXT("OriginY"), OriginY) ||
			!ConfigFile.GetFloat(SectionName, TEXT("SizeX"), SizeX) ||
			!ConfigFile.GetFloat(SectionName, TEXT("SizeY"), SizeY))
		{
			return false;
		}

		OutConfig.GridOrigin = FVector2D(OriginX, OriginY);
		OutConfig.GridSize = FVector2D(SizeX, SizeY);
		ConfigFile.GetFloat(SectionName, TEXT("MapOverflowUU"), OutConfig.MapOverflowUU);
		ConfigFile.GetInt(SectionName, TEXT("MinimapResolutionX"), OutConfig.MinimapGridResolution.X);
		ConfigFile.GetInt(SectionName, TEXT("MinimapResolutionY"), OutConfig.MinimapGridResolution.Y);
		float HashGridCellSizeX = 0.0f;
		float HashGridCellSizeY = 0.0f;
		ConfigFile.GetFloat(SectionName, TEXT("HashGridCellSizeX"), HashGridCellSizeX);
		ConfigFile.GetFloat(SectionName, TEXT("HashGridCellSizeY"), HashGridCellSizeY);
		OutConfig.HashGridCellSize = FVector2D(HashGridCellSizeX, HashGridCellSizeY);
		ConfigFile.GetInt(SectionName, TEXT("HashGridResolutionX"), OutConfig.HashGridResolution.X);
		ConfigFile.GetInt(SectionName, TEXT("HashGridResolutionY"), OutConfig.HashGridResolution.Y);
		return OutConfig.IsValid();
	}
}

bool FFogOfWarMapBoundsConfig::IsValid() const
{
	return GridSize.X > 0.0f && GridSize.Y > 0.0f;
}

FString FFogOfWarMapBoundsConfig::GetConfigFilePath()
{
	return FPaths::ProjectConfigDir() / MapBoundsIniFileName;
}

FString FFogOfWarMapBoundsConfig::GetSectionName(const UWorld* World)
{
	return FString::Printf(TEXT("MapBounds.%s"), *GetCleanMapName(World));
}

bool FFogOfWarMapBoundsConfig::LoadForWorld(const UWorld* World, FFogOfWarMapBoundsConfig& OutConfig)
{
	FConfigFile ConfigFile;
	const FString ConfigFilePath = GetConfigFilePath();
	if (!FPaths::FileExists(ConfigFilePath))
	{
		return false;
	}
	ConfigFile.Read(ConfigFilePath);

	const FString SectionName = GetSectionName(World);
	if (LoadSection(ConfigFile, *SectionName, OutConfig))
	{
		return true;
	}

	return LoadSection(ConfigFile, MapBoundsDefaultSection, OutConfig);
}

bool FFogOfWarMapBoundsConfig::SaveForWorld(const UWorld* World, const FFogOfWarMapBoundsConfig& Config)
{
	if (!Config.IsValid())
	{
		return false;
	}

	FConfigFile ConfigFile;
	const FString ConfigFilePath = GetConfigFilePath();
	ConfigFile.Read(ConfigFilePath);

	const FString SectionName = GetSectionName(World);
	ConfigFile.SetString(*SectionName, TEXT("Version"), TEXT("1"));
	ConfigFile.SetFloat(*SectionName, TEXT("OriginX"), Config.GridOrigin.X);
	ConfigFile.SetFloat(*SectionName, TEXT("OriginY"), Config.GridOrigin.Y);
	ConfigFile.SetFloat(*SectionName, TEXT("SizeX"), Config.GridSize.X);
	ConfigFile.SetFloat(*SectionName, TEXT("SizeY"), Config.GridSize.Y);
	ConfigFile.SetFloat(*SectionName, TEXT("CenterX"), Config.GridOrigin.X + Config.GridSize.X * 0.5f);
	ConfigFile.SetFloat(*SectionName, TEXT("CenterY"), Config.GridOrigin.Y + Config.GridSize.Y * 0.5f);
	ConfigFile.SetFloat(*SectionName, TEXT("ExtentX"), Config.GridSize.X * 0.5f);
	ConfigFile.SetFloat(*SectionName, TEXT("ExtentY"), Config.GridSize.Y * 0.5f);
	ConfigFile.SetFloat(*SectionName, TEXT("MapOverflowUU"), Config.MapOverflowUU);
	ConfigFile.SetString(*SectionName, TEXT("MinimapResolutionX"), *FString::FromInt(Config.MinimapGridResolution.X));
	ConfigFile.SetString(*SectionName, TEXT("MinimapResolutionY"), *FString::FromInt(Config.MinimapGridResolution.Y));
	ConfigFile.SetFloat(*SectionName, TEXT("HashGridCellSizeX"), Config.HashGridCellSize.X);
	ConfigFile.SetFloat(*SectionName, TEXT("HashGridCellSizeY"), Config.HashGridCellSize.Y);
	ConfigFile.SetString(*SectionName, TEXT("HashGridResolutionX"), *FString::FromInt(Config.HashGridResolution.X));
	ConfigFile.SetString(*SectionName, TEXT("HashGridResolutionY"), *FString::FromInt(Config.HashGridResolution.Y));

	return ConfigFile.Write(ConfigFilePath);
}
