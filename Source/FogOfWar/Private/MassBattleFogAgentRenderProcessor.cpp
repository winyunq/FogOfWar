/*
* FogOfWar-owned snapshot of MassBattleAgentRenderProcessor.
* See the matching header for upstream hashes and synchronization rules.
*/

#include "MassBattleFogAgentRenderProcessor.h"
#include "FogOfWarModule.h"
#include "MassBattleFogVisionSourceFragment.h"
#include "Fragments/MassBattleISKMFragment.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Subsystems/MassBattleISKMWorldSubsystem.h"
#include "Subsystems/MassBattleFogRenderSubsystem.h"

CSV_DEFINE_CATEGORY(FogMassBattleRender, true);

namespace UE::FogOfWar::Private
{
	FORCEINLINE bool IsAttackRevealState(const EAttackState State)
	{
		return State == EAttackState::PreCast_FirstExec
			|| State == EAttackState::PreCast
			|| State == EAttackState::PostCast;
	}

	FORCEINLINE float EncodeMinimapTeam(const int32 TeamIndex)
	{
		const uint32 PackedTeam = static_cast<uint32>(FMath::Clamp(TeamIndex, 0, 1023));
		float PackedTeamAsFloat = 0.0f;
		FMemory::Memcpy(&PackedTeamAsFloat, &PackedTeam, sizeof(PackedTeamAsFloat));
		return PackedTeamAsFloat;
	}
}

// Fragments
#include "Fragments/PrimaryType.h"
#include "Fragments/SubType.h"
#include "Fragments/StyleType.h"
#include "Fragments/Transform.h"
#include "Fragments/Collider.h"
#include "Fragments/Animation.h"
#include "Fragments/Health.h"
#include "Fragments/HealthBar.h"
#include "Fragments/TextPop.h"
#include "Fragments/Render.h"
#include "Fragments/LOD.h"
#include "Fragments/RenderBatchData.h"
#include "Fragments/GridData.h"
#include "Fragments/Obstacle.h"
#include "Fragments/Debuff.h"
#include "Fragments/Defence.h"
#include "Fragments/Appear.h"
#include "Fragments/Attack.h"
#include "Fragments/Hit.h"
#include "Fragments/Death.h"
#include "Fragments/Move.h"
#include "Fragments/Trace.h"
#include "Fragments/Event.h"
#include "Fragments/Slow.h"
#include "Fragments/Debug.h"
#include "Fragments/ProjectileHostConfig.h"
#include "Fragments/Loot.h"
#include "Components/MassBattleAgentComponent.h"

// Subsystems
#include "Subsystems/MassBattleSubsystem.h"
#include "Subsystems/MassBattleAgentSubsystem.h"
#include "Subsystems/MassBattleHashGridSubsystem.h"
#include "MassAPISubsystem.h"
#include "MassEntityManager.h"
#include "MassExecutionContext.h"
#include "MassEntityQuery.h"
#include "MassAPIStructs.h"
#include "MassCommandBuffer.h"
#include "MassProcessingTypes.h"
#include "MassBattleEnums.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"

// Niagara / Render
#include "Renderers/MassBattleAgentRenderer.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraDataChannelPublic.h"
#include "NiagaraDataChannel.h"
#include "NiagaraDataChannelAccessor.h"

// Utilities
#include "Async/ParallelFor.h"
#include "Algo/Sort.h"
#include "Containers/Queue.h"

// --------------------------------------------------------------------------------
// Custom Writer for Bulk Access
// --------------------------------------------------------------------------------
struct FMassTextPopWriter : public FNDCWriterBase
{
	FNiagaraDataChannelVariableBuffer* PositionBuffer = nullptr;
	FNiagaraDataChannelVariableBuffer* LocationBuffer = nullptr;
	FNiagaraDataChannelVariableBuffer* ParamsBuffer = nullptr; // Vector4
	FNiagaraDataChannelVariableBuffer* SubTypeBuffer = nullptr;

	bool bLocationIsLWC = false;
	bool bParamsIsLWC = false;

	void InitBuffers()
	{
		if (!Data) return;
		const auto& Layout = Data->GetLayoutInfo();
		if (!Layout) return;

		auto FindBuf = [&](FName Name) -> FNiagaraDataChannelVariableBuffer*
		{
			for (const auto& Pair : Layout->GetGameDataLayout().VariableIndices)
			{
				if (Pair.Key.GetName() == Name)
				{
					return Data->FindVariableBuffer(Pair.Key);
				}
			}
			return nullptr;
		};

		PositionBuffer = FindBuf(FName("TextPosition"));
		LocationBuffer = FindBuf(FName("TextLocation"));
		ParamsBuffer = FindBuf(FName("TextValueStyleScaleOffset"));
		SubTypeBuffer = FindBuf(FName("SubType"));

		// Cache type information
		if (LocationBuffer)
		{
			for (const auto& Pair : Layout->GetGameDataLayout().VariableIndices)
			{
				if (Pair.Key.GetName() == FName("TextLocation"))
				{
					if (Pair.Key.GetSizeInBytes() == sizeof(FVector))
					{
						bLocationIsLWC = true;
					}
					break;
				}
			}
		}

		if (ParamsBuffer)
		{
			for (const auto& Pair : Layout->GetGameDataLayout().VariableIndices)
			{
				if (Pair.Key.GetName() == FName("TextValueStyleScaleOffset"))
				{
					if (Pair.Key.GetSizeInBytes() == sizeof(FVector4))
					{
						bParamsIsLWC = true;
					}
					break;
				}
			}
		}
	}

	void AppendBatch(int32 WriteOffset, TConstArrayView<FVector> Locs, TConstArrayView<FVector4f> Params, int32 SubTypeVal)
	{
		const int32 BatchCount = Locs.Num();
		if (BatchCount == 0) return;

		auto BulkCopy = [&](FNiagaraDataChannelVariableBuffer* Buf, const void* Src, int32 ElementSize)
		{
			if (Buf && Buf->Data.Num() >= (WriteOffset + BatchCount) * ElementSize)
			{
				uint8* Dest = Buf->Data.GetData() + (WriteOffset * ElementSize);
				FMemory::Memcpy(Dest, Src, BatchCount * ElementSize);
			}
		};

		if (PositionBuffer)
		{
			BulkCopy(PositionBuffer, Locs.GetData(), sizeof(FVector));
		}

		if (LocationBuffer)
		{
			if (bLocationIsLWC)
			{
				BulkCopy(LocationBuffer, Locs.GetData(), sizeof(FVector));
			}
			else
			{
				TArray<FVector3f> Locs3f;
				Locs3f.Reserve(BatchCount);
				for (const FVector& L : Locs)
				{
					Locs3f.Add((FVector3f)L);
				}
				BulkCopy(LocationBuffer, Locs3f.GetData(), sizeof(FVector3f));
			}
		}

		if (ParamsBuffer)
		{
			if (bParamsIsLWC)
			{
				TArray<FVector4> ParamsDouble;
				ParamsDouble.Reserve(BatchCount);
				for (const FVector4f& P : Params)
				{
					ParamsDouble.Add(FVector4(P));
				}
				BulkCopy(ParamsBuffer, ParamsDouble.GetData(), sizeof(FVector4));
			}
			else
			{
				BulkCopy(ParamsBuffer, Params.GetData(), sizeof(FVector4f));
			}
		}

		if (SubTypeBuffer && SubTypeBuffer->Data.Num() >= (WriteOffset + BatchCount) * sizeof(int32))
		{
			int32* Dest = reinterpret_cast<int32*>(SubTypeBuffer->Data.GetData() + (WriteOffset * sizeof(int32)));
			for (int32 i = 0; i < BatchCount; ++i)
			{
				Dest[i] = SubTypeVal;
			}
		}
	}
};

UMassBattleFogAgentRenderProcessor::UMassBattleFogAgentRenderProcessor()
	: EntityQuery(*this)
	, ProjectileInterpQuery(*this)
	, LootInterpQuery(*this)
	, MinimapSnapshotQuery(*this)
	, VisibilityWorkSetQuery(*this)
	, VisionSourceSampleQuery(*this)
{
	ExecutionOrder.ExecuteAfter.Add(TEXT("MassBattleHostMonoProcessor"));

	ExecutionFlags = (int32)(EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone);
	ProcessingPhase = EMassProcessingPhase::FrameEnd;
	bAutoRegisterWithProcessingPhases = true;
	bRequiresGameThreadExecution = true;

	ExecutionPriority = 10;
	ActiveRenderEntityCollection = MakeShared<UE::Mass::FEntityCollection>();
	VisionSourceEntityCollection = MakeShared<UE::Mass::FEntityCollection>();
}

FString UMassBattleFogAgentRenderProcessor::GetProcessorName() const
{
	// Preserve downstream dependency edges such as MassBattleFxRenderProcessor
	// without changing MassBattleFrame itself.
	return TEXT("MassBattleAgentRenderProcessor");
}

void UMassBattleFogAgentRenderProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	// Configure the real MassBattleFrame processor as well. When no active fog
	// scene actor exists Execute delegates directly to this base implementation.
	Super::ConfigureQueries(EntityManager);

	FEntityQueryBuilder(EntityQuery)
		.All<FAgentTag>()
		.None<FMassBattleISKMAddonFragment>()
		.Optional<FNotRenderingTag, FRenderingWithParticleTag, FRenderingWithActorTag, FRenderingTag, FAppearingTag, FDyingTag>()
		.All<FSubType, FStyleType, FTeam, FLocating, FRotating, FCollider, FVisualize, FHealth, FHealthBar, FAppear, FAttack, FHit, FDeath, FMove, FDefence, FDebuffing>(MARO)
		.All<FEntityFlagFragment, FStatistics, FScaling, FVisualizing, FTextPop, FAttacking, FMoving>(MARW)
		.Optional<FMassBattleFogLastSeenFragment>(MARW)
		.All<FAnimating>(MARW)
		.All<FLODShared, FAnimShared, FTracingShared>(MARO)
		.Optional<FMassBattleFogVisionSourceFragment>(MARO)
		.Optional<FAgentEvent, FAgentDebug>(MARO)
		.RegisterWithProcessor(*this);

	FEntityQueryBuilder(ProjectileInterpQuery)
		.All<FProjectileTag>()
		.All<FProjectileParams, FLocating, FRotating, FScaling>(MARO)
		.All<FProjectileRendering>(MARW)
		.RegisterWithProcessor(*this);

	FEntityQueryBuilder(LootInterpQuery)
		.All<FLootTag>()
		.All<FLootParams, FLocating, FRotating, FScaling>(MARO)
		.All<FLootRendering>(MARW)
		.RegisterWithProcessor(*this);

	FEntityQueryBuilder(MinimapSnapshotQuery)
		.All<FAgentTag>()
		.All<FEntityFlagFragment, FTeam, FLocating, FVisualize>(MARO)
		.Optional<FMassBattleFogLastSeenFragment>(MARO)
		.Optional<FMassBattleISKMAddonFragment>(MARO)
		.Optional<FMassBattleFogVisionSourceFragment>(MARO)
		.RegisterWithProcessor(*this);

	FEntityQueryBuilder(VisibilityWorkSetQuery)
		.All<FAgentTag>()
		.All<FEntityFlagFragment, FTeam, FLocating, FMoving, FVisualize, FSubType, FAttacking>(MARO)
		.Optional<FMassBattleFogLastSeenFragment, FMassBattleISKMAddonFragment>(MARO)
		.Optional<FMassBattleFogVisionSourceFragment>(MARO)
		.RegisterWithProcessor(*this);

	FEntityQueryBuilder(VisionSourceSampleQuery)
		.All<FAgentTag>()
		.All<FEntityFlagFragment, FLocating, FMoving>(MARO)
		.RegisterWithProcessor(*this);
}

int32 UMassBattleFogAgentRenderProcessor::FindOrCreateProxy(
	UMassAPISubsystem& MassAPI,
	const FMassEntityHandle Entity,
	const int32 SubType)
{
	if (!Entity.IsSet() || Entity.Index < 0)
	{
		return INDEX_NONE;
	}

	if (ProxyIdByEntityIndex.Num() <= Entity.Index)
	{
		const int32 OldNum = ProxyIdByEntityIndex.Num();
		ProxyIdByEntityIndex.SetNum(Entity.Index + 1, EAllowShrinking::No);
		for (int32 Index = OldNum; Index < ProxyIdByEntityIndex.Num(); ++Index)
		{
			ProxyIdByEntityIndex[Index] = INDEX_NONE;
		}
	}

	int32& ProxyIdByIndex = ProxyIdByEntityIndex[Entity.Index];
	if (ProxyPool.IsValidIndex(ProxyIdByIndex))
	{
		FMassBattleFogRenderProxy& Existing = ProxyPool[ProxyIdByIndex];
		if (Existing.Entity == Entity)
		{
			if (Existing.SubType != SubType)
			{
				DeactivateProxy(ProxyIdByIndex, &MassAPI);
				Existing.SubType = SubType;
				Existing.bSnapOnNextUpdate = true;
			}
			return ProxyIdByIndex;
		}

		// Entity indices are recycled by Mass. Never let a stale serial number
		// inherit the old entity's work-set membership or interpolation identity.
		ReleaseProxy(ProxyIdByIndex, &MassAPI);
		ProxyIdByIndex = INDEX_NONE;
	}

	const int32 NewProxyId = FreeProxyIds.IsEmpty()
		? ProxyPool.AddDefaulted()
		: FreeProxyIds.Pop(EAllowShrinking::No);
	FMassBattleFogRenderProxy& Proxy = ProxyPool[NewProxyId];
	Proxy = FMassBattleFogRenderProxy{};
	Proxy.Entity = Entity;
	Proxy.SubType = SubType;
	ProxyIdByIndex = NewProxyId;
	return NewProxyId;
}

void UMassBattleFogAgentRenderProcessor::ActivateProxy(const int32 ProxyId)
{
	if (!ProxyPool.IsValidIndex(ProxyId))
	{
		return;
	}

	FMassBattleFogRenderProxy& Proxy = ProxyPool[ProxyId];
	if (Proxy.ActiveListIndex != INDEX_NONE || Proxy.SubType == INDEX_NONE)
	{
		return;
	}

	FMassBattleFogSubTypeWorkSet& WorkSet = RenderWorkSets.FindOrAdd(Proxy.SubType);
	Proxy.ActiveListIndex = WorkSet.ActiveProxyIds.Add(ProxyId);
	Proxy.bSnapOnNextUpdate = true;
	Proxy.PendingRemovalStartWorldTime = -1.0;
	++Proxy.NiagaraAcquireTag;
	if (Proxy.NiagaraAcquireTag == 0)
	{
		++Proxy.NiagaraAcquireTag;
	}
	++WorkSet.MembershipVersion;
	++ActiveWorkSetMembershipVersion;
}

void UMassBattleFogAgentRenderProcessor::DeactivateProxy(
	const int32 ProxyId,
	UMassAPISubsystem* MassAPI)
{
	if (!ProxyPool.IsValidIndex(ProxyId))
	{
		return;
	}

	FMassBattleFogRenderProxy& Proxy = ProxyPool[ProxyId];
	if (Proxy.ActiveListIndex == INDEX_NONE)
	{
		return;
	}

	FMassBattleFogSubTypeWorkSet* WorkSet = RenderWorkSets.Find(Proxy.SubType);
	if (!WorkSet || !WorkSet->ActiveProxyIds.IsValidIndex(Proxy.ActiveListIndex))
	{
		Proxy.ActiveListIndex = INDEX_NONE;
		Proxy.bSnapOnNextUpdate = true;
		return;
	}

	TArray<int32>& Active = WorkSet->ActiveProxyIds;
	const int32 RemoveIndex = Proxy.ActiveListIndex;
	const int32 LastIndex = Active.Num() - 1;
	if (RemoveIndex != LastIndex)
	{
		const int32 MovedProxyId = Active[LastIndex];
		Active[RemoveIndex] = MovedProxyId;
		if (ProxyPool.IsValidIndex(MovedProxyId))
		{
			ProxyPool[MovedProxyId].ActiveListIndex = RemoveIndex;
		}
	}
	Active.Pop(EAllowShrinking::No);
	Proxy.ActiveListIndex = INDEX_NONE;
	Proxy.bSnapOnNextUpdate = true;
	++WorkSet->MembershipVersion;
	++ActiveWorkSetMembershipVersion;
}

void UMassBattleFogAgentRenderProcessor::ReleaseProxy(
	const int32 ProxyId,
	UMassAPISubsystem* MassAPI)
{
	if (!ProxyPool.IsValidIndex(ProxyId))
	{
		return;
	}

	DeactivateProxy(ProxyId, MassAPI);
	FMassBattleFogRenderProxy& Proxy = ProxyPool[ProxyId];
	if (Proxy.Entity.Index >= 0
		&& ProxyIdByEntityIndex.IsValidIndex(Proxy.Entity.Index)
		&& ProxyIdByEntityIndex[Proxy.Entity.Index] == ProxyId)
	{
		ProxyIdByEntityIndex[Proxy.Entity.Index] = INDEX_NONE;
	}
	Proxy = FMassBattleFogRenderProxy{};
	FreeProxyIds.Add(ProxyId);
}

void UMassBattleFogAgentRenderProcessor::QueueRendererClassLoad(
	const TSoftClassPtr<AMassBattleAgentRenderer>& RendererClass)
{
	if (RendererClass.IsNull() || RendererClass.IsValid())
	{
		return;
	}

	const FSoftObjectPath ClassPath = RendererClass.ToSoftObjectPath();
	if (RendererClassLoadHandles.Contains(ClassPath))
	{
		return;
	}

	RendererClassLoadHandles.Add(
		ClassPath,
		UAssetManager::GetStreamableManager().RequestAsyncLoad(
			ClassPath,
			FStreamableDelegate(),
			FStreamableManager::DefaultAsyncLoadPriority,
			false,
			false,
			TEXT("FogVisibleAgentRenderer")));
}

