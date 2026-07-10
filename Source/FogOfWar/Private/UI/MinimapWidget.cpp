// Copyright Winyunq, 2025. All Rights Reserved.

#include "UI/MinimapWidget.h"
#include "FogOfWarMassBinding.h"
#include "Components/Image.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "MassExecutionContext.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Subsystems/MinimapDataSubsystem.h"
// #include "FogOfWar.h" // Removed for strict decoupling
#include "MassEntitySubsystem.h"
#include "MassCommonFragments.h"
#include "MassFogOfWarFragments.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

DEFINE_LOG_CATEGORY_STATIC(LogMinimapWidget, Log, All);

namespace
{
	constexpr int32 FallbackMinimapTextureResolution = 256;
	constexpr int32 MaxConfigTeamColors = 64;
	const TCHAR* MinimapColorDirectoryName = TEXT("MapRegion");
	const TCHAR* MinimapColorFileName = TEXT("MinimapColors.ini");
	const TCHAR* MinimapColorSectionName = TEXT("MinimapUnitColors");

	struct FMinimapCanvasDot
	{
		FVector2D Position = FVector2D::ZeroVector;
		FLinearColor Color = FLinearColor::White;
	};

	struct FMinimapDisplayOptions
	{
		FLinearColor DefaultTeamColor = FLinearColor::White;
		TArray<FLinearColor> TeamColors;
		bool bNormalizeTeamColorDirection = true;
		float NormalUnitColorLength = 0.5f;
		float SelectedUnitColorLength = 1.0f;
		FLinearColor CombatUnitColor = FLinearColor::White;
		bool bEnableCombatColorFlash = false;
		float CombatColorFlashHz = 3.0f;
		float DefaultUnitPixelRadius = 1.5f;
	};

	FORCEINLINE bool IsValidTextureResolution(const FIntPoint& Resolution)
	{
		return Resolution.X > 0 && Resolution.Y > 0;
	}

	FString GetMinimapColorCleanMapName(const UWorld* World)
	{
		if (!World)
		{
			return TEXT("Default");
		}

		FString MapName = World->GetMapName();
		MapName.RemoveFromStart(World->StreamingLevelsPrefix);
		return MapName.IsEmpty() ? FString(TEXT("Default")) : MapName;
	}

	FString GetMinimapColorIniPath(const UWorld* World)
	{
		return FPaths::ProjectConfigDir() / MinimapColorDirectoryName / GetMinimapColorCleanMapName(World) / MinimapColorFileName;
	}

	void SetMinimapConfigColor(FConfigFile& IniFile, const FString& SectionName, const TCHAR* Key, const FLinearColor& Color)
	{
		IniFile.SetString(*SectionName, Key, *Color.ToString());
	}

	bool GetMinimapConfigColor(const FConfigFile& IniFile, const TCHAR* Key, FLinearColor& InOutColor)
	{
		FString Value;
		if (!IniFile.GetString(MinimapColorSectionName, Key, Value))
		{
			return false;
		}

		FLinearColor ParsedColor;
		if (!ParsedColor.InitFromString(Value))
		{
			UE_LOG(LogMinimapWidget, Warning, TEXT("[MinimapWidget] Invalid color value for %s in %s: %s"),
				Key,
				MinimapColorSectionName,
				*Value);
			return false;
		}

		InOutColor = ParsedColor;
		return true;
	}

	FMinimapDisplayOptions MakeMinimapDisplayOptions(
		const FLinearColor& InDefaultTeamColor,
		const TArray<FLinearColor>& InTeamColors,
		bool bInNormalizeTeamColorDirection,
		float InNormalUnitColorLength,
		float InSelectedUnitColorLength,
		const FLinearColor& InCombatUnitColor,
		bool bInEnableCombatColorFlash,
		float InCombatColorFlashHz,
		float InDefaultUnitPixelRadius)
	{
		FMinimapDisplayOptions Options;
		Options.DefaultTeamColor = InDefaultTeamColor;
		Options.TeamColors = InTeamColors;
		Options.bNormalizeTeamColorDirection = bInNormalizeTeamColorDirection;
		Options.NormalUnitColorLength = InNormalUnitColorLength;
		Options.SelectedUnitColorLength = InSelectedUnitColorLength;
		Options.CombatUnitColor = InCombatUnitColor;
		Options.bEnableCombatColorFlash = bInEnableCombatColorFlash;
		Options.CombatColorFlashHz = InCombatColorFlashHz;
		Options.DefaultUnitPixelRadius = InDefaultUnitPixelRadius;
		return Options;
	}

