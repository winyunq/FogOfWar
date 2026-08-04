// Copyright Winyunq, 2025. All Rights Reserved.
// Commercial extension: see COMMERCIAL_FEATURE_LICENSE.md.

#include "Subsystems/MassBattleFogRenderSubsystem.h"

#include "FogOfWarModule.h"
#include "RTSDiplomacyTypes.h"
#include "Subsystems/MassBattleISKMWorldSubsystem.h"
#include "Subsystems/RTSDiplomacySubsystem.h"

namespace
{
	void BuildFriendlyTeamMask(
		const UWorld* World,
		const int32 ViewingTeamIndex,
		const TArray<int32>& ExplicitAlliedTeamIndices,
		TArray<uint32>& OutMaskWords,
		uint32& OutDiplomacyRevision,
		int32& OutFriendlyTeamCount)
	{
		OutMaskWords.Init(0u, UMassBattleFogRenderSubsystem::TeamWordCount);
		OutDiplomacyRevision = 0;
		OutFriendlyTeamCount = 0;

		auto AddFriendlyTeam = [&OutMaskWords, &OutFriendlyTeamCount](const int32 TeamIndex)
		{
			if (TeamIndex < 0 || TeamIndex >= UMassBattleFogRenderSubsystem::TeamCount)
			{
				return;
			}

			const uint32 Team = static_cast<uint32>(TeamIndex);
			uint32& Word = OutMaskWords[Team >> 5u];
			const uint32 Bit = 1u << (Team & 31u);
			if ((Word & Bit) == 0u)
			{
				Word |= Bit;
				++OutFriendlyTeamCount;
			}
		};

		AddFriendlyTeam(ViewingTeamIndex);
		for (const int32 AlliedTeamIndex : ExplicitAlliedTeamIndices)
		{
			AddFriendlyTeam(AlliedTeamIndex);
		}

		const URTSDiplomacySubsystem* Diplomacy = World
			? World->GetSubsystem<URTSDiplomacySubsystem>()
			: nullptr;
		const TSharedPtr<const FRTSDiplomacySnapshot, ESPMode::ThreadSafe> Snapshot = Diplomacy
			? Diplomacy->GetSnapshot()
			: nullptr;
		if (!Snapshot.IsValid())
		{
			return;
		}

		OutDiplomacyRevision = Snapshot->Revision;
		const int32 TeamLimit = Snapshot->DefaultDifferentTeamRelation == ERTSTeamRelation::Allied
			? UMassBattleFogRenderSubsystem::TeamCount
			: FMath::Min(Snapshot->Stride, UMassBattleFogRenderSubsystem::TeamCount);
		for (int32 CandidateTeamIndex = 0; CandidateTeamIndex < TeamLimit; ++CandidateTeamIndex)
		{
			if (Snapshot->GetRelation(ViewingTeamIndex, CandidateTeamIndex) == ERTSTeamRelation::Allied)
			{
				AddFriendlyTeam(CandidateTeamIndex);
			}
		}
	}
}

