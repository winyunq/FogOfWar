// Copyright Winyunq, 2025. All Rights Reserved.

#include "FogOfWar.h"

#include "FogOfWarMassBinding.h"
#include "Components/BrushComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/Texture2D.h"
#include "MassEntitySubsystem.h"
#include "Subsystems/MassBattleHashGridSubsystem.h"
#include "Subsystems/MinimapDataSubsystem.h"
#include "Utils/ManagerComponent.h"
#include "Utils/ManagerStatics.h"
#include "Utils/Macros.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY(LogFogOfWar);

DECLARE_STATS_GROUP(TEXT("FogOfWar"), STATGROUP_FogOfWar, STATCAT_Advanced);

namespace Names
{
	const TCHAR* ScenePerformanceCsvRelativePath = TEXT("Logs/FogOfWar_ScenePerf.csv");

	DECLARE_STATIC_FNAME(FOW_NotVisibleRegionBrightness);
	DECLARE_STATIC_FNAME(FOW_BottomLeftWorldLocation);
	DECLARE_STATIC_FNAME(FOW_GridSize);
	DECLARE_STATIC_FNAME(FOW_GridWorldSize);
	DECLARE_STATIC_FNAME(FOW_SceneGpuVisionSourceTexture);
	DECLARE_STATIC_FNAME(FOW_SceneGpuVisionSourceCount);
	DECLARE_STATIC_FNAME(FOW_EnableSceneGpuVisionSources);

	UTexture2D* CreateSceneGpuVisionDataTexture(UObject* Outer, int32 Width)
	{
		if (!Outer || Width <= 0)
		{
			return nullptr;
		}

		UTexture2D* Texture = UTexture2D::CreateTransient(Width, 1, PF_A32B32G32R32F, TEXT("SceneGpuVisionSourceTexture"));
		if (!Texture)
		{
			return nullptr;
		}

		Texture->CompressionSettings = TextureCompressionSettings::TC_VectorDisplacementmap;
		Texture->SRGB = 0;
		Texture->Filter = TF_Nearest;
		Texture->UpdateResource();
		return Texture;
	}

}

AFogOfWar::AFogOfWar()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcessComponent"));
	PostProcess->SetupAttachment(RootComponent);
}

void AFogOfWar::SetCommonMIDParameters(UMaterialInstanceDynamic* MID)
{
	if (!MID)
	{
		return;
	}

	MID->SetVectorParameterValue(Names::FOW_BottomLeftWorldLocation, FVector(GridBottomLeftWorldLocation.X, GridBottomLeftWorldLocation.Y, 0));
	MID->SetVectorParameterValue(Names::FOW_GridSize, FVector(GridSize.X, GridSize.Y, 0));
	MID->SetVectorParameterValue(Names::FOW_GridWorldSize, FVector(GridSize.X, GridSize.Y, 0));
	MID->SetScalarParameterValue(Names::FOW_NotVisibleRegionBrightness, NotVisibleRegionBrightness);
	MID->SetScalarParameterValue(Names::FOW_EnableSceneGpuVisionSources, bEnableSceneGpuVisionSources ? 1.0f : 0.0f);
	MID->SetScalarParameterValue(Names::FOW_SceneGpuVisionSourceCount, static_cast<float>(SceneGpuVisionSourceCount));
	if (SceneGpuVisionSourceTexture)
	{
		MID->SetTextureParameterValue(Names::FOW_SceneGpuVisionSourceTexture, SceneGpuVisionSourceTexture);
	}
}

void AFogOfWar::Activate()
{
	if (!ensure(!bActivated))
	{
		return;
	}
	bActivated = true;

	checkf(IsValid(GridVolume), TEXT("Volume was not set for the FogOfWar Volume"));
	checkf(IsValid(PostProcessingMaterial), TEXT("PostProcessingMaterial must be set. GPU FogOfWar uses a single post-process material."));

	Initialize();

	UMinimapDataSubsystem* MinimapSubsystem = UMinimapDataSubsystem::Get();
	if (MinimapSubsystem)
	{
		MinimapSubsystem->SetVisionGridActive(false);
	}

	SceneGpuVisionSourceTexture = Names::CreateSceneGpuVisionDataTexture(this, FMath::Max(1, MaxSceneGpuVisionSources));
	PostProcessingMID = UMaterialInstanceDynamic::Create(PostProcessingMaterial, this);
	SetCommonMIDParameters(PostProcessingMID);
	if (SceneGpuVisionSourceTexture)
	{
		PostProcessingMID->SetTextureParameterValue(Names::FOW_SceneGpuVisionSourceTexture, SceneGpuVisionSourceTexture);
	}

	PostProcess->AddOrUpdateBlendable(PostProcessingMID);

	auto GameManager = UManagerStatics::GetGameManager(this);
	GameManager->Register<ThisClass>(this);
	PrimaryActorTick.SetTickFunctionEnable(true);
}

