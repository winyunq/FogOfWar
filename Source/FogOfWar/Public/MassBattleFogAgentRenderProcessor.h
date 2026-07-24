/*
* MassBattle renderer replacement owned by FogOfWar.
*
* Upstream source snapshot:
*   MassBattle/Private/Processors/MassBattleAgentRenderProcessor.cpp
*   SHA-256 E6250964FCF0E271BF8786D18AA9944C3146AE9761B8C87B055DFC1A8CFC1278
*   MassBattle/Public/Processors/MassBattleAgentRenderProcessor.h
*   SHA-256 1CA98E53574062A0FEB1F52EBFBC80AAE69C11F0411E85425BDBD0FFB5ECA92F
*
* Keep this snapshot mechanically synchronized when MassBattleFrame updates.
* Fog-specific edits are marked FOG-OF-WAR INSERTION below.
*/

#pragma once

#include "CoreMinimal.h"
#include "Processors/MassBattleAgentRenderProcessor.h"
#include "MassAPIEnums.h"
#include "MassBattleEnums.h"
#include "MassBattleStructs.h"
#include "MassEntityCollection.h"
#include "Fragments/Animation.h"
#include "Mass/EntityHandle.h"
#include "MassEntityQuery.h"
#include "Subsystems/MassBattleFogRenderSubsystem.h"
#include "MassBattleFogAgentRenderProcessor.generated.h"

// Forward Declarations
struct FMoving;
struct FAppear;
struct FAttack;
struct FHit;
struct FDeath;
struct FAnimShared;
struct FAnimating;
struct FAgentRenderBatchData;
struct FVisualize;
struct FVisualizing;
struct FStreamableHandle;
class AMassBattleAgentRenderer;
class UMassAPISubsystem;
class UMassBattleAgentSubsystem;
class UMassBattleSubsystem;
class UMassBattleHashGridSubsystem;

/**
 * Stable, lightweight identity owned by FogOfWar. Heavy interpolation state
 * remains in FVisualizing; membership changes move only ProxyId integers.
 */
struct FMassBattleFogRenderProxy
{
	FMassEntityHandle Entity;
	int32 SubType = INDEX_NONE;
	int32 ActiveListIndex = INDEX_NONE;
	uint32 LastVisitedEpoch = 0;
	uint32 LastVisibleEpoch = 0;
	uint32 NiagaraAcquireTag = 0;
	/** Final state produced by the camera + fog broad phase for this refresh. */
	uint8 FogVisibilityState = 0;
	/** True when the independent MassBattleISKM add-on owns presentation. */
	bool bUsesAddonISKM = false;
	bool bFriendlyTeam = false;
	bool bSnapOnNextUpdate = true;
	/** Set only by a 3 Hz convergence pass; removal happens on a later pass. */
	double PendingRemovalStartWorldTime = -1.0;
};

struct FMassBattleFogSubTypeWorkSet
{
	/** Stable work-set membership. Swap removal moves one int32 only. */
	TArray<int32> ActiveProxyIds;

	/** Cached subset that currently owns a Particle VAT representation. */
	TArray<int32> DenseProxyIds;
	/** Scratch pointers paired with DenseProxyIds while rebuilding the layout. */
	TArray<FVisualizing*> DenseRuntimes;
	TArray<int32> FrameBatchIds;

	uint32 MembershipVersion = 0;
	uint32 DenseLayoutMembershipVersion = MAX_uint32;
	TWeakObjectPtr<AMassBattleAgentRenderer> DenseLayoutRenderer;
};

/**
 * A unified processor that handles LOD selection, Animation state updates, and Rendering data packing.
 * Merges functionality from:
 * - MassBattleAgentLODProcessor
 * - MassBattleAgentAnimationProcessor
 * - MassBattleAgentRenderProcessor
 */
UCLASS(Config = Mass)
class FOGOFWAR_API UMassBattleFogAgentRenderProcessor : public UMassBattleAgentRenderProcessor
{
	GENERATED_BODY()

public:

	UMassBattleFogAgentRenderProcessor();
	virtual FString GetProcessorName() const override;

protected:

	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:

	/** The main entity query for this processor. */
	FMassEntityQuery EntityQuery;

	/** Lightweight queries for projectile/loot interpolation (runs every frame for attached FX smoothing) */
	FMassEntityQuery ProjectileInterpQuery;
	FMassEntityQuery LootInterpQuery;

	/** Low-frequency all-world minimap snapshot; never enters the heavy agent render path. */
	FMassEntityQuery MinimapSnapshotQuery;

	/**
	 * Cache-coherent low-frequency visibility/source pass. When the fog mask needs
	 * every friendly source, a HashGrid walk already covers the world; processing
	 * the matching Mass chunks once is cheaper than random fragment access per
	 * grid record.
	 */
	FMassEntityQuery VisibilityWorkSetQuery;

	/** Position/velocity-only sampling over the already filtered source membership. */
	FMassEntityQuery VisionSourceSampleQuery;

	FSpinLockArray<FMassEntityHandle> RegistrationQueue;
	FSpinLockArray<FVector4f> VisionSourceQueue;
	FSpinLockArray<FMassEntityHandle> VisionSourceEntityQueue;
	FSpinLockArray<FVector4f> MinimapVisionSourceUnitQueue;
	FSpinLockArray<FVector4f> MinimapFriendlyNonVisionUnitQueue;
	FSpinLockArray<FVector4f> MinimapOtherUnitQueue;
	FSpinLockArray<FVector4f> MinimapFogVisibleUnitQueue;
	FSpinLockArray<FMassBattleFogAttackRevealObservation> AttackRevealQueue;
	// FOG-OF-WAR INSERTION: stable proxy pool + lightweight active lists.
	TArray<FMassBattleFogRenderProxy> ProxyPool;
	TArray<int32> FreeProxyIds;
	TArray<int32> ProxyIdByEntityIndex;
	TMap<int32, FMassBattleFogSubTypeWorkSet> RenderWorkSets;
	/** One handle per renderer class; prevents repeated requests and GT sync loads. */
	TMap<FSoftObjectPath, TSharedPtr<FStreamableHandle>> RendererClassLoadHandles;
	TArray<FMassEntityHandle> ActiveRenderEntitiesScratch;
	/**
	 * UE's version-aware collection cache. Handles are replaced only when the
	 * Active membership changes; internal archetype ranges rebuild themselves
	 * only when Mass reports an entity-order version change.
	 */
	TSharedPtr<UE::Mass::FEntityCollection> ActiveRenderEntityCollection;
	/**
	 * Camera/radius-filtered vision-source membership is rebuilt with the
	 * low-frequency filter. Its positions are sampled independently at the
	 * scene cadence without walking the full population or HashGrid again.
	 */
	TSharedPtr<UE::Mass::FEntityCollection> VisionSourceEntityCollection;
	uint32 VisionSourceEntityCollectionRevision = 0;
	uint64 ActiveWorkSetMembershipVersion = 1;
	uint64 CachedActiveCollectionMembershipVersion = 0;
	uint32 VisibilityEpoch = 0;
	uint32 LastRenderWorkSetRevision = 0;
	double LastFullWorkSetRefreshWorldTime = -TNumericLimits<double>::Max();
	bool bRenderWorkSetInitialized = false;
	/** Representation selection changes inside the query; rebuild on the next frame. */
	bool bPreviousFrameWasSimulationTick = true;

	/** Vertical extent of the 2D fog/camera HashGrid gather. */
	UPROPERTY(Config, EditAnywhere, Category = "FogOfWar|Performance", meta = (ClampMin = "300.0", Units = "cm"))
	float ActiveWorkSetHalfHeightUU = 4096.0f;

	/** Avoid Niagara component churn when the camera or fog boundary oscillates. */
	UPROPERTY(Config, EditAnywhere, Category = "FogOfWar|Performance", meta = (ClampMin = "0.0", Units = "s"))
	float MinimumBatchIdleSeconds = 20.0f;