void UMassBattleFogRenderSubsystem::Configure(
	const bool bInSceneActive,
	const bool bInDebugRevealAll,
	const int32 InViewingTeamIndex,
	const TArray<int32>& InAlliedTeamIndices,
	const float InVisionRadiusUU,
	const float InFogOpacity,
	const FVector2D& InMaskWorldMin,
	const FVector2D& InMaskCellSize,
	const FIntPoint& InMaskDimensions,
	const FVector2D& InRenderCullWorldMin,
	const FVector2D& InRenderCullWorldMax,
	const float InAttackRevealDuration,
	const float InUnitVisibilityConvergenceRateHz,
	const float InUnitVisibilityRemovalDelay)
{
	const bool bWasActive = bActive;
	const bool bRenderModeChanged = bActive != bInSceneActive
		|| bDebugRevealAll != bInDebugRevealAll;
	const float OldVisionRadiusUU = VisionRadiusUU;
	bActive = bInSceneActive;
	bDebugRevealAll = bInDebugRevealAll;
	if (UMassBattleISKMWorldSubsystem* ISKM = GetWorld()
		? GetWorld()->GetSubsystem<UMassBattleISKMWorldSubsystem>()
		: nullptr)
	{
		// ISKM is a separate presentation backend. Fog supplies only the final
		// visible camera-local work set while the scene controller is active.
		ISKM->SetExternalVisibilityFilterEnabled(bActive);
	}
	VisionRadiusUU = FMath::Max(0.0f, InVisionRadiusUU);
	FogOpacity = FMath::Clamp(InFogOpacity, 0.0f, 1.0f);
	AttackRevealDuration = FMath::Max(0.0f, InAttackRevealDuration);
	UnitVisibilityConvergenceRateHz = FMath::Max(0.0f, InUnitVisibilityConvergenceRateHz);
	UnitVisibilityRemovalDelay = FMath::Max(0.0f, InUnitVisibilityRemovalDelay);

	TArray<uint32> NewFriendlyTeamMaskWords;
	uint32 NewDiplomacyRevision = 0;
	int32 NewFriendlyTeamCount = 0;
	BuildFriendlyTeamMask(
		GetWorld(),
		InViewingTeamIndex,
		InAlliedTeamIndices,
		NewFriendlyTeamMaskWords,
		NewDiplomacyRevision,
		NewFriendlyTeamCount);

	const bool bTeamMaskChanged = FriendlyTeamMaskWords != NewFriendlyTeamMaskWords;
	FriendlyTeamMaskWords = MoveTemp(NewFriendlyTeamMaskWords);
	DiplomacyRevision = NewDiplomacyRevision;
	FriendlyTeamCount = NewFriendlyTeamCount;
	if (bTeamMaskChanged)
	{
		UE_LOG(LogFogOfWar, Log,
			TEXT("Resolved fog vision teams: viewing team=%d friendly teams=%d diplomacy revision=%u."),
			InViewingTeamIndex,
			FriendlyTeamCount,
			DiplomacyRevision);
	}
	const bool bVisionDefinitionChanged = bTeamMaskChanged
		|| !FMath::IsNearlyEqual(OldVisionRadiusUU, VisionRadiusUU);
	if ((!bWasActive && bActive) || bVisionDefinitionChanged)
	{
		bMaskReady = false;
		bVisionSourceSampleRequested = true;
		bVisionSourceCollectionRequested = true;
		bMinimapSnapshotCollectionRequested = true;
	}

	const FVector2D RequestedCellSize(
		FMath::Max(1.0, InMaskCellSize.X),
		FMath::Max(1.0, InMaskCellSize.Y));
	const FIntPoint RequestedDimensions(
		FMath::Max(1, InMaskDimensions.X),
		FMath::Max(1, InMaskDimensions.Y));
	const FVector2D NewVisionCollectionWorldMin = InMaskWorldMin;
	const FVector2D NewVisionCollectionWorldMax = InMaskWorldMin + FVector2D(
		RequestedDimensions.X * RequestedCellSize.X,
		RequestedDimensions.Y * RequestedCellSize.Y);
	const bool bVisionCollectionWindowChanged = NewVisionCollectionWorldMin != VisionCollectionWorldMin
		|| NewVisionCollectionWorldMax != VisionCollectionWorldMax;
	const bool bRenderCullWindowChanged = InRenderCullWorldMin != RenderCullWorldMin
		|| InRenderCullWorldMax != RenderCullWorldMax;
	VisionCollectionWorldMin = InMaskWorldMin;
	VisionCollectionWorldMax = NewVisionCollectionWorldMax;
	RenderCullWorldMin = InRenderCullWorldMin;
	RenderCullWorldMax = InRenderCullWorldMax;
	if ((!bWasActive && bActive) || bVisionDefinitionChanged || bVisionCollectionWindowChanged)
	{
		++VisionCollectionRevision;
		if (VisionCollectionRevision == 0)
		{
			++VisionCollectionRevision;
		}
		bVisionSourceSampleRequested = true;
		bVisionSourceCollectionRequested = true;
	}
	if (bRenderModeChanged || bTeamMaskChanged || bVisionDefinitionChanged
		|| bVisionCollectionWindowChanged || bRenderCullWindowChanged)
	{
		++RenderWorkSetRevision;
		if (RenderWorkSetRevision == 0)
		{
			++RenderWorkSetRevision;
		}
	}

	// Requested render layout is deliberately not installed here. The CPU keeps
	// using the last completed asynchronous GPU generation while a camera-local
	// window is being redrawn. UpdateGpuVisibilityStates atomically publishes the
	// new bytes and their matching layout.

	if (!bActive)
	{
		AttackReveals.Reset();
		LatestVisionSources.Reset();
		LatestMinimapUnits.Reset();
		LatestMinimapFogVisibleUnits.Reset();
		LatestVisionCollectionRevision = 0;
		LatestVisionSourceWorldTimeSeconds = 0.0;
		bVisionSourceSampleRequested = false;
		bVisionSourceCollectionRequested = false;
		VisibilityStateCells.Reset();
		bMaskReady = false;
	}
}

