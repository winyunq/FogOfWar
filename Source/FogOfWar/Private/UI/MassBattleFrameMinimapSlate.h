// Copyright Winyunq, 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHIUtilities.h"
#include "Rendering/RenderingCommon.h"
#include "Widgets/SLeafWidget.h"

struct FMassBattleMinimapUploadData
{
	/**
	 * Float XYZ plus a bit-cast uint in W: Team[0..9], Attacked[10], Selected[11],
	 * FullBrightness[12].
	 * Actual friendly/allied vision providers form a prefix.
	 */
	TArray<FVector4f> Units;
	/** Fog-visible unit-only markers: attacks, permanent units, and remembered buildings. */
	TArray<FVector4f> FogVisibleMarkers;
	FVector2f MapMin = FVector2f::ZeroVector;
	FVector2f MapSize = FVector2f(1.0f, 1.0f);
	int32 LogicalResolution = 256;
	float VisionRadiusUU = 4000.0f;
	float UnitRadiusUU = 100.0f;
	float FogOpacity = 0.3f;
	FLinearColor CombatUnitColor = FLinearColor::White;
	float NormalUnitColorLength = 0.58f;
	float SelectedUnitColorLength = 1.0f;
	bool bNormalizeTeamColorDirection = true;
	int32 UnitCount = 0;
	int32 VisionSourceCount = 0;
};

/** Persistent read-only GPU buffers. They are replaced only at the minimap update cadence. */
class FMassBattleMinimapRenderData final
	: public TSharedFromThis<FMassBattleMinimapRenderData, ESPMode::ThreadSafe>
{
public:
	FMassBattleMinimapRenderData();
	~FMassBattleMinimapRenderData();

	void Upload_GameThread(FMassBattleMinimapUploadData&& UploadData);
	/** Event-driven color upload; unit/fog snapshot uploads never replace this buffer. */
	void UploadTeamColors_GameThread(TConstArrayView<FLinearColor> InTeamColors);
	void Release_GameThread();
	FRHIShaderResourceView* GetTeamColorsSRV_RenderThread() const { return TeamColorsBuffer.SRV; }
	uint32 GetTeamColorCount_RenderThread() const { return TeamColorCount_RenderThread; }
	void Draw_RenderThread(
		FRDGBuilder& GraphBuilder,
		const ICustomSlateElement::FDrawPassInputs& Inputs,
		const FPaintGeometry& PaintGeometry);

private:
	void Upload_RenderThread(FRHICommandListImmediate& RHICmdList, const FMassBattleMinimapUploadData& UploadData);
	void UploadTeamColors_RenderThread(FRHICommandListImmediate& RHICmdList, TArray<FLinearColor>&& InTeamColors);
	void Release_RenderThread();

	FReadBuffer UnitDataBuffer;
	FReadBuffer FogVisibleBuffer;
	FReadBuffer TeamColorsBuffer;

	FVector2f MapMin_RenderThread = FVector2f::ZeroVector;
	FVector2f MapSize_RenderThread = FVector2f(1.0f, 1.0f);
	uint32 LogicalResolution_RenderThread = 256;
	float VisionRadiusUU_RenderThread = 4000.0f;
	float UnitRadiusUU_RenderThread = 100.0f;
	float FogOpacity_RenderThread = 0.3f;
	FLinearColor CombatUnitColor_RenderThread = FLinearColor::White;
	float NormalUnitColorLength_RenderThread = 0.58f;
	float SelectedUnitColorLength_RenderThread = 1.0f;
	bool bNormalizeTeamColorDirection_RenderThread = true;
	uint32 AgentCount_RenderThread = 0;
	uint32 VisionSourceCount_RenderThread = 0;
	uint32 FogVisibleCount_RenderThread = 0;
	uint32 TeamColorCount_RenderThread = 0;
};

using FMassBattleMinimapRenderDataPtr = TSharedPtr<FMassBattleMinimapRenderData, ESPMode::ThreadSafe>;

class SMassBattleFrameMinimap final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMassBattleFrameMinimap) {}
		SLATE_ARGUMENT(FMassBattleMinimapRenderDataPtr, RenderData)
	SLATE_END_ARGS()

	virtual ~SMassBattleFrameMinimap() override;
	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

private:
	FMassBattleMinimapRenderDataPtr RenderData;
	TSharedPtr<ICustomSlateElement, ESPMode::ThreadSafe> CustomDrawer;
};

class FSceneInterface;
void BindMassBattleTeamColorsToScene(FSceneInterface* Scene, FMassBattleMinimapRenderDataPtr RenderData);