	bool SaveMinimapColorIni(const UWorld* World, const FMinimapDisplayOptions& Options, bool bOverwriteExisting)
	{
		const FString IniPath = GetMinimapColorIniPath(World);
		if (!bOverwriteExisting && FPaths::FileExists(IniPath))
		{
			return true;
		}

		IFileManager::Get().MakeDirectory(*FPaths::GetPath(IniPath), true);

		FConfigFile IniFile;
		const FString SectionName = MinimapColorSectionName;
		IniFile.SetString(*SectionName, TEXT("Version"), TEXT("1"));
		SetMinimapConfigColor(IniFile, SectionName, TEXT("DefaultTeamColor"), Options.DefaultTeamColor);
		IniFile.SetString(*SectionName, TEXT("TeamColorCount"), *FString::FromInt(Options.TeamColors.Num()));
		for (int32 Index = 0; Index < Options.TeamColors.Num(); ++Index)
		{
			SetMinimapConfigColor(IniFile, SectionName, *FString::Printf(TEXT("TeamColor%d"), Index), Options.TeamColors[Index]);
		}
		IniFile.SetString(*SectionName, TEXT("bNormalizeTeamColorDirection"), Options.bNormalizeTeamColorDirection ? TEXT("True") : TEXT("False"));
		IniFile.SetFloat(*SectionName, TEXT("NormalUnitColorLength"), Options.NormalUnitColorLength);
		IniFile.SetFloat(*SectionName, TEXT("SelectedUnitColorLength"), Options.SelectedUnitColorLength);
		SetMinimapConfigColor(IniFile, SectionName, TEXT("CombatUnitColor"), Options.CombatUnitColor);
		IniFile.SetString(*SectionName, TEXT("bEnableCombatColorFlash"), Options.bEnableCombatColorFlash ? TEXT("True") : TEXT("False"));
		IniFile.SetFloat(*SectionName, TEXT("CombatColorFlashHz"), Options.CombatColorFlashHz);
		IniFile.SetFloat(*SectionName, TEXT("DefaultUnitPixelRadius"), Options.DefaultUnitPixelRadius);

		const bool bSaved = IniFile.Write(IniPath);
		UE_LOG(LogMinimapWidget, Log, TEXT("[MinimapWidget] Export %s to %s"),
			bSaved ? TEXT("succeeded") : TEXT("failed"),
			*IniPath);
		return bSaved;
	}

	bool LoadMinimapColorIni(const UWorld* World, FMinimapDisplayOptions& InOutOptions)
	{
		const FString IniPath = GetMinimapColorIniPath(World);
		if (!FPaths::FileExists(IniPath))
		{
			return false;
		}

		FConfigFile IniFile;
		IniFile.Read(IniPath);

		bool bLoadedAnyValue = false;
		bLoadedAnyValue |= GetMinimapConfigColor(IniFile, TEXT("DefaultTeamColor"), InOutOptions.DefaultTeamColor);

		int32 TeamColorCount = InOutOptions.TeamColors.Num();
		if (!IniFile.GetInt(MinimapColorSectionName, TEXT("TeamColorCount"), TeamColorCount))
		{
			for (int32 Index = 0; Index < MaxConfigTeamColors; ++Index)
			{
				FString UnusedValue;
				if (IniFile.GetString(MinimapColorSectionName, *FString::Printf(TEXT("TeamColor%d"), Index), UnusedValue))
				{
					TeamColorCount = FMath::Max(TeamColorCount, Index + 1);
				}
			}
		}
		TeamColorCount = FMath::Clamp(TeamColorCount, 0, MaxConfigTeamColors);

		TArray<FLinearColor> LoadedTeamColors;
		LoadedTeamColors.SetNum(TeamColorCount);
		bool bLoadedAnyTeamColor = false;
		for (int32 Index = 0; Index < TeamColorCount; ++Index)
		{
			FLinearColor TeamColor = InOutOptions.TeamColors.IsValidIndex(Index)
				? InOutOptions.TeamColors[Index]
				: InOutOptions.DefaultTeamColor;
			if (GetMinimapConfigColor(IniFile, *FString::Printf(TEXT("TeamColor%d"), Index), TeamColor))
			{
				bLoadedAnyTeamColor = true;
			}
			LoadedTeamColors[Index] = TeamColor;
		}
		if (bLoadedAnyTeamColor)
		{
			InOutOptions.TeamColors = MoveTemp(LoadedTeamColors);
			bLoadedAnyValue = true;
		}

		bool bBoolValue = false;
		if (IniFile.GetBool(MinimapColorSectionName, TEXT("bNormalizeTeamColorDirection"), bBoolValue))
		{
			InOutOptions.bNormalizeTeamColorDirection = bBoolValue;
			bLoadedAnyValue = true;
		}
		float FloatValue = 0.0f;
		if (IniFile.GetFloat(MinimapColorSectionName, TEXT("NormalUnitColorLength"), FloatValue))
		{
			InOutOptions.NormalUnitColorLength = FloatValue;
			bLoadedAnyValue = true;
		}
		if (IniFile.GetFloat(MinimapColorSectionName, TEXT("SelectedUnitColorLength"), FloatValue))
		{
			InOutOptions.SelectedUnitColorLength = FloatValue;
			bLoadedAnyValue = true;
		}
		bLoadedAnyValue |= GetMinimapConfigColor(IniFile, TEXT("CombatUnitColor"), InOutOptions.CombatUnitColor);
		if (IniFile.GetBool(MinimapColorSectionName, TEXT("bEnableCombatColorFlash"), bBoolValue))
		{
			InOutOptions.bEnableCombatColorFlash = bBoolValue;
			bLoadedAnyValue = true;
		}
		if (IniFile.GetFloat(MinimapColorSectionName, TEXT("CombatColorFlashHz"), FloatValue))
		{
			InOutOptions.CombatColorFlashHz = FloatValue;
			bLoadedAnyValue = true;
		}
		if (IniFile.GetFloat(MinimapColorSectionName, TEXT("DefaultUnitPixelRadius"), FloatValue))
		{
			InOutOptions.DefaultUnitPixelRadius = FloatValue;
			bLoadedAnyValue = true;
		}

		if (bLoadedAnyValue)
		{
			UE_LOG(LogMinimapWidget, Log, TEXT("[MinimapWidget] Loaded minimap unit colors from %s"), *IniPath);
		}
		return bLoadedAnyValue;
	}

}