void UMassBattleFogRenderSubsystem::UpdateGpuVisibilityStates(
	TArray<uint8>&& InVisibilityStates,
	const FIntPoint& InDimensions,
	const FVector2D& InWorldMin,
	const FVector2D& InCellSize)
{
	const int64 ExpectedCellCount = static_cast<int64>(InDimensions.X) * static_cast<int64>(InDimensions.Y);
	if (InDimensions.X <= 0
		|| InDimensions.Y <= 0
		|| ExpectedCellCount != InVisibilityStates.Num())
	{
		return;
	}

	MaskDimensions = InDimensions;
	MaskWorldMin = InWorldMin;
	MaskCellSize = FVector2D(FMath::Max(1.0, InCellSize.X), FMath::Max(1.0, InCellSize.Y));
	VisibilityStateCells = MoveTemp(InVisibilityStates);
	bMaskReady = true;
	++MaskGeneration;
	++RenderWorkSetRevision;
	if (RenderWorkSetRevision == 0)
	{
		++RenderWorkSetRevision;
	}
}

uint8 UMassBattleFogRenderSubsystem::GetUnitVisibilityState(
	const int32 TeamIndex,
	const FVector& WorldLocation,
	const bool bAttackRevealed) const
{
	if (!bActive)
	{
		return 3u;
	}
	if (!IsInsideRequestedCameraWindow(WorldLocation))
	{
		return 0u;
	}
	if (bDebugRevealAll || IsFriendlyTeam(TeamIndex))
	{
		return 3u;
	}

	uint8 State = 0u;
	if (bMaskReady)
	{
		const int32 CellIndex = WorldToMaskIndex(WorldLocation);
		// During a camera-local layout handoff, retain an out-of-old-map unit for
		// one generation without making it visible.
		State = VisibilityStateCells.IsValidIndex(CellIndex)
			? FMath::Min<uint8>(VisibilityStateCells[CellIndex], 3u)
			: 1u;
	}

	// Attack exposure and authored permanent visibility reveal only this unit.
	// They never write the terrain mask and never upgrade true-vision state.
	if (bAttackRevealed)
	{
		State = FMath::Max<uint8>(State, 2u);
	}
	return State;
}

bool UMassBattleFogRenderSubsystem::IsInsideRenderWindow(const FVector& WorldLocation) const
{
	return !bActive || IsInsideRequestedCameraWindow(WorldLocation);
}

bool UMassBattleFogRenderSubsystem::IsFriendlyTeam(const int32 TeamIndex) const
{
	if (TeamIndex < 0 || TeamIndex >= TeamCount || FriendlyTeamMaskWords.Num() != TeamWordCount)
	{
		return false;
	}
	const uint32 UnsignedTeam = static_cast<uint32>(TeamIndex);
	return (FriendlyTeamMaskWords[UnsignedTeam >> 5u] & (1u << (UnsignedTeam & 31u))) != 0u;
}

