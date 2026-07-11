// Copyright Winyunq, 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHIUtilities.h"
#include "SceneViewExtension.h"

struct FMassBattleFrameFogSceneUploadData
{
	TArray<FVector> Locations;
	TArray<FVector4f> DynamicParams0;
	TArray<bool> IsHidden;
	float VisionRadiusUU = 1024.0f;
	float FogOpacity = 0.85f;
	uint32 ViewingTeamIndex = 0;
	bool bEnabled = false;
	bool bDebug = false;
	bool bDebugRevealAll = false;
};

/**
 * Scene-side GPU implementation borrowed from the MassBattle minimap fog pass.
 *
 * The render thread rasterizes one projected circle per batch entry into an
 * R8 visibility mask, then composites SceneColor against that mask. The CPU
 * only forwards the already-contiguous MassBattleFrame arrays.
 */
class FMassBattleFrameFogSceneViewExtension final : public FSceneViewExtensionBase
{
public:
	FMassBattleFrameFogSceneViewExtension(const FAutoRegister& AutoRegister);
	~FMassBattleFrameFogSceneViewExtension() override;

	void Upload_GameThread(FMassBattleFrameFogSceneUploadData&& UploadData);
	void Release_GameThread();

	virtual void SubscribeToPostProcessingPass(
		EPostProcessingPass Pass,
		const FSceneView& View,
		FAfterPassCallbackDelegateArray& InOutPassCallbacks,
		bool bIsPassEnabled) override;

	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;

private:
	FScreenPassTexture PostProcessPass_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);

	void Upload_RenderThread(FRHICommandListImmediate& RHICmdList, const FMassBattleFrameFogSceneUploadData& UploadData);
	void Release_RenderThread();

	FReadBuffer LocationWordsBuffer;
	FReadBuffer DynamicParams0Buffer;
	FReadBuffer IsHiddenBuffer;
	uint32 SourceCount_RenderThread = 0;
	float VisionRadiusUU_RenderThread = 1024.0f;
	float FogOpacity_RenderThread = 0.85f;
	uint32 ViewingTeamIndex_RenderThread = 0;
	bool bEnabled_GameThread = false;
	bool bEnabled_RenderThread = false;
	bool bDebug_RenderThread = false;
	bool bDebugRevealAll_RenderThread = false;
};