// 辅助函数：创建一个支持CPU访问的动态数据纹理
UTexture2D* CreateDynamicDataTexture(UObject* Outer, int32 Width, int32 Height, FName Name)
{
	if (!Outer || Width <= 0 || Height <= 0) return nullptr;

	UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_A32B32G32R32F, Name);
	if (Texture)
	{
		Texture->CompressionSettings = TextureCompressionSettings::TC_VectorDisplacementmap;
		Texture->SRGB = 0;
		Texture->Filter = TextureFilter::TF_Nearest;
		Texture->AddToRoot(); // 防止被GC
		Texture->UpdateResource();
	}
	return Texture;
}

FIntPoint UMinimapWidget::GetEffectiveTextureResolution() const
{
	if (IsValidTextureResolution(TextureResolution))
	{
		return TextureResolution;
	}

	return FIntPoint(FallbackMinimapTextureResolution, FallbackMinimapTextureResolution);
}

bool UMinimapWidget::EnsureMinimapRenderTarget(const FIntPoint& DesiredResolution)
{
	const FIntPoint SafeResolution(
		FMath::Max(1, DesiredResolution.X),
		FMath::Max(1, DesiredResolution.Y));

	if (MinimapRenderTarget &&
		MinimapRenderTarget->SizeX == SafeResolution.X &&
		MinimapRenderTarget->SizeY == SafeResolution.Y)
	{
		return true;
	}

	MinimapRenderTarget = UKismetRenderingLibrary::CreateRenderTarget2D(
		this,
		SafeResolution.X,
		SafeResolution.Y,
		ETextureRenderTargetFormat::RTF_RGBA8);

	if (MinimapRenderTarget && MinimapImage)
	{
		FSlateBrush Brush = MinimapImage->GetBrush();
		Brush.SetResourceObject(MinimapRenderTarget);
		MinimapImage->SetBrush(Brush);
	}

	return MinimapRenderTarget != nullptr;
}

