// Copyright Winyunq, 2025. All Rights Reserved.

#include "UI/MassBattleFrameMinimapSlate.h"

#include "CommonRenderResources.h"
#include "GlobalShader.h"
#include "HAL/PlatformTime.h"
#include "PipelineStateCache.h"
#include "DynamicRHI.h"
#include "RHICommandList.h"
#include "RHIGlobals.h"
#include "RHIStaticStates.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "Rendering/DrawElements.h"
#include "ShaderParameterStruct.h"

namespace
{
	class FMassBattleMinimapUnitVS final : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FMassBattleMinimapUnitVS);
		SHADER_USE_PARAMETER_STRUCT(FMassBattleMinimapUnitVS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(FVector2f, RectMin)
			SHADER_PARAMETER(FVector2f, RectSize)
			SHADER_PARAMETER(FVector2f, OutputExtent)
			SHADER_PARAMETER(FVector2f, MapMin)
			SHADER_PARAMETER(FVector2f, MapSize)
			SHADER_PARAMETER(uint32, LogicalResolution)
			SHADER_PARAMETER(float, UnitRadiusUU)
			SHADER_PARAMETER(float, UnitBrightness)
			SHADER_PARAMETER(uint32, TeamColorCount)
			SHADER_PARAMETER_SRV(Buffer<float4>, UnitData)
			SHADER_PARAMETER_SRV(Buffer<float4>, TeamColors)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};

	class FMassBattleMinimapUnitPS final : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FMassBattleMinimapUnitPS);
		FMassBattleMinimapUnitPS() = default;
		FMassBattleMinimapUnitPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
			: FGlobalShader(Initializer)
		{
		}

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};

	class FMassBattleMinimapVisionVS final : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FMassBattleMinimapVisionVS);
		SHADER_USE_PARAMETER_STRUCT(FMassBattleMinimapVisionVS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(FVector2f, RectMin)
			SHADER_PARAMETER(FVector2f, RectSize)
			SHADER_PARAMETER(FVector2f, OutputExtent)
			SHADER_PARAMETER(FVector2f, MapMin)
			SHADER_PARAMETER(FVector2f, MapSize)
			SHADER_PARAMETER(uint32, LogicalResolution)
			SHADER_PARAMETER(float, VisionRadiusUU)
			SHADER_PARAMETER_SRV(Buffer<float4>, VisionSources)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};

	class FMassBattleMinimapVisionPS final : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FMassBattleMinimapVisionPS);
		FMassBattleMinimapVisionPS() = default;
		FMassBattleMinimapVisionPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
			: FGlobalShader(Initializer)
		{
		}

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};

	class FMassBattleMinimapFogVS final : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FMassBattleMinimapFogVS);
		SHADER_USE_PARAMETER_STRUCT(FMassBattleMinimapFogVS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(FVector2f, RectMin)
			SHADER_PARAMETER(FVector2f, RectSize)
			SHADER_PARAMETER(FVector2f, OutputExtent)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};

	class FMassBattleMinimapFogPS final : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FMassBattleMinimapFogPS);
		SHADER_USE_PARAMETER_STRUCT(FMassBattleMinimapFogPS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(float, FogOpacity)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FMassBattleMinimapUnitVS, "/Plugin/FogOfWar/Private/MassBattleMinimap.usf", "UnitVS", SF_Vertex);
	IMPLEMENT_GLOBAL_SHADER(FMassBattleMinimapUnitPS, "/Plugin/FogOfWar/Private/MassBattleMinimap.usf", "UnitPS", SF_Pixel);
	IMPLEMENT_GLOBAL_SHADER(FMassBattleMinimapVisionVS, "/Plugin/FogOfWar/Private/MassBattleMinimap.usf", "VisionVS", SF_Vertex);
	IMPLEMENT_GLOBAL_SHADER(FMassBattleMinimapVisionPS, "/Plugin/FogOfWar/Private/MassBattleMinimap.usf", "VisionPS", SF_Pixel);
	IMPLEMENT_GLOBAL_SHADER(FMassBattleMinimapFogVS, "/Plugin/FogOfWar/Private/MassBattleMinimap.usf", "FogVS", SF_Vertex);
	IMPLEMENT_GLOBAL_SHADER(FMassBattleMinimapFogPS, "/Plugin/FogOfWar/Private/MassBattleMinimap.usf", "FogPS", SF_Pixel);

	BEGIN_SHADER_PARAMETER_STRUCT(FMassBattleMinimapPassParameters, )
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	void InitializeReadBuffer(
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
			OutBuffer.Initialize(
				RHICmdList,
				DebugName,
				BytesPerElement,
				FMath::RoundUpToPowerOfTwo(NumElements),
				Format,
				BUF_Dynamic);
		}

		void* Destination = RHICmdList.LockBuffer(OutBuffer.Buffer, 0, SourceBytes, RLM_WriteOnly);
		FMemory::Memcpy(Destination, SourceData, SourceBytes);
		RHICmdList.UnlockBuffer(OutBuffer.Buffer);
	}

	FIntRect GetWidgetRect(
		const FPaintGeometry& PaintGeometry,
		const ICustomSlateElement::FDrawPassInputs& Inputs,
		const FIntPoint OutputExtent)
	{
		const FSlateRenderTransform& Transform = PaintGeometry.GetAccumulatedRenderTransform();
		const FVector2f LocalSize(PaintGeometry.GetLocalSize());
		const FVector2f P0 = TransformPoint(Transform, FVector2f(0.0f, 0.0f)) + Inputs.ElementsOffset;
		const FVector2f P1 = TransformPoint(Transform, FVector2f(LocalSize.X, 0.0f)) + Inputs.ElementsOffset;
		const FVector2f P2 = TransformPoint(Transform, FVector2f(0.0f, LocalSize.Y)) + Inputs.ElementsOffset;
		const FVector2f P3 = TransformPoint(Transform, LocalSize) + Inputs.ElementsOffset;

		const float MinX = FMath::Min(FMath::Min(P0.X, P1.X), FMath::Min(P2.X, P3.X));
		const float MinY = FMath::Min(FMath::Min(P0.Y, P1.Y), FMath::Min(P2.Y, P3.Y));
		const float MaxX = FMath::Max(FMath::Max(P0.X, P1.X), FMath::Max(P2.X, P3.X));
		const float MaxY = FMath::Max(FMath::Max(P0.Y, P1.Y), FMath::Max(P2.Y, P3.Y));

		FIntRect Rect(
			FMath::FloorToInt(MinX),
			FMath::FloorToInt(MinY),
			FMath::CeilToInt(MaxX),
			FMath::CeilToInt(MaxY));
		// Paint geometry is in Slate output space. SceneViewRect describes the scene
		// viewport and can be empty or use a different origin for a UI-only pass.
		Rect.Min.X = FMath::Clamp(Rect.Min.X, 0, OutputExtent.X);
		Rect.Min.Y = FMath::Clamp(Rect.Min.Y, 0, OutputExtent.Y);
		Rect.Max.X = FMath::Clamp(Rect.Max.X, 0, OutputExtent.X);
		Rect.Max.Y = FMath::Clamp(Rect.Max.Y, 0, OutputExtent.Y);
		return Rect;
	}

	void ConfigureCommonPipeline(
		FRHICommandList& RHICmdList,
		FGraphicsPipelineStateInitializer& GraphicsPSOInit,
		FRHIVertexShader* VertexShader,
		FRHIPixelShader* PixelShader)
	{
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader;
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader;
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
	}

	class FMassBattleMinimapSlateElement final
		: public ICustomSlateElement
		, public TSharedFromThis<FMassBattleMinimapSlateElement, ESPMode::ThreadSafe>
	{
	public:
		explicit FMassBattleMinimapSlateElement(
			TSharedPtr<FMassBattleMinimapRenderData, ESPMode::ThreadSafe> InRenderData)
			: RenderData(MoveTemp(InRenderData))
		{
		}

		void SetPaintGeometry_GameThread(const FPaintGeometry& InPaintGeometry)
		{
			check(IsInGameThread());
			TWeakPtr<FMassBattleMinimapSlateElement, ESPMode::ThreadSafe> WeakSelf = AsShared();
			ENQUEUE_RENDER_COMMAND(FUpdateMassBattleMinimapPaintGeometry)(
				[WeakSelf, InPaintGeometry](FRHICommandListImmediate& RHICmdList)
				{
					if (TSharedPtr<FMassBattleMinimapSlateElement, ESPMode::ThreadSafe> Self = WeakSelf.Pin())
					{
						Self->PaintGeometry_RenderThread = InPaintGeometry;
					}
				});
		}

		virtual void Draw_RenderThread(FRDGBuilder& GraphBuilder, const FDrawPassInputs& Inputs) override
		{
			if (RenderData.IsValid())
			{
				RenderData->Draw_RenderThread(GraphBuilder, Inputs, PaintGeometry_RenderThread);
			}
		}

	private:
		FPaintGeometry PaintGeometry_RenderThread;
		TSharedPtr<FMassBattleMinimapRenderData, ESPMode::ThreadSafe> RenderData;
	};
}

