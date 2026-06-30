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

## 材质节点

创建一个 Post Process 材质：

```text
Material Domain = Post Process
Blendable Location = Before Tonemapping 或 After Tonemapping（二选一，先用 After Tonemapping 调试）
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
Custom Node -> Visibility
Lerp(SceneColor * FogBrightness, SceneColor, Visibility) -> Emissive Color
```

## Custom Node 输入

Custom Node 输出类型用 `CMOT Float 1`。

输入：

```text
SourceTexture: Texture2D
SourceCount: float
Enable: float
CurrentPixelWorldXY: float2
EdgeWidth: float
```

## Custom Node HLSL

```hlsl
float Visible = 0.0;

if (Enable > 0.5)
{
    [loop]
    for (int SourceIndex = 0; SourceIndex < (int)SourceCount; ++SourceIndex)
    {
        float4 Source = SourceTexture.Load(int3(SourceIndex, 0, 0));
        float2 Delta = CurrentPixelWorldXY - Source.xy;
        float DistSq = dot(Delta, Delta);

        float Radius = Source.z;
        float Covered = 0.0;

        if (EdgeWidth > 0.0)
        {
            float Dist = sqrt(DistSq);
            Covered = 1.0 - smoothstep(Radius - EdgeWidth, Radius, Dist);
        }
        else
        {
            Covered = 1.0 - step(Radius * Radius, DistSq);
        }

        Visible = max(Visible, Covered);
    }
}

return saturate(Visible);
```

## 性能注意

- `SourceCount` 是每个屏幕像素循环次数。它越大，GPU 后处理越重。
- 优先通过 HashGrid cell 合并和镜头范围裁剪减少上传圆源数量。
- `SceneGpuVisionSourceSearchPadding` 必须不小于最大视野半径，否则镜头外大视野源可能漏掉。
- AABB 只能用于 CPU broad-phase 查询，不能进入最终材质揭雾逻辑。
- 旧 `FOW_FinalVisibilityTexture` 会带来网格感；新材质不要再采样它。