bool UMinimapWidget::InitializeMinimapSystem()
{
	if (bIsSuccessfullyInitialized)
	{
		return true;
	}

	if (!MinimapDataSubsystem)
	{
		return false;
	}

	bIsSuccessfullyInitialized = false;

	MinimapDataSubsystem->SetMinimapResolution(TextureResolution);

	FMinimapDisplayOptions DisplayOptions = MakeMinimapDisplayOptions(
		DefaultTeamColor,
		RecommendedTeamColors,
		bNormalizeTeamColorDirection,
		NormalUnitColorLength,
		SelectedUnitColorLength,
		CombatUnitColor,
		bEnableCombatColorFlash,
		CombatColorFlashHz,
		DefaultUnitPixelRadius);
	SaveMinimapColorIni(GetWorld(), DisplayOptions, false);
	LoadMinimapColorIni(GetWorld(), DisplayOptions);
	MinimapDataSubsystem->SyncMinimapDisplayOptions(
		DisplayOptions.DefaultTeamColor,
		DisplayOptions.TeamColors,
		DisplayOptions.bNormalizeTeamColorDirection,
		DisplayOptions.NormalUnitColorLength,
		DisplayOptions.SelectedUnitColorLength,
		DisplayOptions.CombatUnitColor,
		DisplayOptions.bEnableCombatColorFlash,
		DisplayOptions.CombatColorFlashHz,
		DisplayOptions.DefaultUnitPixelRadius);

	const FIntPoint EffectiveTextureResolution = GetEffectiveTextureResolution();
	if (!EnsureMinimapRenderTarget(EffectiveTextureResolution))
	{
		UE_LOG(LogMinimapWidget, Error, TEXT("InitializeMinimapSystem failed: MinimapRenderTarget could not be created."));
		bInitPermanentlyFailed = true;
		return false;
	}
	if (!VisionDataTexture)
	{
		VisionDataTexture = CreateDynamicDataTexture(this, MaxUnits, 1, TEXT("VisionDataTexture"));
	}
	if (!IconDataTexture)
	{
		IconDataTexture = CreateDynamicDataTexture(this, MaxUnits, 1, TEXT("IconDataTexture"));
	}
	if (!IconColorTexture)
	{
		IconColorTexture = CreateDynamicDataTexture(this, MaxUnits, 1, TEXT("IconColorTexture"));
	}

	if (!MinimapMaterial)
	{
		UE_LOG(LogMinimapWidget, Error, TEXT("InitializeMinimapSystem failed: MinimapMaterial is not set."));
		bInitPermanentlyFailed = true;
		return false;
	}

	MinimapMaterialInstance = UMaterialInstanceDynamic::Create(MinimapMaterial, this);

	if (!MinimapRenderTarget || !VisionDataTexture || !IconDataTexture || !IconColorTexture || !MinimapMaterialInstance)
	{
		UE_LOG(LogMinimapWidget, Error, TEXT("InitializeMinimapSystem failed: A required resource could not be created."));
		bInitPermanentlyFailed = true;
		return false;
	}
	
	MinimapMaterialInstance->SetTextureParameterValue(TEXT("VisionDataTexture"), VisionDataTexture);
	MinimapMaterialInstance->SetTextureParameterValue(TEXT("VisionSourceDataTexture"), VisionDataTexture);
	MinimapMaterialInstance->SetTextureParameterValue(TEXT("IconDataTexture"), IconDataTexture);
	MinimapMaterialInstance->SetTextureParameterValue(TEXT("UnitLocationDataTexture"), IconDataTexture);
	MinimapMaterialInstance->SetTextureParameterValue(TEXT("IconColorTexture"), IconColorTexture);
	MinimapMaterialInstance->SetTextureParameterValue(TEXT("UnitColorDataTexture"), IconColorTexture);
	MinimapMaterialInstance->SetScalarParameterValue(TEXT("SourceTextureWidth"), FMath::Max(1, MaxUnits));

	// Use Subsystem Data for Bounds
	const int32 MaterialTexX = FMath::Max(1, EffectiveTextureResolution.X);
	const int32 MaterialTexY = FMath::Max(1, EffectiveTextureResolution.Y);
	const FVector2D MaterialUnitWorldSize(
		MinimapDataSubsystem->GridSize.X / static_cast<float>(MaterialTexX),
		MinimapDataSubsystem->GridSize.Y / static_cast<float>(MaterialTexY));
	MinimapMaterialInstance->SetVectorParameterValue(TEXT("GridBottomLeftWorldLocation"), FLinearColor(MinimapDataSubsystem->GridBottomLeftWorldLocation.X, MinimapDataSubsystem->GridBottomLeftWorldLocation.Y, 0));
	MinimapMaterialInstance->SetVectorParameterValue(TEXT("GridSize"), FLinearColor(MinimapDataSubsystem->GridSize.X, MinimapDataSubsystem->GridSize.Y, 0));
	MinimapMaterialInstance->SetVectorParameterValue(TEXT("GridWorldSize"), FLinearColor(MinimapDataSubsystem->GridSize.X, MinimapDataSubsystem->GridSize.Y, 0));
	MinimapMaterialInstance->SetVectorParameterValue(TEXT("UnitSize"), FLinearColor(MaterialUnitWorldSize.X, MaterialUnitWorldSize.Y, 0));
	MinimapMaterialInstance->SetVectorParameterValue(TEXT("FogColor"), FLinearColor::Black);
	MinimapMaterialInstance->SetVectorParameterValue(TEXT("RevealedColor"), MinimapRevealedColor);
	MinimapMaterialInstance->SetScalarParameterValue(TEXT("FogOpacity"), FMath::Clamp(MinimapFogOpacity, 0.0f, 1.0f));

	// Configure Mass Queries once.
	UMassEntitySubsystem* EntitySubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	if (EntitySubsystem)
	{
		FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
		CountQuery = FMassEntityQuery(EntityManager.AsShared());
		CountQuery.AddRequirement<FOW_LOCATION_FRAGMENT>(EMassFragmentAccess::ReadOnly);

		DrawQuery = FMassEntityQuery(EntityManager.AsShared());
		DrawQuery.AddRequirement<FOW_LOCATION_FRAGMENT>(EMassFragmentAccess::ReadOnly);
		DrawQuery.AddRequirement<FOW_TEAM_FRAGMENT>(EMassFragmentAccess::ReadOnly);
		DrawQuery.AddRequirement<FMassVisionFragment>(EMassFragmentAccess::ReadOnly);
	}
	
	UE_LOG(LogMinimapWidget, Log, TEXT("Successfully initialized Minimap System."));

	if (MinimapImage)
	{
		FSlateBrush Brush = MinimapImage->GetBrush();
		Brush.SetResourceObject(MinimapRenderTarget);
		MinimapImage->SetBrush(Brush);
	}
	bIsSuccessfullyInitialized = true;
	return true;
}

FLinearColor UMinimapWidget::GetRecommendedTeamColor(EFogOfWarMinimapTeamColor TeamColor) const
{
	const int32 Index = static_cast<int32>(TeamColor);
	return RecommendedTeamColors.IsValidIndex(Index) ? RecommendedTeamColors[Index] : DefaultTeamColor;
}

