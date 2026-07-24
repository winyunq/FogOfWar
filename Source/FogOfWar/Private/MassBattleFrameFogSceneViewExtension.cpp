// Copyright Winyunq, 2025. All Rights Reserved.

#include "MassBattleFrameFogSceneViewExtension.h"

#include "CommonRenderResources.h"
#include "Engine/TextureRenderTarget.h"
#include "GlobalShader.h"
#include "Misc/ScopeLock.h"
#include "PipelineStateCache.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "RHICommandList.h"
#include "RHIGPUReadback.h"
#include "RHIStaticStates.h"
#include "SceneView.h"
#include "ShaderParameterStruct.h"

DEFINE_LOG_CATEGORY_STATIC(LogMassBattleFrameFogScene, Log, All);
DECLARE_GPU_STAT_NAMED(MassBattleFrameFogComposite, TEXT("MassBattleFrameFog Composite"));
DECLARE_GPU_STAT_NAMED(MassBattleFrameFogSceneVisualMask, TEXT("MassBattleFrameFog Scene Visual Mask"));
DECLARE_GPU_STAT_NAMED(MassBattleFrameFogLogicMask, TEXT("MassBattleFrameFog Logic Mask"));

void FMassBattleFrameFogMaskReadbackMailbox::Publish_RenderThread(FMassBattleFrameFogMaskReadbackData&& InData)
{
	FScopeLock Lock(&CriticalSection);
	PendingData = MoveTemp(InData);
	bHasPendingData = true;
}

bool FMassBattleFrameFogMaskReadbackMailbox::Consume_GameThread(FMassBattleFrameFogMaskReadbackData& OutData)
{
	FScopeLock Lock(&CriticalSection);
	if (!bHasPendingData)
	{
		return false;
	}
	OutData = MoveTemp(PendingData);
	bHasPendingData = false;
	return true;
}