FMassBattleMinimapRenderData::FMassBattleMinimapRenderData() = default;
FMassBattleMinimapRenderData::~FMassBattleMinimapRenderData() = default;

void FMassBattleMinimapRenderData::Upload_GameThread(FMassBattleMinimapUploadData&& UploadData)
{
	check(IsInGameThread());
	TSharedRef<FMassBattleMinimapRenderData, ESPMode::ThreadSafe> Self = AsShared();
	ENQUEUE_RENDER_COMMAND(FUploadMassBattleMinimapData)(
		[Self, Data = MoveTemp(UploadData)](FRHICommandListImmediate& RHICmdList) mutable
		{
			Self->Upload_RenderThread(RHICmdList, Data);
		});
}

void FMassBattleMinimapRenderData::Release_GameThread()
{
	check(IsInGameThread());
	TSharedRef<FMassBattleMinimapRenderData, ESPMode::ThreadSafe> Self = AsShared();
	ENQUEUE_RENDER_COMMAND(FReleaseMassBattleMinimapData)(
		[Self](FRHICommandListImmediate& RHICmdList)
		{
			Self->Release_RenderThread();
		});
}

void FMassBattleMinimapRenderData::Upload_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	const FMassBattleMinimapUploadData& UploadData)
{
	check(IsInRenderingThread());
	static_assert(sizeof(FVector4f) == sizeof(float) * 4, "Unexpected FVector4f storage.");
	static_assert(sizeof(FLinearColor) == sizeof(float) * 4, "Unexpected FLinearColor storage.");

	const int32 AvailableSourceCount = UploadData.Units.Num();
	AgentCount_RenderThread = static_cast<uint32>(FMath::Clamp(UploadData.UnitCount, 0, AvailableSourceCount));
	VisionSourceCount_RenderThread = static_cast<uint32>(FMath::Clamp(UploadData.VisionSourceCount, 0, AvailableSourceCount));
	FogVisibleCount_RenderThread = static_cast<uint32>(UploadData.FogVisibleMarkers.Num());
	TeamColorCount_RenderThread = static_cast<uint32>(UploadData.TeamColors.Num());

	InitializeReadBuffer(
		RHICmdList,
		UnitDataBuffer,
		TEXT("MassBattleMinimap.UnitData"),
		sizeof(FVector4f),
		static_cast<uint32>(UploadData.Units.Num()),
		PF_A32B32G32R32F,
		UploadData.Units.GetData(),
		static_cast<uint32>(UploadData.Units.Num() * sizeof(FVector4f)));
	InitializeReadBuffer(
		RHICmdList,
		FogVisibleBuffer,
		TEXT("MassBattleMinimap.FogVisible"),
		sizeof(FVector4f),
		FogVisibleCount_RenderThread,
		PF_A32B32G32R32F,
		UploadData.FogVisibleMarkers.GetData(),
		static_cast<uint32>(UploadData.FogVisibleMarkers.Num() * sizeof(FVector4f)));
	InitializeReadBuffer(
		RHICmdList,
		TeamColorsBuffer,
		TEXT("MassBattleMinimap.TeamColors"),
		sizeof(FLinearColor),
		TeamColorCount_RenderThread,
		PF_A32B32G32R32F,
		UploadData.TeamColors.GetData(),
		static_cast<uint32>(UploadData.TeamColors.Num() * sizeof(FLinearColor)));

	MapMin_RenderThread = UploadData.MapMin;
	MapSize_RenderThread = UploadData.MapSize.ComponentMax(FVector2f(1.0f, 1.0f));
	LogicalResolution_RenderThread = static_cast<uint32>(FMath::Max(UploadData.LogicalResolution, 1));
	VisionRadiusUU_RenderThread = FMath::Max(UploadData.VisionRadiusUU, 0.0f);
	UnitRadiusUU_RenderThread = FMath::Max(UploadData.UnitRadiusUU, 0.0f);
	FogOpacity_RenderThread = FMath::Clamp(UploadData.FogOpacity, 0.0f, 1.0f);
}