void AFogOfWar::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoActivate)
	{
		Activate();
	}
}

#if WITH_EDITOR
void AFogOfWar::RefreshVolumeInEditor()
{
	if (GetWorld() && !GetWorld()->IsGameWorld())
	{
		Initialize();
	}
}
#endif

#if WITH_EDITOR
bool AFogOfWar::CanEditChange(const FProperty* InProperty) const
{
	if (!Super::CanEditChange(InProperty))
	{
		return false;
	}

	const FName PropertyName = InProperty->GetFName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(AFogOfWar, GridVolume) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(AFogOfWar, PostProcessingMaterial))
	{
		return !GetWorld() || !GetWorld()->IsGameWorld();
	}

	return true;
}
#endif

#if WITH_EDITOR
void AFogOfWar::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.Property->GetFName();

	if (GetWorld() && GetWorld()->IsGameWorld())
	{
		if (PropertyName == GET_MEMBER_NAME_CHECKED(AFogOfWar, NotVisibleRegionBrightness))
		{
			if (IsValid(PostProcessingMID))
			{
				PostProcessingMID->SetScalarParameterValue(Names::FOW_NotVisibleRegionBrightness, NotVisibleRegionBrightness);
			}
			return;
		}
	}

	if (GetWorld() && !GetWorld()->IsGameWorld())
	{
		if (PropertyName == GET_MEMBER_NAME_CHECKED(AFogOfWar, GridVolume))
		{
			RefreshVolumeInEditor();
			return;
		}
	}
}
#endif

void AFogOfWar::Tick(float DeltaSeconds)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("Tick"), STAT_FogOfWarTick, STATGROUP_FogOfWar);

	Super::Tick(DeltaSeconds);

	UpdateSceneGpuVisionSourceTexture();
}

void AFogOfWar::Initialize()
{
	if (!IsValid(GridVolume))
	{
		GridSize = FVector2D::Zero();
		GridBottomLeftWorldLocation = FVector2D::Zero();

		return;
	}

	UBrushComponent* VolumeBrush = GridVolume->GetBrushComponent();
	FBoxSphereBounds Bounds = VolumeBrush->CalcBounds(VolumeBrush->GetComponentTransform());

	GridSize = {
		Bounds.BoxExtent.X * 2,
		Bounds.BoxExtent.Y * 2
	};
	GridBottomLeftWorldLocation = {
		Bounds.Origin.X - GridSize.X / 2,
		Bounds.Origin.Y - GridSize.Y / 2
	};
	if (UMinimapDataSubsystem* MinimapSubsystem = UMinimapDataSubsystem::Get())
	{
		MinimapSubsystem->SyncWorldBounds(GridBottomLeftWorldLocation, GridSize);
	}
}