void UMassBattleFogAgentRenderProcessor::RefreshActiveRenderWorkSet(
	FMassExecutionContext& ProcessingContext,
	UMassAPISubsystem& MassAPI,
	UMassBattleAgentSubsystem& AgentSubsystem,
	UMassBattleHashGridSubsystem& HashGrid,
	UMassBattleFogRenderSubsystem& FogRenderSubsystem,
	const bool bCollectVisionSources,
	const bool bAllowRemoval,
	const double WorldTimeSeconds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FogOfWar_RefreshActiveRenderWorkSet);
	++VisibilityEpoch;
	if (VisibilityEpoch == 0)
	{
		++VisibilityEpoch;
		for (FMassBattleFogRenderProxy& Proxy : ProxyPool)
		{
			Proxy.LastVisitedEpoch = 0;
			Proxy.LastVisibleEpoch = 0;
		}
	}

	FVector2D QueryMin = FogRenderSubsystem.GetRenderCullWorldMin();
	FVector2D QueryMax = FogRenderSubsystem.GetRenderCullWorldMax();
	if (bCollectVisionSources)
	{
		const FVector2D VisionMin = FogRenderSubsystem.GetVisionCollectionWorldMin();
		const FVector2D VisionMax = FogRenderSubsystem.GetVisionCollectionWorldMax();
		QueryMin.X = FMath::Min(QueryMin.X, VisionMin.X);
		QueryMin.Y = FMath::Min(QueryMin.Y, VisionMin.Y);
		QueryMax.X = FMath::Max(QueryMax.X, VisionMax.X);
		QueryMax.Y = FMath::Max(QueryMax.Y, VisionMax.Y);
	}

	const double QueryCenterZ = HashGrid.GridOrigin.Z;
	const double QueryHalfHeight = FMath::Max<double>(HashGrid.AgentCellSize.Z, ActiveWorkSetHalfHeightUU);
	FIntVector MinCoord = HashGrid.AgentLocationToCoord(FVector(QueryMin.X, QueryMin.Y, QueryCenterZ - QueryHalfHeight));
	FIntVector MaxCoord = HashGrid.AgentLocationToCoord(FVector(QueryMax.X, QueryMax.Y, QueryCenterZ + QueryHalfHeight));
	if (MinCoord.X > MaxCoord.X) Swap(MinCoord.X, MaxCoord.X);
	if (MinCoord.Y > MaxCoord.Y) Swap(MinCoord.Y, MaxCoord.Y);
	if (MinCoord.Z > MaxCoord.Z) Swap(MinCoord.Z, MaxCoord.Z);

	int32 OccupiedCellVisits = 0;
	int32 SpatialCandidateVisits = 0;
	int32 FogVisibilityTests = 0;
	int32 VisibleCandidates = 0;
	int32 VisionGatherCandidateVisits = 0;
	const bool bPrimeRendererClasses = !bRenderWorkSetInitialized;
	if (bCollectVisionSources)
	{
		// Source generation already needs a world-scale pass. Walking the HashGrid
		// and then resolving 20-30k fragments by handle is strictly extra work in
		// that case. One narrow Mass query keeps the reads contiguous and produces
		// both the next source snapshot and the final camera/fog render set.
		VisibilityWorkSetQuery.ForEachEntityChunk(
			ProcessingContext,
			[&](FMassExecutionContext& VisibilityContext)
			{
				const int32 NumEntities = VisibilityContext.GetNumEntities();
				const auto FlagsList = VisibilityContext.GetFragmentView<FEntityFlagFragment>();
				const auto TeamList = VisibilityContext.GetFragmentView<FTeam>();
				const auto LocatingList = VisibilityContext.GetFragmentView<FLocating>();
				const auto MovingList = VisibilityContext.GetFragmentView<FMoving>();
				const auto RenderList = VisibilityContext.GetFragmentView<FVisualize>();
				const auto SubTypeList = VisibilityContext.GetFragmentView<FSubType>();
				const auto AttackingList = VisibilityContext.GetFragmentView<FAttacking>();
				const auto LastSeenList =
					VisibilityContext.GetFragmentView<FMassBattleFogLastSeenFragment>();
				const auto AddonISKMList =
					VisibilityContext.GetFragmentView<FMassBattleISKMAddonFragment>();
				const bool bHasLastSeen = LastSeenList.Num() == NumEntities;
				const bool bHasAddonISKM = AddonISKMList.Num() == NumEntities;
				const FMassBattleFogVisionSourceFragment* FogPolicy =
					VisibilityContext.GetConstSharedFragmentPtr<FMassBattleFogVisionSourceFragment>();
				const bool bProvidesVision = !FogPolicy || FogPolicy->bProvidesVision;
				const EMassBattleFogVisibilityPolicy VisibilityPolicy = FogPolicy
					? FogPolicy->VisibilityPolicy
					: EMassBattleFogVisibilityPolicy::Standard;

				for (int32 Index = 0; Index < NumEntities; ++Index)
				{
					++SpatialCandidateVisits;
					const FEntityFlagFragment& Flags = FlagsList[Index];
					if (!Flags.HasFlag(ActivatedFlag))
					{
						continue;
					}
					// This first source pass is already visiting every active agent.
					// Queue each soft renderer class here so a later camera reveal never
					// turns the unavoidable asset load into a game-thread sync flush.
					if (bPrimeRendererClasses && RenderList[Index].bEnable)
					{
						QueueRendererClassLoad(RenderList[Index].RendererClass);
					}

					const FMassEntityHandle Entity = VisibilityContext.GetEntity(Index);
					const FVector& Location = LocatingList[Index].Location;
					const int32 TeamIndex = TeamList[Index].index;
					const bool bFriendly = FogRenderSubsystem.IsFriendlyTeam(TeamIndex);
					if (FogRenderSubsystem.ShouldCollectVisionSource(Location))
					{
						++VisionGatherCandidateVisits;
						if (bFriendly && bProvidesVision)
						{
							const FVector3f& Velocity = MovingList[Index].CurrentVelocity;
							VisionSourceQueue.Items.Add(FVector4f(
								static_cast<float>(Location.X),
								static_cast<float>(Location.Y),
								Velocity.X,
								Velocity.Y));
							VisionSourceEntityQueue.Items.Add(Entity);
						}
					}

					FMassBattleFogRenderProxy* ExistingActiveProxy = nullptr;
					if (ProxyIdByEntityIndex.IsValidIndex(Entity.Index))
					{
						const int32 ExistingProxyId = ProxyIdByEntityIndex[Entity.Index];
						if (ProxyPool.IsValidIndex(ExistingProxyId)
							&& ProxyPool[ExistingProxyId].Entity == Entity
							&& ProxyPool[ExistingProxyId].ActiveListIndex != INDEX_NONE)
						{
							ExistingActiveProxy = &ProxyPool[ExistingProxyId];
						}
					}
					if (!bAllowRemoval
						&& ExistingActiveProxy
						&& ExistingActiveProxy->PendingRemovalStartWorldTime < 0.0)
					{
						// Admission passes only look for newly visible units. Stable active
						// units remain in the per-frame O(V) renderer without another mask test.
						continue;
					}

					if (!FogRenderSubsystem.IsInsideRenderWindow(Location))
					{
						continue;
					}

					bool bAttackRevealed = false;
					if (!bFriendly)
					{
						bAttackRevealed = Flags.HasFlag(AttackingFlag)
							&& UE::FogOfWar::Private::IsAttackRevealState(AttackingList[Index].State);
						bAttackRevealed = bAttackRevealed
							|| FogRenderSubsystem.IsAttackRevealActive(Entity);
					}

					++FogVisibilityTests;
					uint8 VisibilityState = FogRenderSubsystem.GetUnitVisibilityState(
						TeamIndex,
						Location,
						bAttackRevealed);
					if (VisibilityState < 2u)
					{
						if (VisibilityPolicy == EMassBattleFogVisibilityPolicy::AlwaysFogVisible)
						{
							VisibilityState = 2u;
						}
						else if (VisibilityPolicy == EMassBattleFogVisibilityPolicy::RememberLastSeen
							&& bHasLastSeen
							&& LastSeenList[Index].bHasSnapshot
							&& FogRenderSubsystem.IsInsideRenderWindow(LastSeenList[Index].SnapshotLocation))
						{
							VisibilityState = 2u;
						}
					}
					if (VisibilityState < 2u)
					{
						continue;
					}

					const bool bUsesAddonISKM = bHasAddonISKM && AddonISKMList[Index].bEnable;
					if (!RenderList[Index].bEnable && !bUsesAddonISKM)
					{
						continue;
					}
					if (!bUsesAddonISKM)
					{
						QueueRendererClassLoad(RenderList[Index].RendererClass);
					}

					const int32 ProxyId = FindOrCreateProxy(
						MassAPI,
						Entity,
						SubTypeList[Index].Index);
					if (!ProxyPool.IsValidIndex(ProxyId))
					{
						continue;
					}

					FMassBattleFogRenderProxy& Proxy = ProxyPool[ProxyId];
					Proxy.FogVisibilityState = VisibilityState;
					Proxy.bFriendlyTeam = bFriendly;
					Proxy.bUsesAddonISKM = bUsesAddonISKM;
					Proxy.PendingRemovalStartWorldTime = -1.0;
					Proxy.LastVisitedEpoch = VisibilityEpoch;
					Proxy.LastVisibleEpoch = VisibilityEpoch;
					ActivateProxy(ProxyId);
					++VisibleCandidates;
				}
			});
	}
	else
	{
		TSet<FMassEntityHandle> SeenMultiCellEntities;
		HashGrid.ForEachOccupiedAgentCellInRange(
		MinCoord,
		MaxCoord,
		[&](const FHashGridAgentCell& Cell)
		{
			++OccupiedCellVisits;
			for (const FAgentGridData& GridData : Cell.Agents)
			{
				++SpatialCandidateVisits;
				const FMassEntityHandle Entity = GridData.EntityHandle;
				bool bAlreadySeenMultiCell = false;
				if (GridData.bMultiCell)
				{
					SeenMultiCellEntities.Add(Entity, &bAlreadySeenMultiCell);
				}
				if (bAlreadySeenMultiCell)
				{
					continue;
				}
				if (!MassAPI.IsValid(Entity))
				{
					continue;
				}

				// HashGrid already stores the position and velocity in one cache-line.
				// Use them here instead of performing random FLocating/FMoving lookups.
				const FVector GridLocation = Cell.CellLocation + GridData.GetRelativeLocation();
				const bool bInsideRenderWindow = FogRenderSubsystem.IsInsideRenderWindow(GridLocation);
				const bool bInsideVisionCollection = bCollectVisionSources
					&& FogRenderSubsystem.ShouldCollectVisionSource(GridLocation);
				if (!bInsideRenderWindow && !bInsideVisionCollection)
				{
					continue;
				}
				if (!bAllowRemoval
					&& ProxyIdByEntityIndex.IsValidIndex(Entity.Index))
				{
					const int32 ExistingProxyId = ProxyIdByEntityIndex[Entity.Index];
					if (ProxyPool.IsValidIndex(ExistingProxyId))
					{
						const FMassBattleFogRenderProxy& ExistingProxy = ProxyPool[ExistingProxyId];
						if (ExistingProxy.Entity == Entity
							&& ExistingProxy.ActiveListIndex != INDEX_NONE
							&& ExistingProxy.PendingRemovalStartWorldTime < 0.0)
						{
							continue;
						}
					}
				}

				const FTeam* Team = MassAPI.GetFragmentPtr<FTeam>(Entity);
				if (!Team)
				{
					continue;
				}
				const bool bFriendly = FogRenderSubsystem.IsFriendlyTeam(Team->index);
				const FMassBattleFogVisionSourceFragment* FogPolicy = nullptr;
				if (bInsideVisionCollection)
				{
					++VisionGatherCandidateVisits;
					if (bFriendly)
					{
						FogPolicy = MassAPI.GetConstSharedFragmentPtr<FMassBattleFogVisionSourceFragment>(Entity);
						if (!FogPolicy || FogPolicy->bProvidesVision)
						{
							VisionSourceQueue.Items.Add(FVector4f(
								static_cast<float>(GridLocation.X),
								static_cast<float>(GridLocation.Y),
								static_cast<float>(GridData.CurrentVelX),
								static_cast<float>(GridData.CurrentVelY)));
							VisionSourceEntityQueue.Items.Add(Entity);
						}
					}
				}
				if (!bInsideRenderWindow)
				{
					continue;
				}
				const FEntityFlagFragment* Flags = nullptr;
				bool bAttackRevealed = false;
				if (!bFriendly)
				{
					if (GridData.bIsAttacker)
					{
						Flags = MassAPI.GetFragmentPtr<FEntityFlagFragment>(Entity);
						const FAttacking* Attacking = MassAPI.GetFragmentPtr<FAttacking>(Entity);
						bAttackRevealed = Flags && Attacking
							&& Flags->HasFlag(AttackingFlag)
							&& UE::FogOfWar::Private::IsAttackRevealState(Attacking->State);
					}
					bAttackRevealed = bAttackRevealed
						|| FogRenderSubsystem.IsAttackRevealActive(Entity);
				}
				++FogVisibilityTests;
				uint8 VisibilityState = FogRenderSubsystem.GetUnitVisibilityState(
					Team->index,
					GridLocation,
					bAttackRevealed);

				// Policy data cannot change an already final state-2/3 decision. Read it
				// only for a unit that the camera + terrain mask would otherwise reject.
				if (VisibilityState < 2u)
				{
					if (!FogPolicy)
					{
						FogPolicy = MassAPI.GetConstSharedFragmentPtr<FMassBattleFogVisionSourceFragment>(Entity);
					}
					const EMassBattleFogVisibilityPolicy VisibilityPolicy = FogPolicy
						? FogPolicy->VisibilityPolicy
						: EMassBattleFogVisibilityPolicy::Standard;
					if (VisibilityPolicy == EMassBattleFogVisibilityPolicy::AlwaysFogVisible)
					{
						VisibilityState = 2u;
					}
					else if (VisibilityPolicy == EMassBattleFogVisibilityPolicy::RememberLastSeen)
					{
						const FMassBattleFogLastSeenFragment* LastSeen =
							MassAPI.GetFragmentPtr<FMassBattleFogLastSeenFragment>(Entity);
						if (LastSeen && LastSeen->bHasSnapshot
							&& FogRenderSubsystem.IsInsideRenderWindow(LastSeen->SnapshotLocation))
						{
							VisibilityState = 2u;
						}
					}
				}

				if (VisibilityState < 2u)
				{
					continue;
				}

				// Expensive presentation fragments are touched only after the final
				// camera + fog decision. Hidden units stop above this line.
				if (!Flags)
				{
					Flags = MassAPI.GetFragmentPtr<FEntityFlagFragment>(Entity);
				}
				if (!Flags || !Flags->HasFlag(ActivatedFlag))
				{
					continue;
				}
				const FMassBattleISKMAddonFragment* AddonISKM =
					MassAPI.GetFragmentPtr<FMassBattleISKMAddonFragment>(Entity);
				const bool bUsesAddonISKM = AddonISKM && AddonISKM->bEnable;
				const FVisualize* Render = MassAPI.GetFragmentPtr<FVisualize>(Entity);
				if ((!Render || !Render->bEnable) && !bUsesAddonISKM)
				{
					continue;
				}
				if (!bUsesAddonISKM && Render)
				{
					QueueRendererClassLoad(Render->RendererClass);
				}
				const FSubType* SubType = MassAPI.GetFragmentPtr<FSubType>(Entity);
				if (!SubType)
				{
					continue;
				}

				const int32 ProxyId = FindOrCreateProxy(MassAPI, Entity, SubType->Index);
				if (!ProxyPool.IsValidIndex(ProxyId))
				{
					continue;
				}

				FMassBattleFogRenderProxy& Proxy = ProxyPool[ProxyId];
				Proxy.FogVisibilityState = VisibilityState;
				Proxy.bFriendlyTeam = bFriendly;
				Proxy.bUsesAddonISKM = bUsesAddonISKM;
				Proxy.PendingRemovalStartWorldTime = -1.0;
				if (Proxy.LastVisitedEpoch == VisibilityEpoch)
				{
					continue;
				}
				Proxy.LastVisitedEpoch = VisibilityEpoch;
				Proxy.LastVisibleEpoch = VisibilityEpoch;
				ActivateProxy(ProxyId);
				++VisibleCandidates;
			}
			});
	}

	// Admission runs at the fresh mask rate, while removals converge at 3 Hz.
	// Only a convergence pass scans the previous Active set.
	if (bAllowRemoval)
	{
		for (TPair<int32, FMassBattleFogSubTypeWorkSet>& WorkSetPair : RenderWorkSets)
		{
			TArray<int32>& Active = WorkSetPair.Value.ActiveProxyIds;
			for (int32 ActiveIndex = Active.Num() - 1; ActiveIndex >= 0; --ActiveIndex)
			{
				const int32 ProxyId = Active[ActiveIndex];
				if (!ProxyPool.IsValidIndex(ProxyId))
				{
					continue;
				}

				FMassBattleFogRenderProxy& Proxy = ProxyPool[ProxyId];
				if (!MassAPI.IsValid(Proxy.Entity))
				{
					ReleaseProxy(ProxyId, &MassAPI);
					continue;
				}
				if (Proxy.LastVisibleEpoch == VisibilityEpoch)
				{
					Proxy.PendingRemovalStartWorldTime = -1.0;
					continue;
				}

			// Remembered structures may stay at a snapshot that is inside the
			// camera window even after their live entity has moved elsewhere.
				const FMassBattleFogVisionSourceFragment* FogPolicy =
					MassAPI.GetConstSharedFragmentPtr<FMassBattleFogVisionSourceFragment>(Proxy.Entity);
				const bool bRememberLastSeen = FogPolicy
					&& FogPolicy->VisibilityPolicy == EMassBattleFogVisibilityPolicy::RememberLastSeen;
				const FMassBattleFogLastSeenFragment* LastSeen = bRememberLastSeen
					? MassAPI.GetFragmentPtr<FMassBattleFogLastSeenFragment>(Proxy.Entity)
					: nullptr;
				const bool bKeepSnapshot = bRememberLastSeen
					&& LastSeen && LastSeen->bHasSnapshot
					&& FogRenderSubsystem.IsInsideRenderWindow(LastSeen->SnapshotLocation);
				if (bKeepSnapshot)
				{
					Proxy.LastVisibleEpoch = VisibilityEpoch;
					Proxy.PendingRemovalStartWorldTime = -1.0;
					continue;
				}

				const double RemovalDelay = FogRenderSubsystem.GetUnitVisibilityRemovalDelay();
				if (Proxy.PendingRemovalStartWorldTime < 0.0)
				{
					Proxy.PendingRemovalStartWorldTime = WorldTimeSeconds;
					if (RemovalDelay > 0.0)
					{
						continue;
					}
				}
				if (WorldTimeSeconds - Proxy.PendingRemovalStartWorldTime < RemovalDelay)
				{
					continue;
				}

			// Actor-backed representations must leave their ticking pooled actor
			// when the proxy leaves the Active work set. This is a delta-sized queue
			// operation and does not change Mass tags or scan hidden entities.
				FVisualizing* Rendering = MassAPI.GetFragmentPtr<FVisualizing>(Proxy.Entity);
				if (Rendering
					&& Rendering->bIsBindingActorSwappable
					&& (IsValid(Rendering->BindingActorPtr) || IsValid(Rendering->BindingComponentPtr)))
				{
					if (const FStatistics* Statistics = MassAPI.GetFragmentPtr<FStatistics>(Proxy.Entity))
					{
						AgentSubsystem.ActorRecycleQueue.Enqueue({ Proxy.Entity, Statistics->UniqueID });
					}
				}

				DeactivateProxy(ProxyId, &MassAPI);
			}
		}
	}

	bRenderWorkSetInitialized = true;
	LastRenderWorkSetRevision = FogRenderSubsystem.GetRenderWorkSetRevision();
	CSV_CUSTOM_STAT(FogMassBattleRender, SpatialOccupiedCells, OccupiedCellVisits, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(FogMassBattleRender, SpatialCandidates, SpatialCandidateVisits, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(FogMassBattleRender, FogVisibilityTests, FogVisibilityTests, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(FogMassBattleRender, FogVisibleCandidates, VisibleCandidates, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(FogMassBattleRender, VisionGatherCandidates, VisionGatherCandidateVisits, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(FogMassBattleRender, VisionSources, VisionSourceQueue.Items.Num(), ECsvCustomStatOp::Set);
}

void UMassBattleFogAgentRenderProcessor::SetBatchFrameCount(
	FAgentRenderBatchData& Data,
	const int32 Count)
{
	const int32 SafeCount = FMath::Max(0, Count);
	Data.FreeSlotArray.SetNumUninitialized(SafeCount, EAllowShrinking::No);
	Data.IsHiddenArray.SetNumUninitialized(SafeCount, EAllowShrinking::No);
	for (int32 Index = 0; Index < SafeCount; ++Index)
	{
		Data.FreeSlotArray[Index] = true;
		Data.IsHiddenArray[Index] = true;
	}
	Data.LocationArray.SetNumUninitialized(SafeCount, EAllowShrinking::No);
	Data.OrientationArray.SetNumUninitialized(SafeCount, EAllowShrinking::No);
	Data.ScaleArray.SetNumUninitialized(SafeCount, EAllowShrinking::No);
	Data.DynamicParams0_Array.SetNumUninitialized(SafeCount, EAllowShrinking::No);
	Data.HealthBar_Opacity_CurrentRatio_TargetRatio_Array.SetNumUninitialized(SafeCount, EAllowShrinking::No);
	Data.CurrentLODArray.SetNumUninitialized(SafeCount, EAllowShrinking::No);
	Data.StyleArray.SetNumUninitialized(SafeCount, EAllowShrinking::No);
}

void UMassBattleFogAgentRenderProcessor::PrepareDenseRenderFrame(
	UMassAPISubsystem& MassAPI,
	UMassBattleSubsystem& MassBattle,
	TArray<FMassEntityHandle>* OutActiveRenderEntities,
	const bool bForceDenseLayoutRebuild)
{
	// Retain allocations, but make every existing component empty until its
	// current dense extent is known. Cost is O(batch count), never O(old slots).
	for (TPair<int32, TObjectPtr<AMassBattleAgentRenderer>>& RendererPair : MassBattle.AgentRenderers)
	{
		if (!IsValid(RendererPair.Value))
		{
			continue;
		}
		for (TPair<int32, FAgentRenderBatchData>& BatchPair : RendererPair.Value->SpawnedRenderBatches)
		{
			SetBatchFrameCount(BatchPair.Value, 0);
		}
	}

	int32 TotalActiveCount = 0;
	for (TPair<int32, FMassBattleFogSubTypeWorkSet>& WorkSetPair : RenderWorkSets)
	{
		const int32 SubTypeIndex = WorkSetPair.Key;
		FMassBattleFogSubTypeWorkSet& WorkSet = WorkSetPair.Value;

		AMassBattleAgentRenderer* Renderer = MassBattle.AgentRenderers.FindRef(SubTypeIndex);
		const bool bHasRenderer = IsValid(Renderer);
		const bool bRebuildDenseLayout = bForceDenseLayoutRebuild
			|| OutActiveRenderEntities != nullptr
			|| WorkSet.DenseLayoutMembershipVersion != WorkSet.MembershipVersion
			|| WorkSet.DenseLayoutRenderer.Get() != Renderer;

		if (bRebuildDenseLayout)
		{
			WorkSet.DenseProxyIds.Reset();
			WorkSet.DenseRuntimes.Reset();

			for (const int32 ProxyId : WorkSet.ActiveProxyIds)
			{
				if (!ProxyPool.IsValidIndex(ProxyId))
				{
					continue;
				}
				FMassBattleFogRenderProxy& Proxy = ProxyPool[ProxyId];
				if (!MassAPI.IsValid(Proxy.Entity))
				{
					continue;
				}

				// The scheduled collection contains every active representation, while
				// this dense layout contains only the Particle VAT subset.
				if (OutActiveRenderEntities)
				{
					OutActiveRenderEntities->Add(Proxy.Entity);
				}
				if (!bHasRenderer || Proxy.bUsesAddonISKM)
				{
					continue;
				}
				FVisualizing* Rendering = MassAPI.GetFragmentPtr<FVisualizing>(Proxy.Entity);
				if (!Rendering
					|| Rendering->CurrentRepresentation != ERepresentationMode::Particle)
				{
					continue;
				}
				WorkSet.DenseProxyIds.Add(ProxyId);
				WorkSet.DenseRuntimes.Add(Rendering);
			}

			TArray<int32>& BatchIds = WorkSet.FrameBatchIds;
			BatchIds.Reset();
			if (bHasRenderer)
			{
				const int32 BatchSize = FMath::Max(1, Renderer->RenderBatchSize);
				const int32 RequiredBatchCount =
					FMath::DivideAndRoundUp(WorkSet.DenseProxyIds.Num(), BatchSize);
				BatchIds.Reserve(Renderer->SpawnedRenderBatches.Num());
				for (const TPair<int32, FAgentRenderBatchData>& BatchPair : Renderer->SpawnedRenderBatches)
				{
					BatchIds.Add(BatchPair.Key);
				}
				Algo::Sort(BatchIds);
				while (BatchIds.Num() < RequiredBatchCount)
				{
					const int32 NewBatchId = AddRenderBatch(Renderer);
					if (NewBatchId == INDEX_NONE)
					{
						// Niagara components are unavailable with rendering disabled
						// (for example -nullrhi). Keep simulation entities alive and
						// simply omit their particle representation for this frame.
						WorkSet.DenseProxyIds.Reset();
						BatchIds.Reset();
						break;
					}
					BatchIds.Add(NewBatchId);
				}
				Algo::Sort(BatchIds);

				for (int32 DenseIndex = 0; DenseIndex < WorkSet.DenseProxyIds.Num(); ++DenseIndex)
				{
					FVisualizing* Rendering = WorkSet.DenseRuntimes.IsValidIndex(DenseIndex)
						? WorkSet.DenseRuntimes[DenseIndex]
						: nullptr;
					if (!Rendering)
					{
						continue;
					}

					const int32 ProxyId = WorkSet.DenseProxyIds[DenseIndex];
					FMassBattleFogRenderProxy& Proxy = ProxyPool[ProxyId];
					const int32 BatchIndex = DenseIndex / BatchSize;
					Rendering->RenderBatchId = BatchIds[BatchIndex];
					Rendering->InstanceId = DenseIndex % BatchSize;
					Rendering->RendererActor = Renderer;
					if (Proxy.bSnapOnNextUpdate)
					{
						Rendering->bInterpInitialized = false;
						Proxy.bSnapOnNextUpdate = false;
					}
				}
			}

			// Fragment pointers are scratch only; Mass may move fragments after this frame.
			WorkSet.DenseRuntimes.Reset();
			WorkSet.DenseLayoutMembershipVersion = WorkSet.MembershipVersion;
			WorkSet.DenseLayoutRenderer = Renderer;
		}

		if (!bHasRenderer)
		{
			TotalActiveCount += WorkSet.ActiveProxyIds.Num();
			continue; // The scheduled simulation pass performs first registration.
		}

		const int32 BatchSize = FMath::Max(1, Renderer->RenderBatchSize);
		const int32 RequiredBatchCount = FMath::DivideAndRoundUp(WorkSet.DenseProxyIds.Num(), BatchSize);
		TArray<int32>& BatchIds = WorkSet.FrameBatchIds;

		for (int32 BatchIndex = 0; BatchIndex < RequiredBatchCount; ++BatchIndex)
		{
			const int32 FirstDenseIndex = BatchIndex * BatchSize;
			const int32 FrameCount = FMath::Min(BatchSize, WorkSet.DenseProxyIds.Num() - FirstDenseIndex);
			if (FAgentRenderBatchData* Data = Renderer->SpawnedRenderBatches.Find(BatchIds[BatchIndex]))
			{
				SetBatchFrameCount(*Data, FrameCount);
			}
		}
		TotalActiveCount += WorkSet.ActiveProxyIds.Num();
	}

	CSV_CUSTOM_STAT(FogMassBattleRender, ActiveProxies, TotalActiveCount, ECsvCustomStatOp::Set);
}


void UMassBattleFogAgentRenderProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// No configured scene actor means FogOfWar is logically disabled. Execute
	// MassBattleFrame's actual implementation, not a copied "full scan" branch
	// inside the fog pipeline. This keeps the no-actor map as the A/B baseline.
	UWorld* const World = Context.GetWorld();
	UMassBattleFogRenderSubsystem* FogRenderSubsystem = World
		? World->GetSubsystem<UMassBattleFogRenderSubsystem>()
		: nullptr;
	if (!FogRenderSubsystem || !FogRenderSubsystem->IsConfiguredActive())
	{
		Super::Execute(EntityManager, Context);
		return;
	}

	CSV_SCOPED_TIMING_STAT(FogMassBattleRender, Processor);
	auto& MB = UMassBattleSubsystem::GetRef(this);

	// Track whether this is a simulation tick or just a render frame
	const bool bIsSimTick = MB.IsSubFrameActive(ESubFrame::CombatAndTick);
	const float SimDeltaTime = MB.GetCalculatedStepTime();
	const float RenderDeltaTime = World->GetDeltaSeconds();
	const double FogWorldTimeSeconds = World->GetTimeSeconds();
	if (bIsSimTick)
	{
		AttackRevealQueue.Reset();
	}
	uint32 RequestedVisionSampleRevision = 0;
	const bool bSampleVisionSources =
		FogRenderSubsystem->ConsumeVisionSourceSampleRequest(RequestedVisionSampleRevision);
	uint32 VisionCollectionRevision = 0;
	bool bCollectVisionSources = false;
	const bool bCollectMinimapSnapshot = FogRenderSubsystem->ConsumeMinimapSnapshotCollectionRequest();
	if (bCollectMinimapSnapshot)
	{
		MinimapVisionSourceUnitQueue.Reset();
		MinimapFriendlyNonVisionUnitQueue.Reset();
		MinimapOtherUnitQueue.Reset();
		MinimapFogVisibleUnitQueue.Reset();
	}

	auto& MA = UMassAPISubsystem::GetRef(this);

	auto& MS = UMassBattleAgentSubsystem::GetRef(this);
	auto& HashGrid = UMassBattleHashGridSubsystem::GetRef(this);
	const bool bRenderRevisionChanged = !bRenderWorkSetInitialized
		|| LastRenderWorkSetRevision != FogRenderSubsystem->GetRenderWorkSetRevision();
	const float VisibilityConvergenceRateHz =
		FogRenderSubsystem->GetUnitVisibilityConvergenceRateHz();
	const double VisibilityConvergenceInterval = VisibilityConvergenceRateHz > 0.0f
		? 1.0 / static_cast<double>(VisibilityConvergenceRateHz)
		: 0.0;
	// Convergence never creates another traversal. It rides on a fresh mask/camera
	// revision that already requires an admission pass, so 3 Hz replaces work
	// instead of turning the existing 15 Hz refresh into 15 + 3 Hz.
	const bool bFullWorkSetConvergence = !bRenderWorkSetInitialized
		|| (bRenderRevisionChanged
			&& (VisibilityConvergenceRateHz <= 0.0f
				|| FogWorldTimeSeconds - LastFullWorkSetRefreshWorldTime >= VisibilityConvergenceInterval));
	const bool bRefreshRenderWorkSet = bRenderRevisionChanged;
	if (bRefreshRenderWorkSet)
	{
		// Keep the low-frequency source-membership request pending until the next
		// filter revision. Position/velocity sampling is handled independently at
		// the scene cadence from that compact membership.
		bCollectVisionSources =
			FogRenderSubsystem->ConsumeVisionSourceCollectionRequest(VisionCollectionRevision);
		if (bCollectVisionSources)
		{
			VisionSourceQueue.Reset();
			VisionSourceEntityQueue.Reset();
		}
	}
	CSV_CUSTOM_STAT(FogMassBattleRender, WorkSetRefresh, bRefreshRenderWorkSet ? 1 : 0, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(
		FogMassBattleRender,
		FullWorkSetConvergence,
		bFullWorkSetConvergence ? 1 : 0,
		ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(
		FogMassBattleRender,
		VisionGather,
		bCollectVisionSources ? 1 : 0,
		ECsvCustomStatOp::Set);
	if (bRefreshRenderWorkSet)
	{
		FogRenderSubsystem->PruneExpiredAttackReveals(FogWorldTimeSeconds);
		// One union scan when both logical-source collection and presentation
		// refresh are due. Never traverse the overlapping HashGrid window twice.
		RefreshActiveRenderWorkSet(
			Context,
			MA,
			MS,
			HashGrid,
			*FogRenderSubsystem,
			bCollectVisionSources,
			bFullWorkSetConvergence,
			FogWorldTimeSeconds);
		if (bFullWorkSetConvergence)
		{
			LastFullWorkSetRefreshWorldTime = FogWorldTimeSeconds;
		}
		if (bCollectVisionSources)
		{
			*VisionSourceEntityCollection =
				UE::Mass::FEntityCollection(VisionSourceEntityQueue.Items);
			VisionSourceEntityCollectionRevision = VisionCollectionRevision;
		}
	}

	const bool bRebuildActiveEntityHandles =
		CachedActiveCollectionMembershipVersion != ActiveWorkSetMembershipVersion;
	CSV_CUSTOM_STAT(
		FogMassBattleRender,
		ActiveHandleListRebuilt,
		bRebuildActiveEntityHandles ? 1 : 0,
		ECsvCustomStatOp::Set);
	if (bRebuildActiveEntityHandles)
	{
		ActiveRenderEntitiesScratch.Reset();
	}
	PrepareDenseRenderFrame(
		MA,
		MB,
		bRebuildActiveEntityHandles ? &ActiveRenderEntitiesScratch : nullptr,
		bPreviousFrameWasSimulationTick);
	if (bRebuildActiveEntityHandles)
	{
		*ActiveRenderEntityCollection = UE::Mass::FEntityCollection(ActiveRenderEntitiesScratch);
		CachedActiveCollectionMembershipVersion = ActiveWorkSetMembershipVersion;
		if (UMassBattleISKMWorldSubsystem* ISKM =
			World->GetSubsystem<UMassBattleISKMWorldSubsystem>())
		{
			ISKM->PublishExternalVisibleEntities(
				ActiveRenderEntityCollection.ToSharedRef(),
				ActiveWorkSetMembershipVersion);
		}
	}

	// Combat simulation remains owned by MassBattle's logical processors. This
	// replacement is a presentation processor: under fog filtering it must never
	// turn a simulation sub-frame into a full render traversal. LOD, VAT state,
	// interpolation and packing therefore run only for the active work set.
	// This is O(archetype count) while valid. UE rebuilds ranges from the stored
	// handles only after an entity-order/archetype version change.
	const TConstArrayView<FMassArchetypeEntityCollection> ActiveRenderCollections =
		ActiveRenderEntityCollection->GetUpToDatePerArchetypeCollections(EntityManager);

	TArray<FAgentRenderBatchData*> AllRenderBatches;
	TMap<FAgentRenderBatchData*, int32> StableBatchIndices;

	if (bIsSimTick)
	{
		for (TPair<int32, TObjectPtr<AMassBattleAgentRenderer>>& Pair : MB.AgentRenderers)
		{
			if (!IsValid(Pair.Value))
			{
				continue;
			}
			for (auto& BatchPair : Pair.Value->SpawnedRenderBatches)
			{
				FAgentRenderBatchData* BatchData = &BatchPair.Value;
				const int32 BatchIndex = AllRenderBatches.Add(BatchData);
				StableBatchIndices.Add(BatchData, BatchIndex);
			}
		}
	}

	auto ProcessAgentChunk = [&, this](FMassExecutionContext& Context)
	{
		auto NumEntities = Context.GetNumEntities();
		const auto& TracingShared = Context.GetSharedFragment<FTracingShared>();

		TArray<FAnimStateChangeData> LocalAnimEvents;
		TArray<FMassEntityHandle> LocalRegistration;
		TArray<FMassBattleFogAttackRevealObservation> LocalAttackReveals;

		auto LocatingList = Context.GetFragmentView<FLocating>();
		auto RotatingList = Context.GetFragmentView<FRotating>();
		auto ScalingList = Context.GetMutableFragmentView<FScaling>();
		auto ColliderList = Context.GetFragmentView<FCollider>();
		auto RenderList = Context.GetFragmentView<FVisualize>();
		const FLODShared& LODShared = Context.GetSharedFragment<FLODShared>();
		auto RenderingList = Context.GetMutableFragmentView<FVisualizing>();
		auto FogLastSeenList = Context.GetMutableFragmentView<FMassBattleFogLastSeenFragment>();
		auto AnimatingList = Context.GetMutableFragmentView<FAnimating>();

		auto FlagsList = Context.GetMutableFragmentView<FEntityFlagFragment>();
		auto StatisticsList = Context.GetMutableFragmentView<FStatistics>();

		auto StyleTypeList = Context.GetFragmentView<FStyleType>();
		auto HealthBarList = Context.GetFragmentView<FHealthBar>();
		auto TeamList = Context.GetFragmentView<FTeam>();
		auto TextPopList = Context.GetMutableFragmentView<FTextPop>();

		const FAnimShared& Animation = Context.GetSharedFragment<FAnimShared>();
		const FMassBattleFogVisionSourceFragment* FogVisionSourcePolicy =
			Context.GetConstSharedFragmentPtr<FMassBattleFogVisionSourceFragment>();
		const EMassBattleFogVisibilityPolicy ChunkFogVisibilityPolicy = FogVisionSourcePolicy
			? FogVisionSourcePolicy->VisibilityPolicy
			: EMassBattleFogVisibilityPolicy::Standard;
		const bool bChunkHasLastSeenMemory = FogLastSeenList.Num() == NumEntities;
		auto AppearList = Context.GetFragmentView<FAppear>();
		auto AttackList = Context.GetFragmentView<FAttack>();
		auto AttackingList = Context.GetMutableFragmentView<FAttacking>();
		auto HitList = Context.GetFragmentView<FHit>();
		auto DeathList = Context.GetFragmentView<FDeath>();
		auto MoveList = Context.GetFragmentView<FMove>();
		auto MovingList = Context.GetMutableFragmentView<FMoving>();
		auto DefenceList = Context.GetFragmentView<FDefence>();
		auto DebuffingList = Context.GetFragmentView<FDebuffing>();
		auto EventList = Context.GetFragmentView<FAgentEvent>();
		auto AgentDebugList = Context.GetFragmentView<FAgentDebug>();

		const bool bChunkHasRenderingTag = Context.DoesArchetypeHaveTag<FRenderingTag>();
		const bool bChunkHasAgentDebug = AgentDebugList.Num() > 0;

		for (int32 i = 0; i < NumEntities; ++i)
		{
			//TRACE_CPUPROFILER_EVENT_SCOPE_STR("AgentRender");
			auto& Flags = FlagsList[i];
			auto& Rendering = RenderingList[i];
			if (!Flags.HasFlag(ActivatedFlag))
			{
				// The slot was left free by the batch reset above. Invalidate the
				// entity-side handle so a later activation cannot alias another unit.
				Rendering.InstanceId = INDEX_NONE;
				Rendering.bInterpInitialized = false;
				continue;
			}

			// cached flag reads | 缓存的旗标查询

			auto Entity = Context.GetEntity(i);
			FMassBattleFogLastSeenFragment* FogLastSeen = bChunkHasLastSeenMemory
				? &FogLastSeenList[i]
				: nullptr;
			checkSlow(ProxyIdByEntityIndex.IsValidIndex(Entity.Index));
			const int32 FogProxyId = ProxyIdByEntityIndex[Entity.Index];
			checkSlow(ProxyPool.IsValidIndex(FogProxyId));
			const FMassBattleFogRenderProxy& FogProxy = ProxyPool[FogProxyId];
			checkSlow(FogProxy.Entity == Entity);

			// This query is reached only through the final camera-local work set.
			const int32 TeamIndex = TeamList[i].index;
			const bool bRenderEnabled = RenderList[i].bEnable;
			const bool bFriendlyTeam = FogProxy.bFriendlyTeam;

			const bool bInAttackRevealState = Flags.HasFlag(AttackingFlag)
				&& UE::FogOfWar::Private::IsAttackRevealState(AttackingList[i].State);
			if (bIsSimTick && bInAttackRevealState && !bFriendlyTeam)
			{
				LocalAttackReveals.Add({ Entity, LocatingList[i].Location, TeamIndex });
			}
			const uint8 FogVisibilityState = FogProxy.FogVisibilityState;
			const bool bUseRememberedSnapshot = ChunkFogVisibilityPolicy
					== EMassBattleFogVisibilityPolicy::RememberLastSeen
				&& FogLastSeen
				&& FogLastSeen->bHasSnapshot
				&& FogVisibilityState < 3u
				&& FogRenderSubsystem->IsInsideRenderWindow(FogLastSeen->SnapshotLocation);

			// ============== SIM TICK ONLY: Handle tag changes ==============
			if (bIsSimTick)
			{
				// Fog never changes Mass tags. Tag transitions remain exclusively tied
				// to MBF's authored bEnable state; otherwise a unit crossing a fog edge
				// would trigger an archetype migration.
				// Actor representations intentionally do not carry FRenderingTag, so use
				// Add/Remove instead of an unconditional Swap.
				if (!bRenderEnabled)
				{
					Rendering.InstanceId = INDEX_NONE;
					Rendering.bInterpInitialized = false;
					if (Rendering.bIsBindingActorSwappable
						&& (IsValid(Rendering.BindingActorPtr) || IsValid(Rendering.BindingComponentPtr)))
					{
						MS.ActorRecycleQueue.Enqueue({ Entity, StatisticsList[i].UniqueID });
					}
					if (!Flags.HasFlag(NotRenderingFlag))
					{
						if (Flags.HasFlag(RenderingFlag))
						{
							Context.Defer().SwapTags<FRenderingTag, FNotRenderingTag>(Entity);
							Flags.ClearFlag(RenderingFlag);
						}
						else
						{
							Context.Defer().AddTag<FNotRenderingTag>(Entity);
						}
						Flags.SetFlag(NotRenderingFlag);
					}
					continue;
				}

				// Handle re-enabling rendering (agent was previously disabled).
				if (Flags.HasFlag(NotRenderingFlag))
				{
					if (Rendering.CurrentRepresentation == ERepresentationMode::Particle)
					{
						Context.Defer().SwapTags<FNotRenderingTag, FRenderingTag>(Entity);
						Flags.SetFlag(RenderingFlag);
					}
					else
					{
						Context.Defer().RemoveTag<FNotRenderingTag>(Entity);
					}
					Flags.ClearFlag(NotRenderingFlag);
				}
			}
			else
			{
				// On non-sim frames, skip disabled entities
				if (!bRenderEnabled)
				{
					Rendering.InstanceId = INDEX_NONE;
					Rendering.bInterpInitialized = false;
					continue;
				}
			}

			auto& Locating = LocatingList[i];
			auto& Scale = ScalingList[i];
			auto& Collider = ColliderList[i];
			auto& Animating = AnimatingList[i];
			auto& Scaling = ScalingList[i];

			// ============== SIM TICK ONLY: Animation state machine ==============
			if (bIsSimTick && Animating.AnimState == EAnimState::Montage && !Animating.bMontageLooping)
			{
				const FPerLevelAnimState* ActiveState = (Animating.CurrentAnimBlendLevel == 1) ? &Animating.Level1State : ((Animating.CurrentAnimBlendLevel == 2) ? &Animating.Level2State : &Animating.Level3State);
				if (ActiveState->CurrentFrame2 >= ActiveState->EndFrame2)
				{
					Animating.AnimState = Animating.PreviousAnimState;
					Animating.bUpdateAnimState = true;
				}
			}

			// Get fragment references needed for both sim tick and render frame
			auto& Statistics = StatisticsList[i];
			auto& Attacking = AttackingList[i];
			auto& Appear = AppearList[i];
			auto& Attack = AttackList[i];
			auto& Hit = HitList[i];
			auto& Death = DeathList[i];
			auto& MoveFrag = MoveList[i];
			auto& MovingFrag = MovingList[i];
			auto& Rotating = RotatingList[i];
			auto& Defence = DefenceList[i];
			auto& Debuffing = DebuffingList[i];


			// ============== SIM TICK ONLY: LOD Selection + Animation State Machine ==============
			if (bIsSimTick)
			{
				//TRACE_CPUPROFILER_EVENT_SCOPE_STR("LOD");
				const float Distance = FVector::Dist(TracingShared.PlayerCameraLocation, Locating.Location);
				const float ScalingRadius = Collider.Radius * Scale.Scale;
				const float ScreenSize = (Distance > KINDA_SMALL_NUMBER) ? (ScalingRadius / Distance) * TracingShared.ProjectionFactor : 1.0f;

				const FLODData* SelectedLOD = nullptr;
				const FLODData* FallbackLOD = nullptr;

				auto CheckLOD = [&](const FLODData& InLOD)
				{
					if (InLOD.LODIndex != -1 && InLOD.ScreenSize >= 0.f)
					{
						if (FallbackLOD == nullptr || InLOD.ScreenSize < FallbackLOD->ScreenSize)
						{
							FallbackLOD = &InLOD;
						}
						if (ScreenSize >= InLOD.ScreenSize)
						{
							if (SelectedLOD == nullptr || InLOD.ScreenSize > SelectedLOD->ScreenSize)
							{
								SelectedLOD = &InLOD;
							}
						}
					}
				};

				CheckLOD(LODShared.RenderLOD.Data0);
				CheckLOD(LODShared.RenderLOD.Data1);
				CheckLOD(LODShared.RenderLOD.Data2);
				CheckLOD(LODShared.RenderLOD.Data3);
				CheckLOD(LODShared.RenderLOD.Data4);

				if (SelectedLOD == nullptr)
				{
					SelectedLOD = FallbackLOD;
				}

				int32 CurrentLOD = 0;
				int32 NewAnimBlendLevel = 0;
				ERepresentationMode NewRepresentation = ERepresentationMode::Particle;

				if (SelectedLOD)
				{
					CurrentLOD = SelectedLOD->LODIndex;
					NewAnimBlendLevel = SelectedLOD->AnimBlendLevel;
					NewRepresentation = SelectedLOD->Representation;
				}

				Animating.CurrentAnimBlendLevel = NewAnimBlendLevel;
				Rendering.CurrentLOD = CurrentLOD;
				Rendering.PreviousRepresentation = Rendering.CurrentRepresentation;
				Rendering.CurrentRepresentation = NewRepresentation;
				if (Rendering.CurrentRepresentation == ERepresentationMode::Actor)
				{
					const bool bActorShouldSubmit = !bUseRememberedSnapshot;
					if (Flags.HasFlag(RenderingWithParticleFlag))
					{
						Context.Defer().SwapTags<FRenderingWithParticleTag, FRenderingWithActorTag>(Entity);
						Flags.ClearFlag(RenderingWithParticleFlag);
						Flags.SetFlag(RenderingWithActorFlag);
					}
					else if (!Flags.HasFlag(RenderingWithActorFlag))
					{
						Context.Defer().AddTag<FRenderingWithActorTag>(Entity);
						Flags.SetFlag(RenderingWithActorFlag);
					}

					if (Flags.HasFlag(RenderingFlag))
					{
						Context.Defer().RemoveTag<FRenderingTag>(Entity);
						Flags.ClearFlag(RenderingFlag);
					}
					if (bActorShouldSubmit
						&& (Rendering.PreviousRepresentation != ERepresentationMode::Actor
							|| !IsValid(Rendering.BindingActorPtr)
							|| !IsValid(Rendering.BindingComponentPtr)))
					{
						MS.ActorSpawnRequestQueue.Enqueue({ Entity, Statistics.UniqueID });
					}
					else if (!bActorShouldSubmit
						&& Rendering.bIsBindingActorSwappable
						&& (IsValid(Rendering.BindingActorPtr) || IsValid(Rendering.BindingComponentPtr)))
					{
						MS.ActorRecycleQueue.Enqueue({ Entity, Statistics.UniqueID });
					}
					Rendering.InstanceId = INDEX_NONE;
					Rendering.bInterpInitialized = false;
					continue;
				}
				else // Particle VAT
				{
					if (Flags.HasFlag(RenderingWithActorFlag))
					{
						Context.Defer().SwapTags<FRenderingWithActorTag, FRenderingWithParticleTag>(Entity);
						Flags.ClearFlag(RenderingWithActorFlag);
						Flags.SetFlag(RenderingWithParticleFlag);
						MS.ActorRecycleQueue.Enqueue({ Entity, Statistics.UniqueID });
					}
					else if (!Flags.HasFlag(RenderingWithParticleFlag))
					{
						Context.Defer().AddTag<FRenderingWithParticleTag>(Entity);
						Flags.SetFlag(RenderingWithParticleFlag);
					}
				}

				if (Flags.HasFlag(DeathAnimFlag) && !Flags.HasFlag(DyingFlag)) { Flags.ClearFlag(DeathAnimFlag);}
				if (Flags.HasFlag(AppearAnimFlag) && !Flags.HasFlag(AppearingFlag)) { Flags.ClearFlag(AppearAnimFlag);}
				if (Flags.HasFlag(AttackAnimFlag) && !Flags.HasFlag(AttackingFlag)) { Flags.ClearFlag(AttackAnimFlag);}

				if (Flags.HasFlag(HitAnimFlag))
				{
					if (!Flags.HasFlag(BeingHitFlag)) { Flags.ClearFlag(HitAnimFlag);}
					else if (Flags.HasFlag(AttackingFlag) && Attacking.State != EAttackState::PreCast) { Flags.ClearFlag(AttackAnimFlag);}
				}

				EAnimState NextAnimState = EAnimState::Idle;
				bool bForceReset = false;
				const bool bIsFirstUpdate = (Animating.PreviousAnimState == EAnimState::Dirty);

				if (bIsFirstUpdate)
				{
					int32 IdleIdx = AnimationHelpers::PickRandomIndex(Animation.AnimData.IdleAnimData, Statistics.RandomStream);
					if (IdleIdx != -1) Animating.SelectedIdleAnimIndex = IdleIdx;

					int32 MoveIdx = AnimationHelpers::PickRandomIndex(Animation.AnimData.MoveAnimData, Statistics.RandomStream);
					if (MoveIdx != -1) Animating.SelectedMoveAnimIndex = MoveIdx;
				}

				else if (Flags.HasFlag(PauseAnimChangeFlag)) { NextAnimState = Animating.AnimState; Flags.ClearFlag(PauseAnimChangeFlag); bForceReset = true; }
				else if (Animating.AnimState == EAnimState::Montage) { NextAnimState = EAnimState::Montage; }
				else if (Flags.HasFlag(DeathAnimFlag)) NextAnimState = EAnimState::Dying;
				else if (Flags.HasFlag(AppearAnimFlag)) NextAnimState = EAnimState::Appearing;
				else if (Flags.HasFlag(AttackAnimFlag)) NextAnimState = EAnimState::Attacking;
				else if (Flags.HasFlag(FallingFlag) && MovingFrag.FallingTimer > Animation.FallingAnimDelay) NextAnimState = EAnimState::Falling; // anim delay decoupled from physics-falling | 落地动画延迟与物理 Falling 解耦
				else if (Flags.HasFlag(HitAnimFlag)) NextAnimState = EAnimState::BeingHit;
				else NextAnimState = MovingFrag.CurrentVelocity.Size2D() > MoveFrag.XY.MinInitSpeed ? EAnimState::Move : EAnimState::Idle;

				EAnimState EffectiveAnimState = (NextAnimState == EAnimState::Move || NextAnimState == EAnimState::Idle) ? EAnimState::BS_IdleMove : NextAnimState;

				bool bGlobalStateChanged = false;

				if (Animating.AnimState != NextAnimState || bForceReset || bIsFirstUpdate)
				{
					if (!bIsFirstUpdate)
					{
						switch (Animating.AnimState)
						{
							case EAnimState::Falling: Animating.SelectedFallAnimIndex = -1; break;
							case EAnimState::Appearing: Animating.SelectedAppearAnimIndex = -1; break;
							case EAnimState::Attacking: Animating.SelectedAttackAnimIndex = -1; break;
							case EAnimState::BeingHit: Animating.SelectedHitAnimIndex = -1; break;
							case EAnimState::Dying: Animating.SelectedDeathAnimIndex = -1; break;
							case EAnimState::Montage: Animating.SelectedMontageAnimIndex = -1; break;
							default: break;
						}
					}

					int32 NewIndex = -1;

					switch (NextAnimState)
					{
						case EAnimState::Falling:
							if (Animating.SelectedFallAnimIndex == AnimationHelpers::INVALID_ANIM_INDEX)
							{
								NewIndex = AnimationHelpers::PickRandomIndex(Animation.AnimData.FallAnimData, Statistics.RandomStream);
								if (NewIndex != -1) Animating.SelectedFallAnimIndex = NewIndex;
							}
							break;
						case EAnimState::Appearing:
							if (Animating.SelectedAppearAnimIndex == AnimationHelpers::INVALID_ANIM_INDEX)
							{
								NewIndex = AnimationHelpers::PickRandomIndex(Animation.AnimData.AppearAnimData, Statistics.RandomStream);
								if (NewIndex != -1) Animating.SelectedAppearAnimIndex = NewIndex;
							}
							break;
						case EAnimState::Attacking:
							if (Animating.SelectedAttackAnimIndex == AnimationHelpers::INVALID_ANIM_INDEX)
							{
								NewIndex = AnimationHelpers::PickRandomIndex(Animation.AnimData.AttackAnimData, Statistics.RandomStream);
								if (NewIndex != -1) Animating.SelectedAttackAnimIndex = NewIndex;
							}
							break;
						case EAnimState::BeingHit:
							if (Animating.SelectedHitAnimIndex == AnimationHelpers::INVALID_ANIM_INDEX)
							{
								NewIndex = AnimationHelpers::PickRandomIndex(Animation.AnimData.HitAnimData, Statistics.RandomStream);
								if (NewIndex != -1) Animating.SelectedHitAnimIndex = NewIndex;
							}
							break;
						case EAnimState::Dying:
							if (Animating.SelectedDeathAnimIndex == AnimationHelpers::INVALID_ANIM_INDEX)
							{
								NewIndex = AnimationHelpers::PickRandomIndex(Animation.AnimData.DeathAnimData, Statistics.RandomStream);
								if (NewIndex != -1) Animating.SelectedDeathAnimIndex = NewIndex;
							}
							break;
						case EAnimState::Montage:
							if (Animating.SelectedMontageAnimIndex == AnimationHelpers::INVALID_ANIM_INDEX)
							{
								NewIndex = AnimationHelpers::PickRandomIndex(Animation.AnimData.OtherAnimData, Statistics.RandomStream);
								if (NewIndex != -1) Animating.SelectedMontageAnimIndex = NewIndex;
							}
							break;
						default: break;
					}

					Animating.PreviousAnimState = Animating.AnimState;
					Animating.AnimState = NextAnimState;
					Animating.bUpdateAnimState = true;
					bGlobalStateChanged = true;

					auto& Event = EventList[i];

					if (Event.bEnable && Event.bGenerateAnimStateChangeEvent)
					{
						FAnimStateChangeData Data;
						Data.SelfEntity = Entity;
						Data.PreviousState = Animating.PreviousAnimState;
						Data.NewState = Animating.AnimState;
						Data.UniqueID = Statistics.UniqueID;

						// Default values for CurrentAnimInfo
						float AnimPlayRate = 1.0f;
						bool bIsLooping = false;

						switch (NextAnimState)
						{
							case EAnimState::Idle:
							case EAnimState::BS_IdleMove:
								Data.AnimData = AnimationHelpers::GetDataFromIndex(Animation.AnimData.IdleAnimData, Animating.SelectedIdleAnimIndex);
								bIsLooping = true;
								break;
							case EAnimState::Move:
								Data.AnimData = AnimationHelpers::GetDataFromIndex(Animation.AnimData.MoveAnimData, Animating.SelectedMoveAnimIndex);
								bIsLooping = true;
								break;
							case EAnimState::Falling:
								Data.AnimData = AnimationHelpers::GetDataFromIndex(Animation.AnimData.FallAnimData, Animating.SelectedFallAnimIndex);
								bIsLooping = true;
								break;
							case EAnimState::Appearing:
								Data.AnimData = AnimationHelpers::GetDataFromIndex(Animation.AnimData.AppearAnimData, Animating.SelectedAppearAnimIndex);
								bIsLooping = false;
								break;
							case EAnimState::Attacking:
								Data.AnimData = AnimationHelpers::GetDataFromIndex(Animation.AnimData.AttackAnimData, Animating.SelectedAttackAnimIndex);
								bIsLooping = false;
								break;
							case EAnimState::BeingHit:
								Data.AnimData = AnimationHelpers::GetDataFromIndex(Animation.AnimData.HitAnimData, Animating.SelectedHitAnimIndex);
								bIsLooping = false;
								break;
							case EAnimState::Dying:
								Data.AnimData = AnimationHelpers::GetDataFromIndex(Animation.AnimData.DeathAnimData, Animating.SelectedDeathAnimIndex);
								bIsLooping = false;
								break;
							case EAnimState::Montage:
								Data.AnimData = AnimationHelpers::GetDataFromIndex(Animation.AnimData.OtherAnimData, Animating.SelectedMontageAnimIndex);
								AnimPlayRate = Animating.MontagePlayRate;
								bIsLooping = Animating.bMontageLooping;
								break;
							default:
								break;
						}

						// Calculate progress ratio from Level1State (base layer)
						// At state change, use current frame position
						float ProgressRatio = 0.0f;
						const float StartFrame = Data.AnimData.Z;
						const float EndFrame = Data.AnimData.W;
						if (EndFrame > StartFrame)
						{
							const float CurrentFrame = Animating.Level1State.CurrentFrame0;
							ProgressRatio = FMath::Clamp((CurrentFrame - StartFrame) / (EndFrame - StartFrame), 0.0f, 1.0f);
						}

						// Populate CurrentAnimInfo
						Data.CurrentAnimInfo.AnimIndex = static_cast<int32>(Data.AnimData.X);
						Data.CurrentAnimInfo.AnimPlayRate = AnimPlayRate;
						Data.CurrentAnimInfo.AnimProgressRatio = ProgressRatio;
						Data.CurrentAnimInfo.bIsLooping = bIsLooping;

						LocalAnimEvents.Add(Data);
					}
				}

				float AttackSpeedMultiplier = 1.0f;
				if (Defence.bCanSlowATKSpeed && Flags.HasFlag(SlowingFlag))
				{
					float MaxSlowStrength = 0.f;
					Debuffing.Lock();
					for (const auto& SlowHandle : Debuffing.SlowCausers)
					{
						if (MA.IsValid(SlowHandle))
						{
							const float Strength = MA.GetFragment<FSlowDebuff>(SlowHandle).SlowStrength;
							MaxSlowStrength = FMath::Max(MaxSlowStrength, Strength);
						}
					}
					Debuffing.Unlock();
					const float SlowEffect = MaxSlowStrength * (1.f - Defence.SlowImmune);
					AttackSpeedMultiplier = FMath::Clamp(1.f - SlowEffect, 0.1f, 1.f);
				}

				float MoveDirectionMultiplier = 1.0f;
				if (Animation.bMoveCanReverse && !MovingFrag.CurrentVelocity.IsNearlyZero())
				{
					if ((MovingFrag.CurrentVelocity.GetSafeNormal() | Rotating.Direction) < -0.01f) MoveDirectionMultiplier = -1.0f;
				}

				bool bReset0 = false, bReset1 = false, bReset2 = false;
				bool bL2Reset0 = false, bL2Reset1 = false, bL2Reset2 = false;
				bool bL3Reset0 = false, bL3Reset1 = false, bL3Reset2 = false;

				UpdateAnimBlendLevel1(Animating.Level1State, Animation, Animating, NextAnimState, Animating.bUpdateAnimState, SimDeltaTime, Appear, Attack, Hit, Death, AttackSpeedMultiplier, bReset0);
				UpdateAnimBlendLevel2(Animating.Level2State, Animation, Animating, EffectiveAnimState, Animating.bUpdateAnimState, bIsFirstUpdate, SimDeltaTime, MovingFrag, Appear, Attack, Hit, Death, AttackSpeedMultiplier, bL2Reset1, bL2Reset2);
				UpdateAnimBlendLevel3(Animating.Level3State, Animation, Animating, EffectiveAnimState, bGlobalStateChanged, bIsFirstUpdate, SimDeltaTime, MovingFrag, Appear, Attack, Hit, Death, AttackSpeedMultiplier, bL3Reset0, bL3Reset1, bL3Reset2);

				const FPerLevelAnimState* ActiveState = (Animating.CurrentAnimBlendLevel == 1) ? &Animating.Level1State : ((Animating.CurrentAnimBlendLevel == 2) ? &Animating.Level2State : &Animating.Level3State);

				Animating.AnimBlendResult.Index0 = ActiveState->Index0;
				Animating.AnimBlendResult.PlayRate0 = ActiveState->PlayRate0;
				Animating.AnimBlendResult.CurrentFrame0 = ActiveState->CurrentFrame0;
				Animating.AnimBlendResult.bLoop0 = ActiveState->bLoop0;

				Animating.AnimBlendResult.Index1 = ActiveState->Index1;
				Animating.AnimBlendResult.PlayRate1 = ActiveState->PlayRate1;
				Animating.AnimBlendResult.CurrentFrame1 = ActiveState->CurrentFrame1;
				Animating.AnimBlendResult.bLoop1 = ActiveState->bLoop1;

				Animating.AnimBlendResult.Index2 = ActiveState->Index2;
				Animating.AnimBlendResult.PlayRate2 = ActiveState->PlayRate2;
				Animating.AnimBlendResult.CurrentFrame2 = ActiveState->CurrentFrame2;
				Animating.AnimBlendResult.bLoop2 = ActiveState->bLoop2;

				Animating.AnimBlendResult.Lerp0 = ActiveState->Lerp0;
				Animating.AnimBlendResult.Lerp1 = ActiveState->Lerp1;
				Animating.bUpdateAnimState = false;

				auto AdvanceAll = [&](FPerLevelAnimState& S, bool bS0, bool bS1, bool bS2)
				{
					AdvanceFrame(S, 0, SimDeltaTime, Animating.SampleRate, Animation, MoveDirectionMultiplier, bS0);
					AdvanceFrame(S, 1, SimDeltaTime, Animating.SampleRate, Animation, MoveDirectionMultiplier, bS1);
					AdvanceFrame(S, 2, SimDeltaTime, Animating.SampleRate, Animation, MoveDirectionMultiplier, bS2);
				};

				AdvanceAll(Animating.Level1State, bReset0, false, false);
				AdvanceAll(Animating.Level2State, bL2Reset0, bL2Reset1, bL2Reset2);
				AdvanceAll(Animating.Level3State, bL3Reset0, bL3Reset1, bL3Reset2);

				//TRACE_CPUPROFILER_EVENT_SCOPE_STR("RenderUpdate");
				if (!bChunkHasRenderingTag)
				{
					LocalRegistration.Add(Entity);
					continue;
				}
			} // End of bIsSimTick block
			else
			{
				// On non-sim frames, skip unregistered and non-VAT representations.
				if (Rendering.CurrentRepresentation == ERepresentationMode::Actor)
				{
					Rendering.InstanceId = INDEX_NONE;
					if (Rendering.CurrentRepresentation == ERepresentationMode::Actor)
					{
						Rendering.bInterpInitialized = false;
						const bool bActorShouldSubmit = !bUseRememberedSnapshot;
						if (bActorShouldSubmit
							&& (!IsValid(Rendering.BindingActorPtr) || !IsValid(Rendering.BindingComponentPtr)))
						{
							MS.ActorSpawnRequestQueue.Enqueue({ Entity, StatisticsList[i].UniqueID });
						}
						else if (!bActorShouldSubmit
							&& Rendering.bIsBindingActorSwappable
							&& (IsValid(Rendering.BindingActorPtr) || IsValid(Rendering.BindingComponentPtr)))
						{
							MS.ActorRecycleQueue.Enqueue({ Entity, StatisticsList[i].UniqueID });
						}
					}
					continue;
				}
				if (!bChunkHasRenderingTag)
				{
					Rendering.InstanceId = INDEX_NONE;
					Rendering.bInterpInitialized = false;
					continue;
				}
			}

			// ============== EVERY FRAME: Interpolation + Render Data Packing ==============
			auto& HealthBar = HealthBarList[i];
			auto& TextPop = TextPopList[i];
			auto& Team = TeamList[i];

			// Dense layout assignment already resolves the renderer once per subtype.
			// Reuse that fragment pointer instead of doing a TMap lookup per unit/frame.
			AMassBattleAgentRenderer* RendererActor =
				static_cast<AMassBattleAgentRenderer*>(Rendering.RendererActor.Get());
			FAgentRenderBatchData* DataPtr = IsValid(RendererActor)
				? RendererActor->SpawnedRenderBatches.Find(Rendering.RenderBatchId)
				: nullptr;
			const int32 InstanceId = Rendering.InstanceId;
			if (UNLIKELY(!DataPtr
				|| InstanceId < 0
				|| !DataPtr->FreeSlotArray.IsValidIndex(InstanceId)
				|| !DataPtr->IsHiddenArray.IsValidIndex(InstanceId)))
			{
				// Reattach only after the unit enters the padded submission region.
				// Registration is transition-sized and remains part of this same
				// processor; it also initializes interpolation at the current pose.
				Rendering.InstanceId = INDEX_NONE;
				Rendering.bInterpInitialized = false;
				if (bIsSimTick)
				{
					LocalRegistration.Add(Entity);
				}
				continue;
			}
			FAgentRenderBatchData& Data = *DataPtr;

			if (bUseRememberedSnapshot)
			{
				// Keep the stable slot but write only the last truly visible payload.
				// The live entity can continue simulating in the background without
				// leaking its position, animation, health, selection or status effects.
				TextPopList[i].TextLocationArray.Reset();
				TextPopList[i].Text_Value_Style_Scale_Offset_Array.Reset();
				Data.FreeSlotArray[InstanceId] = false;
				Data.IsHiddenArray[InstanceId] = false;
				Data.LocationArray[InstanceId] = FogLastSeen->SnapshotLocation;
				Data.OrientationArray[InstanceId] = FogLastSeen->SnapshotRotation;
				Data.ScaleArray[InstanceId] = FogLastSeen->SnapshotScale;
				Data.DynamicParams0_Array[InstanceId] = FogLastSeen->SnapshotDynamicParams0;
				Data.HealthBar_Opacity_CurrentRatio_TargetRatio_Array[InstanceId] = FogLastSeen->SnapshotHealthBar;
				Data.CurrentLODArray[InstanceId] = FogLastSeen->SnapshotLOD;
				Data.StyleArray[InstanceId] = FogLastSeen->SnapshotStyle;

				Rendering.TargetLocation = FogLastSeen->SnapshotLocation;
				Rendering.InterpLocation = FogLastSeen->SnapshotLocation;
				Rendering.TargetRotation = FogLastSeen->SnapshotRotation;
				Rendering.InterpRotation = FogLastSeen->SnapshotRotation;
				Rendering.TargetScale = FogLastSeen->SnapshotScale;
				Rendering.InterpScale = FogLastSeen->SnapshotScale;
				Rendering.CachedVelocity = FVector3f::ZeroVector;
				Rendering.bInterpInitialized = true;
				FogLastSeen->bShowingSnapshot = true;
				continue;
			}

			const bool bReturningFromRememberedSnapshot = FogLastSeen
				&& FogLastSeen->bShowingSnapshot
				&& FogVisibilityState == 3u;
			if (bReturningFromRememberedSnapshot)
			{
				// Reacquisition must snap to the authoritative pose. Interpolating
				// from a stale building snapshot would reveal a fake movement path.
				Rendering.bInterpInitialized = false;
				FogLastSeen->bShowingSnapshot = false;
			}

			const FInterpParams& InterpParams = RenderList[i].InterpParams;
			const bool bUseInterpolation = InterpParams.IsEnabled() && Rendering.bInterpInitialized;
			FVector AgentRenderLocation = FVector::ZeroVector;
			FQuat AgentRenderRotation = FQuat::Identity;
			FVector AgentRenderScale = FVector::OneVector;

			// Authoritative transform targets change on simulation ticks. Render-only
			// frames interpolate the cached target and must not recompute the same mesh,
			// collider and pivot transforms for every visible unit.
			if (bIsSimTick || !bUseInterpolation)
			{
				const FVisualize& RenderFrag = RenderList[i];
				const FTransform3f& RenderTransform = RenderFrag.Transform;
				const FTransform& RendererOffset = RendererActor->Offset;

				FTransform CombinedTransform;
				if (!RendererOffset.Equals(FTransform::Identity))
				{
					CombinedTransform = !RenderTransform.Equals(FTransform3f::Identity)
						? FTransform(RenderTransform) * RendererOffset
						: RendererOffset;
				}
				else
				{
					CombinedTransform = FTransform(RenderTransform);
				}

				AgentRenderScale = CombinedTransform.GetScale3D()
					* Scaling.Scale * FVector(Scaling.JiggleMultiplier);
				FQuat AgentFacingRot = FQuat(Rotating.RotationQuat);
				if (MoveFrag.Tilt.TiltMode == ETiltMode::OnlyMesh)
				{
					AgentFacingRot *= FQuat(MovingFrag.CurrentTilt);
				}
				else if (MoveFrag.Tilt.TiltMode == ETiltMode::OnlyCollider)
				{
					AgentFacingRot *= FQuat(MovingFrag.CurrentTilt.Inverse());
				}

				const float SelfRadius = Collider.Radius * Scaling.Scale;
				const float SelfShaftHalfHeight = Collider.Height * Scaling.Scale * 0.5f;
				const float SelfTotalHalfHeight = SelfShaftHalfHeight + SelfRadius;
				const FQuat PhysicsRotation = FQuat(Rotating.RotationQuat)
					* FQuat(Collider.RelativeRotation.Quaternion());
				AgentRenderLocation = Locating.Location;
				AgentRenderLocation -= PhysicsRotation.RotateVector(
					FVector(0.0, 0.0, SelfTotalHalfHeight));
				AgentRenderLocation += AgentFacingRot.RotateVector(CombinedTransform.GetLocation());
				AgentRenderRotation = CombinedTransform.GetRotation().IsIdentity()
					? AgentFacingRot
					: AgentFacingRot * CombinedTransform.GetRotation();
			}

			// On sim tick: Update target values and cache animation params
			if (bIsSimTick)
			{
				Rendering.TargetLocation = AgentRenderLocation;
				Rendering.TargetRotation = (FQuat4f)AgentRenderRotation;
				Rendering.TargetScale = (FVector3f)AgentRenderScale;

				// Cache velocity for predictive interpolation
				Rendering.CachedVelocity = MovingFrag.CurrentVelocity;

				// Update target animation frames (like TargetLocation for transforms) | 更新目标动画帧（类似变换的TargetLocation）
				Rendering.TargetFrame0 = Animating.AnimBlendResult.CurrentFrame0;
				Rendering.TargetFrame1 = Animating.AnimBlendResult.CurrentFrame1;
				Rendering.TargetFrame2 = Animating.AnimBlendResult.CurrentFrame2;
				Rendering.TargetLerp0 = Animating.AnimBlendResult.Lerp0;
				Rendering.TargetLerp1 = Animating.AnimBlendResult.Lerp1;

				// Cache animation params for between-tick advancement
				const FPerLevelAnimState* ActiveState = (Animating.CurrentAnimBlendLevel == 1) ? &Animating.Level1State
					: ((Animating.CurrentAnimBlendLevel == 2) ? &Animating.Level2State : &Animating.Level3State);
				Rendering.CachedPlayRate0 = ActiveState->PlayRate0;
				Rendering.CachedPlayRate1 = ActiveState->PlayRate1;
				Rendering.CachedPlayRate2 = ActiveState->PlayRate2;
				Rendering.CachedSampleRate = Animating.SampleRate;
				Rendering.CachedStartFrame0 = ActiveState->StartFrame0;
				Rendering.CachedEndFrame0 = ActiveState->EndFrame0;
				Rendering.CachedStartFrame1 = ActiveState->StartFrame1;
				Rendering.CachedEndFrame1 = ActiveState->EndFrame1;
				Rendering.CachedStartFrame2 = ActiveState->StartFrame2;
				Rendering.CachedEndFrame2 = ActiveState->EndFrame2;
				Rendering.CachedLoop0 = ActiveState->bLoop0;
				Rendering.CachedLoop1 = ActiveState->bLoop1;
				Rendering.CachedLoop2 = ActiveState->bLoop2;

				// Update MatFX targets
				Rendering.TargetIceFx = Animating.IceFx;
				Rendering.TargetFireFx = Animating.FireFx;
				Rendering.TargetPoisonFx = Animating.PoisonFx;
				Rendering.TargetHitGlow = Animating.HitGlow;
				Rendering.TargetDissolve = Animating.Dissolve;

				// Update HealthBar ratio targets (opacity is read directly, no interp)
				Rendering.TargetHBCurrentRatio = HealthBar.CurrentRatio;
				Rendering.TargetHBTargetRatio = HealthBar.TargetRatio;
			}

			// Final values to send to Niagara
			FVector FinalLocation;
			FQuat4f FinalRotation;
			FVector3f FinalScale;
			float FinalFrame0, FinalFrame1, FinalFrame2;
			uint8 FinalLerp0, FinalLerp1;
			uint8 FinalIceFx, FinalFireFx, FinalPoisonFx, FinalHitGlow, FinalDissolve;
			float FinalHBOpacity, FinalHBCurrentRatio, FinalHBTargetRatio;

			if (bUseInterpolation)
			{
				// 1. Snap check - if too far from ground truth, snap immediately
				if (InterpParams.SnapDist > 0.f)
				{
					const float DistSqr = FVector::DistSquared(Rendering.InterpLocation, Rendering.TargetLocation);
					if (DistSqr > FMath::Square(InterpParams.SnapDist))
					{
						Rendering.InterpLocation = Rendering.TargetLocation;
						Rendering.InterpRotation = Rendering.TargetRotation;
						Rendering.InterpScale = Rendering.TargetScale;
					}
				}

				// 2. Calculate predictive target using velocity
				FVector PredictedTarget = Rendering.TargetLocation;
				if (InterpParams.PredictTime > 0.f)
				{
					PredictedTarget += FVector(Rendering.CachedVelocity) * InterpParams.PredictTime;
				}

				// 3. Interpolate transform toward predictive target (exponential ease-out)
				Rendering.InterpLocation = FMath::VInterpTo(
					Rendering.InterpLocation, PredictedTarget,
					RenderDeltaTime, InterpParams.InterpSpeed);

				Rendering.InterpRotation = FQuat4f(FMath::QInterpTo(
					FQuat(Rendering.InterpRotation), FQuat(Rendering.TargetRotation),
					RenderDeltaTime, InterpParams.InterpSpeed));

				Rendering.InterpScale = FVector3f(FMath::VInterpTo(
					FVector(Rendering.InterpScale), FVector(Rendering.TargetScale),
					RenderDeltaTime, InterpParams.InterpSpeed));

				// Predict and interpolate animation frames (mirrors transform prediction)
				auto PredictAndInterpFrame = [RenderDeltaTime, &InterpParams](
					float& InterpFrame, float TargetFrame, float PlayRate, int32 SampleRate,
					uint16 StartFrame, uint16 EndFrame, bool bLoop)
				{
					// 1. Compute predicted target frame (analogous to PredictedTarget = TargetLocation + Velocity * PredictTime)
					float PredictedFrame = TargetFrame + PlayRate * SampleRate * InterpParams.PredictTime;

					// 2. Wrap/clamp the predicted frame
					if (bLoop && EndFrame > StartFrame)
					{
						float Range = EndFrame - StartFrame;
						while (PredictedFrame >= EndFrame) PredictedFrame -= Range;
						while (PredictedFrame < StartFrame) PredictedFrame += Range;
					}
					else
					{
						PredictedFrame = FMath::Clamp(PredictedFrame, (float)StartFrame, (float)EndFrame);
					}

					// 3. Interpolate toward predicted frame (wrapping-aware for loops)
					if (bLoop && EndFrame > StartFrame)
					{
						float Range = EndFrame - StartFrame;
						float Diff = PredictedFrame - InterpFrame;
						// Shortest path around the loop (handles boundary crossing)
						if (Diff > Range * 0.5f) Diff -= Range;
						else if (Diff < -Range * 0.5f) Diff += Range;

						float EffectiveTarget = InterpFrame + Diff;
						InterpFrame = FMath::FInterpTo(InterpFrame, EffectiveTarget, RenderDeltaTime, InterpParams.InterpSpeed);

						// Wrap result back into [StartFrame, EndFrame)
						while (InterpFrame >= EndFrame) InterpFrame -= Range;
						while (InterpFrame < StartFrame) InterpFrame += Range;
					}
					else
					{
						InterpFrame = FMath::FInterpTo(InterpFrame, PredictedFrame, RenderDeltaTime, InterpParams.InterpSpeed);
						InterpFrame = FMath::Clamp(InterpFrame, (float)StartFrame, (float)EndFrame);
					}
				};

				PredictAndInterpFrame(Rendering.InterpFrame0, Rendering.TargetFrame0, Rendering.CachedPlayRate0, Rendering.CachedSampleRate,
									  Rendering.CachedStartFrame0, Rendering.CachedEndFrame0, Rendering.CachedLoop0);
				PredictAndInterpFrame(Rendering.InterpFrame1, Rendering.TargetFrame1, Rendering.CachedPlayRate1, Rendering.CachedSampleRate,
									  Rendering.CachedStartFrame1, Rendering.CachedEndFrame1, Rendering.CachedLoop1);
				PredictAndInterpFrame(Rendering.InterpFrame2, Rendering.TargetFrame2, Rendering.CachedPlayRate2, Rendering.CachedSampleRate,
									  Rendering.CachedStartFrame2, Rendering.CachedEndFrame2, Rendering.CachedLoop2);

				// Interpolate MatFX (uses InterpSpeed from FRender, clamped to 0-255 via uint8 conversion) | 插值材质特效（使用FRender的InterpSpeed，通过uint8转换钳制到0-255）
				const float Speed = InterpParams.InterpSpeed;

				// Interpolate animation blend alphas (mirrors MatFX uint8 pattern; smooths state transitions between sim ticks) | 插值动画混合权重（镜像材质特效的uint8模式；在模拟刻之间平滑状态过渡）
				float lerp0Target = AnimationHelpers::NormalizedUint8ToFloat(Rendering.TargetLerp0);
				float lerp0Current = AnimationHelpers::NormalizedUint8ToFloat(Rendering.InterpLerp0);
				Rendering.InterpLerp0 = AnimationHelpers::FloatToNormalizedUint8(FMath::Clamp(FMath::FInterpTo(lerp0Current, lerp0Target, RenderDeltaTime, Speed), 0.f, 1.f));

				float lerp1Target = AnimationHelpers::NormalizedUint8ToFloat(Rendering.TargetLerp1);
				float lerp1Current = AnimationHelpers::NormalizedUint8ToFloat(Rendering.InterpLerp1);
				Rendering.InterpLerp1 = AnimationHelpers::FloatToNormalizedUint8(FMath::Clamp(FMath::FInterpTo(lerp1Current, lerp1Target, RenderDeltaTime, Speed), 0.f, 1.f));

				float iceFxTarget = AnimationHelpers::NormalizedUint8ToFloat(Rendering.TargetIceFx);
				float iceFxCurrent = AnimationHelpers::NormalizedUint8ToFloat(Rendering.InterpIceFx);
				Rendering.InterpIceFx = AnimationHelpers::FloatToNormalizedUint8(FMath::Clamp(FMath::FInterpTo(iceFxCurrent, iceFxTarget, RenderDeltaTime, Speed), 0.f, 1.f));

				float fireFxTarget = AnimationHelpers::NormalizedUint8ToFloat(Rendering.TargetFireFx);
				float fireFxCurrent = AnimationHelpers::NormalizedUint8ToFloat(Rendering.InterpFireFx);
				Rendering.InterpFireFx = AnimationHelpers::FloatToNormalizedUint8(FMath::Clamp(FMath::FInterpTo(fireFxCurrent, fireFxTarget, RenderDeltaTime, Speed), 0.f, 1.f));

				float poisonFxTarget = AnimationHelpers::NormalizedUint8ToFloat(Rendering.TargetPoisonFx);
				float poisonFxCurrent = AnimationHelpers::NormalizedUint8ToFloat(Rendering.InterpPoisonFx);
				Rendering.InterpPoisonFx = AnimationHelpers::FloatToNormalizedUint8(FMath::Clamp(FMath::FInterpTo(poisonFxCurrent, poisonFxTarget, RenderDeltaTime, Speed), 0.f, 1.f));

				float hitGlowTarget = AnimationHelpers::NormalizedUint8ToFloat(Rendering.TargetHitGlow);
				float hitGlowCurrent = AnimationHelpers::NormalizedUint8ToFloat(Rendering.InterpHitGlow);
				Rendering.InterpHitGlow = AnimationHelpers::FloatToNormalizedUint8(FMath::Clamp(FMath::FInterpTo(hitGlowCurrent, hitGlowTarget, RenderDeltaTime, Speed), 0.f, 1.f));

				float dissolveTarget = AnimationHelpers::NormalizedUint8ToFloat(Rendering.TargetDissolve);
				float dissolveCurrent = AnimationHelpers::NormalizedUint8ToFloat(Rendering.InterpDissolve);
				Rendering.InterpDissolve = AnimationHelpers::FloatToNormalizedUint8(FMath::Clamp(FMath::FInterpTo(dissolveCurrent, dissolveTarget, RenderDeltaTime, Speed), 0.f, 1.f));

				// Interpolate HealthBar ratios (opacity skipped — used directly below)
				Rendering.InterpHBCurrentRatio = FMath::Clamp(FMath::FInterpTo(Rendering.InterpHBCurrentRatio, Rendering.TargetHBCurrentRatio, RenderDeltaTime, Speed), 0.f, 1.f);
				Rendering.InterpHBTargetRatio = FMath::Clamp(FMath::FInterpTo(Rendering.InterpHBTargetRatio, Rendering.TargetHBTargetRatio, RenderDeltaTime, Speed), 0.f, 1.f);

				// Use interpolated values
				FinalLocation = Rendering.InterpLocation;
				FinalRotation = Rendering.InterpRotation;
				FinalScale = Rendering.InterpScale;
				FinalFrame0 = Rendering.InterpFrame0;
				FinalFrame1 = Rendering.InterpFrame1;
				FinalFrame2 = Rendering.InterpFrame2;
				FinalLerp0 = Rendering.InterpLerp0;
				FinalLerp1 = Rendering.InterpLerp1;
				FinalIceFx = Rendering.InterpIceFx;
				FinalFireFx = Rendering.InterpFireFx;
				FinalPoisonFx = Rendering.InterpPoisonFx;
				FinalHitGlow = Rendering.InterpHitGlow;
				FinalDissolve = Rendering.InterpDissolve;
				FinalHBOpacity = HealthBar.Opacity;
				FinalHBCurrentRatio = Rendering.InterpHBCurrentRatio;
				FinalHBTargetRatio = Rendering.InterpHBTargetRatio;
			}
			else
			{
				// No interpolation - use direct values (original behavior)
				// FX interpolation only on sim ticks to avoid over-interpolation
				if (bIsSimTick)
				{
					float iceFx = AnimationHelpers::NormalizedUint8ToFloat(Animating.IceFx);
					float iceFxInterped = AnimationHelpers::NormalizedUint8ToFloat(Animating.IceFxInterped);
					Animating.IceFxInterped = AnimationHelpers::FloatToNormalizedUint8(FMath::FInterpTo(iceFxInterped, iceFx, SimDeltaTime, 5.0f));

					float fireFx = AnimationHelpers::NormalizedUint8ToFloat(Animating.FireFx);
					float fireFxInterped = AnimationHelpers::NormalizedUint8ToFloat(Animating.FireFxInterped);
					Animating.FireFxInterped = AnimationHelpers::FloatToNormalizedUint8(FMath::FInterpTo(fireFxInterped, fireFx, SimDeltaTime, 5.0f));

					float poisonFx = AnimationHelpers::NormalizedUint8ToFloat(Animating.PoisonFx);
					float poisonFxInterped = AnimationHelpers::NormalizedUint8ToFloat(Animating.PoisonFxInterped);
					Animating.PoisonFxInterped = AnimationHelpers::FloatToNormalizedUint8(FMath::FInterpTo(poisonFxInterped, poisonFx, SimDeltaTime, 5.0f));
				}

				// Keep InterpLocation/Rotation/Scale in sync for attached FX to use
				Rendering.InterpLocation = AgentRenderLocation;
				Rendering.InterpRotation = (FQuat4f)AgentRenderRotation;
				Rendering.InterpScale = (FVector3f)AgentRenderScale;

				FinalLocation = AgentRenderLocation;
				FinalRotation = (FQuat4f)AgentRenderRotation;
				FinalScale = (FVector3f)AgentRenderScale;
				FinalFrame0 = Animating.AnimBlendResult.CurrentFrame0;
				FinalFrame1 = Animating.AnimBlendResult.CurrentFrame1;
				FinalFrame2 = Animating.AnimBlendResult.CurrentFrame2;
				FinalLerp0 = Animating.AnimBlendResult.Lerp0;
				FinalLerp1 = Animating.AnimBlendResult.Lerp1;
				FinalIceFx = Animating.IceFxInterped;
				FinalFireFx = Animating.FireFxInterped;
				FinalPoisonFx = Animating.PoisonFxInterped;
				FinalHitGlow = Animating.HitGlow;
				FinalDissolve = Animating.Dissolve;
				FinalHBOpacity = HealthBar.Opacity;
				FinalHBCurrentRatio = HealthBar.CurrentRatio;
				FinalHBTargetRatio = HealthBar.TargetRatio;
			}

			// Pack render data
			const bool bDrawLODColor = bChunkHasAgentDebug && AgentDebugList[i].bDrawLODColor;
			const bool bBeingSelect = Flags.HasFlag(BeingSelectFlag);
			const bool bSelected = Flags.HasFlag(SelectedFlag);
			float PackedTeamDissolve = EncodeDynamicParams0(Team.index, FinalDissolve, Rendering.CurrentLOD, bDrawLODColor, bBeingSelect, bSelected);
			float PackedFrames01 = EncodeFrames01(FinalFrame0, FinalFrame1);
			float PackedData2 = EncodeFrame2AndLerps(FinalFrame2, FinalLerp0, FinalLerp1);
			float MatFx = EncodeStatusEffects(FinalHitGlow, FinalIceFx, FinalFireFx, FinalPoisonFx);
			FVector4f DynamicParams0 = FVector4f(PackedFrames01, PackedData2, MatFx, PackedTeamDissolve);

			if (UNLIKELY(!TextPop.TextLocationArray.IsEmpty() && !TextPop.Text_Value_Style_Scale_Offset_Array.IsEmpty()))
			{
				Data.TextData.Lock();
				Data.TextData.Text_Location_Array.Append(TextPop.TextLocationArray);
				Data.TextData.Text_Value_Style_Scale_Offset_Array.Append(TextPop.Text_Value_Style_Scale_Offset_Array);
				Data.TextData.Unlock();

				TextPop.TextLocationArray.Reset();
				TextPop.Text_Value_Style_Scale_Offset_Array.Reset();
			}
			Data.FreeSlotArray[InstanceId] = false;
			Data.IsHiddenArray[InstanceId] = false;
			Data.LocationArray[InstanceId] = FinalLocation;
			Data.OrientationArray[InstanceId] = FinalRotation;
			Data.ScaleArray[InstanceId] = FinalScale;
			Data.DynamicParams0_Array[InstanceId] = DynamicParams0;
			Data.HealthBar_Opacity_CurrentRatio_TargetRatio_Array[InstanceId] = FVector3f(FinalHBOpacity, FinalHBCurrentRatio, FinalHBTargetRatio);
			Data.CurrentLODArray[InstanceId] = Rendering.CurrentLOD;
			Data.StyleArray[InstanceId] = StyleTypeList[i].Index;

			if (bReturningFromRememberedSnapshot)
			{
				Rendering.bInterpInitialized = true;
			}
			if (ChunkFogVisibilityPolicy == EMassBattleFogVisibilityPolicy::RememberLastSeen
				&& FogLastSeen
				&& FogVisibilityState == 3u)
			{
				FogLastSeen->bHasSnapshot = true;
				FogLastSeen->bShowingSnapshot = false;
				FogLastSeen->SnapshotLocation = FinalLocation;
				FogLastSeen->SnapshotRotation = FinalRotation;
				FogLastSeen->SnapshotScale = FinalScale;
				FogLastSeen->SnapshotDynamicParams0 = DynamicParams0;
				FogLastSeen->SnapshotHealthBar = FVector3f(FinalHBOpacity, FinalHBCurrentRatio, FinalHBTargetRatio);
				FogLastSeen->SnapshotLOD = Rendering.CurrentLOD;
				FogLastSeen->SnapshotStyle = StyleTypeList[i].Index;
			}
		}

		if (bIsSimTick)
		{
			if (LocalAnimEvents.Num() > 0) MB.OnAnimStateChangeQueue.Append(LocalAnimEvents);
			if (LocalRegistration.Num() > 0) RegistrationQueue.Append(LocalRegistration);
		}
		if (LocalAttackReveals.Num() > 0)
		{
			AttackRevealQueue.Append(LocalAttackReveals);
		}
	};

	{
		if (!ActiveRenderCollections.IsEmpty())
		{
			if constexpr (MBParallelToggle::RenderProcessor || MBParallelToggle::ForceAllSingleThread)
			{
				EntityQuery.ForEachEntityChunkInCollections(ActiveRenderCollections, Context, ProcessAgentChunk);
			}
			else
			{
				EntityQuery.ParallelForEachEntityChunkInCollection(
					ActiveRenderCollections,
					Context,
					ProcessAgentChunk,
					FMassEntityQuery::EParallelExecutionFlags::AutoBalance);
			}
		}
	}

	// The minimap needs an all-world snapshot at a low frequency, but that does
	// not justify running the heavy render/animation query for N agents. Keep the
	// scan narrow: four read-only fragments plus the optional fog policy/memory.
	if (bCollectMinimapSnapshot)
	{
		MBForEachEntityChunk<MBParallelToggle::RenderProcessor>(
			MinimapSnapshotQuery,
			Context,
			[&, this](FMassExecutionContext& MinimapContext)
			{
				const int32 NumEntities = MinimapContext.GetNumEntities();
				const auto Flags = MinimapContext.GetFragmentView<FEntityFlagFragment>();
				const auto Teams = MinimapContext.GetFragmentView<FTeam>();
				const auto Locations = MinimapContext.GetFragmentView<FLocating>();
				const auto Visualize = MinimapContext.GetFragmentView<FVisualize>();
				const auto AddonISKM = MinimapContext.GetFragmentView<FMassBattleISKMAddonFragment>();
				const auto LastSeen = MinimapContext.GetFragmentView<FMassBattleFogLastSeenFragment>();
				const FMassBattleFogVisionSourceFragment* FogPolicy =
					MinimapContext.GetConstSharedFragmentPtr<FMassBattleFogVisionSourceFragment>();
				const bool bProvidesVision = !FogPolicy || FogPolicy->bProvidesVision;
				const EMassBattleFogVisibilityPolicy VisibilityPolicy = FogPolicy
					? FogPolicy->VisibilityPolicy
					: EMassBattleFogVisibilityPolicy::Standard;
				const bool bHasLastSeen = LastSeen.Num() == NumEntities;
				const bool bHasAddonISKM = AddonISKM.Num() == NumEntities;

				TArray<FVector4f> LocalVisionSources;
				TArray<FVector4f> LocalFriendlyNonVision;
				TArray<FVector4f> LocalOther;
				TArray<FVector4f> LocalFogVisible;
				LocalVisionSources.Reserve(FMath::Max(1, NumEntities / 4));
				LocalFriendlyNonVision.Reserve(FMath::Max(1, NumEntities / 8));
				LocalOther.Reserve(NumEntities);

				for (int32 Index = 0; Index < NumEntities; ++Index)
				{
					const bool bPresentationEnabled = Visualize[Index].bEnable
						|| (bHasAddonISKM && AddonISKM[Index].bEnable);
					if (!Flags[Index].HasFlag(ActivatedFlag) || !bPresentationEnabled)
					{
						continue;
					}

					const int32 TeamIndex = Teams[Index].index;
					const FVector& Location = Locations[Index].Location;
					const FVector4f MinimapUnit(
						static_cast<float>(Location.X),
						static_cast<float>(Location.Y),
						static_cast<float>(Location.Z),
						UE::FogOfWar::Private::EncodeMinimapTeam(TeamIndex));
					if (!FogRenderSubsystem->IsFriendlyTeam(TeamIndex))
					{
						LocalOther.Add(MinimapUnit);
						if (VisibilityPolicy == EMassBattleFogVisibilityPolicy::AlwaysFogVisible)
						{
							LocalFogVisible.Add(MinimapUnit);
						}
						else if (VisibilityPolicy == EMassBattleFogVisibilityPolicy::RememberLastSeen
							&& bHasLastSeen && LastSeen[Index].bHasSnapshot)
						{
							const FVector& Snapshot = LastSeen[Index].SnapshotLocation;
							LocalFogVisible.Add(FVector4f(
								static_cast<float>(Snapshot.X),
								static_cast<float>(Snapshot.Y),
								static_cast<float>(Snapshot.Z),
								UE::FogOfWar::Private::EncodeMinimapTeam(TeamIndex)));
						}
					}
					else if (bProvidesVision)
					{
						LocalVisionSources.Add(MinimapUnit);
					}
					else
					{
						LocalFriendlyNonVision.Add(MinimapUnit);
					}
				}

				if (!LocalVisionSources.IsEmpty()) MinimapVisionSourceUnitQueue.Append(LocalVisionSources);
				if (!LocalFriendlyNonVision.IsEmpty()) MinimapFriendlyNonVisionUnitQueue.Append(LocalFriendlyNonVision);
				if (!LocalOther.IsEmpty()) MinimapOtherUnitQueue.Append(LocalOther);
				if (!LocalFogVisible.IsEmpty()) MinimapFogVisibleUnitQueue.Append(LocalFogVisible);
			},
			FMassEntityQuery::EParallelExecutionFlags::AutoBalance);
	}

	bool bPublishVisionSources = bCollectVisionSources;
	uint32 PublishedVisionCollectionRevision = VisionCollectionRevision;
	if (bSampleVisionSources
		&& !bCollectVisionSources
		&& VisionSourceEntityCollection.IsValid()
		&& VisionSourceEntityCollectionRevision == RequestedVisionSampleRevision)
	{
		// Membership comes from the low-frequency camera/radius filter. At the
		// scene cadence, read only position and velocity for that compact set.
		// This does not walk the world population or the HashGrid again.
		VisionSourceQueue.Reset();
		const TConstArrayView<FMassArchetypeEntityCollection> VisionSourceCollections =
			VisionSourceEntityCollection->GetUpToDatePerArchetypeCollections(EntityManager);
		auto SampleVisionSourceChunk = [&, this](FMassExecutionContext& VisionContext)
		{
			const int32 NumEntities = VisionContext.GetNumEntities();
			const auto FlagsList = VisionContext.GetFragmentView<FEntityFlagFragment>();
			const auto LocatingList = VisionContext.GetFragmentView<FLocating>();
			const auto MovingList = VisionContext.GetFragmentView<FMoving>();
			TArray<FVector4f> LocalVisionSources;
			LocalVisionSources.Reserve(NumEntities);
			for (int32 Index = 0; Index < NumEntities; ++Index)
			{
				if (!FlagsList[Index].HasFlag(ActivatedFlag))
				{
					continue;
				}
				const FVector& Location = LocatingList[Index].Location;
				if (!FogRenderSubsystem->ShouldCollectVisionSource(Location))
				{
					continue;
				}
				const FVector3f& Velocity = MovingList[Index].CurrentVelocity;
				LocalVisionSources.Add(FVector4f(
					static_cast<float>(Location.X),
					static_cast<float>(Location.Y),
					Velocity.X,
					Velocity.Y));
			}
			if (!LocalVisionSources.IsEmpty())
			{
				VisionSourceQueue.Append(LocalVisionSources);
			}
		};
		if (!VisionSourceCollections.IsEmpty())
		{
			if constexpr (MBParallelToggle::RenderProcessor || MBParallelToggle::ForceAllSingleThread)
			{
				VisionSourceSampleQuery.ForEachEntityChunkInCollections(
					VisionSourceCollections,
					Context,
					SampleVisionSourceChunk);
			}
			else
			{
				VisionSourceSampleQuery.ParallelForEachEntityChunkInCollection(
					VisionSourceCollections,
					Context,
					SampleVisionSourceChunk,
					FMassEntityQuery::EParallelExecutionFlags::AutoBalance);
			}
		}
		bPublishVisionSources = true;
		PublishedVisionCollectionRevision = VisionSourceEntityCollectionRevision;
	}

	if (bPublishVisionSources)
	{
		FogRenderSubsystem->PublishVisionSources(
			MoveTemp(VisionSourceQueue.Items),
			PublishedVisionCollectionRevision,
			FogWorldTimeSeconds);
	}
	if (bCollectMinimapSnapshot)
	{
		CSV_CUSTOM_STAT(
			FogMassBattleRender,
			MinimapSnapshotAgents,
			MinimapVisionSourceUnitQueue.Items.Num()
				+ MinimapFriendlyNonVisionUnitQueue.Items.Num()
				+ MinimapOtherUnitQueue.Items.Num(),
			ECsvCustomStatOp::Set);
		FogRenderSubsystem->PublishMinimapSnapshot(
			MoveTemp(MinimapVisionSourceUnitQueue.Items),
			MoveTemp(MinimapFriendlyNonVisionUnitQueue.Items),
			MoveTemp(MinimapOtherUnitQueue.Items),
			MoveTemp(MinimapFogVisibleUnitQueue.Items));
	}
	if (bIsSimTick && AttackRevealQueue.Items.Num() > 0)
	{
		FogRenderSubsystem->NotifyAttackReveals(AttackRevealQueue.Items, FogWorldTimeSeconds);
	}

	// Dense mode already owns the exact frame extent. Build a free-slot list only
	// when a simulation tick actually has first-time registrations.
	const bool bNeedsRegistrationSlots = bIsSimTick && !RegistrationQueue.Items.IsEmpty();
	TArray<TArray<int32>> AvailableFreeSlots;
	if (bNeedsRegistrationSlots)
	{
		AvailableFreeSlots.SetNum(AllRenderBatches.Num());
	}
	MBParallelFor<MBParallelToggle::RenderProcessor_Batches>(AllRenderBatches.Num(), [&](int32 BatchIndex)
	{
		FAgentRenderBatchData* Data = AllRenderBatches[BatchIndex];
		const int32 NumSlots = Data->FreeSlotArray.Num();
		// Only first registration/representation transitions need a free-list.
		// Build it once per batch instead of calling Find/Contains for every unit
		// (which becomes quadratic on camera jumps).
		if (bNeedsRegistrationSlots)
		{
			TArray<int32>& FreeSlots = AvailableFreeSlots[BatchIndex];
			for (int32 SlotIndex = 0; SlotIndex < NumSlots; ++SlotIndex)
			{
				if (Data->FreeSlotArray[SlotIndex])
				{
					FreeSlots.Add(SlotIndex);
				}
			}
		}
	});

	// ============== SIM TICK ONLY: Entity Registration ==============
	if (bIsSimTick)
	{
		if (MB.bDeterministic)
		{
			RegistrationQueue.Items.Sort([&](const FMassEntityHandle& A, const FMassEntityHandle& B)
			{
				int32 ID_A = -1; int32 ID_B = -1;
				if (MA.IsValid(A)) if (const auto* Stats = MA.GetFragmentPtr<FStatistics>(A)) ID_A = Stats->UniqueID;
				if (MA.IsValid(B)) if (const auto* Stats = MA.GetFragmentPtr<FStatistics>(B)) ID_B = Stats->UniqueID;
				return ID_A < ID_B;
			});
		}

		for (FMassEntityHandle EntityToRegister : RegistrationQueue.Items)
		{
			if (!MA.IsValid(EntityToRegister)) continue;
			auto& PendingRendering = MA.GetFragmentRef<FVisualizing>(EntityToRegister);
			if (PendingRendering.CurrentRepresentation != ERepresentationMode::Particle) continue;

			auto& SubType = MA.GetFragmentRef<FSubType>(EntityToRegister);
			auto& StyleType = MA.GetFragmentRef<FStyleType>(EntityToRegister);
			static const FVisualize GRenderFallback{};
			const FVisualize* RenderPtr = MA.GetFragmentPtr<FVisualize>(EntityToRegister);
			const FVisualize& Render = RenderPtr ? *RenderPtr : GRenderFallback;
			auto& Rendering = PendingRendering;
			auto& Flags = MA.GetFragmentRef<FEntityFlagFragment>(EntityToRegister);
			auto& Locating = MA.GetFragmentRef<FLocating>(EntityToRegister);
			auto& Rotating = MA.GetFragmentRef<FRotating>(EntityToRegister);
			auto& Scale = MA.GetFragmentRef<FScaling>(EntityToRegister);
			auto& Scaling = MA.GetFragmentRef<FScaling>(EntityToRegister);
			auto& Collider = MA.GetFragmentRef<FCollider>(EntityToRegister);
			auto& Animating = MA.GetFragmentRef<FAnimating>(EntityToRegister);
			auto& HealthBar = MA.GetFragmentRef<FHealthBar>(EntityToRegister);
			auto& Team = MA.GetFragmentRef<FTeam>(EntityToRegister);
			auto& Move = MA.GetFragmentRef<FMove>(EntityToRegister);
			auto& Moving = MA.GetFragmentRef<FMoving>(EntityToRegister);
			const FMassBattleFogVisionSourceFragment* RegisterFogPolicy =
				MA.GetConstSharedFragmentPtr<FMassBattleFogVisionSourceFragment>(EntityToRegister);
			FMassBattleFogLastSeenFragment* RegisterLastSeen =
				MA.GetFragmentPtr<FMassBattleFogLastSeenFragment>(EntityToRegister);
			const EMassBattleFogVisibilityPolicy RegisterVisibilityPolicy = RegisterFogPolicy
				? RegisterFogPolicy->VisibilityPolicy
				: EMassBattleFogVisibilityPolicy::Standard;
			checkSlow(ProxyIdByEntityIndex.IsValidIndex(EntityToRegister.Index));
			const int32 RegisterProxyId = ProxyIdByEntityIndex[EntityToRegister.Index];
			checkSlow(ProxyPool.IsValidIndex(RegisterProxyId));
			const FMassBattleFogRenderProxy& RegisterProxy = ProxyPool[RegisterProxyId];
			checkSlow(RegisterProxy.Entity == EntityToRegister);
			const uint8 RegistrationFogState = RegisterProxy.FogVisibilityState;
			const bool bRegisterRememberedSnapshot = RegisterVisibilityPolicy
					== EMassBattleFogVisibilityPolicy::RememberLastSeen
				&& RegisterLastSeen
				&& RegisterLastSeen->bHasSnapshot
				&& RegistrationFogState < 3u
				&& FogRenderSubsystem->IsInsideRenderWindow(RegisterLastSeen->SnapshotLocation);

			TObjectPtr<AMassBattleAgentRenderer> RendererActor;
			if (AMassBattleAgentRenderer* ExistingRenderer = MB.AgentRenderers.FindRef(SubType.Index);
				IsValid(ExistingRenderer))
			{
				RendererActor = ExistingRenderer;
			}
			else
			{
				MB.AgentRenderers.Remove(SubType.Index);
				TSubclassOf<AMassBattleAgentRenderer> RendererClass = Render.RendererClass.Get();
				if (!IsValid(RendererClass))
				{
					QueueRendererClassLoad(Render.RendererClass);
					continue;
				}

				FActorSpawnParameters SpawnParams;
				SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				RendererActor = World->SpawnActor<AMassBattleAgentRenderer>(RendererClass, FTransform::Identity, SpawnParams);

				if (!IsValid(RendererActor)) continue;
				RendererActor->SubType.Index = SubType.Index;
				MB.AgentRenderers.Add(SubType.Index, RendererActor);
			}

			int32 RenderBatchId = -1;
			for (auto& BatchPair : RendererActor->SpawnedRenderBatches)
			{
				const int32* ExistingBatchIndex = StableBatchIndices.Find(&BatchPair.Value);
				const bool bHasReusableSlot = ExistingBatchIndex
					&& AvailableFreeSlots.IsValidIndex(*ExistingBatchIndex)
					&& !AvailableFreeSlots[*ExistingBatchIndex].IsEmpty();
				if (bHasReusableSlot
					|| BatchPair.Value.FreeSlotArray.Num() < RendererActor->RenderBatchSize)
				{
					RenderBatchId = BatchPair.Key;
					break;
				}
			}

			if (RenderBatchId == -1) RenderBatchId = AddRenderBatch(RendererActor);
			if (RenderBatchId == INDEX_NONE)
			{
				continue;
			}

			FAgentRenderBatchData* BatchData =
				RendererActor->SpawnedRenderBatches.Find(RenderBatchId);
			if (!BatchData)
			{
				continue;
			}
			auto& Data = *BatchData;
			int32 NewInstanceId = INDEX_NONE;
			if (const int32* ExistingBatchIndex = StableBatchIndices.Find(&Data);
				ExistingBatchIndex
				&& AvailableFreeSlots.IsValidIndex(*ExistingBatchIndex)
				&& !AvailableFreeSlots[*ExistingBatchIndex].IsEmpty())
			{
				NewInstanceId = AvailableFreeSlots[*ExistingBatchIndex].Pop(EAllowShrinking::No);
			}

			const FTransform3f& RenderTransform = Render.Transform;
			const FTransform& RendererOffset = RendererActor->Offset;

			// Combine transforms with identity checks for performance
			FTransform CombinedTransform;
			if (!RendererOffset.Equals(FTransform::Identity))
			{
				if (!RenderTransform.Equals(FTransform3f::Identity))
				{
					// Both transforms are valid, combine them
					CombinedTransform = FTransform(RenderTransform) * RendererOffset;
				}
				else
				{
					// Only renderer offset is valid
					CombinedTransform = RendererOffset;
				}
			}
			else
			{
				// Renderer offset is identity, use render transform as-is
				CombinedTransform = FTransform(RenderTransform);
			}

			FVector AgentRenderScale = CombinedTransform.GetScale3D() * Scaling.Scale * (FVector)Scaling.JiggleMultiplier;
			FQuat AgentFacingRot = (FQuat)Rotating.RotationQuat;

			// Apply Mesh Tilt
			if (Move.Tilt.TiltMode == ETiltMode::OnlyMesh)
			{
				AgentFacingRot = AgentFacingRot * (FQuat)Moving.CurrentTilt;
			}
			else if (Move.Tilt.TiltMode == ETiltMode::OnlyCollider)
			{
				AgentFacingRot = AgentFacingRot * (FQuat)Moving.CurrentTilt.Inverse();
			}

			const float SelfRadius = Collider.Radius * Scaling.Scale;
			const float SelfShaftHalfHeight = Collider.Height * Scaling.Scale * 0.5f;
			const float SelfTotalHalfHeight = SelfShaftHalfHeight + SelfRadius;

			const FQuat4f PhysicsRotation = Rotating.RotationQuat * Collider.RelativeRotation.Quaternion();
			const FVector PhysicsUp = (FVector)PhysicsRotation.GetUpVector();
			const float PhysicsOffset = SelfShaftHalfHeight * FMath::Abs(PhysicsUp.Z) + SelfRadius;
			const float GroundZ = Locating.Location.Z - PhysicsOffset;

			// Use PhysicsRotation for visual calculations to match capsule
			const float VisualOffset = SelfShaftHalfHeight * FMath::Abs(PhysicsUp.Z) + SelfRadius;
			const float VisualCenterZ = GroundZ + VisualOffset;

			FVector AgentRenderLocation = Locating.Location;
			AgentRenderLocation.Z = VisualCenterZ;

			// Pivot uses physics rotation for correct pivot point during tilt
			FVector PivotOffset = ((FQuat)PhysicsRotation).RotateVector(FVector(0, 0, SelfTotalHalfHeight));

			AgentRenderLocation -= PivotOffset;
			// Apply offset as absolute transform (rotated by facing, not scaled)
			AgentRenderLocation += AgentFacingRot.RotateVector(CombinedTransform.GetLocation());

			FQuat AgentRenderRotation = CombinedTransform.GetRotation().IsIdentity() ? AgentFacingRot : (AgentFacingRot * CombinedTransform.GetRotation());
			FTransform EntityTransform(AgentRenderRotation, AgentRenderLocation, AgentRenderScale);

			// Determine LOD debug visualization state
			const FAgentDebug* AgentDebug = MA.GetFragmentPtr<FAgentDebug>(EntityToRegister);
			const bool bDrawLODColor = AgentDebug && AgentDebug->bDrawLODColor;
			const bool bBeingSelect = Flags.HasFlag(BeingSelectFlag);
			const bool bSelected = Flags.HasFlag(SelectedFlag);
			float PackedTeamDissolve = EncodeDynamicParams0(Team.index, Animating.Dissolve, Rendering.CurrentLOD, bDrawLODColor, bBeingSelect, bSelected);
			float PackedFrames01 = EncodeFrames01(static_cast<float>(Animating.AnimBlendResult.CurrentFrame0), static_cast<float>(Animating.AnimBlendResult.CurrentFrame1));
			float PackedData2 = EncodeFrame2AndLerps(Animating.AnimBlendResult.CurrentFrame2, Animating.AnimBlendResult.Lerp0, Animating.AnimBlendResult.Lerp1);
			float MatFx = EncodeStatusEffects(Animating.HitGlow, Animating.IceFxInterped, Animating.FireFxInterped, Animating.PoisonFxInterped);
			FVector4f DynamicParams0 = FVector4f(PackedFrames01, PackedData2, MatFx, PackedTeamDissolve);
			const FVector SlotLocation = bRegisterRememberedSnapshot
				? RegisterLastSeen->SnapshotLocation
				: EntityTransform.GetLocation();
			const FQuat4f SlotRotation = bRegisterRememberedSnapshot
				? RegisterLastSeen->SnapshotRotation
				: FQuat4f(EntityTransform.GetRotation());
			const FVector3f SlotScale = bRegisterRememberedSnapshot
				? RegisterLastSeen->SnapshotScale
				: FVector3f(EntityTransform.GetScale3D());
			const FVector4f SlotDynamicParams = bRegisterRememberedSnapshot
				? RegisterLastSeen->SnapshotDynamicParams0
				: DynamicParams0;
			const FVector3f SlotHealthBar = bRegisterRememberedSnapshot
				? RegisterLastSeen->SnapshotHealthBar
				: FVector3f(HealthBar.Opacity, HealthBar.CurrentRatio, HealthBar.TargetRatio);
			const int32 SlotLOD = bRegisterRememberedSnapshot
				? RegisterLastSeen->SnapshotLOD
				: Rendering.CurrentLOD;
			const int32 SlotStyle = bRegisterRememberedSnapshot
				? RegisterLastSeen->SnapshotStyle
				: StyleType.Index;

			if (NewInstanceId == INDEX_NONE)
			{
				NewInstanceId = Data.FreeSlotArray.Add(false);
				Data.IsHiddenArray.Add(false);
				Data.LocationArray.Add(SlotLocation);
				Data.OrientationArray.Add(SlotRotation);
				Data.ScaleArray.Add(SlotScale);
				Data.DynamicParams0_Array.Add(SlotDynamicParams);
				Data.HealthBar_Opacity_CurrentRatio_TargetRatio_Array.Add(SlotHealthBar);
				Data.CurrentLODArray.Add(SlotLOD);
				Data.StyleArray.Add(SlotStyle);
			}
			else
			{
				Data.FreeSlotArray[NewInstanceId] = false;
				Data.IsHiddenArray[NewInstanceId] = false;
				Data.LocationArray[NewInstanceId] = SlotLocation;
				Data.OrientationArray[NewInstanceId] = SlotRotation;
				Data.ScaleArray[NewInstanceId] = SlotScale;
				Data.DynamicParams0_Array[NewInstanceId] = SlotDynamicParams;
				Data.HealthBar_Opacity_CurrentRatio_TargetRatio_Array[NewInstanceId] = SlotHealthBar;
				Data.CurrentLODArray[NewInstanceId] = SlotLOD;
				Data.StyleArray[NewInstanceId] = SlotStyle;
			}

			Rendering.InstanceId = NewInstanceId;
			Rendering.RenderBatchId = RenderBatchId;
			Rendering.RendererActor = RendererActor;

			// Initialize interpolation state to current values (no interpolation lag on spawn)
			Rendering.TargetLocation = SlotLocation;
			Rendering.TargetRotation = SlotRotation;
			Rendering.TargetScale = SlotScale;
			Rendering.InterpLocation = Rendering.TargetLocation;
			Rendering.InterpRotation = Rendering.TargetRotation;
			Rendering.InterpScale = Rendering.TargetScale;

			Rendering.TargetFrame0 = Animating.AnimBlendResult.CurrentFrame0;
			Rendering.TargetFrame1 = Animating.AnimBlendResult.CurrentFrame1;
			Rendering.TargetFrame2 = Animating.AnimBlendResult.CurrentFrame2;
			Rendering.InterpFrame0 = Rendering.TargetFrame0;
			Rendering.InterpFrame1 = Rendering.TargetFrame1;
			Rendering.InterpFrame2 = Rendering.TargetFrame2;
			Rendering.TargetLerp0 = Animating.AnimBlendResult.Lerp0;
			Rendering.TargetLerp1 = Animating.AnimBlendResult.Lerp1;
			Rendering.InterpLerp0 = Rendering.TargetLerp0;
			Rendering.InterpLerp1 = Rendering.TargetLerp1;

			// Cache animation params
			const FPerLevelAnimState* ActiveState = (Animating.CurrentAnimBlendLevel == 1) ? &Animating.Level1State
				: ((Animating.CurrentAnimBlendLevel == 2) ? &Animating.Level2State : &Animating.Level3State);
			Rendering.CachedPlayRate0 = ActiveState->PlayRate0;
			Rendering.CachedPlayRate1 = ActiveState->PlayRate1;
			Rendering.CachedPlayRate2 = ActiveState->PlayRate2;
			Rendering.CachedSampleRate = Animating.SampleRate;
			Rendering.CachedStartFrame0 = ActiveState->StartFrame0;
			Rendering.CachedEndFrame0 = ActiveState->EndFrame0;
			Rendering.CachedStartFrame1 = ActiveState->StartFrame1;
			Rendering.CachedEndFrame1 = ActiveState->EndFrame1;
			Rendering.CachedStartFrame2 = ActiveState->StartFrame2;
			Rendering.CachedEndFrame2 = ActiveState->EndFrame2;
			Rendering.CachedLoop0 = ActiveState->bLoop0;
			Rendering.CachedLoop1 = ActiveState->bLoop1;
			Rendering.CachedLoop2 = ActiveState->bLoop2;

			// Initialize MatFX state
			Rendering.TargetIceFx = Animating.IceFx;
			Rendering.TargetFireFx = Animating.FireFx;
			Rendering.TargetPoisonFx = Animating.PoisonFx;
			Rendering.TargetHitGlow = Animating.HitGlow;
			Rendering.TargetDissolve = Animating.Dissolve;
			Rendering.InterpIceFx = Animating.IceFxInterped;
			Rendering.InterpFireFx = Animating.FireFxInterped;
			Rendering.InterpPoisonFx = Animating.PoisonFxInterped;
			Rendering.InterpHitGlow = Animating.HitGlow;
			Rendering.InterpDissolve = Animating.Dissolve;

			// Initialize HealthBar ratio state (opacity is read directly, not interpolated)
			Rendering.TargetHBCurrentRatio = HealthBar.CurrentRatio;
			Rendering.TargetHBTargetRatio = HealthBar.TargetRatio;
			Rendering.InterpHBCurrentRatio = HealthBar.CurrentRatio;
			Rendering.InterpHBTargetRatio = HealthBar.TargetRatio;

			Rendering.bInterpInitialized = true;
			if (RegisterLastSeen)
			{
				RegisterLastSeen->bShowingSnapshot = bRegisterRememberedSnapshot;
			}

			Context.Defer().AddTag<FRenderingTag>(EntityToRegister);
			Flags.SetFlag(RenderingFlag);
		}
		RegistrationQueue.Reset();
	} // End of bIsSimTick block for registration

	// =====================================================================
	// Projectile interpolation (every frame for attached FX smoothing)
	// Target values are updated by ProjectileMonoProcessor on sim ticks.
	// =====================================================================
	ProjectileInterpQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& Ctx)
	{
		const int32 NumEntities = Ctx.GetNumEntities();
		auto ProjRenderingList = Ctx.GetMutableFragmentView<FProjectileRendering>();
		auto ParamsList = Ctx.GetFragmentView<FProjectileParams>();
		auto LocatingList = Ctx.GetFragmentView<FLocating>();
		auto RotatingList = Ctx.GetFragmentView<FRotating>();
		auto ScalingList = Ctx.GetFragmentView<FScaling>();

		for (int32 i = 0; i < NumEntities; ++i)
		{
			auto& ProjRendering = ProjRenderingList[i];
			const auto& InterpParams = ParamsList[i].InterpParams;

			if (!ProjRendering.bInterpInitialized)
			{
				ProjRendering.TargetLocation = LocatingList[i].Location;
				ProjRendering.TargetRotation = RotatingList[i].RotationQuat;
				ProjRendering.TargetScale = FVector3f(ScalingList[i].Scale);
				ProjRendering.InterpLocation = ProjRendering.TargetLocation;
				ProjRendering.InterpRotation = ProjRendering.TargetRotation;
				ProjRendering.InterpScale = ProjRendering.TargetScale;
				ProjRendering.bInterpInitialized = true;
				continue;
			}

			if (InterpParams.IsEnabled())
			{
				if (InterpParams.SnapDist > 0.f)
				{
					const float DistSqr = FVector::DistSquared(ProjRendering.InterpLocation, ProjRendering.TargetLocation);
					if (DistSqr > FMath::Square(InterpParams.SnapDist))
					{
						ProjRendering.InterpLocation = ProjRendering.TargetLocation;
						ProjRendering.InterpRotation = ProjRendering.TargetRotation;
						ProjRendering.InterpScale = ProjRendering.TargetScale;
					}
				}

				FVector PredictedTarget = ProjRendering.TargetLocation;
				if (InterpParams.PredictTime > 0.f)
				{
					PredictedTarget += FVector(ProjRendering.CachedVelocity) * InterpParams.PredictTime;
				}

				ProjRendering.InterpLocation = FMath::VInterpTo(ProjRendering.InterpLocation, PredictedTarget, RenderDeltaTime, InterpParams.InterpSpeed);
				ProjRendering.InterpRotation = FQuat4f(FMath::QInterpTo(FQuat(ProjRendering.InterpRotation), FQuat(ProjRendering.TargetRotation), RenderDeltaTime, InterpParams.InterpSpeed));
				ProjRendering.InterpScale = FVector3f(FMath::VInterpTo(FVector(ProjRendering.InterpScale), FVector(ProjRendering.TargetScale), RenderDeltaTime, InterpParams.InterpSpeed));
			}
			else
			{
				ProjRendering.InterpLocation = LocatingList[i].Location;
				ProjRendering.InterpRotation = RotatingList[i].RotationQuat;
				ProjRendering.InterpScale = FVector3f(ScalingList[i].Scale);
			}
		}
	});

	// =====================================================================
	// Loot interpolation (every frame for attached FX smoothing)
	// Target values are updated by LootMonoProcessor on sim ticks.
	// =====================================================================
	LootInterpQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& Ctx)
	{
		const int32 NumEntities = Ctx.GetNumEntities();
		auto LootRenderingList = Ctx.GetMutableFragmentView<FLootRendering>();
		auto ParamsList = Ctx.GetFragmentView<FLootParams>();
		auto LocatingList = Ctx.GetFragmentView<FLocating>();
		auto RotatingList = Ctx.GetFragmentView<FRotating>();
		auto ScalingList = Ctx.GetFragmentView<FScaling>();

		for (int32 i = 0; i < NumEntities; ++i)
		{
			auto& LootRend = LootRenderingList[i];
			const auto& InterpParams = ParamsList[i].InterpParams;

			if (!LootRend.bInterpInitialized)
			{
				LootRend.TargetLocation = LocatingList[i].Location;
				LootRend.TargetRotation = RotatingList[i].RotationQuat;
				LootRend.TargetScale = FVector3f(ScalingList[i].Scale);
				LootRend.InterpLocation = LootRend.TargetLocation;
				LootRend.InterpRotation = LootRend.TargetRotation;
				LootRend.InterpScale = LootRend.TargetScale;
				LootRend.bInterpInitialized = true;
				continue;
			}

			if (InterpParams.IsEnabled())
			{
				if (InterpParams.SnapDist > 0.f)
				{
					const float DistSqr = FVector::DistSquared(LootRend.InterpLocation, LootRend.TargetLocation);
					if (DistSqr > FMath::Square(InterpParams.SnapDist))
					{
						LootRend.InterpLocation = LootRend.TargetLocation;
						LootRend.InterpRotation = LootRend.TargetRotation;
						LootRend.InterpScale = LootRend.TargetScale;
					}
				}

				FVector PredictedTarget = LootRend.TargetLocation;
				if (InterpParams.PredictTime > 0.f)
				{
					PredictedTarget += FVector(LootRend.CachedVelocity) * InterpParams.PredictTime;
				}

				LootRend.InterpLocation = FMath::VInterpTo(LootRend.InterpLocation, PredictedTarget, RenderDeltaTime, InterpParams.InterpSpeed);
				LootRend.InterpRotation = FQuat4f(FMath::QInterpTo(FQuat(LootRend.InterpRotation), FQuat(LootRend.TargetRotation), RenderDeltaTime, InterpParams.InterpSpeed));
				LootRend.InterpScale = FVector3f(FMath::VInterpTo(FVector(LootRend.InterpScale), FVector(LootRend.TargetScale), RenderDeltaTime, InterpParams.InterpSpeed));
			}
			else
			{
				LootRend.InterpLocation = LocatingList[i].Location;
				LootRend.InterpRotation = RotatingList[i].RotationQuat;
				LootRend.InterpScale = FVector3f(ScalingList[i].Scale);
			}
		}
	});

	// The state stage is complete here. Independent backends execute later in
	// FrameEnd and consume the published final visible entity collection.
	for (auto RendererIt = MB.AgentRenderers.CreateIterator(); RendererIt; ++RendererIt)
	{
		AMassBattleAgentRenderer* Renderer = RendererIt.Value();
		if (IsValid(Renderer))
		{
			IdleCheck(Renderer, RenderDeltaTime);
		}
		if (!IsValid(Renderer))
		{
			RendererIt.RemoveCurrent();
		}
	}

	int32 TotalUploadedElements = 0;
	for (auto const& [SubTypeIndex, Renderer] : MB.AgentRenderers)
	{
		if (!IsValid(Renderer)) continue;
		int32 TextCount = 0;

		for (auto& BatchPair : Renderer->SpawnedRenderBatches)
		{
			auto& Data = BatchPair.Value;
			TObjectPtr<UNiagaraComponent> NiagaraSystem = Data.SpawnedNiagaraSystem;
			if (!IsValid(NiagaraSystem)) continue;
			const int32 FrameCount = Data.LocationArray.Num();
			TotalUploadedElements += FrameCount;
			NiagaraSystem->SetVariableBool(FName("EnableTextPop"), false);
			NiagaraSystem->SetVariableInt(FName("SubType"), SubTypeIndex);
			NiagaraSystem->SetVariableInt(FName("InstanceCount"), FrameCount);
			TextCount += Data.TextData.Text_Location_Array.Num();

			// Warm empty components keep their CPU allocation and Niagara system,
			// but they do not receive seven empty Array DI uploads every frame.
			if (FrameCount == 0)
			{
				continue;
			}

			UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(NiagaraSystem, FName("LocationArray"), Data.LocationArray);
			UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayQuat(NiagaraSystem, FName("OrientationArray"), Data.OrientationArray);
			UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(NiagaraSystem, FName("ScaleArray"), Data.ScaleArray);
			UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector4(NiagaraSystem, FName("DynamicParams0_Array"), Data.DynamicParams0_Array);
			UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(NiagaraSystem, FName("HealthBar_Opacity_CurrentRatio_TargetRatio_Array"), Data.HealthBar_Opacity_CurrentRatio_TargetRatio_Array);
			UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayBool(NiagaraSystem, FName("IsHidden_Array"), Data.IsHiddenArray);
			UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayInt32(NiagaraSystem, FName("MeshIndex_Array"), Data.CurrentLODArray);

			if (Data.bUsePositionArray)UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayPosition(NiagaraSystem, FName("PositionArray"), Data.LocationArray);
			if (Data.bUseStyleArray) UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayInt32(NiagaraSystem, FName("StyleArray"), Data.StyleArray);

		}

		if (TextCount > 0 && IsValid(Renderer->NDC_TextPop))
		{
			FMassTextPopWriter Writer;
			Writer.DebugSource = TEXT("MassBattleAgentRenderProcessor");

			if (Writer.BeginWrite(GetWorld(), Renderer->NDC_TextPop->Get(), FNiagaraDataChannelSearchParameters(), TextCount, false, true, true))
			{
				Writer.InitBuffers();
				int32 PoppedText = 0;
				for (auto& BatchPair : Renderer->SpawnedRenderBatches)
				{
					auto& Data = BatchPair.Value;
					int32 Num = Data.TextData.Text_Location_Array.Num();
					if (Num > 0)
					{
						Writer.AppendBatch(PoppedText, Data.TextData.Text_Location_Array, Data.TextData.Text_Value_Style_Scale_Offset_Array, SubTypeIndex);
						PoppedText += Num;
					}
				}
				Writer.EndWrite();

				// Clear text data after writing to prevent re-triggering on render-only frames
				for (auto& BatchPair : Renderer->SpawnedRenderBatches)
				{
					BatchPair.Value.TextData.Text_Location_Array.Reset();
					BatchPair.Value.TextData.Text_Value_Style_Scale_Offset_Array.Reset();
				}
			}

			if (Renderer->SpawnedRenderBatches.Num() > 0)
			{
				Renderer->SpawnedRenderBatches.CreateIterator().Value().SpawnedNiagaraSystem->SetVariableBool(FName("EnableTextPop"), true);
			}
		}
	}
	CSV_CUSTOM_STAT(FogMassBattleRender, UploadedElements, TotalUploadedElements, ECsvCustomStatOp::Set);
	FogRenderSubsystem->PublishAgentRenderWorkload(
		ActiveRenderEntitiesScratch.Num(),
		TotalUploadedElements);
	bPreviousFrameWasSimulationTick = bIsSimTick;
}

void UMassBattleFogAgentRenderProcessor::IdleCheck(AMassBattleAgentRenderer* RendererActor, float DeltaTime)
{
	auto& RenderBatches = RendererActor->SpawnedRenderBatches;
	for (auto BatchIt = RenderBatches.CreateIterator(); BatchIt; ++BatchIt)
	{
		auto& BatchData = BatchIt.Value();
		// Trailing free slots were already trimmed, so an empty array is an O(1)
		// indication that this batch has no submitted unit.
		const bool bIsIdle = BatchData.FreeSlotArray.IsEmpty() && BatchData.TextData.Text_Location_Array.IsEmpty();

		if (bIsIdle)
		{
			if (BatchData.IdleRemoveCountdown < 0.0f)
			{
				BatchData.IdleRemoveCountdown = FMath::Max(
					RendererActor->RenderBatchIdleRemoveCoolDown,
					MinimumBatchIdleSeconds);
			}
			else BatchData.IdleRemoveCountdown -= DeltaTime;

			if (BatchData.IdleRemoveCountdown <= KINDA_SMALL_NUMBER)
			{
				if (IsValid(BatchData.SpawnedNiagaraSystem))
				{
					BatchData.SpawnedNiagaraSystem->DestroyComponent();
				}
				BatchIt.RemoveCurrent();
			}
		}
		else
		{
			BatchData.IdleRemoveCountdown = -1.0f;
		}
	}
	// The renderer actor is the persistent registry object for one unit subtype.
	// It has no tick and owns no per-unit state once its Niagara batches are gone.
	// Destroying it here leaves FVisualizing::RendererActor dangling in ordinary
	// Mass fragments (those fragments are not GC reference containers), and also
	// forces an avoidable SpawnActor cycle when the subtype becomes visible again.
	// World teardown owns the renderer actor lifetime; only idle batches are transient.
}

int32 UMassBattleFogAgentRenderProcessor::AddRenderBatch(AMassBattleAgentRenderer* RendererActor)
{
	if (!IsValid(RendererActor) || !IsValid(RendererActor->NiagaraSystemAsset))
	{
		return INDEX_NONE;
	}
	const int32 NewBatchId = RendererActor->NextRenderBatchId++;
	auto& RenderBatches = RendererActor->SpawnedRenderBatches;
	FAgentRenderBatchData& NewData = RenderBatches.Emplace(NewBatchId);
	NewData.Reserve(RendererActor->RenderBatchSize);

	auto System = UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), RendererActor->NiagaraSystemAsset, RendererActor->GetActorLocation(), FRotator::ZeroRotator, FVector(1), false, true, ENCPoolMethod::None, true);
	if (!IsValid(System))
	{
		RenderBatches.Remove(NewBatchId);
		static bool bLoggedMissingNiagaraComponent = false;
		if (!bLoggedMissingNiagaraComponent)
		{
			bLoggedMissingNiagaraComponent = true;
			UE_LOG(LogTemp, Warning,
				TEXT("FogOfWar: Niagara renderer component was unavailable; Mass simulation will continue without particle batches (expected with -nullrhi)."));
		}
		return INDEX_NONE;
	}
	System->SetVariableStaticMesh(TEXT("AgentMesh"), RendererActor->AgentMesh);
	System->SetCastShadow(true);

	// -----------------------------------------------------------------------
	// NEW: Auto-detect if "PositionArray" exists to enable LWC Precision
	// -----------------------------------------------------------------------
	TArray<FNiagaraVariable> UserVars;
	System->GetOverrideParameters().GetParameters(UserVars);
	for (const FNiagaraVariable& Var : UserVars)
	{
		if (Var.GetName() == FName("User.PositionArray"))
		{
			NewData.bUsePositionArray = true;
		}
		else if (Var.GetName() == FName("User.StyleArray"))
		{
			NewData.bUseStyleArray = true;
		}
	}
	// -----------------------------------------------------------------------

	NewData.SpawnedNiagaraSystem = System;

	return NewBatchId;
}

