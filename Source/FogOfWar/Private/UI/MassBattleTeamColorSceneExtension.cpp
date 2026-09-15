// Copyright Winyunq, 2026. All Rights Reserved.
#include "UI/MassBattleFrameMinimapSlate.h"
#include "Misc/EngineVersionComparison.h"

#if UE_VERSION_NEWER_THAN(5, 7, 0)
#include "ScenePrivate.h"
#include "SceneExtensions.h"
#include "SceneUniformBuffer.h"
#include "GlobalRenderResources.h"

BEGIN_SHADER_PARAMETER_STRUCT(FMassBattleTeamColorParameters, )
    SHADER_PARAMETER(uint32, Count)
    SHADER_PARAMETER_SRV(Buffer<float4>, Colors)
END_SHADER_PARAMETER_STRUCT()

DECLARE_SCENE_UB_STRUCT(FMassBattleTeamColorParameters, MassBattleTeamColors, )

static void DefaultTeamColors(FMassBattleTeamColorParameters& Out, FRDGBuilder& GraphBuilder)
{
    Out.Count = 0;
    Out.Colors = GNullColorVertexBuffer.VertexBufferSRV;
}
IMPLEMENT_SCENE_UB_STRUCT(FMassBattleTeamColorParameters, MassBattleTeamColors, DefaultTeamColors);

class FMassBattleTeamColorSceneExtension final : public ISceneExtension
{
    DECLARE_SCENE_EXTENSION(, FMassBattleTeamColorSceneExtension);
public:
    explicit FMassBattleTeamColorSceneExtension(FScene& Scene) : ISceneExtension(Scene) {}
    TWeakPtr<FMassBattleMinimapRenderData, ESPMode::ThreadSafe> MinimapData;

    class FRenderer final : public ISceneExtensionRenderer
    {
    public:
        FRenderer(FSceneRendererBase& Renderer, FMassBattleMinimapRenderDataPtr Data)
            : ISceneExtensionRenderer(Renderer), MinimapData(MoveTemp(Data)) {}
        virtual void UpdateSceneUniformBuffer(FRDGBuilder& GraphBuilder, FSceneUniformBuffer& SceneUniforms) override
        {
            FMassBattleTeamColorParameters Parameters;
            DefaultTeamColors(Parameters, GraphBuilder);
            if (MinimapData && MinimapData->GetTeamColorsSRV_RenderThread())
            {
                Parameters.Count = MinimapData->GetTeamColorCount_RenderThread();
                Parameters.Colors = MinimapData->GetTeamColorsSRV_RenderThread();
            }
            // The SRV is the minimap's existing buffer, not a new palette allocation.
            SceneUniforms.Set(SceneUB::MassBattleTeamColors, Parameters);
        }
    private:
        FMassBattleMinimapRenderDataPtr MinimapData;
    };

    virtual ISceneExtensionRenderer* CreateRenderer(FSceneRendererBase& Renderer, const FEngineShowFlags&) override
    {
        return new FRenderer(Renderer, MinimapData.Pin());
    }
};
IMPLEMENT_SCENE_EXTENSION(FMassBattleTeamColorSceneExtension);
#endif

void BindMassBattleTeamColorsToScene(FSceneInterface* Scene, FMassBattleMinimapRenderDataPtr RenderData)
{
#if UE_VERSION_NEWER_THAN(5, 7, 0)
    if (!Scene) return;
    ENQUEUE_RENDER_COMMAND(BindMassBattleTeamColors)(
        [Scene, RenderData = MoveTemp(RenderData)](FRHICommandListImmediate&)
        {
            if (FScene* RenderScene = Scene->GetRenderScene())
            {
                RenderScene->GetExtension<FMassBattleTeamColorSceneExtension>().MinimapData = RenderData;
            }
        });
#endif
}