void FMassBattleMinimapRenderData::Release_RenderThread()
{
	check(IsInRenderingThread());
	AgentCount_RenderThread = 0;
	VisionSourceCount_RenderThread = 0;
	FogVisibleCount_RenderThread = 0;
	TeamColorCount_RenderThread = 0;
	UnitDataBuffer.Release();
	FogVisibleBuffer.Release();
	TeamColorsBuffer.Release();
}

void FMassBattleMinimapRenderData::Draw_RenderThread(
	FRDGBuilder& GraphBuilder,
	const ICustomSlateElement::FDrawPassInputs& Inputs,
	const FPaintGeometry& PaintGeometry)
{
	check(IsInRenderingThread());
	if (Inputs.OutputTexture == nullptr)
	{
		return;
	}

	const FIntPoint OutputExtent = Inputs.OutputTexture->Desc.Extent;
	const FIntRect WidgetRect = GetWidgetRect(PaintGeometry, Inputs, OutputExtent);
	if (WidgetRect.Width() <= 0 || WidgetRect.Height() <= 0)
	{
		static bool bLoggedEmptyRect = false;
		if (!bLoggedEmptyRect)
		{
			bLoggedEmptyRect = true;
			UE_LOG(LogTemp, Error, TEXT("MassBattleMinimap: empty Slate draw rect; Output=%dx%d SceneRect=(%d,%d)-(%d,%d)"),
				OutputExtent.X, OutputExtent.Y,
				Inputs.SceneViewRect.Min.X, Inputs.SceneViewRect.Min.Y,
				Inputs.SceneViewRect.Max.X, Inputs.SceneViewRect.Max.Y);
		}
		return;
	}

	static bool bLoggedFirstDraw = false;
	if (!bLoggedFirstDraw)
	{
		bLoggedFirstDraw = true;
		UE_LOG(LogTemp, Display, TEXT("MassBattleMinimap: Slate GPU draw active; Rect=(%d,%d)-(%d,%d) Output=%dx%d Agents=%u"),
			WidgetRect.Min.X, WidgetRect.Min.Y, WidgetRect.Max.X, WidgetRect.Max.Y,
			OutputExtent.X, OutputExtent.Y, AgentCount_RenderThread);
	}
	const FVector2f RectMin(static_cast<float>(WidgetRect.Min.X), static_cast<float>(WidgetRect.Min.Y));
	const FVector2f RectSize(static_cast<float>(WidgetRect.Width()), static_cast<float>(WidgetRect.Height()));
	const FVector2f OutputExtentF(static_cast<float>(OutputExtent.X), static_cast<float>(OutputExtent.Y));
	const bool bCanDrawUnits = AgentCount_RenderThread > 0
		&& TeamColorCount_RenderThread > 0
		&& UnitDataBuffer.SRV.IsValid()
		&& TeamColorsBuffer.SRV.IsValid();

	FRDGTextureDesc StencilDesc = FRDGTextureDesc::Create2D(
		OutputExtent,
		PF_DepthStencil,
		FClearValueBinding::DepthFar,
		TexCreate_DepthStencilTargetable);
	StencilDesc.NumSamples = Inputs.OutputTexture->Desc.NumSamples;
	FRDGTextureRef VisibilityStencil = GraphBuilder.CreateTexture(StencilDesc, TEXT("MassBattleMinimap.VisibilityStencil"));

	// First raster pass: friendly/allied sources are a compact prefix selected
	// inside the existing MBF render traversal at the 3 Hz snapshot cadence.
	{
		FMassBattleMinimapPassParameters* PassParameters = GraphBuilder.AllocParameters<FMassBattleMinimapPassParameters>();
		PassParameters->RenderTargets[0] = FRenderTargetBinding(Inputs.OutputTexture, ERenderTargetLoadAction::ELoad);
		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
			VisibilityStencil,
			ERenderTargetLoadAction::ENoAction,
			ERenderTargetLoadAction::EClear,
			FExclusiveDepthStencil::DepthNop_StencilWrite);

		FMassBattleMinimapVisionVS::FParameters VSParameters;
		VSParameters.RectMin = RectMin;
		VSParameters.RectSize = RectSize;
		VSParameters.OutputExtent = OutputExtentF;
		VSParameters.MapMin = MapMin_RenderThread;
		VSParameters.MapSize = MapSize_RenderThread;
		VSParameters.LogicalResolution = LogicalResolution_RenderThread;
		VSParameters.VisionRadiusUU = VisionRadiusUU_RenderThread;
		VSParameters.VisionSources = UnitDataBuffer.SRV;

		TShaderMapRef<FMassBattleMinimapVisionVS> VertexShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FMassBattleMinimapVisionPS> PixelShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		const uint32 VisionSourceCount = VisionSourceCount_RenderThread;
		const bool bCanDrawVision = VisionSourceCount > 0
			&& UnitDataBuffer.SRV.IsValid();

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("MassBattleMinimap.VisionStencil(%u)", bCanDrawVision ? VisionSourceCount : 0),
			PassParameters,
			ERDGPassFlags::Raster,
			[PassParameters, VSParameters, VertexShader, PixelShader, VisionSourceCount, bCanDrawVision, WidgetRect, OutputExtent](FRDGAsyncTask, FRHICommandList& RHICmdList)
			{
				if (!bCanDrawVision)
				{
					return;
				}

				RHICmdList.SetViewport(0.0f, 0.0f, 0.0f, OutputExtent.X, OutputExtent.Y, 1.0f);
				RHICmdList.SetScissorRect(true, WidgetRect.Min.X, WidgetRect.Min.Y, WidgetRect.Max.X, WidgetRect.Max.Y);

				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				ConfigureCommonPipeline(RHICmdList, GraphicsPSOInit, VertexShader.GetVertexShader(), PixelShader.GetPixelShader());
				GraphicsPSOInit.BlendState = TStaticBlendState<CW_NONE>::GetRHI();
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<
					false, CF_Always,
					true, CF_Always, SO_Keep, SO_Keep, SO_Replace,
					true, CF_Always, SO_Keep, SO_Keep, SO_Replace>::GetRHI();
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 1);
				SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), VSParameters);
				RHICmdList.SetStreamSource(0, nullptr, 0);
				RHICmdList.DrawPrimitive(0, 2, VisionSourceCount);
				RHICmdList.SetScissorRect(false, 0, 0, 0, 0);
			});
	}

	// Next pass: darken only stencil 0. Units have not been drawn yet, so fog
	// can never leak or merely dim an enemy marker.
	{
		FMassBattleMinimapPassParameters* PassParameters = GraphBuilder.AllocParameters<FMassBattleMinimapPassParameters>();
		PassParameters->RenderTargets[0] = FRenderTargetBinding(Inputs.OutputTexture, ERenderTargetLoadAction::ELoad);
		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
			VisibilityStencil,
			ERenderTargetLoadAction::ENoAction,
			ERenderTargetLoadAction::ELoad,
			FExclusiveDepthStencil::DepthNop_StencilRead);

		FMassBattleMinimapFogVS::FParameters VSParameters;
		VSParameters.RectMin = RectMin;
		VSParameters.RectSize = RectSize;
		VSParameters.OutputExtent = OutputExtentF;
		FMassBattleMinimapFogPS::FParameters PSParameters;
		PSParameters.FogOpacity = FogOpacity_RenderThread;

		TShaderMapRef<FMassBattleMinimapFogVS> VertexShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FMassBattleMinimapFogPS> PixelShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("MassBattleMinimap.Fog"),
			PassParameters,
			ERDGPassFlags::Raster,
			[PassParameters, VSParameters, PSParameters, VertexShader, PixelShader, WidgetRect, OutputExtent](FRDGAsyncTask, FRHICommandList& RHICmdList)
			{
				RHICmdList.SetViewport(0.0f, 0.0f, 0.0f, OutputExtent.X, OutputExtent.Y, 1.0f);
				RHICmdList.SetScissorRect(true, WidgetRect.Min.X, WidgetRect.Min.Y, WidgetRect.Max.X, WidgetRect.Max.Y);

				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				ConfigureCommonPipeline(RHICmdList, GraphicsPSOInit, VertexShader.GetVertexShader(), PixelShader.GetPixelShader());
				GraphicsPSOInit.BlendState = TStaticBlendState<
					CW_RGBA,
					BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha,
					BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<
					false, CF_Always,
					true, CF_Equal, SO_Keep, SO_Keep, SO_Keep,
					true, CF_Equal, SO_Keep, SO_Keep, SO_Keep>::GetRHI();
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);
				SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), VSParameters);
				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PSParameters);
				RHICmdList.SetStreamSource(0, nullptr, 0);
				RHICmdList.DrawPrimitive(0, 2, 1);
				RHICmdList.SetScissorRect(false, 0, 0, 0, 0);
			});
	}

	// Fog-visible policies are unit-only markers. They are deliberately drawn
	// after the fog pass without touching the visibility stencil, so attacks,
	// permanent units, and remembered buildings never grant terrain vision. A
	// true-vision normal marker below overwrites the dark marker at the same spot.
	if (FogVisibleCount_RenderThread > 0
		&& FogVisibleBuffer.SRV.IsValid()
		&& TeamColorCount_RenderThread > 0
		&& TeamColorsBuffer.SRV.IsValid())
	{
		FMassBattleMinimapPassParameters* PassParameters = GraphBuilder.AllocParameters<FMassBattleMinimapPassParameters>();
		PassParameters->RenderTargets[0] = FRenderTargetBinding(Inputs.OutputTexture, ERenderTargetLoadAction::ELoad);

		FMassBattleMinimapUnitVS::FParameters VSParameters;
		VSParameters.RectMin = RectMin;
		VSParameters.RectSize = RectSize;
		VSParameters.OutputExtent = OutputExtentF;
		VSParameters.MapMin = MapMin_RenderThread;
		VSParameters.MapSize = MapSize_RenderThread;
		VSParameters.LogicalResolution = LogicalResolution_RenderThread;
		VSParameters.UnitRadiusUU = UnitRadiusUU_RenderThread;
		VSParameters.UnitBrightness = FMath::Clamp(1.0f - FogOpacity_RenderThread, 0.15f, 1.0f);
		VSParameters.TeamColorCount = TeamColorCount_RenderThread;
		VSParameters.UnitData = FogVisibleBuffer.SRV;
		VSParameters.TeamColors = TeamColorsBuffer.SRV;

		TShaderMapRef<FMassBattleMinimapUnitVS> VertexShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FMassBattleMinimapUnitPS> PixelShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		const uint32 FogVisibleCount = FogVisibleCount_RenderThread;
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("MassBattleMinimap.FogVisibleUnits(%u)", FogVisibleCount),
			PassParameters,
			ERDGPassFlags::Raster,
			[PassParameters, VSParameters, VertexShader, PixelShader, FogVisibleCount, WidgetRect, OutputExtent](FRDGAsyncTask, FRHICommandList& RHICmdList)
			{
				RHICmdList.SetViewport(0.0f, 0.0f, 0.0f, OutputExtent.X, OutputExtent.Y, 1.0f);
				RHICmdList.SetScissorRect(true, WidgetRect.Min.X, WidgetRect.Min.Y, WidgetRect.Max.X, WidgetRect.Max.Y);

				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				ConfigureCommonPipeline(RHICmdList, GraphicsPSOInit, VertexShader.GetVertexShader(), PixelShader.GetPixelShader());
				GraphicsPSOInit.BlendState = TStaticBlendState<
					CW_RGBA,
					BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha,
					BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);
				SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), VSParameters);
				RHICmdList.SetStreamSource(0, nullptr, 0);
				RHICmdList.DrawPrimitive(0, 2, FogVisibleCount);
				RHICmdList.SetScissorRect(false, 0, 0, 0, 0);
			});
	}

	// Final pass: render only stencil 1. This all-world 3 Hz snapshot is independent
	// of the camera-local Niagara submission set, so scene culling cannot erase map data.
	if (bCanDrawUnits)
	{
		FMassBattleMinimapPassParameters* PassParameters = GraphBuilder.AllocParameters<FMassBattleMinimapPassParameters>();
		PassParameters->RenderTargets[0] = FRenderTargetBinding(Inputs.OutputTexture, ERenderTargetLoadAction::ELoad);
		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
			VisibilityStencil,
			ERenderTargetLoadAction::ENoAction,
			ERenderTargetLoadAction::ELoad,
			FExclusiveDepthStencil::DepthNop_StencilRead);

		FMassBattleMinimapUnitVS::FParameters VSParameters;
		VSParameters.RectMin = RectMin;
		VSParameters.RectSize = RectSize;
		VSParameters.OutputExtent = OutputExtentF;
		VSParameters.MapMin = MapMin_RenderThread;
		VSParameters.MapSize = MapSize_RenderThread;
		VSParameters.LogicalResolution = LogicalResolution_RenderThread;
		VSParameters.UnitRadiusUU = UnitRadiusUU_RenderThread;
		VSParameters.UnitBrightness = 1.0f;
		VSParameters.TeamColorCount = TeamColorCount_RenderThread;
		VSParameters.UnitData = UnitDataBuffer.SRV;
		VSParameters.TeamColors = TeamColorsBuffer.SRV;

		TShaderMapRef<FMassBattleMinimapUnitVS> VertexShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FMassBattleMinimapUnitPS> PixelShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		const uint32 AgentCount = AgentCount_RenderThread;
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("MassBattleMinimap.VisibleUnits(%u)", AgentCount),
			PassParameters,
			ERDGPassFlags::Raster,
			[PassParameters, VSParameters, VertexShader, PixelShader, AgentCount, WidgetRect, OutputExtent](FRDGAsyncTask, FRHICommandList& RHICmdList)
			{
				RHICmdList.SetViewport(0.0f, 0.0f, 0.0f, OutputExtent.X, OutputExtent.Y, 1.0f);
				RHICmdList.SetScissorRect(true, WidgetRect.Min.X, WidgetRect.Min.Y, WidgetRect.Max.X, WidgetRect.Max.Y);

				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				ConfigureCommonPipeline(RHICmdList, GraphicsPSOInit, VertexShader.GetVertexShader(), PixelShader.GetPixelShader());
				GraphicsPSOInit.BlendState = TStaticBlendState<
					CW_RGBA,
					BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha,
					BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<
					false, CF_Always,
					true, CF_Equal, SO_Keep, SO_Keep, SO_Keep,
					true, CF_Equal, SO_Keep, SO_Keep, SO_Keep>::GetRHI();
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 1);
				SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), VSParameters);
				RHICmdList.SetStreamSource(0, nullptr, 0);
				RHICmdList.DrawPrimitive(0, 2, AgentCount);
				RHICmdList.SetScissorRect(false, 0, 0, 0, 0);
			});
	}
}