void AFogOfWar::UpdateSceneGpuVisionSourceTexture()
{
	const double TotalStartTime = FPlatformTime::Seconds();
	if (!PostProcessingMID)
	{
		return;
	}

	PostProcessingMID->SetScalarParameterValue(Names::FOW_EnableSceneGpuVisionSources, bEnableSceneGpuVisionSources ? 1.0f : 0.0f);
	if (!bEnableSceneGpuVisionSources)
	{
		PostProcessingMID->SetScalarParameterValue(Names::FOW_SceneGpuVisionSourceCount, 0.0f);
		SceneGpuVisionSourceCount = 0;
		return;
	}

	if (!SceneGpuVisionSourceTexture || SceneGpuVisionSourceTexture->GetSizeX() != FMath::Max(1, MaxSceneGpuVisionSources))
	{
		SceneGpuVisionSourceTexture = Names::CreateSceneGpuVisionDataTexture(this, FMath::Max(1, MaxSceneGpuVisionSources));
		if (SceneGpuVisionSourceTexture)
		{
			PostProcessingMID->SetTextureParameterValue(Names::FOW_SceneGpuVisionSourceTexture, SceneGpuVisionSourceTexture);
		}
	}
	if (!SceneGpuVisionSourceTexture)
	{
		return;
	}

	UWorld* World = GetWorld();
	UMassEntitySubsystem* EntitySubsystem = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	UMassBattleHashGridSubsystem* HashGrid = World ? World->GetSubsystem<UMassBattleHashGridSubsystem>() : nullptr;
	if (!EntitySubsystem || !HashGrid)
	{
		return;
	}

	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();

	const int32 SafeMaxSources = FMath::Max(1, MaxSceneGpuVisionSources);
	SceneGpuVisionSourceDataBuffer.SetNumZeroed(SafeMaxSources);
	SceneGpuVisionSourceCount = 0;
	int32 VisitedCells = 0;
	int32 VisitedAgents = 0;

	const double CollectStartTime = FPlatformTime::Seconds();
	auto UploadCellVisionSources = [this, SafeMaxSources, &EntityManager, &VisitedCells, &VisitedAgents](const FHashGridAgentCell& Cell)
	{
		if (SceneGpuVisionSourceCount >= SafeMaxSources)
		{
			return;
		}

		VisitedCells++;
		for (const FAgentGridData& AgentData : Cell.Agents)
		{
			VisitedAgents++;
			if (SceneGpuVisionSourceCount >= SafeMaxSources)
			{
				break;
			}

			if (!EntityManager.IsEntityValid(AgentData.EntityHandle))
			{
				continue;
			}

			const FMassVisionFragment* VisionFrag = EntityManager.GetFragmentDataPtr<FMassVisionFragment>(AgentData.EntityHandle);
			if (!VisionFrag)
			{
				continue;
			}

			const float SightRadius = VisionFrag->SightRadius;
			if (SightRadius <= 0.0f)
			{
				continue;
			}

			const FVector WorldLocation = Cell.CellLocation + AgentData.GetRelativeLocation();
			const float UploadRadius = SightRadius + SceneGpuVisionSourceRadiusPadding;

			SceneGpuVisionSourceDataBuffer[SceneGpuVisionSourceCount] = FLinearColor(WorldLocation.X, WorldLocation.Y, UploadRadius, 0.0f);
			SceneGpuVisionSourceCount++;
		}
	};

	for (const TPair<FIntVector, TSharedPtr<FAgentGridBlock>>& BlockPair : HashGrid->AgentGrid)
	{
		if (SceneGpuVisionSourceCount >= SafeMaxSources)
		{
			break;
		}
		if (!BlockPair.Value.IsValid())
		{
			continue;
		}

		const FAgentGridBlock& Block = *BlockPair.Value;
		for (TConstSetBitIterator<> CellIt(Block.OccupiedCells.OccupiedCellBitArray); CellIt; ++CellIt)
		{
			if (SceneGpuVisionSourceCount >= SafeMaxSources)
			{
				break;
			}

			UploadCellVisionSources(Block.Cells[CellIt.GetIndex()]);
		}
	}
	const float CollectMs = static_cast<float>((FPlatformTime::Seconds() - CollectStartTime) * 1000.0);

	const double UploadStartTime = FPlatformTime::Seconds();
	FTexture2DMipMap& Mip = SceneGpuVisionSourceTexture->GetPlatformData()->Mips[0];
	void* TextureData = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(TextureData, SceneGpuVisionSourceDataBuffer.GetData(), sizeof(FLinearColor) * SafeMaxSources);
	Mip.BulkData.Unlock();
	SceneGpuVisionSourceTexture->UpdateResource();
	const float UploadMs = static_cast<float>((FPlatformTime::Seconds() - UploadStartTime) * 1000.0);

	PostProcessingMID->SetScalarParameterValue(Names::FOW_SceneGpuVisionSourceCount, static_cast<float>(SceneGpuVisionSourceCount));
	PostProcessingMID->SetTextureParameterValue(Names::FOW_SceneGpuVisionSourceTexture, SceneGpuVisionSourceTexture);

	if (bEnableSceneGpuVisionPerformanceStats)
	{
		const float TotalMs = static_cast<float>((FPlatformTime::Seconds() - TotalStartTime) * 1000.0);
		RecordSceneGpuVisionPerfStats(TotalMs, CollectMs, UploadMs, VisitedCells, VisitedAgents);
	}
}