// ------------------------------------------------------------------------------------------------
// NEW HELPER: Calculate Play Rate based on User Logic
// PlayRate = (TotalFrames / SampleRate) / TargetDuration
// ------------------------------------------------------------------------------------------------
static float CalculatePlayRate(float StartFrame, float EndFrame, float TargetDuration, int32 SampleRate)
{
	if (TargetDuration <= KINDA_SMALL_NUMBER) return 1.0f;
	float TotalFrames = FMath::Abs(EndFrame - StartFrame);
	// (Frames / SampleRate) = Natural Time in Seconds
	// Rate = NaturalTime / TargetTime
	return (TotalFrames / (float)SampleRate) / TargetDuration;
}

// Renamed to prevent Unity Build collision with MassBattleAgentSubsystem.cpp::GetDataFromIndex
static FVector4f GetAnimDataFromSlotIndex(const FAnimData& Data, int32 SlotIndex)
{
	switch (SlotIndex)
	{
		case 0: return Data.Anim0;
		case 1: return Data.Anim1;
		case 2: return Data.Anim2;
		case 3: return Data.Anim3;
		case 4: return Data.Anim4;
		default: return FVector4f(-1, 0, 0, 0);
	}
}

void UMassBattleFogAgentRenderProcessor::UpdateAnimBlendLevel1(FPerLevelAnimState& State, const FAnimShared& Animation, const FAnimating& Animating, EAnimState CurrentState, bool bStateChanged, float DeltaTime, const FAppear& Appear, const FAttack& Attack, const FHit& Hit, const FDeath& Death, float AttackSpeedMultiplier, bool& bOutReset0)
{
	if (bStateChanged)
	{
		State.Lerp0 = 0;
		State.Lerp1 = 0;
		bOutReset0 = true;

		FVector4f CurrentAnimData;

		switch (CurrentState)
		{
			case EAnimState::Idle:
			case EAnimState::BS_IdleMove:
				CurrentAnimData = GetAnimDataFromSlotIndex(Animation.AnimData.IdleAnimData, Animating.SelectedIdleAnimIndex);
				State.Index0 = static_cast<uint8>(CurrentAnimData.X);
				State.StartFrame0 = static_cast<uint16>(CurrentAnimData.Z);
				State.EndFrame0 = static_cast<uint16>(CurrentAnimData.W);
				State.bLoop0 = true;
				State.PlayRate0 = CalculatePlayRate(State.StartFrame0, State.EndFrame0, CurrentAnimData.Y, Animating.SampleRate);
				{
					FVector4f MoveAnimCheck = GetAnimDataFromSlotIndex(Animation.AnimData.MoveAnimData, Animating.SelectedMoveAnimIndex);
					if (CurrentAnimData.X == MoveAnimCheck.X && Animating.PreviousAnimState == EAnimState::Move)
						bOutReset0 = false; // Same anim - inherit play progress
					else
						State.CurrentFrame0 = Animating.IdleRandomStartFrame;
				}
				break;
			case EAnimState::Move:
				CurrentAnimData = GetAnimDataFromSlotIndex(Animation.AnimData.MoveAnimData, Animating.SelectedMoveAnimIndex);
				State.Index0 = static_cast<uint8>(CurrentAnimData.X);
				State.StartFrame0 = static_cast<uint16>(CurrentAnimData.Z);
				State.EndFrame0 = static_cast<uint16>(CurrentAnimData.W);
				State.bLoop0 = true;
				State.PlayRate0 = CalculatePlayRate(State.StartFrame0, State.EndFrame0, CurrentAnimData.Y, Animating.SampleRate);
				{
					FVector4f IdleAnimCheck = GetAnimDataFromSlotIndex(Animation.AnimData.IdleAnimData, Animating.SelectedIdleAnimIndex);
					if (CurrentAnimData.X == IdleAnimCheck.X && Animating.PreviousAnimState == EAnimState::Idle)
						bOutReset0 = false; // Same anim - inherit play progress
					else
						State.CurrentFrame0 = Animating.MoveRandomStartFrame;
				}
				break;
			case EAnimState::Falling:
				CurrentAnimData = GetAnimDataFromSlotIndex(Animation.AnimData.FallAnimData, Animating.SelectedFallAnimIndex);
				State.Index0 = static_cast<uint8>(CurrentAnimData.X);
				State.StartFrame0 = static_cast<uint16>(CurrentAnimData.Z);
				State.EndFrame0 = static_cast<uint16>(CurrentAnimData.W);
				State.bLoop0 = true;
				State.PlayRate0 = CalculatePlayRate(State.StartFrame0, State.EndFrame0, CurrentAnimData.Y, Animating.SampleRate);
				State.CurrentFrame0 = State.StartFrame0;
				break;
			case EAnimState::Appearing:
				CurrentAnimData = GetAnimDataFromSlotIndex(Animation.AnimData.AppearAnimData, Animating.SelectedAppearAnimIndex);
				State.Index0 = static_cast<uint8>(CurrentAnimData.X);
				State.StartFrame0 = static_cast<uint16>(CurrentAnimData.Z);
				State.EndFrame0 = static_cast<uint16>(CurrentAnimData.W);
				State.bLoop0 = false;
				{
					float TargetDur = (Appear.bCanPlayAnim && Appear.bAnimAsDuration) ? CurrentAnimData.Y : Appear.Duration;
					State.PlayRate0 = CalculatePlayRate(State.StartFrame0, State.EndFrame0, TargetDur, Animating.SampleRate);
				}
				State.CurrentFrame0 = State.StartFrame0;
				break;
			case EAnimState::BeingHit:
				CurrentAnimData = GetAnimDataFromSlotIndex(Animation.AnimData.HitAnimData, Animating.SelectedHitAnimIndex);
				State.Index0 = static_cast<uint8>(CurrentAnimData.X);
				State.StartFrame0 = static_cast<uint16>(CurrentAnimData.Z);
				State.EndFrame0 = static_cast<uint16>(CurrentAnimData.W);
				State.bLoop0 = false;
				{
					float TargetDur = (Hit.bPlayAnim && Hit.bAnimAsDuration) ? CurrentAnimData.Y : Hit.AnimLength;
					State.PlayRate0 = CalculatePlayRate(State.StartFrame0, State.EndFrame0, TargetDur, Animating.SampleRate);
				}
				State.CurrentFrame0 = State.StartFrame0;
				break;
			case EAnimState::Attacking:
				CurrentAnimData = GetAnimDataFromSlotIndex(Animation.AnimData.AttackAnimData, Animating.SelectedAttackAnimIndex);
				State.Index0 = static_cast<uint8>(CurrentAnimData.X);
				State.StartFrame0 = static_cast<uint16>(CurrentAnimData.Z);
				State.EndFrame0 = static_cast<uint16>(CurrentAnimData.W);
				State.bLoop0 = false;
				{
					float TargetDur = (Attack.bCanPlayAnim && Attack.bAnimAsDuration) ? CurrentAnimData.Y : Attack.DurationPerRound;
					State.PlayRate0 = CalculatePlayRate(State.StartFrame0, State.EndFrame0, TargetDur, Animating.SampleRate);
				}
				State.PlayRate0 *= AttackSpeedMultiplier;
				State.CurrentFrame0 = State.StartFrame0;
				break;
			case EAnimState::Dying:
				CurrentAnimData = GetAnimDataFromSlotIndex(Animation.AnimData.DeathAnimData, Animating.SelectedDeathAnimIndex);
				State.Index0 = static_cast<uint8>(CurrentAnimData.X);
				State.StartFrame0 = static_cast<uint16>(CurrentAnimData.Z);
				State.EndFrame0 = static_cast<uint16>(CurrentAnimData.W);
				State.bLoop0 = false;
				{
					float TargetDur = (Death.bCanPlayAnim && Death.bAnimAsDuration) ? CurrentAnimData.Y : Death.AnimLength;
					State.PlayRate0 = CalculatePlayRate(State.StartFrame0, State.EndFrame0, TargetDur, Animating.SampleRate);
				}
				State.CurrentFrame0 = State.StartFrame0;
				break;
			default: break;
		}
	}
}