	// Cached flag enum values for fast HasFlag/SetFlag/ClearFlag | 缓存旗标枚举值
	EEntityFlags AttackingFlag  = static_cast<EEntityFlags>(EBattleFlags::Attacking);
	EEntityFlags DeathAnimFlag  = static_cast<EEntityFlags>(EBattleFlags::DeathAnim);
	EEntityFlags AppearAnimFlag = static_cast<EEntityFlags>(EBattleFlags::AppearAnim);
	EEntityFlags AttackAnimFlag = static_cast<EEntityFlags>(EBattleFlags::AttackAnim);
	EEntityFlags HitAnimFlag    = static_cast<EEntityFlags>(EBattleFlags::HitAnim);
	EEntityFlags ActivatedFlag = static_cast<EEntityFlags>(EBattleFlags::Activated);
	EEntityFlags NotRenderingFlag = static_cast<EEntityFlags>(EBattleFlags::NotRendering);
	EEntityFlags RenderingFlag = static_cast<EEntityFlags>(EBattleFlags::Rendering);
	EEntityFlags RenderingWithParticleFlag = static_cast<EEntityFlags>(EBattleFlags::RenderingWithParticle);
	EEntityFlags RenderingWithActorFlag = static_cast<EEntityFlags>(EBattleFlags::RenderingWithActor);
	EEntityFlags DyingFlag = static_cast<EEntityFlags>(EBattleFlags::Dying);
	EEntityFlags AppearingFlag = static_cast<EEntityFlags>(EBattleFlags::Appearing);
	EEntityFlags BeingHitFlag = static_cast<EEntityFlags>(EBattleFlags::BeingHit);
	EEntityFlags PauseAnimChangeFlag = static_cast<EEntityFlags>(EBattleFlags::PauseAnimChange);
	EEntityFlags FallingFlag = static_cast<EEntityFlags>(EBattleFlags::Falling);
	EEntityFlags SlowingFlag = static_cast<EEntityFlags>(EBattleFlags::Slowing);
	EEntityFlags BeingSelectFlag = static_cast<EEntityFlags>(EBattleFlags::BeingSelect);
	EEntityFlags SelectedFlag = static_cast<EEntityFlags>(EBattleFlags::Selected);

	//----------------------------------------------------------------------------------
	// Render / Batch Management Helpers
	//----------------------------------------------------------------------------------

	int32 FindOrCreateProxy(UMassAPISubsystem& MassAPI, const FMassEntityHandle Entity, int32 SubType);
	void ActivateProxy(int32 ProxyId);
	void DeactivateProxy(int32 ProxyId, UMassAPISubsystem* MassAPI = nullptr);
	void ReleaseProxy(int32 ProxyId, UMassAPISubsystem* MassAPI = nullptr);
	void QueueRendererClassLoad(const TSoftClassPtr<AMassBattleAgentRenderer>& RendererClass);
	void RefreshActiveRenderWorkSet(
		FMassExecutionContext& ProcessingContext,
		UMassAPISubsystem& MassAPI,
		UMassBattleAgentSubsystem& AgentSubsystem,
		UMassBattleHashGridSubsystem& HashGrid,
		UMassBattleFogRenderSubsystem& FogRenderSubsystem,
		bool bCollectVisionSources,
		bool bAllowRemoval,
		double WorldTimeSeconds);
	void PrepareDenseRenderFrame(
		UMassAPISubsystem& MassAPI,
		UMassBattleSubsystem& MassBattle,
		TArray<FMassEntityHandle>* OutActiveRenderEntities,
		bool bForceDenseLayoutRebuild);
	static void SetBatchFrameCount(FAgentRenderBatchData& Data, int32 Count);

	void IdleCheck(AMassBattleAgentRenderer* RendererActor, float DeltaTime);

	/** Adds a new render batch (e.g., a new Niagara system) to a renderer actor when existing ones are full. */
	int32 AddRenderBatch(AMassBattleAgentRenderer* RendererActor);

