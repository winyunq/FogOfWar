// Copyright Winyunq, 2025. All Rights Reserved.

#include "MassBattleFrameFogSceneViewExtension.h"

#include "GlobalShader.h"
#include "PipelineStateCache.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "RHICommandList.h"
#include "RHIStaticStates.h"
#include "SceneView.h"
#include "ShaderParameterStruct.h"
#include "PostProcess/PostProcessMaterialInputs.h"

DEFINE_LOG_CATEGORY_STATIC(LogMassBattleFrameFogScene, Log, All);
DECLARE_GPU_STAT_NAMED(MassBattleFrameFogVisionMask, TEXT("MassBattleFrameFog Vision Mask"));
DECLARE_GPU_STAT_NAMED(MassBattleFrameFogComposite, TEXT("MassBattleFrameFog Composite"));

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
			SHADER_PARAMETER(uint32, ViewingTeamIndex)
			SHADER_PARAMETER(uint32, DebugRevealAll)
			SHADER_PARAMETER_SRV(Buffer<uint>, LocationWords)
			SHADER_PARAMETER_SRV(Buffer<float4>, DynamicParams0)
			SHADER_PARAMETER_SRV(Buffer<uint>, IsHidden)
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
	IMPLEMENT_GLOBAL_SHADER(FMassBattleFrameFogCompositeVS, "/Plugin/FogOfWar/Private/MassBattleFrameFogScene.usf", "CompositeVS", SF_Vertex);
	IMPLEMENT_GLOBAL_SHADER(FMassBattleFrameFogCompositePS, "/Plugin/FogOfWar/Private/MassBattleFrameFogScene.usf", "CompositePS", SF_Pixel);

	BEGIN_SHADER_PARAMETER_STRUCT(FVisionPassParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FMassBattleFrameFogVisionVS::FParameters, VSParameters)
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
		OutBuffer.Release();
		if (NumElements == 0 || SourceBytes == 0 || SourceData == nullptr)
		{
			return;
		}

		OutBuffer.InitializeWithData(
			RHICmdList,
			DebugName,
			BytesPerElement,
			NumElements,
			Format,
			BUF_Static,
			[SourceData, SourceBytes](FRHIBufferInitializer& Initializer)
			{
				Initializer.WriteData(SourceData, SourceBytes);
			});
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

FMassBattleFrameFogSceneViewExtension::~FMassBattleFrameFogSceneViewExtension()
{
}

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

void FMassBattleFrameFogSceneViewExtension::Upload_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	const FMassBattleFrameFogSceneUploadData& UploadData)
{
	check(IsInRenderingThread());
	static_assert(sizeof(FVector) == sizeof(uint32) * 6, "Scene fog shader expects UE5 double FVector storage.");
	static_assert(sizeof(FVector4f) == sizeof(float) * 4, "Unexpected FVector4f storage.");
	static_assert(sizeof(bool) == sizeof(uint8), "Scene fog hidden buffer expects byte bool storage.");

	SourceCount_RenderThread = static_cast<uint32>(FMath::Min(
		UploadData.Locations.Num(),
		FMath::Min(UploadData.DynamicParams0.Num(), UploadData.IsHidden.Num())));
	InitializeMassBattleFrameFogReadBuffer(
		RHICmdList,
		LocationWordsBuffer,
		TEXT("MassBattleFrameFog.LocationWords"),
		sizeof(uint32),
		static_cast<uint32>(UploadData.Locations.Num() * 6),
		PF_R32_UINT,
		UploadData.Locations.GetData(),
		static_cast<uint32>(UploadData.Locations.Num() * sizeof(FVector)));
	InitializeMassBattleFrameFogReadBuffer(
		RHICmdList,
		DynamicParams0Buffer,
		TEXT("MassBattleFrameFog.DynamicParams0"),
		sizeof(FVector4f),
		static_cast<uint32>(UploadData.DynamicParams0.Num()),
		PF_A32B32G32R32F,
		UploadData.DynamicParams0.GetData(),
		static_cast<uint32>(UploadData.DynamicParams0.Num() * sizeof(FVector4f)));
	InitializeMassBattleFrameFogReadBuffer(
		RHICmdList,
		IsHiddenBuffer,
		TEXT("MassBattleFrameFog.IsHidden"),
		sizeof(uint8),
		static_cast<uint32>(UploadData.IsHidden.Num()),
		PF_R8_UINT,
		UploadData.IsHidden.GetData(),
		static_cast<uint32>(UploadData.IsHidden.Num() * sizeof(bool)));

	VisionRadiusUU_RenderThread = FMath::Max(0.0f, UploadData.VisionRadiusUU);
	FogOpacity_RenderThread = FMath::Clamp(UploadData.FogOpacity, 0.0f, 1.0f);
	ViewingTeamIndex_RenderThread = FMath::Min(UploadData.ViewingTeamIndex, 1023u);
	bEnabled_RenderThread = UploadData.bEnabled;
	bDebug_RenderThread = UploadData.bDebug;
	bDebugRevealAll_RenderThread = UploadData.bDebugRevealAll;
}

