// Copyright Winyunq, 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Mass/EntityHandle.h"
#include "Subsystems/WorldSubsystem.h"
#include "MassBattleFogRenderSubsystem.generated.h"

struct FMassBattleFogAttackRevealObservation
{
	FMassEntityHandle Entity;
	FVector WorldLocation = FVector::ZeroVector;
	int32 TeamIndex = 0;
};

/**
 * CPU mirror of the coarse GPU fog mask used by the replacement MBF renderer.
 *
 * This is not a unit cache and never queries or walks Mass entities. The GPU
 * returns one byte per HashGrid-sized XY cell. The replacement render
 * processor performs one O(1) lookup while it is already visiting that unit.
 */
UCLASS()
class FOGOFWAR_API UMassBattleFogRenderSubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static constexpr int32 TeamCount = 1024;
	static constexpr int32 TeamWordCount = TeamCount / 32;

	void Configure(
		bool bInSceneActive,
		bool bInDebugRevealAll,
		int32 InViewingTeamIndex,
		const TArray<int32>& InAlliedTeamIndices,
		float InVisionRadiusUU,
		float InFogOpacity,
		const FVector2D& InMaskWorldMin,
		const FVector2D& InMaskCellSize,
		const FIntPoint& InMaskDimensions,
		const FVector2D& InRenderCullWorldMin,
		const FVector2D& InRenderCullWorldMax,
		float InAttackRevealDuration,
		float InUnitVisibilityConvergenceRateHz,
		float InUnitVisibilityRemovalDelay);

	/** Accepts one R8 map: 0 hidden, 1 retained, 2 fog-visible, 3 clear-visible. */
	void UpdateGpuVisibilityStates(
		TArray<uint8>&& InVisibilityStates,
		const FIntPoint& InDimensions,
		const FVector2D& InWorldMin,
		const FVector2D& InCellSize);

	/**
	 * Single O(1) lookup used by the replacement renderer. Returns
	 * 0=deep hidden, 1=retained only, 2=fog-visible, 3=true vision.
	 */
	uint8 GetUnitVisibilityState(
		int32 TeamIndex,
		const FVector& WorldLocation,
		bool bAttackRevealed) const;

	/** Cheap camera-window bounds test for last-known building snapshots. */
	bool IsInsideRenderWindow(const FVector& WorldLocation) const;

	bool IsFriendlyTeam(int32 TeamIndex) const;
	/** Used only when no active scene-fog controller owns the relationship. */
	void ConfigureStandaloneMinimapTeams(int32 InViewingTeamIndex, const TArray<int32>& InAlliedTeamIndices);

	/**
	 * The scene controller requests a source snapshot at its own update rate.
	 * Source membership is rebuilt with the low-frequency camera/logic filter;
	 * its compact position/velocity set is sampled independently at scene rate.
	 */
	void RequestVisionSourceCollection();
	/** Consumed at the scene cadence; does not consume the low-frequency membership refresh. */
	bool ConsumeVisionSourceSampleRequest(uint32& OutCollectionRevision);
	bool ConsumeVisionSourceCollectionRequest(uint32& OutCollectionRevision);
	bool ShouldCollectVisionSource(const FVector& WorldLocation) const;
	/**
	 * Each packed source is XY position + XY velocity. The renderer extrapolates
	 * this 24 Hz snapshot on the GPU, so the scene edge moves every render frame.
	 */
	void PublishVisionSources(
		TArray<FVector4f>&& InSources,
		uint32 CollectionRevision,
		double SourceWorldTimeSeconds);
	uint32 CopyLatestVisionSources(
		TArray<FVector4f>& OutSources,
		uint32& OutCollectionRevision,
		double& OutSourceWorldTimeSeconds) const;
	uint32 GetVisionCollectionRevision() const { return VisionCollectionRevision; }

	/**
	 * The minimap requests one narrow all-world read-only snapshot at 3 Hz, with
	 * actual friendly/allied vision providers stored as a prefix,
	 * so the widget never reads scene-culled Niagara batches or queries Mass.
	 */
	void RequestMinimapSnapshotCollection();
	bool ConsumeMinimapSnapshotCollectionRequest();
	void PublishMinimapSnapshot(
		TArray<FVector4f>&& InVisionSourceUnits,
		TArray<FVector4f>&& InFriendlyNonVisionUnits,
		TArray<FVector4f>&& InOtherUnits,
		TArray<FVector4f>&& InFogVisibleUnits);
	uint32 CopyLatestMinimapSnapshot(
		TArray<FVector4f>& OutUnits,
		int32& OutFriendlySourceCount,
		TArray<FVector4f>& OutFogVisibleUnits) const;

	/** One lock/batch for attackers gathered by all parallel render chunks. */
	void NotifyAttackReveals(TConstArrayView<FMassBattleFogAttackRevealObservation> Observations, double WorldTimeSeconds);
	void PruneExpiredAttackReveals(double WorldTimeSeconds);
	bool IsAttackRevealActive(const FMassEntityHandle& Entity) const;

	/**
	 * Iterates active attack events, never the unit population. Returned samples
	 * are XYZ plus bit-cast team id in W for one-unit minimap markers only.
	 */
	void CollectAttackRevealMarkers(double WorldTimeSeconds, TArray<FVector4f>& OutMarkers);

	/** Latest replacement-renderer workload, published once per render frame. */
	void PublishAgentRenderWorkload(int32 InActiveProxyCount, int32 InUploadedElementCount)
	{
		ActiveProxyCount = FMath::Max(0, InActiveProxyCount);
		UploadedElementCount = FMath::Max(0, InUploadedElementCount);
	}
	int32 GetActiveProxyCount() const { return ActiveProxyCount; }
	int32 GetUploadedElementCount() const { return UploadedElementCount; }

	const TArray<uint32>& GetFriendlyTeamMaskWords() const { return FriendlyTeamMaskWords; }
	bool IsFilteringActive() const { return bActive; }
	bool IsConfiguredActive() const { return bActive; }
	float GetVisionRadiusUU() const { return VisionRadiusUU; }
	/** Shared scene/minimap presentation strength while the scene controller is active. */
	float GetFogOpacity() const { return FogOpacity; }
	float GetUnitVisibilityConvergenceRateHz() const { return UnitVisibilityConvergenceRateHz; }
	float GetUnitVisibilityRemovalDelay() const { return UnitVisibilityRemovalDelay; }
	uint32 GetDiplomacyRevision() const { return DiplomacyRevision; }
	int32 GetFriendlyTeamCount() const { return FriendlyTeamCount; }
	bool IsMaskReady() const { return bMaskReady; }
	uint32 GetMaskGeneration() const { return MaskGeneration; }
	/** Changes whenever camera/fog/team state can change ActiveProxyIds. */
	uint32 GetRenderWorkSetRevision() const { return RenderWorkSetRevision; }
	FIntPoint GetMaskDimensions() const { return MaskDimensions; }
	FVector2D GetMaskWorldMin() const { return MaskWorldMin; }
	FVector2D GetMaskCellSize() const { return MaskCellSize; }
	FVector2D GetRenderCullWorldMin() const { return RenderCullWorldMin; }
	FVector2D GetRenderCullWorldMax() const { return RenderCullWorldMax; }
	FVector2D GetVisionCollectionWorldMin() const { return VisionCollectionWorldMin; }
	FVector2D GetVisionCollectionWorldMax() const { return VisionCollectionWorldMax; }

