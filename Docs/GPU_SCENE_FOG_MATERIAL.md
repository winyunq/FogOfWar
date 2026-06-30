# GPU 圆形场景战争迷雾材质

当前 Mass 分支已经裁剪旧场景战争迷雾路径：

- 不再使用 `FOW_FinalVisibilityTexture` 决定主画面可见性。
- 不再执行旧 `SnapshotTexture -> Interpolation -> AfterInterpolation -> SuperSampling` RT 管线。
- `AFogOfWar` 只需要一个 `PostProcessingMaterial`。
- CPU 每帧只上传能影响当前镜头的圆形视野源。

## AFogOfWar 参数

场景中放置 `AFogOfWar`，设置：

```text
GridVolume
PostProcessingMaterial
bAutoActivate = true
```

旧参数不用再配：

```text
InterpolationMaterial
AfterInterpolationMaterial
SuperSamplingMaterial
TileSize
MinimalVisibility
ApproximateSecondsToAbsorbNewSnapshot
```

## 后处理材质参数

`AFogOfWar` 会自动给 `PostProcessingMaterial` 的 MID 设置：

```text
FOW_SceneGpuVisionSourceTexture: Texture2D，RGBA32F，每个像素是 (WorldX, WorldY, SightRadius, Reserved)
FOW_SceneGpuVisionSourceCount: float
FOW_EnableSceneGpuVisionSources: float
FOW_NotVisibleRegionBrightness: float
FOW_BottomLeftWorldLocation: float3
FOW_GridSize / FOW_GridWorldSize: float3
```

## 推荐：用 UMGMCP 生成材质

不要用 `hlsl_set_target/hlsl_set/hlsl_compile` 写这个材质；那组接口是 UMG/UI 材质协议，会把目标限制为 `MD_UI`。

启用 `UmgMcp` 后，用材质 MCP 调：

```text
material_setup_scene_fog_postprocess(
  path="/FogOfWar/Core/Materials/M_FogOfWarPostProcessing",
  overwrite=true
)
```

它会生成：

```text
Material Domain = Post Process
Blendable Location = After Tonemapping
SceneTexture(PostProcessInput0)
WorldPosition
TextureObjectParameter(FOW_SceneGpuVisionSourceTexture)
ScalarParameter(FOW_SceneGpuVisionSourceCount)
ScalarParameter(FOW_EnableSceneGpuVisionSources)
ScalarParameter(FOW_NotVisibleRegionBrightness)
ScalarParameter(FOW_FogEdgeWidth)
Custom HLSL -> EmissiveColor
```

## 材质节点

如果手动创建，目标仍然是一个 Post Process 材质：

```text
Material Domain = Post Process
Blendable Location = After Tonemapping
Shading Model = Unlit
```

节点建议：

```text
SceneTexture(PostProcessInput0).Color -> SceneColor
AbsoluteWorldPosition.xy -> CurrentPixelWorldXY
TextureObjectParameter(FOW_SceneGpuVisionSourceTexture) -> SourceTexture
ScalarParameter(FOW_SceneGpuVisionSourceCount) -> SourceCount
ScalarParameter(FOW_EnableSceneGpuVisionSources) -> Enable
ScalarParameter(FOW_NotVisibleRegionBrightness) -> FogBrightness
ScalarParameter(FOW_FogEdgeWidth) -> EdgeWidth
Custom Node -> Emissive Color
```

## Custom Node 输入

Custom Node 输出类型用 `CMOT Float 4`。

输入：

```text
SceneColor: float4
CurrentPixelWorldPosition: float3
FOW_SceneGpuVisionSourceTexture: Texture2D
FOW_SceneGpuVisionSourceCount: float
FOW_EnableSceneGpuVisionSources: float
FOW_NotVisibleRegionBrightness: float
FOW_FogEdgeWidth: float
```

## Custom Node HLSL

```hlsl
if (FOW_EnableSceneGpuVisionSources <= 0.5)
{
    return SceneColor;
}

float Visible = 0.0;
float2 CurrentPixelWorldXY = CurrentPixelWorldPosition.xy;
int SourceCount = (int)min(FOW_SceneGpuVisionSourceCount, 4096.0);
float EdgeWidth = max(FOW_FogEdgeWidth, 0.0);

[loop]
for (int SourceIndex = 0; SourceIndex < SourceCount; ++SourceIndex)
{
    float4 Source = FOW_SceneGpuVisionSourceTexture.Load(int3(SourceIndex, 0, 0));
    float Radius = max(Source.z, 0.0);
    float2 Delta = CurrentPixelWorldXY - Source.xy;
    float DistSq = dot(Delta, Delta);
    float Covered = 1.0 - step(Radius * Radius, DistSq);

    if (EdgeWidth > 0.0)
    {
        float Dist = sqrt(DistSq);
        Covered = 1.0 - smoothstep(Radius - EdgeWidth, Radius, Dist);
    }

    Visible = max(Visible, Covered);
}

float FogBrightness = saturate(FOW_NotVisibleRegionBrightness);
float3 OutputRgb = lerp(SceneColor.rgb * FogBrightness, SceneColor.rgb, saturate(Visible));
return float4(OutputRgb, SceneColor.a);
```

## 性能注意

- `SourceCount` 是每个屏幕像素循环次数。它越大，GPU 后处理越重。
- 优先通过 HashGrid cell 合并和镜头范围裁剪减少上传圆源数量。
- `SceneGpuVisionSourceSearchPadding` 必须不小于最大视野半径，否则镜头外大视野源可能漏掉。
- AABB 只能用于 CPU broad-phase 查询，不能进入最终材质揭雾逻辑。
- 旧 `FOW_FinalVisibilityTexture` 会带来网格感；新材质不要再采样它。