namespace
{
	class FMassBattleFrameFogVisionVS final : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FMassBattleFrameFogVisionVS);
		SHADER_USE_PARAMETER_STRUCT(FMassBattleFrameFogVisionVS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
			SHADER_PARAMETER(float, VisionRadiusUU)
			SHADER_PARAMETER(float, VisionPredictionSeconds)
			SHADER_PARAMETER(float, SceneProjectionPlaneZ)
			SHADER_PARAMETER_SRV(Buffer<float>, VisionSourcePositions)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};

	class FMassBattleFrameFogVisionPS final : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FMassBattleFrameFogVisionPS);
		SHADER_USE_PARAMETER_STRUCT(FMassBattleFrameFogVisionPS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(float, VisibilityStateValue)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};

	class FMassBattleFrameFogWorldMaskVS final : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FMassBattleFrameFogWorldMaskVS);
		SHADER_USE_PARAMETER_STRUCT(FMassBattleFrameFogWorldMaskVS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(FVector2f, WorldMaskMin)
			SHADER_PARAMETER(FVector2f, WorldMaskSize)
			SHADER_PARAMETER(float, VisionRadiusUU)
			SHADER_PARAMETER(float, VisionPredictionSeconds)
			SHADER_PARAMETER_SRV(Buffer<float>, VisionSourcePositions)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};

	class FMassBattleFrameFogWorldMaskPS final : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FMassBattleFrameFogWorldMaskPS);
		SHADER_USE_PARAMETER_STRUCT(FMassBattleFrameFogWorldMaskPS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(float, VisibilityStateValue)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};

	class FMassBattleFrameFogCompositeVS final : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FMassBattleFrameFogCompositeVS);
		SHADER_USE_PARAMETER_STRUCT(FMassBattleFrameFogCompositeVS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(FVector2f, OutputExtent)
			SHADER_PARAMETER(FVector2f, ViewRectMin)
			SHADER_PARAMETER(FVector2f, ViewRectSize)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};

	class FMassBattleFrameFogCompositePS final : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FMassBattleFrameFogCompositePS);
		SHADER_USE_PARAMETER_STRUCT(FMassBattleFrameFogCompositePS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(FVector2f, OutputExtent)
			SHADER_PARAMETER(float, FogOpacity)
			SHADER_PARAMETER(uint32, Debug)
			SHADER_PARAMETER(uint32, DebugRevealAll)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColor)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D, VisibilityMask)
			SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FMassBattleFrameFogVisionVS, "/Plugin/FogOfWar/Private/MassBattleFrameFogScene.usf", "VisionVS", SF_Vertex);
	IMPLEMENT_GLOBAL_SHADER(FMassBattleFrameFogVisionPS, "/Plugin/FogOfWar/Private/MassBattleFrameFogScene.usf", "VisionPS", SF_Pixel);
	IMPLEMENT_GLOBAL_SHADER(FMassBattleFrameFogWorldMaskVS, "/Plugin/FogOfWar/Private/MassBattleFrameFogScene.usf", "WorldMaskVS", SF_Vertex);
	IMPLEMENT_GLOBAL_SHADER(FMassBattleFrameFogWorldMaskPS, "/Plugin/FogOfWar/Private/MassBattleFrameFogScene.usf", "WorldMaskPS", SF_Pixel);
	IMPLEMENT_GLOBAL_SHADER(FMassBattleFrameFogCompositeVS, "/Plugin/FogOfWar/Private/MassBattleFrameFogScene.usf", "CompositeVS", SF_Vertex);
	IMPLEMENT_GLOBAL_SHADER(FMassBattleFrameFogCompositePS, "/Plugin/FogOfWar/Private/MassBattleFrameFogScene.usf", "CompositePS", SF_Pixel);

	BEGIN_SHADER_PARAMETER_STRUCT(FVisionPassParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FMassBattleFrameFogVisionVS::FParameters, VSParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FMassBattleFrameFogVisionPS::FParameters, PSParameters)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	BEGIN_SHADER_PARAMETER_STRUCT(FWorldMaskPassParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FMassBattleFrameFogWorldMaskVS::FParameters, VSParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FMassBattleFrameFogWorldMaskPS::FParameters, PSParameters)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	BEGIN_SHADER_PARAMETER_STRUCT(FCompositePassParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FMassBattleFrameFogCompositeVS::FParameters, VSParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FMassBattleFrameFogCompositePS::FParameters, PSParameters)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	void InitializeMassBattleFrameFogReadBuffer(
		FRHICommandListImmediate& RHICmdList,
		FReadBuffer& OutBuffer,
		const TCHAR* DebugName,
		const uint32 BytesPerElement,
		const uint32 NumElements,
		const EPixelFormat Format,
		const void* SourceData,
		const uint32 SourceBytes)
	{
		if (NumElements == 0 || SourceBytes == 0 || SourceData == nullptr)
		{
			return;
		}

		const uint32 RequiredBytes = BytesPerElement * NumElements;
		if (!OutBuffer.Buffer.IsValid() || OutBuffer.NumBytes < RequiredBytes)
		{
			OutBuffer.Release();
			const uint32 CapacityElements = FMath::RoundUpToPowerOfTwo(NumElements);
			OutBuffer.Initialize(
				RHICmdList,
				DebugName,
				BytesPerElement,
				CapacityElements,
				Format,
				BUF_Dynamic);
		}

		void* Destination = RHICmdList.LockBuffer(OutBuffer.Buffer, 0, SourceBytes, RLM_WriteOnly);
		FMemory::Memcpy(Destination, SourceData, SourceBytes);
		RHICmdList.UnlockBuffer(OutBuffer.Buffer);
	}

	void ConfigurePipeline(
		FRHICommandList& RHICmdList,
		FGraphicsPipelineStateInitializer& PSO,
		FRHIVertexShader* VertexShader,
		FRHIPixelShader* PixelShader)
	{
		RHICmdList.ApplyCachedRenderTargets(PSO);
		PSO.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
		PSO.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
		PSO.BoundShaderState.VertexShaderRHI = VertexShader;
		PSO.BoundShaderState.PixelShaderRHI = PixelShader;
		PSO.PrimitiveType = PT_TriangleList;
	}
}