void AFogOfWar::RecordSceneGpuVisionPerfStats(float TotalMs, float CollectMs, float UploadMs, int32 VisitedCells, int32 VisitedAgents)
{
	SceneGpuVisionPerfTotalMsAccum += TotalMs;
	SceneGpuVisionPerfCollectMsAccum += CollectMs;
	SceneGpuVisionPerfUploadMsAccum += UploadMs;
	SceneGpuVisionPerfSourceCountAccum += SceneGpuVisionSourceCount;
	SceneGpuVisionPerfVisitedCellsAccum += VisitedCells;
	SceneGpuVisionPerfVisitedAgentsAccum += VisitedAgents;
	SceneGpuVisionPerfSampleCount++;

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const double CurrentTime = World->GetTimeSeconds();
	if (CurrentTime - SceneGpuVisionPerfLastFlushTime >= SceneGpuVisionPerformanceLogInterval)
	{
		FlushSceneGpuVisionPerfStats(CurrentTime);
	}
}

void AFogOfWar::FlushSceneGpuVisionPerfStats(double CurrentTime)
{
	if (SceneGpuVisionPerfSampleCount <= 0)
	{
		return;
	}

	const float InvSamples = 1.0f / static_cast<float>(SceneGpuVisionPerfSampleCount);
	const FString CsvColumns = FString::Printf(
		TEXT("%d,%.3f,%.3f,%.3f,%.1f,%.1f,%.1f"),
		SceneGpuVisionPerfSampleCount,
		SceneGpuVisionPerfTotalMsAccum * InvSamples,
		SceneGpuVisionPerfCollectMsAccum * InvSamples,
		SceneGpuVisionPerfUploadMsAccum * InvSamples,
		SceneGpuVisionPerfSourceCountAccum * InvSamples,
		SceneGpuVisionPerfVisitedCellsAccum * InvSamples,
		SceneGpuVisionPerfVisitedAgentsAccum * InvSamples);

	if (bLogSceneGpuVisionPerformanceToOutputLog)
	{
		UE_LOG(LogFogOfWar, Log, TEXT("[FogOfWarPerf][SceneGpuVisionAvg] %s"), *CsvColumns);
	}
	AppendSceneGpuVisionPerfCsvLine(CsvColumns);

	SceneGpuVisionPerfSampleCount = 0;
	SceneGpuVisionPerfTotalMsAccum = 0.0f;
	SceneGpuVisionPerfCollectMsAccum = 0.0f;
	SceneGpuVisionPerfUploadMsAccum = 0.0f;
	SceneGpuVisionPerfSourceCountAccum = 0;
	SceneGpuVisionPerfVisitedCellsAccum = 0;
	SceneGpuVisionPerfVisitedAgentsAccum = 0;
	SceneGpuVisionPerfLastFlushTime = CurrentTime;
}

void AFogOfWar::AppendSceneGpuVisionPerfCsvLine(const FString& CsvColumns) const
{
	if (!bWriteSceneGpuVisionPerformanceCsv || !GetWorld())
	{
		return;
	}

	const FString FilePath = FPaths::ProjectSavedDir() / Names::ScenePerformanceCsvRelativePath;
	const bool bNeedsHeader = !FPaths::FileExists(FilePath);
	FString Output;
	if (bNeedsHeader)
	{
		Output += TEXT("WorldTime,Channel,Samples,AvgTotalMs,AvgCollectMs,AvgUploadMs,AvgSourceCount,AvgVisitedCells,AvgVisitedAgents\n");
	}
	Output += FString::Printf(TEXT("%.3f,SceneGpuVisionAvg,%s\n"), GetWorld()->GetTimeSeconds(), *CsvColumns);
	FFileHelper::SaveStringToFile(Output, *FilePath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
}