private:
	struct FAttackRevealState
	{
		FVector WorldLocation = FVector::ZeroVector;
		int32 TeamIndex = 0;
		double ExpiresAtWorldTime = 0.0;
	};

	int32 WorldToMaskIndex(const FVector& WorldLocation) const;
	bool IsInsideRequestedCameraWindow(const FVector& WorldLocation) const;

	TArray<uint8> VisibilityStateCells;
	TArray<uint32> FriendlyTeamMaskWords;
	TArray<FVector4f> LatestVisionSources;
	TArray<FVector4f> LatestMinimapUnits;
	TArray<FVector4f> LatestMinimapFogVisibleUnits;
	TMap<FMassEntityHandle, FAttackRevealState> AttackReveals;

	FVector2D MaskWorldMin = FVector2D::ZeroVector;
	FVector2D MaskCellSize = FVector2D(300.0, 300.0);
	FIntPoint MaskDimensions = FIntPoint::ZeroValue;
	FVector2D VisionCollectionWorldMin = FVector2D(-HALF_WORLD_MAX, -HALF_WORLD_MAX);
	FVector2D VisionCollectionWorldMax = FVector2D(HALF_WORLD_MAX, HALF_WORLD_MAX);
	FVector2D RenderCullWorldMin = FVector2D(-HALF_WORLD_MAX, -HALF_WORLD_MAX);
	FVector2D RenderCullWorldMax = FVector2D(HALF_WORLD_MAX, HALF_WORLD_MAX);
	float VisionRadiusUU = 1024.0f;
	float FogOpacity = 0.3f;
	float AttackRevealDuration = 2.0f;
	float UnitVisibilityConvergenceRateHz = 3.0f;
	float UnitVisibilityRemovalDelay = 0.333333f;
	uint32 DiplomacyRevision = 0;
	int32 FriendlyTeamCount = 0;
	uint32 MaskGeneration = 0;
	uint32 RenderWorkSetRevision = 1;
	uint32 VisionSourceGeneration = 0;
	uint32 VisionCollectionRevision = 1;
	uint32 LatestVisionCollectionRevision = 0;
	double LatestVisionSourceWorldTimeSeconds = 0.0;
	uint32 MinimapSnapshotGeneration = 0;
	int32 LatestMinimapFriendlySourceCount = 0;
	int32 ActiveProxyCount = 0;
	int32 UploadedElementCount = 0;
	bool bVisionSourceSampleRequested = false;
	bool bVisionSourceCollectionRequested = false;
	bool bMinimapSnapshotCollectionRequested = false;
	bool bActive = false;
	bool bDebugRevealAll = false;
	bool bMaskReady = false;
};