	//----------------------------------------------------------------------------------
	// Animation Logic Helpers
	//----------------------------------------------------------------------------------

	void UpdateAnimBlendLevel1(FPerLevelAnimState& State, const FAnimShared& Animation, const FAnimating& Animating, EAnimState CurrentState, bool bStateChanged, float DeltaTime, const FAppear& Appear, const FAttack& Attack, const FHit& Hit, const FDeath& Death, float AttackSpeedMultiplier, bool& bOutReset0);

	void UpdateAnimBlendLevel2(FPerLevelAnimState& State, const FAnimShared& Animation, const FAnimating& Animating, EAnimState CurrentState, bool bStateChanged, bool bIsFirstUpdate, float DeltaTime, const FMoving& Moving, const FAppear& Appear, const FAttack& Attack, const FHit& Hit, const FDeath& Death, float AttackSpeedMultiplier, bool& bOutReset1, bool& bOutReset2);

	void UpdateAnimBlendLevel3(FPerLevelAnimState& State, const FAnimShared& Animation, const FAnimating& Animating, EAnimState CurrentState, bool bStateChanged, bool bIsFirstUpdate, float DeltaTime, const FMoving& Moving, const FAppear& Appear, const FAttack& Attack, const FHit& Hit, const FDeath& Death, float AttackSpeedMultiplier, bool& bOutReset0, bool& bOutReset1, bool& bOutReset2);

	void AdvanceFrame(FPerLevelAnimState& State, int32 SlotIndex, float DeltaTime, int32 SampleRate, const FAnimShared& Animation, float MoveDirectionMultiplier, bool bSkipAdvance);

	//----------------------------------------------------------------------------------
	// Inline Helpers (Animation Calculation)
	//----------------------------------------------------------------------------------

	FORCEINLINE static void ShiftSlotsForState(FPerLevelAnimState& State, int32 FromSlot, int32 ToSlot)
	{
		// Helpers to get references based on slot index
		auto GetFrame = [&](int32 Slot) -> float& { return (Slot == 2) ? State.CurrentFrame2 : ((Slot == 1) ? State.CurrentFrame1 : State.CurrentFrame0); };
		auto GetRate = [&](int32 Slot) -> float& { return (Slot == 2) ? State.PlayRate2 : ((Slot == 1) ? State.PlayRate1 : State.PlayRate0); };
		auto GetIndex = [&](int32 Slot) -> uint8& { return (Slot == 2) ? State.Index2 : ((Slot == 1) ? State.Index1 : State.Index0); };
		auto GetLoop = [&](int32 Slot) -> bool& { return (Slot == 2) ? State.bLoop2 : ((Slot == 1) ? State.bLoop1 : State.bLoop0); };

		// Updated: Shift Cached Bounds as well (StartFrame and EndFrame added to FPerLevelAnimState)
		auto GetStart = [&](int32 Slot) -> uint16& { return (Slot == 2) ? State.StartFrame2 : ((Slot == 1) ? State.StartFrame1 : State.StartFrame0); };
		auto GetEnd = [&](int32 Slot) -> uint16& { return (Slot == 2) ? State.EndFrame2 : ((Slot == 1) ? State.EndFrame1 : State.EndFrame0); };

		GetFrame(ToSlot) = GetFrame(FromSlot);
		GetRate(ToSlot) = GetRate(FromSlot);
		GetIndex(ToSlot) = GetIndex(FromSlot);
		GetLoop(ToSlot) = GetLoop(FromSlot);
		GetStart(ToSlot) = GetStart(FromSlot);
		GetEnd(ToSlot) = GetEnd(FromSlot);
	}

	//----------------------------------------------------------------------------------
	// Inline Helpers (Data Packing / Encoding)
	//----------------------------------------------------------------------------------