void UMassBattleFogRenderSubsystem::ConfigureStandaloneMinimapTeams(
	const int32 InViewingTeamIndex,
	const TArray<int32>& InAlliedTeamIndices)
{
	if (bActive)
	{
		return;
	}

	TArray<uint32> NewFriendlyTeamMaskWords;
	uint32 NewDiplomacyRevision = 0;
	int32 NewFriendlyTeamCount = 0;
	BuildFriendlyTeamMask(
		GetWorld(),
		InViewingTeamIndex,
		InAlliedTeamIndices,
		NewFriendlyTeamMaskWords,
		NewDiplomacyRevision,
		NewFriendlyTeamCount);

	if (FriendlyTeamMaskWords != NewFriendlyTeamMaskWords)
	{
		FriendlyTeamMaskWords = MoveTemp(NewFriendlyTeamMaskWords);
		bMinimapSnapshotCollectionRequested = true;
		UE_LOG(LogFogOfWar, Log,
			TEXT("Resolved standalone minimap vision teams: viewing team=%d friendly teams=%d diplomacy revision=%u."),
			InViewingTeamIndex,
			NewFriendlyTeamCount,
			NewDiplomacyRevision);
	}
	DiplomacyRevision = NewDiplomacyRevision;
	FriendlyTeamCount = NewFriendlyTeamCount;
}

void UMassBattleFogRenderSubsystem::RequestVisionSourceCollection()
{
	bVisionSourceSampleRequested = true;
	bVisionSourceCollectionRequested = true;
}

bool UMassBattleFogRenderSubsystem::ConsumeVisionSourceSampleRequest(uint32& OutCollectionRevision)
{
	const bool bWasRequested = bVisionSourceSampleRequested;
	bVisionSourceSampleRequested = false;
	OutCollectionRevision = bWasRequested ? VisionCollectionRevision : 0;
	return bWasRequested;
}

bool UMassBattleFogRenderSubsystem::ConsumeVisionSourceCollectionRequest(uint32& OutCollectionRevision)
{
	const bool bWasRequested = bVisionSourceCollectionRequested;
	bVisionSourceCollectionRequested = false;
	OutCollectionRevision = bWasRequested ? VisionCollectionRevision : 0;
	return bWasRequested;
}

bool UMassBattleFogRenderSubsystem::ShouldCollectVisionSource(const FVector& WorldLocation) const
{
	const double Radius = FMath::Max(0.0f, VisionRadiusUU);
	return WorldLocation.X + Radius >= VisionCollectionWorldMin.X
		&& WorldLocation.Y + Radius >= VisionCollectionWorldMin.Y
		&& WorldLocation.X - Radius <= VisionCollectionWorldMax.X
		&& WorldLocation.Y - Radius <= VisionCollectionWorldMax.Y;
}

void UMassBattleFogRenderSubsystem::PublishVisionSources(
	TArray<FVector4f>&& InSources,
	const uint32 CollectionRevision,
	const double SourceWorldTimeSeconds)
{
	LatestVisionSources = MoveTemp(InSources);
	LatestVisionCollectionRevision = CollectionRevision;
	LatestVisionSourceWorldTimeSeconds = SourceWorldTimeSeconds;
	++VisionSourceGeneration;
}

uint32 UMassBattleFogRenderSubsystem::CopyLatestVisionSources(
	TArray<FVector4f>& OutSources,
	uint32& OutCollectionRevision,
	double& OutSourceWorldTimeSeconds) const
{
	OutSources = LatestVisionSources;
	OutCollectionRevision = LatestVisionCollectionRevision;
	OutSourceWorldTimeSeconds = LatestVisionSourceWorldTimeSeconds;
	return VisionSourceGeneration;
}

void UMassBattleFogRenderSubsystem::RequestMinimapSnapshotCollection()
{
	bMinimapSnapshotCollectionRequested = true;
}

bool UMassBattleFogRenderSubsystem::ConsumeMinimapSnapshotCollectionRequest()
{
	const bool bWasRequested = bMinimapSnapshotCollectionRequested;
	bMinimapSnapshotCollectionRequested = false;
	return bWasRequested;
}

void UMassBattleFogRenderSubsystem::PublishMinimapSnapshot(
	TArray<FVector4f>&& InVisionSourceUnits,
	TArray<FVector4f>&& InFriendlyNonVisionUnits,
	TArray<FVector4f>&& InOtherUnits,
	TArray<FVector4f>&& InFogVisibleUnits)
{
	LatestMinimapFriendlySourceCount = InVisionSourceUnits.Num();
	InVisionSourceUnits.Append(MoveTemp(InFriendlyNonVisionUnits));
	InVisionSourceUnits.Append(MoveTemp(InOtherUnits));
	LatestMinimapUnits = MoveTemp(InVisionSourceUnits);
	LatestMinimapFogVisibleUnits = MoveTemp(InFogVisibleUnits);
	++MinimapSnapshotGeneration;
}