void FMassBattleFrameFogSceneViewExtension::Release_RenderThread()
{
	check(IsInRenderingThread());
	LocationWordsBuffer.Release();
	DynamicParams0Buffer.Release();
	IsHiddenBuffer.Release();
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
	if (!SceneColorSlice.IsValid())
	{
		return FScreenPassTexture(SceneColorSlice);
	}

	const FScreenPassTexture SceneColor(SceneColorSlice);
	FScreenPassRenderTarget Output = Inputs.OverrideOutput;
	if (!Output.IsValid())
	{
		// The composite pass samples SceneColor, so it must not render into the
		// same texture when the post-process stack did not provide an override.
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
		PF_R8,
		FClearValueBinding::Black,
		TexCreate_RenderTargetable | TexCreate_ShaderResource);
	FRDGTextureRef VisibilityMask = GraphBuilder.CreateTexture(MaskDesc, TEXT("MassBattleFrameFog.VisibilityMask"));

	const FIntRect ViewRect = SceneColor.ViewRect;
	const uint32 SourceCount = SourceCount_RenderThread;
	const bool bCanDrawVision = SourceCount > 0
		&& LocationWordsBuffer.SRV.IsValid()
		&& DynamicParams0Buffer.SRV.IsValid()
		&& IsHiddenBuffer.SRV.IsValid();
	if (bCanDrawVision)
	{
		FVisionPassParameters* VisionPass = GraphBuilder.AllocParameters<FVisionPassParameters>();
		VisionPass->RenderTargets[0] = FRenderTargetBinding(VisibilityMask, ERenderTargetLoadAction::EClear);
		FMassBattleFrameFogVisionVS::FParameters VisionVS;
		VisionVS.View = View.ViewUniformBuffer;
		VisionVS.VisionRadiusUU = VisionRadiusUU_RenderThread;
		VisionVS.ViewingTeamIndex = ViewingTeamIndex_RenderThread;
		VisionVS.DebugRevealAll = bDebugRevealAll_RenderThread ? 1u : 0u;
		VisionVS.LocationWords = LocationWordsBuffer.SRV;
		VisionVS.DynamicParams0 = DynamicParams0Buffer.SRV;
		VisionVS.IsHidden = IsHiddenBuffer.SRV;
		VisionPass->VSParameters = VisionVS;

		TShaderMapRef<FMassBattleFrameFogVisionVS> VisionVertexShader(GetGlobalShaderMap(View.GetFeatureLevel()));
		TShaderMapRef<FMassBattleFrameFogVisionPS> VisionPixelShader(GetGlobalShaderMap(View.GetFeatureLevel()));
		RDG_EVENT_SCOPE_STAT(GraphBuilder, MassBattleFrameFogVisionMask, "MassBattleFrameFog VisionMask");
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("MassBattleFrameFog.VisionMask(%u)", SourceCount),
			VisionPass,
			ERDGPassFlags::Raster,
			[VisionPass, VisionVertexShader, VisionPixelShader, ViewRect, SourceCount](FRDGAsyncTask, FRHICommandList& RHICmdList)
			{
				FGraphicsPipelineStateInitializer PSO;
				ConfigurePipeline(RHICmdList, PSO, VisionVertexShader.GetVertexShader(), VisionPixelShader.GetPixelShader());
				PSO.BlendState = TStaticBlendState<CW_RED, BO_Add, BF_One, BF_One>::GetRHI();
				PSO.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
				SetGraphicsPipelineState(RHICmdList, PSO, 0);
				SetShaderParameters(RHICmdList, VisionVertexShader, VisionVertexShader.GetVertexShader(), VisionPass->VSParameters);
				RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);
				RHICmdList.SetScissorRect(true, ViewRect.Min.X, ViewRect.Min.Y, ViewRect.Max.X, ViewRect.Max.Y);
				RHICmdList.SetStreamSource(0, nullptr, 0);
				RHICmdList.DrawPrimitive(0, 2, SourceCount);
				RHICmdList.SetScissorRect(false, 0, 0, 0, 0);
			});
	}
	else
	{
		// An empty source list still needs to produce the mask resource.  The
		// composite pass intentionally treats this as full fog.
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
	CompositePS.SceneColor = SceneColor.Texture;
	CompositePS.VisibilityMask = VisibilityMask;
	CompositePS.LinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	CompositePass->VSParameters = CompositeVS;
	CompositePass->PSParameters = CompositePS;

	TShaderMapRef<FMassBattleFrameFogCompositeVS> CompositeVertexShader(GetGlobalShaderMap(View.GetFeatureLevel()));
	TShaderMapRef<FMassBattleFrameFogCompositePS> CompositePixelShader(GetGlobalShaderMap(View.GetFeatureLevel()));
	RDG_EVENT_SCOPE_STAT(GraphBuilder, MassBattleFrameFogComposite, "MassBattleFrameFog Composite");
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("MassBattleFrameFog.Composite"),
		CompositePass,
		ERDGPassFlags::Raster,
		[CompositePass, CompositeVertexShader, CompositePixelShader, ViewRect](FRDGAsyncTask, FRHICommandList& RHICmdList)
		{
			FGraphicsPipelineStateInitializer PSO;
			ConfigurePipeline(RHICmdList, PSO, CompositeVertexShader.GetVertexShader(), CompositePixelShader.GetPixelShader());
			PSO.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero>::GetRHI();
			PSO.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			SetGraphicsPipelineState(RHICmdList, PSO, 0);
			SetShaderParameters(RHICmdList, CompositeVertexShader, CompositeVertexShader.GetVertexShader(), CompositePass->VSParameters);
			SetShaderParameters(RHICmdList, CompositePixelShader, CompositePixelShader.GetPixelShader(), CompositePass->PSParameters);
			RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);
			RHICmdList.SetScissorRect(true, ViewRect.Min.X, ViewRect.Min.Y, ViewRect.Max.X, ViewRect.Max.Y);
			RHICmdList.SetStreamSource(0, nullptr, 0);
			RHICmdList.DrawPrimitive(0, 2, 1);
			RHICmdList.SetScissorRect(false, 0, 0, 0, 0);
		});

	return MoveTemp(Output);
}