void UMassBattleFogAgentRenderProcessor::UpdateAnimBlendLevel2(FPerLevelAnimState& State, const FAnimShared& Animation, const FAnimating& Animating, EAnimState CurrentState, bool bStateChanged, bool bIsFirstUpdate, float DeltaTime, const FMoving& Moving, const FAppear& Appear, const FAttack& Attack, const FHit& Hit, const FDeath& Death, float AttackSpeedMultiplier, bool& bOutReset1, bool& bOutReset2)
{
	FVector4f IdleData = GetAnimDataFromSlotIndex(Animation.AnimData.IdleAnimData, Animating.SelectedIdleAnimIndex);
	State.Index0 = static_cast<uint8>(IdleData.X);
	State.StartFrame0 = static_cast<uint16>(IdleData.Z);
	State.EndFrame0 = static_cast<uint16>(IdleData.W);
	State.bLoop0 = true;
	State.PlayRate0 = CalculatePlayRate(State.StartFrame0, State.EndFrame0, IdleData.Y, Animating.SampleRate);

	FVector4f MoveData = GetAnimDataFromSlotIndex(Animation.AnimData.MoveAnimData, Animating.SelectedMoveAnimIndex);
	int32 TargetIndex1 = MoveData.X;
	float TargetStartFrame1 = Animating.MoveRandomStartFrame;
	float TargetEndFrame1 = MoveData.W;
	float TargetRealStartFrame1 = MoveData.Z;
	float TargetPlayRate1 = CalculatePlayRate(TargetRealStartFrame1, TargetEndFrame1, MoveData.Y, Animating.SampleRate);

	bool TargetLoop1 = true;
	bool bIsMontage = false;

	FVector4f MontageData;

	switch (CurrentState)
	{
		case EAnimState::Falling:
			bIsMontage = true;
			MontageData = GetAnimDataFromSlotIndex(Animation.AnimData.FallAnimData, Animating.SelectedFallAnimIndex);
			TargetIndex1 = MontageData.X;
			TargetLoop1 = true;
			TargetStartFrame1 = MontageData.Z;
			TargetRealStartFrame1 = MontageData.Z;
			TargetEndFrame1 = MontageData.W;
			TargetPlayRate1 = CalculatePlayRate(TargetRealStartFrame1, TargetEndFrame1, MontageData.Y, Animating.SampleRate);
			break;
		case EAnimState::Appearing:
			bIsMontage = true;
			MontageData = GetAnimDataFromSlotIndex(Animation.AnimData.AppearAnimData, Animating.SelectedAppearAnimIndex);
			TargetIndex1 = MontageData.X;
			TargetLoop1 = false;
			TargetStartFrame1 = MontageData.Z;
			TargetRealStartFrame1 = MontageData.Z;
			TargetEndFrame1 = MontageData.W;
			{
				float TargetDur = (Appear.bCanPlayAnim && Appear.bAnimAsDuration) ? MontageData.Y : Appear.Duration;
				TargetPlayRate1 = CalculatePlayRate(TargetRealStartFrame1, TargetEndFrame1, TargetDur, Animating.SampleRate);
			}
			break;
		case EAnimState::BeingHit:
			bIsMontage = true;
			MontageData = GetAnimDataFromSlotIndex(Animation.AnimData.HitAnimData, Animating.SelectedHitAnimIndex);
			TargetIndex1 = MontageData.X;
			TargetLoop1 = false;
			TargetStartFrame1 = MontageData.Z;
			TargetRealStartFrame1 = MontageData.Z;
			TargetEndFrame1 = MontageData.W;
			{
				float TargetDur = (Hit.bPlayAnim && Hit.bAnimAsDuration) ? MontageData.Y : Hit.AnimLength;
				TargetPlayRate1 = CalculatePlayRate(TargetRealStartFrame1, TargetEndFrame1, TargetDur, Animating.SampleRate);
			}
			break;
		case EAnimState::Attacking:
			bIsMontage = true;
			MontageData = GetAnimDataFromSlotIndex(Animation.AnimData.AttackAnimData, Animating.SelectedAttackAnimIndex);
			TargetIndex1 = MontageData.X;
			TargetLoop1 = false;
			TargetStartFrame1 = MontageData.Z;
			TargetRealStartFrame1 = MontageData.Z;
			TargetEndFrame1 = MontageData.W;
			{
				float TargetDur = (Attack.bCanPlayAnim && Attack.bAnimAsDuration) ? MontageData.Y : Attack.DurationPerRound;
				TargetPlayRate1 = CalculatePlayRate(TargetRealStartFrame1, TargetEndFrame1, TargetDur, Animating.SampleRate);
				TargetPlayRate1 *= AttackSpeedMultiplier;
			}
			break;
		case EAnimState::Dying:
			bIsMontage = true;
			MontageData = GetAnimDataFromSlotIndex(Animation.AnimData.DeathAnimData, Animating.SelectedDeathAnimIndex);
			TargetIndex1 = MontageData.X;
			TargetLoop1 = false;
			TargetStartFrame1 = MontageData.Z;
			TargetRealStartFrame1 = MontageData.Z;
			TargetEndFrame1 = MontageData.W;
			{
				float TargetDur = (Death.bCanPlayAnim && Death.bAnimAsDuration) ? MontageData.Y : Death.AnimLength;
				TargetPlayRate1 = CalculatePlayRate(TargetRealStartFrame1, TargetEndFrame1, TargetDur, Animating.SampleRate);
			}
			break;
		default: break;
	}

	if (bIsFirstUpdate)
	{
		State.Index1 = static_cast<uint8>(TargetIndex1);
		State.PlayRate1 = TargetPlayRate1;
		State.CurrentFrame1 = TargetStartFrame1;
		State.StartFrame1 = static_cast<uint16>(TargetRealStartFrame1);
		State.EndFrame1 = static_cast<uint16>(TargetEndFrame1);
		State.bLoop1 = TargetLoop1;
	}

	bool bSlot1MatchesTarget = (State.Index1 == TargetIndex1);

	if (bSlot1MatchesTarget)
	{
		if (bStateChanged && bIsMontage)
		{
			State.CurrentFrame1 = TargetStartFrame1;
			State.PlayRate1 = TargetPlayRate1;
			State.StartFrame1 = static_cast<uint16>(TargetRealStartFrame1);
			State.EndFrame1 = static_cast<uint16>(TargetEndFrame1);
			State.bLoop1 = TargetLoop1;
			bOutReset1 = true;
		}
		float TargetLerp = 0.f;
		if (bIsMontage)
		{
			TargetLerp = 1.0f;
			State.Lerp0 = bIsFirstUpdate ? AnimationHelpers::FloatToNormalizedUint8(TargetLerp) : AnimationHelpers::FloatToNormalizedUint8(FMath::FInterpTo(AnimationHelpers::NormalizedUint8ToFloat(State.Lerp0), TargetLerp, DeltaTime, Animation.AnimBlendSpeed));
		}
		else
		{
			float Velocity = Moving.CurrentVelocity.Size2D();
			float EffectiveVelocity = Velocity + FMath::Abs(Moving.CurrentAngularVelocity) * Animation.AngularSpeedBlendMultiplier;

			{
				const TRange<float> InputRange(Animation.BS_IdleMove[0], Animation.BS_IdleMove[1]);
				const TRange<float> OutputRange(0.f, 1.f);
				TargetLerp = FMath::GetMappedRangeValueClamped(InputRange, OutputRange, EffectiveVelocity);
			}
			State.Lerp0 = bIsFirstUpdate ? AnimationHelpers::FloatToNormalizedUint8(TargetLerp) : AnimationHelpers::FloatToNormalizedUint8(FMath::FInterpTo(AnimationHelpers::NormalizedUint8ToFloat(State.Lerp0), TargetLerp, DeltaTime, Animation.AnimBlendSpeed));
			if (IdleData.X == MoveData.X)
			{
				// Same anim: use smoothed alpha as playrate multiplier (0 = stopped, 1 = full rate)
				float Alpha = AnimationHelpers::NormalizedUint8ToFloat(State.Lerp0);
				State.PlayRate0 = TargetPlayRate1 * Alpha;
				State.PlayRate1 = TargetPlayRate1 * Alpha;
				State.CurrentFrame0 = State.CurrentFrame1;
			}
		}
	}
	else
	{
		State.Lerp0 = AnimationHelpers::FloatToNormalizedUint8(FMath::FInterpTo(AnimationHelpers::NormalizedUint8ToFloat(State.Lerp0), 0.0f, DeltaTime, Animation.AnimBlendSpeed * 2.0f));
		if (bIsMontage)
		{
			if (State.Index2 != TargetIndex1)
			{
				State.Index2 = static_cast<uint8>(TargetIndex1);
				State.PlayRate2 = TargetPlayRate1;
				State.CurrentFrame2 = TargetStartFrame1;
				State.StartFrame2 = static_cast<uint16>(TargetRealStartFrame1);
				State.EndFrame2 = static_cast<uint16>(TargetEndFrame1);
				State.bLoop2 = TargetLoop1;
				bOutReset2 = true;
			}
		}
		if (State.Lerp0 < 13) // 0.05 * 255 ≈ 13
		{
			if (bIsMontage)
			{
				State.Index1 = static_cast<uint8>(State.Index2);
				State.PlayRate1 = State.PlayRate2;
				State.CurrentFrame1 = State.CurrentFrame2;
				State.StartFrame1 = static_cast<uint16>(State.StartFrame2);
				State.EndFrame1 = static_cast<uint16>(State.EndFrame2);
				State.bLoop1 = State.bLoop2;
				State.Index2 = AnimationHelpers::INVALID_ANIM_INDEX;
			}
			else
			{
				State.Index1 = static_cast<uint8>(TargetIndex1);
				State.PlayRate1 = TargetPlayRate1;
				State.CurrentFrame1 = TargetStartFrame1;
				State.StartFrame1 = static_cast<uint16>(TargetRealStartFrame1);
				State.EndFrame1 = static_cast<uint16>(TargetEndFrame1);
				State.bLoop1 = TargetLoop1;
				bOutReset1 = true;
			}
		}
	}
	State.Lerp1 = 0;
}