FMassBattleFrameFogSceneViewExtension::FMassBattleFrameFogSceneViewExtension(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
{
}

FMassBattleFrameFogSceneViewExtension::~FMassBattleFrameFogSceneViewExtension() = default;

void FMassBattleFrameFogSceneViewExtension::Upload_GameThread(FMassBattleFrameFogSceneUploadData&& UploadData)
{
	check(IsInGameThread());
	bEnabled_GameThread = UploadData.bEnabled;
	TSharedRef<FMassBattleFrameFogSceneViewExtension, ESPMode::ThreadSafe> Self =
		StaticCastSharedRef<FMassBattleFrameFogSceneViewExtension>(FSceneViewExtensionBase::AsShared());
	ENQUEUE_RENDER_COMMAND(FUploadMassBattleFrameFogSceneData)(
		[Self, Data = MoveTemp(UploadData)](FRHICommandListImmediate& RHICmdList) mutable
		{
			Self->Upload_RenderThread(RHICmdList, Data);
		});
}

void FMassBattleFrameFogSceneViewExtension::Release_GameThread()
{
	if (!IsInGameThread())
	{
		return;
	}
	bEnabled_GameThread = false;
	TSharedRef<FMassBattleFrameFogSceneViewExtension, ESPMode::ThreadSafe> Self =
		StaticCastSharedRef<FMassBattleFrameFogSceneViewExtension>(FSceneViewExtensionBase::AsShared());
	ENQUEUE_RENDER_COMMAND(FReleaseMassBattleFrameFogSceneData)(
		[Self](FRHICommandListImmediate& RHICmdList)
		{
			Self->Release_RenderThread();
		});
}

void FMassBattleFrameFogSceneViewExtension::ResolvePendingReadback_RenderThread()
{
	check(IsInRenderingThread());
	if (!VisibilityStateReadback || !VisibilityStateReadback->IsReady())
	{
		return;
	}

	int32 RowPitchInPixels = 0;
	int32 BufferHeight = 0;
	const uint8* Source = static_cast<const uint8*>(
		VisibilityStateReadback->Lock(RowPitchInPixels, &BufferHeight));
	const int32 Width = ReadbackDimensions_RenderThread.X;
	const int32 Height = ReadbackDimensions_RenderThread.Y;
	if (Source
		&& Width > 0
		&& Height > 0
		&& RowPitchInPixels >= Width
		&& BufferHeight >= Height)
	{
		FMassBattleFrameFogMaskReadbackData ReadbackData;
		ReadbackData.Dimensions = ReadbackDimensions_RenderThread;
		ReadbackData.WorldMin = ReadbackWorldMin_RenderThread;
		ReadbackData.CellSize = ReadbackCellSize_RenderThread;
		ReadbackData.VisibilityStates.SetNumUninitialized(Width * Height);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			FMemory::Memcpy(
				ReadbackData.VisibilityStates.GetData() + Y * Width,
				Source + Y * RowPitchInPixels,
				Width);
		}
		if (ReadbackMailbox_RenderThread.IsValid())
		{
			ReadbackMailbox_RenderThread->Publish_RenderThread(MoveTemp(ReadbackData));
		}
	}

	VisibilityStateReadback->Unlock();
	VisibilityStateReadback.Reset();
}