void UMinimapWidget::NormalizeRecommendedTeamColors()
{
	const float TargetLength = FMath::Max(0.0f, SelectedUnitColorLength);
	auto NormalizeColor = [TargetLength](FLinearColor& Color)
	{
		const FVector3f Rgb(
			FMath::Max(0.0f, Color.R),
			FMath::Max(0.0f, Color.G),
			FMath::Max(0.0f, Color.B));
		const float Length = Rgb.Size();
		if (Length <= KINDA_SMALL_NUMBER)
		{
			return;
		}

		const float Scale = TargetLength / Length;
		Color.R = FMath::Clamp(Rgb.X * Scale, 0.0f, 1.0f);
		Color.G = FMath::Clamp(Rgb.Y * Scale, 0.0f, 1.0f);
		Color.B = FMath::Clamp(Rgb.Z * Scale, 0.0f, 1.0f);
	};

	NormalizeColor(DefaultTeamColor);
	for (FLinearColor& TeamColor : RecommendedTeamColors)
	{
		NormalizeColor(TeamColor);
	}
}

void UMinimapWidget::ExportMinimapColorConfig()
{
	const FMinimapDisplayOptions DisplayOptions = MakeMinimapDisplayOptions(
		DefaultTeamColor,
		RecommendedTeamColors,
		bNormalizeTeamColorDirection,
		NormalUnitColorLength,
		SelectedUnitColorLength,
		CombatUnitColor,
		bEnableCombatColorFlash,
		CombatColorFlashHz,
		DefaultUnitPixelRadius);
	SaveMinimapColorIni(GetWorld(), DisplayOptions, true);
}

FString UMinimapWidget::GetMinimapColorConfigPath() const
{
	return GetMinimapColorIniPath(GetWorld());
}

void UMinimapWidget::NativeConstruct()
{
	Super::NativeConstruct();
	bIsSuccessfullyInitialized = false;
	bInitPermanentlyFailed = false;
	TimeSinceLastInitRetry = 0.0f;

	// Get Subsystem reference generally
	MinimapDataSubsystem = UMinimapDataSubsystem::Get();

	// Initialize
	InitializeMinimapSystem();
}

FVector UMinimapWidget::ConvertMinimapUVToWorldLocation(const FVector2D& UVPosition) const
{
	if (!MinimapDataSubsystem)
	{
		return FVector::ZeroVector;
	}
	// Coordinate System: X is Up (North), Y is Right (East)
	// UI: X is Right, Y is Down.
	// Map:
	// UI X (Right) -> World Y (East)
	// UI Y (Down)  -> World X (South/Down) => Invert for North

	const FVector2D& Origin = MinimapDataSubsystem->GridBottomLeftWorldLocation;
	const FVector2D& Size = MinimapDataSubsystem->GridSize;

	// UV.X (0..1 Right) -> Delta Y (0..SizeY)
	float WorldY = Origin.Y + UVPosition.X * Size.Y;
	
	// UV.Y (0..1 Down) -> Delta X (SizeX..0)
	// Top (0) -> Max X. Bottom (1) -> Min X.
	float WorldX = Origin.X + (1.0f - UVPosition.Y) * Size.X;

	return FVector(WorldX, WorldY, 0.0f);
}

void UMinimapWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bIsSuccessfullyInitialized)
	{
		if (bInitPermanentlyFailed)
		{
			return;
		}

		TimeSinceLastInitRetry += InDeltaTime;
		const float InitRetryInterval = 0.2f;
		if (TimeSinceLastInitRetry < InitRetryInterval)
		{
			return;
		}
		TimeSinceLastInitRetry = 0.0f;

		if (!MinimapDataSubsystem)
		{
			MinimapDataSubsystem = UMinimapDataSubsystem::Get();
		}
		if (!MinimapDataSubsystem)
		{
			return;
		}

		if (!InitializeMinimapSystem() && MinimapDataSubsystem)
		{
			bInitPermanentlyFailed = true;
			return;
		}

		return;
	}

	TimeSinceLastUpdate += InDeltaTime;
	if (TimeSinceLastUpdate < UpdateInterval && UpdateInterval > 0.0f)
	{
		return;
	}

	TimeSinceLastUpdate = 0.0f;

	// [Path B] 每一帧触发 HashGrid 的直接查询和调试绘制
	// 以后这里会移动到 Mass 的 Processor 中，但为了立即见效，我们在 UI Tick 中驱动。
	if (MinimapDataSubsystem && GetOwningPlayer())
	{
		FVector CenterLocation = FVector::ZeroVector;
		if (APawn* Pawn = GetOwningPlayerPawn())
		{
			CenterLocation = Pawn->GetActorLocation();
		}
		
		// 触发子系统的逻辑 (含 DebugDrawing)
		MinimapDataSubsystem->UpdateMinimapFromHashGrid(CenterLocation, 16); // 半径 16 Block = 范围 32x32 Block
	}

	UpdateMinimapTexture();
}