void UMassBattleFogAgentRenderProcessor::UpdateAnimBlendLevel3(FPerLevelAnimState& State, const FAnimShared& Animation, const FAnimating& Animating, EAnimState CurrentState, bool bStateChanged, bool bIsFirstUpdate, float DeltaTime, const FMoving& Moving, const FAppear& Appear, const FAttack& Attack, const FHit& Hit, const FDeath& Death, float AttackSpeedMultiplier, bool& bOutReset0, bool& bOutReset1, bool& bOutReset2)
{
	float TargetLerp0 = 0.f;

	FVector4f IdleData = GetAnimDataFromSlotIndex(Animation.AnimData.IdleAnimData, Animating.SelectedIdleAnimIndex);
	FVector4f MoveData = GetAnimDataFromSlotIndex(Animation.AnimData.MoveAnimData, Animating.SelectedMoveAnimIndex);

	if (CurrentState == EAnimState::Montage)
	{
		if (Animating.SelectedMontageAnimIndex != -1)
		{
			FVector4f MontageData = GetAnimDataFromSlotIndex(Animation.AnimData.OtherAnimData, Animating.SelectedMontageAnimIndex);
			State.Index2 = static_cast<uint8>(MontageData.X);
			State.bLoop2 = Animating.bMontageLooping;
			State.StartFrame2 = static_cast<uint16>(MontageData.Z);
			State.EndFrame2 = static_cast<uint16>(MontageData.W);
			State.CurrentFrame2 = State.StartFrame2;
			State.PlayRate2 = Animating.MontagePlayRate;
			bOutReset2 = true;
		}
	}

	bool bUseSingleAnimScaling = (IdleData.X == MoveData.X);
	float Velocity = Moving.CurrentVelocity.Size2D();
	float EffectiveVelocity = Velocity + FMath::Abs(Moving.CurrentAngularVelocity) * Animation.AngularSpeedBlendMultiplier;

	{
		const TRange<float> InputRange(Animation.BS_IdleMove[0], Animation.BS_IdleMove[1]);
		const TRange<float> OutputRange(0.f, 1.f);
		TargetLerp0 = FMath::GetMappedRangeValueClamped(InputRange, OutputRange, EffectiveVelocity);
	}

	bool bIsMontageState = (CurrentState != EAnimState::BS_IdleMove);

	if (bStateChanged)
	{
		if (bIsMontageState)
		{
			if (Animating.PreviousAnimState != EAnimState::BS_IdleMove && Animating.PreviousAnimState != EAnimState::Idle && Animating.PreviousAnimState != EAnimState::Move)
			{
				ShiftSlotsForState(State, 2, 1); State.bBaseLayerIsMontage = true; State.Lerp0 = 255;
			}
			else State.bBaseLayerIsMontage = false;

			bOutReset2 = true;
			FVector4f MontageData;

			switch (CurrentState)
			{
				case EAnimState::Falling:
					MontageData = GetAnimDataFromSlotIndex(Animation.AnimData.FallAnimData, Animating.SelectedFallAnimIndex);
					State.Index2 = static_cast<uint8>(MontageData.X);
					State.bLoop2 = true;
					State.StartFrame2 = static_cast<uint16>(MontageData.Z);
					State.EndFrame2 = static_cast<uint16>(MontageData.W);
					State.CurrentFrame2 = State.StartFrame2;
					State.PlayRate2 = CalculatePlayRate(State.StartFrame2, State.EndFrame2, MontageData.Y, Animating.SampleRate);
					break;
				case EAnimState::Appearing:
					MontageData = GetAnimDataFromSlotIndex(Animation.AnimData.AppearAnimData, Animating.SelectedAppearAnimIndex);
					State.Index2 = static_cast<uint8>(MontageData.X);
					State.bLoop2 = false;
					State.StartFrame2 = static_cast<uint16>(MontageData.Z);
					State.EndFrame2 = static_cast<uint16>(MontageData.W);
					State.CurrentFrame2 = State.StartFrame2;
					{
						float TargetDur = (Appear.bCanPlayAnim && Appear.bAnimAsDuration) ? MontageData.Y : Appear.Duration;
						State.PlayRate2 = CalculatePlayRate(State.StartFrame2, State.EndFrame2, TargetDur, Animating.SampleRate);
					}
					break;
				case EAnimState::BeingHit:
					MontageData = GetAnimDataFromSlotIndex(Animation.AnimData.HitAnimData, Animating.SelectedHitAnimIndex);
					State.Index2 = static_cast<uint8>(MontageData.X);
					State.bLoop2 = false;
					State.StartFrame2 = static_cast<uint16>(MontageData.Z);
					State.EndFrame2 = static_cast<uint16>(MontageData.W);
					State.CurrentFrame2 = State.StartFrame2;
					{
						float TargetDur = (Hit.bPlayAnim && Hit.bAnimAsDuration) ? MontageData.Y : Hit.AnimLength;
						State.PlayRate2 = CalculatePlayRate(State.StartFrame2, State.EndFrame2, TargetDur, Animating.SampleRate);
					}
					break;
				case EAnimState::Attacking:
					MontageData = GetAnimDataFromSlotIndex(Animation.AnimData.AttackAnimData, Animating.SelectedAttackAnimIndex);
					State.Index2 = static_cast<uint8>(MontageData.X);
					State.bLoop2 = false;
					State.StartFrame2 = static_cast<uint16>(MontageData.Z);
					State.EndFrame2 = static_cast<uint16>(MontageData.W);
					State.CurrentFrame2 = State.StartFrame2;
					{
						float TargetDur = (Attack.bCanPlayAnim && Attack.bAnimAsDuration) ? MontageData.Y : Attack.DurationPerRound;
						State.PlayRate2 = CalculatePlayRate(State.StartFrame2, State.EndFrame2, TargetDur, Animating.SampleRate);
						State.PlayRate2 = State.PlayRate2 * AttackSpeedMultiplier;
					}
					break;
				case EAnimState::Dying:
					MontageData = GetAnimDataFromSlotIndex(Animation.AnimData.DeathAnimData, Animating.SelectedDeathAnimIndex);
					State.Index2 = static_cast<uint8>(MontageData.X);
					State.bLoop2 = false;
					State.StartFrame2 = static_cast<uint16>(MontageData.Z);
					State.EndFrame2 = static_cast<uint16>(MontageData.W);
					State.CurrentFrame2 = State.StartFrame2;
					{
						float TargetDur = (Death.bCanPlayAnim && Death.bAnimAsDuration) ? MontageData.Y : Death.AnimLength;
						State.PlayRate2 = CalculatePlayRate(State.StartFrame2, State.EndFrame2, TargetDur, Animating.SampleRate);
					}
					break;
				default: break;
			}
			State.Lerp1 = 0;
		}
		else if (State.bBaseLayerIsMontage)
		{
			State.bBaseLayerIsMontage = false;
			State.Lerp0 = AnimationHelpers::FloatToNormalizedUint8(TargetLerp0);
		}
	}

	if (State.Index0 != IdleData.X)
	{
		State.Index0 = static_cast<uint8>(IdleData.X);
		State.StartFrame0 = static_cast<uint16>(IdleData.Z);
		State.EndFrame0 = static_cast<uint16>(IdleData.W);
		State.CurrentFrame0 = Animating.IdleRandomStartFrame;
		bOutReset0 = true;
	}
	State.bLoop0 = true;
	State.PlayRate0 = CalculatePlayRate(State.StartFrame0, State.EndFrame0, IdleData.Y, Animating.SampleRate);

	if (!State.bBaseLayerIsMontage)
	{
		if (State.Index1 != MoveData.X)
		{
			State.Index1 = static_cast<uint8>(MoveData.X);
			State.StartFrame1 = static_cast<uint16>(MoveData.Z);
			State.EndFrame1 = static_cast<uint16>(MoveData.W);
			State.CurrentFrame1 = Animating.MoveRandomStartFrame;
			bOutReset1 = true;
		}
		State.bLoop1 = true;
		State.PlayRate1 = CalculatePlayRate(State.StartFrame1, State.EndFrame1, MoveData.Y, Animating.SampleRate);
		if (bUseSingleAnimScaling) { State.PlayRate0 = State.PlayRate1; }
	}

	if (bIsFirstUpdate)
	{
		State.Lerp0 = !State.bBaseLayerIsMontage ? AnimationHelpers::FloatToNormalizedUint8(TargetLerp0) : 255;
		State.Lerp1 = bIsMontageState ? 255 : 0;
	}
	else
	{
		State.Lerp0 = !State.bBaseLayerIsMontage ? AnimationHelpers::FloatToNormalizedUint8(FMath::FInterpTo(AnimationHelpers::NormalizedUint8ToFloat(State.Lerp0), TargetLerp0, DeltaTime, Animation.AnimBlendSpeed)) : 255;
		State.Lerp1 = AnimationHelpers::FloatToNormalizedUint8(FMath::FInterpTo(AnimationHelpers::NormalizedUint8ToFloat(State.Lerp1), bIsMontageState ? 1.0f : 0.0f, DeltaTime, Animation.AnimBlendSpeed * 2.0f));
	}

	// Same anim: use smoothed alpha as playrate multiplier (0 = stopped, 1 = full rate)
	if (bUseSingleAnimScaling && !State.bBaseLayerIsMontage)
	{
		float Alpha = AnimationHelpers::NormalizedUint8ToFloat(State.Lerp0);
		float BaseRate = State.PlayRate1;
		State.PlayRate0 = BaseRate * Alpha;
		State.PlayRate1 = BaseRate * Alpha;
		State.CurrentFrame0 = State.CurrentFrame1;
	}
}

