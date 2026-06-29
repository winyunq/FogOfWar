# FogOfWar

面向 MassBattle / RTS 的战争迷雾与小地图插件。

当前默认分支是 `Mass`，这是 MassBattle 集成版本。插件直接复用 MassBattle 的运行时数据，重点读取：

- `FLocating`：MassBattle 单位真实位置。
- `FTeam`：队伍编号。
- `UMassBattleHashGridSubsystem`：MassBattle 已经维护好的空间 HashGrid。

原则：不要再维护第二套单位列表，不要用 `FTransformFragment` 当 MassBattle 单位位置来源。

## 依赖

本插件依赖以下项目插件：

- `MassBattle`
- `MassBattleMinimap`
- `OpenRTSCamera`

同时使用 UE Mass / UI / 渲染相关模块：

- `MassGameplay`
- `MassEntity`、`MassCommon`、`MassMovement`、`MassSpawner`
- `MassRepresentation`、`MassSignals`、`MassLOD`
- `EnhancedInput`
- `UMG`、`Slate`、`SlateCore`
- `RHI`、`RenderCore`

依赖声明在 `FogOfWar.uplugin` 和 `Source/FogOfWar/FogOfWar.Build.cs`。

如果项目不使用 MassBattle，需要改 `Source/FogOfWar/Public/FogOfWarMassBinding.h`，把位置和队伍 Fragment 绑定到你自己的数据结构，并移除 `.uplugin` / `Build.cs` 里的 MassBattle 依赖。

## 场景战争迷雾怎么用

在关卡里拖入一个 `AFogOfWar`。

必须配置：

- `GridVolume`：覆盖战场范围的 Volume。
- `InterpolationMaterial`
- `AfterInterpolationMaterial`
- `SuperSamplingMaterial`
- `PostProcessingMaterial`
- `bAutoActivate = true`

MassBattle 单位默认会自动绑定视野，开关在：

```text
UMinimapDataSubsystem::bAutoBindMassBattleAgents
```

默认参数：

```text
DefaultMassBattleSightRadius
DefaultMinimapUnitPixelRadius
TeamColors
```

如果希望在 Mass AgentConfig 中显式配置，给单位加 `UMassVisionTrait`：

- `SightRadius`：视野半径。
- `bShouldBeRepresentedOnMinimap`：是否显示在小地图。
- `MinimapIconColor`：小地图颜色。
- `MinimapIconSize`：小地图像素半径。

## 小地图怎么用

创建一个继承 `UMinimapWidget` 的 UMG Widget。

Widget 里需要：

- 添加一个 `Image`，名字必须是 `MinimapImage`。
- 给 `MinimapMaterial` 绑定一个小地图材质。
- 按需要设置 `TextureResolution`，默认是 `256x256`。
- 像普通 UMG 一样 Add To Viewport。

`UMinimapWidget` 会自动创建 RenderTarget，并把 RenderTarget 设置给 `MinimapImage`。运行时它会驱动：

```text
UMinimapDataSubsystem::UpdateMinimapFromHashGrid
```

小地图材质会收到这些参数：

```text
VisionDataTexture / VisionSourceDataTexture: (WorldX, WorldY, Reserved, SightRadiusWorld)
IconDataTexture / UnitLocationDataTexture:   (WorldX, WorldY, IconPixelRadius, Reserved)
IconColorTexture / UnitColorDataTexture:     单位颜色
NumberOfVisionSources
NumberOfUnits
GridBottomLeftWorldLocation
GridSize / GridWorldSize
UnitSize
```

小地图是低分辨率 UI 表示。256x256 小地图本质是 65536 个显示采样点，不是场景战争迷雾的真实模型。

## 镜头战争迷雾材质怎么改

重点：镜头内战争迷雾不要走小地图数据，不要走 tile。为了性能，CPU 只做三件事：

1. 用 RTSCamera 的地面四点确定当前镜头附近区域。
2. 用 MassBattle HashGrid 收集可能影响镜头的视野源。
3. 对同一个 HashGrid cell 内的视野源做保守合并，上传圆形视野源给 GPU。