void UMinimapWidget::UpdateMinimapTexture()
{
	if (!ensure(bIsSuccessfullyInitialized && MinimapDataSubsystem))
	{
		return;
	}

	const FIntPoint EffectiveTextureResolution = GetEffectiveTextureResolution();
	if (!EnsureMinimapRenderTarget(EffectiveTextureResolution))
	{
		return;
	}

	MinimapMaterialInstance->SetVectorParameterValue(TEXT("GridBottomLeftWorldLocation"), FLinearColor(MinimapDataSubsystem->GridBottomLeftWorldLocation.X, MinimapDataSubsystem->GridBottomLeftWorldLocation.Y, 0));
	MinimapMaterialInstance->SetVectorParameterValue(TEXT("GridSize"), FLinearColor(MinimapDataSubsystem->GridSize.X, MinimapDataSubsystem->GridSize.Y, 0));
	MinimapMaterialInstance->SetVectorParameterValue(TEXT("GridWorldSize"), FLinearColor(MinimapDataSubsystem->GridSize.X, MinimapDataSubsystem->GridSize.Y, 0));
	const int32 MaterialTexX = FMath::Max(1, EffectiveTextureResolution.X);
	const int32 MaterialTexY = FMath::Max(1, EffectiveTextureResolution.Y);
	const FVector2D MaterialUnitWorldSize(
		MinimapDataSubsystem->GridSize.X / static_cast<float>(MaterialTexX),
		MinimapDataSubsystem->GridSize.Y / static_cast<float>(MaterialTexY));
	MinimapMaterialInstance->SetVectorParameterValue(TEXT("UnitSize"), FLinearColor(MaterialUnitWorldSize.X, MaterialUnitWorldSize.Y, 0));

	// Always use the optimized Tile-based Rendering (Path B)
	// This relies on the Subsystem populating MinimapTiles from the HashGrid each frame.
	DrawInMassSize();
}

void UMinimapWidget::DrawInLessSize()
{
	UMassEntitySubsystem* EntitySubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	if (!EntitySubsystem || !IconDataTexture || !IconColorTexture || !VisionDataTexture)
	{
		return;
	}

	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();

	// --- 1. Lock Textures for Direct Writing ---
	FTexture2DMipMap& IconDataMip = IconDataTexture->GetPlatformData()->Mips[0];
	FLinearColor* IconDataPtr = static_cast<FLinearColor*>(IconDataMip.BulkData.Lock(LOCK_READ_WRITE));

	FTexture2DMipMap& IconColorMip = IconColorTexture->GetPlatformData()->Mips[0];
	FLinearColor* IconColorPtr = static_cast<FLinearColor*>(IconColorMip.BulkData.Lock(LOCK_READ_WRITE));

	FTexture2DMipMap& VisionDataMip = VisionDataTexture->GetPlatformData()->Mips[0];
	FLinearColor* VisionDataPtr = static_cast<FLinearColor*>(VisionDataMip.BulkData.Lock(LOCK_READ_WRITE));

	FMemory::Memzero(IconDataPtr, IconDataTexture->GetSizeX() * IconDataTexture->GetSizeY() * sizeof(FLinearColor));
	FMemory::Memzero(IconColorPtr, IconColorTexture->GetSizeX() * IconColorTexture->GetSizeY() * sizeof(FLinearColor));
	FMemory::Memzero(VisionDataPtr, VisionDataTexture->GetSizeX() * VisionDataTexture->GetSizeY() * sizeof(FLinearColor));

	// --- 2. Define and Execute Query for All Minimap Entities ---
	int32 UnitCount = 0;
	int32 VisionSourceCount = 0;

	if (!DrawQuery.IsInitialized())
	{
		return;
	}

	// The FMassExecutionContext is now created via the EntityManager and passed to the query.
	FMassExecutionContext Context = EntityManager.CreateExecutionContext(0.f);
	DrawQuery.ForEachEntityChunk(Context, [this, &UnitCount, &VisionSourceCount, IconDataPtr, IconColorPtr, VisionDataPtr](FMassExecutionContext& Context)
	{
		const TConstArrayView<FOW_LOCATION_FRAGMENT> LocationList = Context.GetFragmentView<FOW_LOCATION_FRAGMENT>();
		const TConstArrayView<FOW_TEAM_FRAGMENT> TeamList = Context.GetFragmentView<FOW_TEAM_FRAGMENT>();
		const TConstArrayView<FMassVisionFragment> VisionList = Context.GetFragmentView<FMassVisionFragment>();

		for (int32 i = 0; i < Context.GetNumEntities(); ++i)
		{
			if (UnitCount >= MaxUnits) break;

			const FVector WorldLocation = FOW_GET_LOCATION(LocationList[i]);
			const FMassVisionFragment& VisionFragment = VisionList[i];
			const int32 TeamIndex = FOW_GET_TEAM_INDEX(TeamList[i]);
			const int32 ColorIndex = TeamIndex << 1;
			const FLinearColor UnitColor = MinimapDataSubsystem->TeamDisplayColorsByState.IsValidIndex(ColorIndex)
				? MinimapDataSubsystem->TeamDisplayColorsByState[ColorIndex]
				: MinimapDataSubsystem->DefaultNormalTeamDisplayColor;

			// Write data directly to texture pointers
			IconDataPtr[UnitCount] = FLinearColor(WorldLocation.X, WorldLocation.Y, MinimapDataSubsystem->DefaultMinimapUnitPixelRadius, 1.0f);
			IconColorPtr[UnitCount] = UnitColor;
			UnitCount++;

			if (MinimapDataSubsystem->bEncodeMinimapVisionSources && VisionFragment.SightRadius > 0.0f)
			{
				if (VisionSourceCount >= MaxUnits) break;
				VisionDataPtr[VisionSourceCount] = FLinearColor(WorldLocation.X, WorldLocation.Y, 0.0f, VisionFragment.SightRadius);
				VisionSourceCount++;
			}
		}
	});

	// --- 3. Unlock Textures & Finalize ---
	IconDataMip.BulkData.Unlock();
	IconColorMip.BulkData.Unlock();
	VisionDataMip.BulkData.Unlock();
	IconDataTexture->UpdateResource();
	IconColorTexture->UpdateResource();
	VisionDataTexture->UpdateResource();

	UE_LOG(LogMinimapWidget, Log, TEXT("DrawInLessSize: %d vision sources, %d icons."), VisionSourceCount, UnitCount);

	MinimapMaterialInstance->SetScalarParameterValue(TEXT("NumberOfUnits"), UnitCount);
	MinimapMaterialInstance->SetScalarParameterValue(TEXT("NumberOfVisionSources"), VisionSourceCount);
	MinimapMaterialInstance->SetScalarParameterValue(TEXT("SourceTextureWidth"), FMath::Max(1, MaxUnits));
	MinimapMaterialInstance->SetVectorParameterValue(TEXT("RevealedColor"), MinimapRevealedColor);
	MinimapMaterialInstance->SetScalarParameterValue(TEXT("FogOpacity"), FMath::Clamp(MinimapFogOpacity, 0.0f, 1.0f));

	UKismetRenderingLibrary::ClearRenderTarget2D(this, MinimapRenderTarget, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
	UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, MinimapRenderTarget, MinimapMaterialInstance);
}