void FMassBattleFrameFogSceneViewExtension::Upload_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	const FMassBattleFrameFogSceneUploadData& UploadData)
{
	check(IsInRenderingThread());
	static_assert(sizeof(FVector4f) == sizeof(float) * 4, "Unexpected FVector4f storage.");

	ResolvePendingReadback_RenderThread();

	SourceCount_RenderThread = static_cast<uint32>(UploadData.VisionSourceSamples.Num());
	InitializeMassBattleFrameFogReadBuffer(
		RHICmdList,
		VisionSourcePositionBuffer,
		TEXT("MassBattleFrameFog.VisionSourcePositions"),
		sizeof(float),
		static_cast<uint32>(UploadData.VisionSourceSamples.Num() * 4),
		PF_R32_FLOAT,
		UploadData.VisionSourceSamples.GetData(),
		static_cast<uint32>(UploadData.VisionSourceSamples.Num() * sizeof(FVector4f)));

	VisionRadiusUU_RenderThread = FMath::Max(0.0f, UploadData.VisionRadiusUU);
	VisionPredictionSeconds_RenderThread = FMath::Clamp(
		static_cast<float>(UploadData.UploadWorldTimeSeconds - UploadData.SourceSampleWorldTimeSeconds),
		0.0f,
		FMath::Max(0.0f, UploadData.MaxSourcePredictionSeconds));
	FogOpacity_RenderThread = FMath::Clamp(UploadData.FogOpacity, 0.0f, 1.0f);
	SceneProjectionPlaneZ_RenderThread = UploadData.SceneProjectionPlaneZ;
	bEnabled_RenderThread = UploadData.bEnabled;
	bDebug_RenderThread = UploadData.bDebug;
	bDebugRevealAll_RenderThread = UploadData.bDebugRevealAll;

	const bool bHasSourceBuffer = SourceCount_RenderThread == 0
		|| VisionSourcePositionBuffer.SRV.IsValid();
	const bool bCanBuildLogicMask = UploadData.bUpdateLogicMask
		&& UploadData.WorldMaskResource != nullptr
		&& UploadData.WorldMaskDimensions.X > 0
		&& UploadData.WorldMaskDimensions.Y > 0
		&& UploadData.WorldMaskSize.X > 0.0
		&& UploadData.WorldMaskSize.Y > 0.0
		&& bHasSourceBuffer;
	FTextureRHIRef LogicMaskTextureRHI = bCanBuildLogicMask
		? UploadData.WorldMaskResource->GetRenderTargetTexture()
		: nullptr;
	const bool bHasLogicTarget = LogicMaskTextureRHI.IsValid();

	if (!bHasLogicTarget)
	{
		return;
	}

	FRDGBuilder GraphBuilder(RHICmdList);
	const bool bCanDrawVisionSources = SourceCount_RenderThread > 0
		&& VisionSourcePositionBuffer.SRV.IsValid();
	TShaderMapRef<FMassBattleFrameFogWorldMaskVS> MaskVertexShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	TShaderMapRef<FMassBattleFrameFogWorldMaskPS> MaskPixelShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

	auto AddMaskRasterPass = [&] (
		FRDGTextureRef TargetTexture,
		const FIntPoint& TargetDimensions,
		const float RadiusUU,
		const float VisibilityValue,
		const TCHAR* PassLabel)
	{
		if (UploadData.bDebugRevealAll)
		{
			AddClearRenderTargetPass(GraphBuilder, TargetTexture, FLinearColor::White);
			return;
		}

		AddClearRenderTargetPass(GraphBuilder, TargetTexture, FLinearColor::Black);
		if (!bCanDrawVisionSources)
		{
			return;
		}

		FWorldMaskPassParameters* MaskPass = GraphBuilder.AllocParameters<FWorldMaskPassParameters>();
		MaskPass->RenderTargets[0] = FRenderTargetBinding(TargetTexture, ERenderTargetLoadAction::ELoad);
		FMassBattleFrameFogWorldMaskVS::FParameters MaskVS;
		MaskVS.WorldMaskMin = FVector2f(UploadData.WorldMaskMin);
		MaskVS.WorldMaskSize = FVector2f(UploadData.WorldMaskSize);
		MaskVS.VisionRadiusUU = RadiusUU;
		MaskVS.VisionPredictionSeconds = VisionPredictionSeconds_RenderThread;
		MaskVS.VisionSourcePositions = VisionSourcePositionBuffer.SRV;
		MaskPass->VSParameters = MaskVS;
		FMassBattleFrameFogWorldMaskPS::FParameters MaskPS;
		MaskPS.VisibilityStateValue = VisibilityValue;
		MaskPass->PSParameters = MaskPS;
		const uint32 InstanceCount = SourceCount_RenderThread;
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("MassBattleFrameFog.%s(%u)", PassLabel, InstanceCount),
			MaskPass,
			ERDGPassFlags::Raster,
			[MaskPass, MaskVertexShader, MaskPixelShader, TargetDimensions, InstanceCount](FRDGAsyncTask, FRHICommandList& PassRHICmdList)
			{
				FGraphicsPipelineStateInitializer PSO;
				ConfigurePipeline(PassRHICmdList, PSO, MaskVertexShader.GetVertexShader(), MaskPixelShader.GetPixelShader());
				// Every surviving circle fragment writes the same binary value.
				// Last-writer-wins is the required OR, so blending only adds an
				// unnecessary render-target read/modify/write.
				PSO.BlendState = TStaticBlendState<CW_RED>::GetRHI();
				PSO.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
				SetGraphicsPipelineState(PassRHICmdList, PSO, 0);
				SetShaderParameters(PassRHICmdList, MaskVertexShader, MaskVertexShader.GetVertexShader(), MaskPass->VSParameters);
				SetShaderParameters(PassRHICmdList, MaskPixelShader, MaskPixelShader.GetPixelShader(), MaskPass->PSParameters);
				PassRHICmdList.SetViewport(0, 0, 0.0f, TargetDimensions.X, TargetDimensions.Y, 1.0f);
				PassRHICmdList.SetScissorRect(true, 0, 0, TargetDimensions.X, TargetDimensions.Y);
				PassRHICmdList.SetStreamSource(0, nullptr, 0);
				PassRHICmdList.DrawPrimitive(0, 2, InstanceCount);
				PassRHICmdList.SetScissorRect(false, 0, 0, 0, 0);
			});
	};

	FRDGTextureRef LogicMaskTexture = GraphBuilder.RegisterExternalTexture(
		CreateRenderTarget(LogicMaskTextureRHI, TEXT("MassBattleFrameFog.LogicVisibilityMask")));
	{
		// This function owns and executes a local graph. End the breadcrumb/event
		// scope before Execute(); RDG requires the current breadcrumb to have
		// returned to its sentinel when graph execution begins.
		RDG_EVENT_SCOPE_STAT(
			GraphBuilder,
			MassBattleFrameFogLogicMask,
			"MassBattleFrameFog LogicMask");
		AddMaskRasterPass(
			LogicMaskTexture,
			UploadData.WorldMaskDimensions,
			VisionRadiusUU_RenderThread,
			3.0f / 255.0f,
			TEXT("LogicCircles"));
	}

	// Maintain at most one low-frequency staging copy.
	if (!VisibilityStateReadback)
	{
		VisibilityStateReadback = MakeUnique<FRHIGPUTextureReadback>(TEXT("MassBattleFrameFog.VisibilityStateReadback"));
		ReadbackMailbox_RenderThread = UploadData.ReadbackMailbox;
		ReadbackDimensions_RenderThread = UploadData.WorldMaskDimensions;
		ReadbackWorldMin_RenderThread = UploadData.WorldMaskMin;
		ReadbackCellSize_RenderThread = UploadData.WorldMaskCellSize;
		AddEnqueueCopyPass(
			GraphBuilder,
			VisibilityStateReadback.Get(),
			LogicMaskTexture,
			FResolveRect(0, 0, UploadData.WorldMaskDimensions.X, UploadData.WorldMaskDimensions.Y));
	}
	GraphBuilder.Execute();
}