SMassBattleFrameMinimap::~SMassBattleFrameMinimap()
{
	// Slate render batches keep a raw custom-drawer pointer after batching. Defer the
	// final strong-reference release until earlier render commands have drained.
	TSharedPtr<ICustomSlateElement, ESPMode::ThreadSafe> DrawerToRelease = MoveTemp(CustomDrawer);
	ENQUEUE_RENDER_COMMAND(FReleaseMassBattleMinimapSlateDrawer)(
		[DrawerToRelease = MoveTemp(DrawerToRelease)](FRHICommandListImmediate& RHICmdList) mutable
		{
			DrawerToRelease.Reset();
		});
}

void SMassBattleFrameMinimap::Construct(const FArguments& InArgs)
{
	RenderData = InArgs._RenderData;
	CustomDrawer = MakeShared<FMassBattleMinimapSlateElement, ESPMode::ThreadSafe>(RenderData);
}

int32 SMassBattleFrameMinimap::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	const int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	const bool bParentEnabled) const
{
	if (RenderData.IsValid() && AllottedGeometry.GetLocalSize().X > 0.0f && AllottedGeometry.GetLocalSize().Y > 0.0f)
	{
		static bool bLoggedFirstPaint = false;
		if (!bLoggedFirstPaint)
		{
			bLoggedFirstPaint = true;
			UE_LOG(LogTemp, Display, TEXT("MassBattleMinimap: Widget paint active; LocalSize=(%.1f,%.1f)"),
				AllottedGeometry.GetLocalSize().X, AllottedGeometry.GetLocalSize().Y);
		}
		FPaintGeometry PaintGeometry = AllottedGeometry.ToPaintGeometry();
		PaintGeometry.CommitTransformsIfUsingLegacyConstructor();
		StaticCastSharedPtr<FMassBattleMinimapSlateElement>(CustomDrawer)->SetPaintGeometry_GameThread(PaintGeometry);
		FSlateDrawElement::MakeCustom(
			OutDrawElements,
			LayerId,
			CustomDrawer);
	}
	return LayerId + 1;
}

FVector2D SMassBattleFrameMinimap::ComputeDesiredSize(const float LayoutScaleMultiplier) const
{
	return FVector2D::ZeroVector;
}