void UMinimapWidget::DrawInMassSize()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("Minimap.DrawInMassSize");

	if (!MinimapDataSubsystem || !IconDataTexture || !IconColorTexture || !VisionDataTexture)
	{
		return;
	}

	const FIntPoint GridResolution = MinimapDataSubsystem->MinimapGridResolution;
	const TArray<FMinimapTile>& Tiles = MinimapDataSubsystem->MinimapTiles;
	if (GridResolution.X <= 0 || GridResolution.Y <= 0 || Tiles.Num() == 0)
	{
		return;
	}

	const FIntPoint EffectiveTextureResolution = GetEffectiveTextureResolution();
	const double TotalStartTime = FPlatformTime::Seconds();
	FMinimapDrawPerfStats DrawStats;

	// --- 1. Lock Textures for Direct Writing ---
	const double LockStartTime = FPlatformTime::Seconds();
	FTexture2DMipMap& IconDataMip = IconDataTexture->GetPlatformData()->Mips[0];
	FLinearColor* IconDataPtr = static_cast<FLinearColor*>(IconDataMip.BulkData.Lock(LOCK_READ_WRITE));

	FTexture2DMipMap& IconColorMip = IconColorTexture->GetPlatformData()->Mips[0];
	FLinearColor* IconColorPtr = static_cast<FLinearColor*>(IconColorMip.BulkData.Lock(LOCK_READ_WRITE));

	FTexture2DMipMap& VisionDataMip = VisionDataTexture->GetPlatformData()->Mips[0];
	FLinearColor* VisionDataPtr = static_cast<FLinearColor*>(VisionDataMip.BulkData.Lock(LOCK_READ_WRITE));

	FMemory::Memzero(IconDataPtr, IconDataTexture->GetSizeX() * IconDataTexture->GetSizeY() * sizeof(FLinearColor));
	FMemory::Memzero(IconColorPtr, IconColorTexture->GetSizeX() * IconColorTexture->GetSizeY() * sizeof(FLinearColor));
	FMemory::Memzero(VisionDataPtr, VisionDataTexture->GetSizeX() * VisionDataTexture->GetSizeY() * sizeof(FLinearColor));
	DrawStats.LockTexturesMs = static_cast<float>((FPlatformTime::Seconds() - LockStartTime) * 1000.0);

	// --- 2. Read from Tile Cache and Write to Pointers ---
	DrawStats.SourceTilesScanned = Tiles.Num();
	int32 ActiveTileCount = 0;
	int32 MaterialUnitCount = 0;
	int32 VisionSourceCount = 0;

	int32 MaxUnitsInSingleTile = 0;
	int32 TotalRealUnits = 0;
	TArray<FMinimapCanvasDot> CanvasDots;
	if (bDrawUnitsWithCanvasOverlay)
	{
		CanvasDots.Reserve(FMath::Min(MaxUnits, Tiles.Num()));
	}

	const double ScanStartTime = FPlatformTime::Seconds();
	for (int32 i = 0; i < Tiles.Num(); ++i)
	{
		if (ActiveTileCount >= MaxUnits) break;

		const FMinimapTile& Tile = Tiles[i];
		if (Tile.UnitCount > 0)
		{
			MaxUnitsInSingleTile = FMath::Max(MaxUnitsInSingleTile, Tile.UnitCount);
			TotalRealUnits += Tile.UnitCount;

			const FIntPoint TileIJ(i / GridResolution.Y, i % GridResolution.Y);
			const FVector2D WorldLocation = UMinimapDataSubsystem::ConvertMinimapTileIJToWorldLocation_Static(TileIJ);

			if (bEncodeUnitsIntoMinimapMaterial && MaterialUnitCount < MaxUnits)
			{
				IconDataPtr[MaterialUnitCount] = FLinearColor(WorldLocation.X, WorldLocation.Y, Tile.MaxIconSize, 1.0f);
				IconColorPtr[MaterialUnitCount] = Tile.Color;
				MaterialUnitCount++;
			}

			if (bDrawUnitsWithCanvasOverlay)
			{
				const FVector2D DotPosition(
					((static_cast<float>(TileIJ.Y) + 0.5f) / static_cast<float>(GridResolution.Y)) * static_cast<float>(EffectiveTextureResolution.X),
					(1.0f - ((static_cast<float>(TileIJ.X) + 0.5f) / static_cast<float>(GridResolution.X))) * static_cast<float>(EffectiveTextureResolution.Y));
				CanvasDots.Add({ DotPosition, Tile.Color });
			}
			ActiveTileCount++;

			if (MinimapDataSubsystem->bEncodeMinimapVisionSources && Tile.MaxSightRadius > 0.0f)
			{
				if (VisionSourceCount >= MaxUnits) break;
				VisionDataPtr[VisionSourceCount] = FLinearColor(WorldLocation.X, WorldLocation.Y, 0.0f, Tile.MaxSightRadius);
				VisionSourceCount++;
			}
		}
	}
	DrawStats.ScanTilesMs = static_cast<float>((FPlatformTime::Seconds() - ScanStartTime) * 1000.0);
	DrawStats.ActiveTiles = ActiveTileCount;
	DrawStats.EncodedUnits = MaterialUnitCount;
	DrawStats.EncodedVisionSources = VisionSourceCount;
	DrawStats.TotalUnitsRepresented = TotalRealUnits;
	DrawStats.MaxUnitsInSingleTile = MaxUnitsInSingleTile;

	// --- 3. Unlock Textures & Finalize ---
	IconDataMip.BulkData.Unlock();
	IconColorMip.BulkData.Unlock();
	VisionDataMip.BulkData.Unlock();
	const double UploadStartTime = FPlatformTime::Seconds();
	IconDataTexture->UpdateResource();
	IconColorTexture->UpdateResource();
	VisionDataTexture->UpdateResource();
	DrawStats.UploadTexturesMs = static_cast<float>((FPlatformTime::Seconds() - UploadStartTime) * 1000.0);

	MinimapMaterialInstance->SetScalarParameterValue(TEXT("NumberOfUnits"), MaterialUnitCount);
	MinimapMaterialInstance->SetScalarParameterValue(TEXT("NumberOfVisionSources"), VisionSourceCount);
	MinimapMaterialInstance->SetScalarParameterValue(TEXT("SourceTextureWidth"), FMath::Max(1, MaxUnits));
	MinimapMaterialInstance->SetVectorParameterValue(TEXT("RevealedColor"), MinimapRevealedColor);
	MinimapMaterialInstance->SetScalarParameterValue(TEXT("FogOpacity"), FMath::Clamp(MinimapFogOpacity, 0.0f, 1.0f));

	const double DrawStartTime = FPlatformTime::Seconds();
	UKismetRenderingLibrary::ClearRenderTarget2D(this, MinimapRenderTarget, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
	UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, MinimapRenderTarget, MinimapMaterialInstance);
	if (bDrawUnitsWithCanvasOverlay && CanvasDots.Num() > 0)
	{
		UCanvas* Canvas = nullptr;
		FVector2D CanvasSize = FVector2D::ZeroVector;
		FDrawToRenderTargetContext DrawContext;
		UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, MinimapRenderTarget, Canvas, CanvasSize, DrawContext);
		if (Canvas)
		{
			const FVector2D DotSize(CanvasUnitDotSize, CanvasUnitDotSize);
			const FVector2D HalfDotSize = DotSize * 0.5f;
			for (const FMinimapCanvasDot& Dot : CanvasDots)
			{
				Canvas->K2_DrawBox(Dot.Position - HalfDotSize, DotSize, CanvasUnitDotSize, Dot.Color);
			}
		}
		UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, DrawContext);
	}
	DrawStats.DrawRenderTargetMs = static_cast<float>((FPlatformTime::Seconds() - DrawStartTime) * 1000.0);
	DrawStats.TotalMs = static_cast<float>((FPlatformTime::Seconds() - TotalStartTime) * 1000.0);
	MinimapDataSubsystem->RecordMinimapDrawPerfStats(DrawStats);
}
