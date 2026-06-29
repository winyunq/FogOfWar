# FogOfWar

MassBattle-oriented fog of war and minimap plugin for RTS projects.

This branch is the MassBattle integration branch. It reads MassBattle runtime data directly, especially `FLocating`, `FTeam`, and `UMassBattleHashGridSubsystem`, instead of maintaining a second unit registry.

## Dependencies

The plugin currently depends on these open-source project plugins:

- `MassBattle`
- `MassBattleMinimap`
- `OpenRTSCamera`

It also uses Unreal Engine Mass modules and standard engine plugins/modules:

- `MassGameplay`
- `MassEntity`, `MassCommon`, `MassMovement`, `MassSpawner`, `MassRepresentation`, `MassSignals`, `MassLOD`
- `EnhancedInput`
- `UMG`, `Slate`, `SlateCore`, `RHI`, `RenderCore`

The declared plugin dependencies are in `FogOfWar.uplugin`. If you want to use FogOfWar without MassBattle, change `Source/FogOfWar/Public/FogOfWarMassBinding.h` to bind to your own location/team fragments or to the fallback fragments, and remove the MassBattle-specific Build.cs/uplugin dependencies.

## Quick Start

### 1. Enable dependencies

Place the dependency plugins next to this plugin or enable them in the host project. Then enable `FogOfWar` in the `.uproject`.

### 2. Scene fog

Add an `AFogOfWar` actor to the level.

Set these properties:

- `GridVolume`: a volume covering the playable battlefield.
- `InterpolationMaterial`
- `AfterInterpolationMaterial`
- `SuperSamplingMaterial`
- `PostProcessingMaterial`
- `bAutoActivate = true`

MassBattle agents are auto-bound when `UMinimapDataSubsystem::bAutoBindMassBattleAgents` is true. The default auto-bound values are:

- `DefaultMassBattleSightRadius`
- `DefaultMinimapUnitPixelRadius`
- `TeamColors`

For explicit per-archetype setup, add `UMassVisionTrait` to the Mass agent config and set:

- `SightRadius`
- `bShouldBeRepresentedOnMinimap`
- `MinimapIconColor`
- `MinimapIconSize`

### 3. Minimap widget

Create a UMG widget derived from `UMinimapWidget`.

In the widget:

- Add an `Image` named `MinimapImage`.
- Assign `MinimapMaterial`.
- Set `TextureResolution` if the default `256x256` is not enough.
- Add the widget to the viewport like any other UMG widget.

`UMinimapWidget` creates its own render target and binds it to `MinimapImage`. It also drives `UMinimapDataSubsystem::UpdateMinimapFromHashGrid`, so no separate unit list is required.

The minimap material receives these parameters:

```text
VisionDataTexture / VisionSourceDataTexture: (WorldX, WorldY, Reserved, SightRadiusWorld)
IconDataTexture / UnitLocationDataTexture:   (WorldX, WorldY, IconPixelRadius, Reserved)
IconColorTexture / UnitColorDataTexture:     unit color
NumberOfVisionSources
NumberOfUnits
GridBottomLeftWorldLocation
GridSize / GridWorldSize
UnitSize
```

The minimap draws one representative icon per occupied minimap presentation sample. This is deliberate: the minimap is a low-resolution UI layer, not the authoritative scene fog model.

## Updating the Scene Fog Material

Scene fog and minimap fog are separate systems.

For the main camera, the CPU uploads a compact list of circular vision sources to the post-process material:

```text
FOW_SceneGpuVisionSourceTexture: (WorldX, WorldY, SightRadius, Reserved)
FOW_SceneGpuVisionSourceCount
FOW_EnableSceneGpuVisionSources
```

Update `PostProcessingMaterial` so it:

1. Reconstructs or reads the current pixel world position in XY.
2. If `FOW_EnableSceneGpuVisionSources > 0`, loops from `0` to `FOW_SceneGpuVisionSourceCount - 1`.
3. Reads each source from `FOW_SceneGpuVisionSourceTexture`.
4. Uses circular visibility:

```hlsl
float4 Source = FOW_SceneGpuVisionSourceTexture.Load(int3(SourceIndex, 0, 0));
float2 Delta = CurrentPixelWorldXY - Source.xy;
bool bVisible = dot(Delta, Delta) <= Source.z * Source.z;
```

5. Applies fog only when no circle covers the pixel.

Do not use AABB as the final reveal shape. Bounds are only used on the CPU as a broad-phase HashGrid query window. The final scene reveal shape is circular.

For a soft edge, replace the boolean test with a smooth falloff:

```hlsl
float Dist = length(CurrentPixelWorldXY - Source.xy);
float Visibility = 1.0 - smoothstep(Source.z - EdgeWidth, Source.z, Dist);
```

For temporal smoothing, blend the resulting visibility with a previous visibility/history render target or the existing interpolation pass. Keep the source data circular.

## Important Settings

`AFogOfWar`:

- `MaxSceneGpuVisionSources`: upload limit for the scene post-process material.
- `bCullSceneGpuVisionSourcesToCamera`: collect only sources near the current camera ground quadrilateral.
- `SceneGpuVisionCullPadding`: extra camera-edge padding.
- `SceneGpuVisionSourceSearchPadding`: must be at least the largest expected sight radius, otherwise off-camera large vision sources can be missed.
- `SceneGpuVisionQueryZHalfRange`: Z range used when querying the 3D MassBattle HashGrid.

`UMinimapDataSubsystem`:

- `bAutoBindMassBattleAgents`
- `DefaultMassBattleSightRadius`
- `DefaultMinimapUnitPixelRadius`
- `bEncodeMinimapVisionSources`
- `TeamColors`
- `bEnableMinimapPerformanceStats`

`UMinimapWidget`:

- `TextureResolution`
- `UpdateInterval`
- `MaxUnits`
- `bEncodeUnitsIntoMinimapMaterial`
- `bDrawUnitsWithCanvasOverlay`

## Debugging

Enable minimap performance logs with:

```text
UMinimapDataSubsystem::bEnableMinimapPerformanceStats = true
```

Typical log categories:

```text
[MinimapPerf][HashGridRead]
[MinimapPerf][Draw]
```

Use these logs to distinguish HashGrid traversal cost, texture upload cost, render-target draw cost, and number of represented units.

## Architecture Rules

- `FLocating` is the MassBattle authoritative location fragment.
- Do not read `FTransformFragment` for MassBattle agents.
- Do not maintain a second authoritative FogOfWar unit list.
- Do not use minimap presentation data as scene fog source data.
- Scene fog final visibility is circular.
- AABB/bounds are broad-phase CPU query helpers only.
- Team color should be indexed through `TeamColors[TeamId]`, not repeated if/switch logic.

See `ARCHITECTURE_MEMORY.md` for integration notes and pitfalls.
