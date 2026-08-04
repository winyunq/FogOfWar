// Copyright Winyunq, 2025. All Rights Reserved.
// Commercial extension: see COMMERCIAL_FEATURE_LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "RHIUtilities.h"
#include "SceneViewExtension.h"

class FTextureRenderTargetResource;
class FRHIGPUTextureReadback;

struct FMassBattleFrameFogMaskReadbackData
{
	TArray<uint8> VisibilityStates;
	FIntPoint Dimensions = FIntPoint::ZeroValue;
	FVector2D WorldMin = FVector2D::ZeroVector;
	FVector2D CellSize = FVector2D(1.0, 1.0);
};

/** Lock-protected one-frame mailbox between the render and game threads. */
class FMassBattleFrameFogMaskReadbackMailbox final
{
public:
	void Publish_RenderThread(FMassBattleFrameFogMaskReadbackData&& InData);
	bool Consume_GameThread(FMassBattleFrameFogMaskReadbackData& OutData);

private:
	FCriticalSection CriticalSection;
	FMassBattleFrameFogMaskReadbackData PendingData;
	bool bHasPendingData = false;
};

/**
 * One compact upload shared by the high-resolution scene mask and the
 * HashGrid-aligned unit filter. Every source is float4(world XY + velocity XY);
 * there is no second entity traversal and no Landscape/material binding.
 */
struct FMassBattleFrameFogSceneUploadData
{
	TArray<FVector4f> VisionSourceSamples;
	float VisionRadiusUU = 1024.0f;
	float FogOpacity = 0.3f;
	float SceneProjectionPlaneZ = 0.0f;
	float MaxSourcePredictionSeconds = 0.125f;
	double SourceSampleWorldTimeSeconds = 0.0;
	double UploadWorldTimeSeconds = 0.0;
	FVector2D WorldMaskMin = FVector2D::ZeroVector;
	FVector2D WorldMaskSize = FVector2D(1.0, 1.0);
	FVector2D WorldMaskCellSize = FVector2D(1.0, 1.0);
	FIntPoint WorldMaskDimensions = FIntPoint::ZeroValue;
	FTextureRenderTargetResource* WorldMaskResource = nullptr;
	TSharedPtr<FMassBattleFrameFogMaskReadbackMailbox, ESPMode::ThreadSafe> ReadbackMailbox;
	bool bUpdateLogicMask = false;
	bool bEnabled = false;
	bool bDebug = false;
	bool bDebugRevealAll = false;
};

/**
 * Camera scene fog plus the asynchronous unit-state producer. Sources rasterize
 * at the scene cadence directly into a native viewport-sized presentation mask
 * and at a lower cadence into the HashGrid-aligned readback mask. Render frames
 * sample the cached screen mask while compositing SceneColor.
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
	void ResolvePendingReadback_RenderThread();

	FReadBuffer VisionSourcePositionBuffer;
	TUniquePtr<FRHIGPUTextureReadback> VisibilityStateReadback;
	TSharedPtr<FMassBattleFrameFogMaskReadbackMailbox, ESPMode::ThreadSafe> ReadbackMailbox_RenderThread;
	FIntPoint ReadbackDimensions_RenderThread = FIntPoint::ZeroValue;
	FVector2D ReadbackWorldMin_RenderThread = FVector2D::ZeroVector;
	FVector2D ReadbackCellSize_RenderThread = FVector2D(1.0, 1.0);
	uint32 SourceCount_RenderThread = 0;
	float VisionRadiusUU_RenderThread = 1024.0f;
	float VisionPredictionSeconds_RenderThread = 0.0f;
	float FogOpacity_RenderThread = 0.3f;
	float SceneProjectionPlaneZ_RenderThread = 0.0f;
	bool bEnabled_GameThread = false;
	bool bEnabled_RenderThread = false;
	bool bDebug_RenderThread = false;
	bool bDebugRevealAll_RenderThread = false;
};