最终揭雾一定在 GPU 后处理材质里按圆判断。

`PostProcessingMaterial` 会收到：

```text
FOW_SceneGpuVisionSourceTexture: (WorldX, WorldY, SightRadius, Reserved)
FOW_SceneGpuVisionSourceCount
FOW_EnableSceneGpuVisionSources
```

材质逻辑应该是：

1. 取当前像素对应的世界坐标 `CurrentPixelWorldXY`。
2. 遍历 `FOW_SceneGpuVisionSourceTexture` 中的圆形视野源。
3. 用距离平方判断当前像素是否被任意圆覆盖。
4. 没有被覆盖才应用迷雾。

HLSL 核心逻辑：

```hlsl
float Visible = 0.0;

if (FOW_EnableSceneGpuVisionSources > 0.5)
{
    [loop]
    for (int SourceIndex = 0; SourceIndex < (int)FOW_SceneGpuVisionSourceCount; ++SourceIndex)
    {
        float4 Source = FOW_SceneGpuVisionSourceTexture.Load(int3(SourceIndex, 0, 0));
        float2 Delta = CurrentPixelWorldXY - Source.xy;
        float RadiusSq = Source.z * Source.z;

        Visible = max(Visible, dot(Delta, Delta) <= RadiusSq ? 1.0 : 0.0);
    }
}
```

如果要柔边：

```hlsl
float Dist = length(CurrentPixelWorldXY - Source.xy);
float CircleVisibility = 1.0 - smoothstep(Source.z - EdgeWidth, Source.z, Dist);
Visible = max(Visible, CircleVisibility);
```

然后：

```hlsl
FinalColor = lerp(FogColor, SceneColor, Visible);
```

不要把 AABB 当成最终揭雾形状。AABB 只允许在 CPU 上作为 HashGrid broad-phase 查询窗口，目的是少扫数据。真正显示层只认圆。

## 性能参数

`AFogOfWar`：

- `MaxSceneGpuVisionSources`：最多上传多少个镜头迷雾圆源。
- `bCullSceneGpuVisionSourcesToCamera`：是否只收集镜头附近源。
- `SceneGpuVisionCullPadding`：镜头边缘额外保留距离。
- `SceneGpuVisionSourceSearchPadding`：必须不小于最大视野半径，否则镜头外大视野源可能漏掉。
- `SceneGpuVisionQueryZHalfRange`：查询 HashGrid 的 Z 半范围。

`UMinimapDataSubsystem`：

- `bAutoBindMassBattleAgents`
- `DefaultMassBattleSightRadius`
- `DefaultMinimapUnitPixelRadius`
- `bEncodeMinimapVisionSources`
- `TeamColors`
- `bEnableMinimapPerformanceStats`

`UMinimapWidget`：

- `TextureResolution`
- `UpdateInterval`
- `MaxUnits`
- `bEncodeUnitsIntoMinimapMaterial`
- `bDrawUnitsWithCanvasOverlay`

## 调试性能

打开：

```text
UMinimapDataSubsystem::bEnableMinimapPerformanceStats = true
```

日志里看：

```text
[MinimapPerf][HashGridRead]
[MinimapPerf][Draw]
```

这些日志可以区分 HashGrid 遍历、纹理上传、RenderTarget 绘制和实际代表单位数量。

## 架构注意事项

- MassBattle 位置真理是 `FLocating`。
- 不要对 MassBattle 单位读取 `FTransformFragment`。
- 不要为 FogOfWar 再建一套权威单位列表。
- 小地图数据不能当作镜头战争迷雾数据。
- 镜头战争迷雾最终形状是圆。
- AABB / Bounds 只用于 CPU broad-phase。
- 队伍颜色走 `TeamColors[TeamId]`，不要在热路径写大量 if/switch。

更多坑点见 `ARCHITECTURE_MEMORY.md`。