void UMassBattleFogAgentRenderProcessor::AdvanceFrame(FPerLevelAnimState& State, int32 SlotIndex, float DeltaTime, int32 SampleRate, const FAnimShared& Animation, float MoveDirectionMultiplier, bool bSkipAdvance)
{
	if (bSkipAdvance) return;
	float* CurrentFramePtr;
	float* PlayRatePtr;
	uint8* IndexPtr;
	bool* LoopPtr;
	uint16* StartFramePtr;
	uint16* EndFramePtr;

	if (SlotIndex == 0)
	{
		CurrentFramePtr = &State.CurrentFrame0; PlayRatePtr = &State.PlayRate0; IndexPtr = &State.Index0; LoopPtr = &State.bLoop0;
		StartFramePtr = &State.StartFrame0; EndFramePtr = &State.EndFrame0;
	}
	else if (SlotIndex == 1)
	{
		CurrentFramePtr = &State.CurrentFrame1; PlayRatePtr = &State.PlayRate1; IndexPtr = &State.Index1; LoopPtr = &State.bLoop1;
		StartFramePtr = &State.StartFrame1; EndFramePtr = &State.EndFrame1;
	}
	else
	{
		CurrentFramePtr = &State.CurrentFrame2; PlayRatePtr = &State.PlayRate2; IndexPtr = &State.Index2; LoopPtr = &State.bLoop2;
		StartFramePtr = &State.StartFrame2; EndFramePtr = &State.EndFrame2;
	}

	uint8 Index = *IndexPtr;
	if (Index == AnimationHelpers::INVALID_ANIM_INDEX) return;

	// Convert to float for arithmetic
	float currentFrame = static_cast<float>(*CurrentFramePtr);
	float playRate = static_cast<float>(*PlayRatePtr);
	float startFrame = static_cast<float>(*StartFramePtr);
	float endFrame = static_cast<float>(*EndFramePtr);

	float effectivePlayRate = playRate;

	bool bIsMoveAnim = false;
	const FAnimData& MoveData = Animation.AnimData.MoveAnimData;
	if ((int32)MoveData.Anim0.X == Index) bIsMoveAnim = true;
	else if ((int32)MoveData.Anim1.X == Index) bIsMoveAnim = true;
	else if ((int32)MoveData.Anim2.X == Index) bIsMoveAnim = true;
	else if ((int32)MoveData.Anim3.X == Index) bIsMoveAnim = true;
	else if ((int32)MoveData.Anim4.X == Index) bIsMoveAnim = true;

	if (bIsMoveAnim) effectivePlayRate *= MoveDirectionMultiplier;

	// Perform frame advancement
	currentFrame += DeltaTime * effectivePlayRate * SampleRate;

	if (*LoopPtr)
	{
		const float duration = endFrame - startFrame;
		if (duration > 0.f)
		{
			float relativeFrame = FMath::Fmod(currentFrame - startFrame, duration);
			if (relativeFrame < 0.f) relativeFrame += duration;
			currentFrame = startFrame + relativeFrame;
		}
	}
	else
	{
		currentFrame = FMath::Clamp(currentFrame, startFrame, endFrame);
	}

	// Write back (clamped to valid frame range)
	*CurrentFramePtr = FMath::Clamp(currentFrame, 0.0f, 65535.0f);
}