	// PackData: Frame0 (16 bits), Frame1 (16 bits)
	// Param0[0]
	FORCEINLINE static float EncodeFrames01(float Frame0, float Frame1)
	{
		// Range 0-65535 for each frame
		uint32 iFrame0 = FMath::Clamp(static_cast<uint32>(Frame0), 0u, 65535u);
		uint32 iFrame1 = FMath::Clamp(static_cast<uint32>(Frame1), 0u, 65535u);

		// | Frame1 (16 bits) | Frame0 (16 bits) |
		uint32 packed = (iFrame1 << 16) | iFrame0;
		return *reinterpret_cast<float*>(&packed);
	}

	// PackData: Frame2 (16 bits), Lerp0 (8 bits), Lerp1 (8 bits)
	// Param0[1]
	FORCEINLINE static float EncodeFrame2AndLerps(uint16 Frame2, uint8 Lerp0, uint8 Lerp1)
	{
		// Range 0-65535 for Frame2
		uint32 iFrame2 = FMath::Clamp(static_cast<uint32>(Frame2), 0u, 65535u);

		// Lerps already in 0-255 range, no conversion needed
		uint32 iLerp0 = Lerp0;
		uint32 iLerp1 = Lerp1;

		// | Frame2 (16 bits) | Lerp0 (8 bits) | Lerp1 (8 bits) |
		uint32 packed = (iFrame2 << 16) | (iLerp0 << 8) | iLerp1;
		return *reinterpret_cast<float*>(&packed);
	}

	// PackData: Team (10 bits), Dissolve (10 bits), LODIndex (9 bits), DrawLOD (1 bit), BeingSelect (1 bit), Selected (1 bit) = 32 bits total
	// Layout: | Selected (1 bit, bit 31) | BeingSelect (1 bit, bit 30) | DrawLOD (1 bit, bit 29) | LODIndex (9 bits, bits 20-28) | Dissolve (10 bits, bits 10-19) | Team (10 bits, bits 0-9) |
	FORCEINLINE static float EncodeDynamicParams0(uint8 Team, uint8 Dissolve, int32 LOD, bool bDrawLOD, bool bBeingSelect, bool bSelected)
	{
		// Team: 10 bits (0-1023), clamp from 0-255
		uint32 iTeam = FMath::Clamp(static_cast<uint32>(Team), 0u, 1023u);

		// Dissolve: 10 bits (0-1023), map 0-255 → 0-1023 (255 * 4.01 ≈ 1023)
		uint32 iDissolve = FMath::Clamp((static_cast<uint32>(Dissolve) * 1023u) / 255u, 0u, 1023u);

		// LODIndex: 9 bits (0-511), clamp from input (typically 0-4)
		uint32 iLOD = FMath::Clamp(static_cast<uint32>(FMath::Max(0, LOD)), 0u, 511u);

		// DrawLOD: 1 bit
		uint32 iDrawLOD = bDrawLOD ? 1u : 0u;

		// BeingSelect: 1 bit
		uint32 iBeingSelect = bBeingSelect ? 1u : 0u;

		// Selected: 1 bit
		uint32 iSelected = bSelected ? 1u : 0u;

		// | Selected (1 bit) | BeingSelect (1 bit) | DrawLOD (1 bit) | LODIndex (9 bits) | Dissolve (10 bits) | Team (10 bits) |
		uint32 packed = (iSelected << 31) | (iBeingSelect << 30) | (iDrawLOD << 29) | (iLOD << 20) | (iDissolve << 10) | iTeam;

		return *reinterpret_cast<float*>(&packed);
	}

	// PackData: Four MaterialFx effects, each allocated 8 bits (0-255)
	FORCEINLINE static float EncodeStatusEffects(uint8 HitGlow, uint8 Frozen, uint8 Burning, uint8 Poisoned)
	{
		// Already in 0-255 range, direct packing
		uint32 packed = (static_cast<uint32>(Poisoned) << 24) |
		                (static_cast<uint32>(Burning) << 16) |
		                (static_cast<uint32>(Frozen) << 8) |
		                static_cast<uint32>(HitGlow);
		return *reinterpret_cast<float*>(&packed);
	}

	//----------------------------------------------------------------------------------
	// Entity Flags / Constants
	//----------------------------------------------------------------------------------


};