uint32 UMassBattleFogRenderSubsystem::CopyLatestMinimapSnapshot(
	TArray<FVector4f>& OutUnits,
	int32& OutFriendlySourceCount,
	TArray<FVector4f>& OutFogVisibleUnits) const
{
	OutUnits = LatestMinimapUnits;
	OutFriendlySourceCount = LatestMinimapFriendlySourceCount;
	OutFogVisibleUnits = LatestMinimapFogVisibleUnits;
	return MinimapSnapshotGeneration;
}

void UMassBattleFogRenderSubsystem::NotifyAttackReveals(
	const TConstArrayView<FMassBattleFogAttackRevealObservation> Observations,
	const double WorldTimeSeconds)
{
	if (!bActive || AttackRevealDuration <= 0.0f || Observations.IsEmpty())
	{
		return;
	}
	const double ExpiresAtWorldTime = WorldTimeSeconds + AttackRevealDuration;
	for (const FMassBattleFogAttackRevealObservation& Observation : Observations)
	{
		FAttackRevealState& State = AttackReveals.FindOrAdd(Observation.Entity);
		State.WorldLocation = Observation.WorldLocation;
		State.TeamIndex = Observation.TeamIndex;
		State.ExpiresAtWorldTime = FMath::Max(State.ExpiresAtWorldTime, ExpiresAtWorldTime);
	}
}

void UMassBattleFogRenderSubsystem::PruneExpiredAttackReveals(const double WorldTimeSeconds)
{
	for (auto It = AttackReveals.CreateIterator(); It; ++It)
	{
		if (It.Value().ExpiresAtWorldTime < WorldTimeSeconds)
		{
			It.RemoveCurrent();
		}
	}
}

bool UMassBattleFogRenderSubsystem::IsAttackRevealActive(const FMassEntityHandle& Entity) const
{
	return AttackReveals.Contains(Entity);
}

void UMassBattleFogRenderSubsystem::CollectAttackRevealMarkers(
	const double WorldTimeSeconds,
	TArray<FVector4f>& OutMarkers)
{
	OutMarkers.Reset();
	for (auto It = AttackReveals.CreateIterator(); It; ++It)
	{
		if (It.Value().ExpiresAtWorldTime < WorldTimeSeconds)
		{
			It.RemoveCurrent();
			continue;
		}
		const FAttackRevealState& State = It.Value();
		const uint32 PackedTeam = static_cast<uint32>(FMath::Clamp(State.TeamIndex, 0, TeamCount - 1));
		float PackedTeamAsFloat = 0.0f;
		FMemory::Memcpy(&PackedTeamAsFloat, &PackedTeam, sizeof(PackedTeamAsFloat));
		OutMarkers.Add(FVector4f(FVector3f(State.WorldLocation), PackedTeamAsFloat));
	}
}

int32 UMassBattleFogRenderSubsystem::WorldToMaskIndex(const FVector& WorldLocation) const
{
	const int32 X = FMath::FloorToInt((WorldLocation.X - MaskWorldMin.X) / MaskCellSize.X);
	const int32 Y = FMath::FloorToInt((WorldLocation.Y - MaskWorldMin.Y) / MaskCellSize.Y);
	if (X < 0 || Y < 0 || X >= MaskDimensions.X || Y >= MaskDimensions.Y)
	{
		return INDEX_NONE;
	}
	return Y * MaskDimensions.X + X;
}

bool UMassBattleFogRenderSubsystem::IsInsideRequestedCameraWindow(const FVector& WorldLocation) const
{
	return WorldLocation.X >= RenderCullWorldMin.X
		&& WorldLocation.Y >= RenderCullWorldMin.Y
		&& WorldLocation.X <= RenderCullWorldMax.X
		&& WorldLocation.Y <= RenderCullWorldMax.Y;
}