void FMassBattleFrameFogSceneViewExtension::Release_RenderThread()
{
	check(IsInRenderingThread());
	VisionSourcePositionBuffer.Release();
	VisibilityStateReadback.Reset();
	ReadbackMailbox_RenderThread.Reset();
	ReadbackDimensions_RenderThread = FIntPoint::ZeroValue;
	SourceCount_RenderThread = 0;
	bEnabled_RenderThread = false;
}

void FMassBattleFrameFogSceneViewExtension::SubscribeToPostProcessingPass(
	const EPostProcessingPass Pass,
	const FSceneView& View,
	FAfterPassCallbackDelegateArray& InOutPassCallbacks,
	const bool bIsPassEnabled)
{
	if (Pass == EPostProcessingPass::Tonemap && bIsPassEnabled)
	{
		InOutPassCallbacks.Add(FAfterPassCallbackDelegate::CreateRaw(
			this,
			&FMassBattleFrameFogSceneViewExtension::PostProcessPass_RenderThread));
	}
}

bool FMassBattleFrameFogSceneViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	return bEnabled_GameThread;
}

FScreenPassTexture FMassBattleFrameFogSceneViewExtension::PostProcessPass_RenderThread(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FPostProcessMaterialInputs& Inputs)
{
	const FScreenPassTextureSlice SceneColorSlice = Inputs.GetInput(EPostProcessMaterialInput::SceneColor);
	if (!SceneColorSlice.IsValid() || !bEnabled_RenderThread || bDebugRevealAll_RenderThread)
	{
		return FScreenPassTexture(SceneColorSlice);
	}

	const FScreenPassTexture SceneColor(SceneColorSlice);
	FScreenPassRenderTarget Output = Inputs.OverrideOutput;
	if (!Output.IsValid())
	{
		Output = FScreenPassRenderTarget::CreateFromInput(
			GraphBuilder,
			SceneColor,
			ERenderTargetLoadAction::ENoAction,
			TEXT("MassBattleFrameFog.SceneColor"));
	}
	else
	{
		AddDrawTexturePass(GraphBuilder, View, SceneColorSlice, Output);
	}

	const FIntPoint Extent = SceneColor.Texture->Desc.Extent;
	FRDGTextureDesc MaskDesc = FRDGTextureDesc::Create2D(
		Extent,
		PF_G8,
		FClearValueBinding::Black,
		TexCreate_RenderTargetable | TexCreate_ShaderResource);
	FRDGTextureRef VisibilityMask = GraphBuilder.CreateTexture(MaskDesc, TEXT("MassBattleFrameFog.VisibilityMask"));

	const FIntRect ViewRect = SceneColor.ViewRect;
	const bool bCanDrawVision = SourceCount_RenderThread > 0
		&& VisionSourcePositionBuffer.SRV.IsValid();
	if (bCanDrawVision)
	{
		FVisionPassParameters* VisionPass = GraphBuilder.AllocParameters<FVisionPassParameters>();
		VisionPass->RenderTargets[0] = FRenderTargetBinding(VisibilityMask, ERenderTargetLoadAction::EClear);
		FMassBattleFrameFogVisionVS::FParameters VisionVS;
		VisionVS.View = View.ViewUniformBuffer;
		VisionVS.VisionRadiusUU = VisionRadiusUU_RenderThread;
		VisionVS.VisionPredictionSeconds = VisionPredictionSeconds_RenderThread;
		VisionVS.SceneProjectionPlaneZ = SceneProjectionPlaneZ_RenderThread;
		VisionVS.VisionSourcePositions = VisionSourcePositionBuffer.SRV;
		VisionPass->VSParameters = VisionVS;
		FMassBattleFrameFogVisionPS::FParameters VisionPS;
		VisionPS.VisibilityStateValue = 1.0f;
		VisionPass->PSParameters = VisionPS;

		TShaderMapRef<FMassBattleFrameFogVisionVS> VertexShader(GetGlobalShaderMap(View.GetFeatureLevel()));
		TShaderMapRef<FMassBattleFrameFogVisionPS> PixelShader(GetGlobalShaderMap(View.GetFeatureLevel()));
		RDG_EVENT_SCOPE_STAT(
			GraphBuilder,
			MassBattleFrameFogSceneVisualMask,
			"MassBattleFrameFog Scene Visual Circles");
		const uint32 InstanceCount = SourceCount_RenderThread;
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("MassBattleFrameFog.SceneVisualCircles(%u)", InstanceCount),
			VisionPass,
			ERDGPassFlags::Raster,
			[VisionPass, VertexShader, PixelShader, ViewRect, InstanceCount](FRDGAsyncTask, FRHICommandList& RHICmdList)
			{
				FGraphicsPipelineStateInitializer PSO;
				ConfigurePipeline(RHICmdList, PSO, VertexShader.GetVertexShader(), PixelShader.GetPixelShader());
				// Every circle writes the same binary visibility value. Overlap is
				// therefore an order-independent OR with blending disabled.
				PSO.BlendState = TStaticBlendState<CW_RED>::GetRHI();
				PSO.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
				SetGraphicsPipelineState(RHICmdList, PSO, 0);
				SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), VisionPass->VSParameters);
				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), VisionPass->PSParameters);
				RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);
				RHICmdList.SetScissorRect(true, ViewRect.Min.X, ViewRect.Min.Y, ViewRect.Max.X, ViewRect.Max.Y);
				RHICmdList.SetStreamSource(0, nullptr, 0);
				RHICmdList.DrawPrimitive(0, 2, InstanceCount);
				RHICmdList.SetScissorRect(false, 0, 0, 0, 0);
			});
	}
	else
	{
		AddClearRenderTargetPass(GraphBuilder, VisibilityMask, FLinearColor::Black);
	}

	FCompositePassParameters* CompositePass = GraphBuilder.AllocParameters<FCompositePassParameters>();
	CompositePass->RenderTargets[0] = Output.GetRenderTargetBinding();
	FMassBattleFrameFogCompositeVS::FParameters CompositeVS;
	CompositeVS.OutputExtent = FVector2f(Extent.X, Extent.Y);
	CompositeVS.ViewRectMin = FVector2f(ViewRect.Min.X, ViewRect.Min.Y);
	CompositeVS.ViewRectSize = FVector2f(ViewRect.Width(), ViewRect.Height());
	FMassBattleFrameFogCompositePS::FParameters CompositePS;
	CompositePS.OutputExtent = FVector2f(Extent.X, Extent.Y);
	CompositePS.FogOpacity = FogOpacity_RenderThread;
	CompositePS.Debug = bDebug_RenderThread ? 1u : 0u;
	CompositePS.DebugRevealAll = 0u;
	CompositePS.SceneColor = SceneColor.Texture;
	CompositePS.VisibilityMask = VisibilityMask;
	CompositePS.LinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	CompositePass->VSParameters = CompositeVS;
	CompositePass->PSParameters = CompositePS;

	TShaderMapRef<FMassBattleFrameFogCompositeVS> VertexShader(GetGlobalShaderMap(View.GetFeatureLevel()));
	TShaderMapRef<FMassBattleFrameFogCompositePS> PixelShader(GetGlobalShaderMap(View.GetFeatureLevel()));
	RDG_EVENT_SCOPE_STAT(GraphBuilder, MassBattleFrameFogComposite, "MassBattleFrameFog Composite");
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("MassBattleFrameFog.Composite"),
		CompositePass,
		ERDGPassFlags::Raster,
		[CompositePass, VertexShader, PixelShader, ViewRect](FRDGAsyncTask, FRHICommandList& RHICmdList)
		{
			FGraphicsPipelineStateInitializer PSO;
			ConfigurePipeline(RHICmdList, PSO, VertexShader.GetVertexShader(), PixelShader.GetPixelShader());
			PSO.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero>::GetRHI();
			PSO.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			SetGraphicsPipelineState(RHICmdList, PSO, 0);
			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), CompositePass->VSParameters);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), CompositePass->PSParameters);
			RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);
			RHICmdList.SetScissorRect(true, ViewRect.Min.X, ViewRect.Min.Y, ViewRect.Max.X, ViewRect.Max.Y);
			RHICmdList.SetStreamSource(0, nullptr, 0);
			RHICmdList.DrawPrimitive(0, 2, 1);
			RHICmdList.SetScissorRect(false, 0, 0, 0, 0);
		});

	return MoveTemp(Output);
}
