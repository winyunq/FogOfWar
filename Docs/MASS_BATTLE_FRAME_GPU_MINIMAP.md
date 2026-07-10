# Mass Battle Frame GPU Minimap And Scene Fog Experiment

## Goal

The existing `UMinimapWidget` path remains the CPU/downsampled baseline:

```text
UMinimapWidget Tick
-> UMinimapDataSubsystem::UpdateMinimapFromHashGrid
-> CPU minimap tile cache
-> CPU data-texture upload
-> DrawMaterialToRenderTarget
```

The new experiment adds `UMassBattleFrameMinimapWidget` as a MassBattle-prefixed replacement producer:

```text
UMassBattleFrameMinimapWidget Tick
-> camera ground-quad culling
-> MassBattle HashGrid occupied cells only
-> per-agent exact quad test
-> Niagara Data Channel write
-> Niagara system renders minimap dots
```

This keeps both paths available in the same level for performance comparison. The MassBattle widget does not inherit `UMinimapWidget`, but it preserves the common Blueprint entry points `InitializeMinimapSystem` and `ConvertMinimapUVToWorldLocation`.

## Widget

Use a UMG widget subclass based on:

```text
/Script/FogOfWar.MassBattleFrameMinimapWidget
```

Important properties:

| Property | Purpose |
| :-- | :-- |
| `MinimapUnitDataChannel` | Niagara Data Channel asset consumed by the minimap Niagara system. |
| `MaxUnits` | Hard cap for units written per update, matching the baseline minimap field name. |
| `MaxNdcUnits` | Optional MassBattle-specific override. `0` means use `MaxUnits`. |
| `CullingMode` | `CameraGroundQuad` for RTS camera-visible units, or `MinimapWorldBounds` for full-map experiments. |
| `GroundPlaneZ` | Z plane used when deprojecting screen corners to a ground quad. |
| `CameraGroundQuadPadding` | Expands the camera quad before querying HashGrid. |
| `VerticalQueryHalfHeight` | Z span used for HashGrid coordinate range. |
| `bResolveTeamFromMassFragments` | Enables per-visible-unit `FTeam` lookup. Disable to isolate pure culling/write cost. |

Optional preview properties (`PreviewNiagaraSystem`, `PreviewRenderTarget`, `MinimapImage`) are just wiring helpers. The actual rendering behavior belongs to the Niagara asset.

## NDC Contract

The widget writes the following variable names by default. They are configurable on the widget to match the final Niagara Data Channel asset.

| Variable | Type | Contents |
| :-- | :-- | :-- |
| `MinimapUnitLocation` | Position or Vector | Unit world location. |
| `MinimapUnitPosition` | Position or Vector | Same location, for NDC assets that prefer Position naming. |
| `MinimapUnitData` | Vector4 | `(MinimapU, MinimapV, IconPixelRadius, Flags)`. |
| `Team` | int | Team index resolved through `FogOfWarMassBinding.h`. |
| `Style` | int | Currently mirrors team index. |
| `UniqueID` | int | MassBattle grid unique id. |
| `SubType` | int | Widget `NdcSubType`, useful for filtering multiple producers. |

The expected Niagara side is a GPU system that consumes the NDC rows and draws one dot/quad per row. Material work is intentionally deferred until the Niagara/MCP asset workflow is available.

## Performance CSV

The baseline and experiment share:

```text
<Project>/Saved/Logs/FogOfWar_MinimapPerf.csv
```

Existing channels:

```text
MinimapHashGridAvg
MinimapDrawAvg
```

New channels:

```text
MassBattleFrameMinimapNdcProducerAvg
MassBattleFrameMinimapNdcRenderTargetAvg
```

Use these together to compare:

1. CPU HashGrid traversal/downsample cost.
2. CPU texture lock/upload/draw cost.
3. Camera-visible HashGrid culling plus NDC write cost.

`MassBattleFrameMinimapNdcProducerAvg` is producer-only. It does not include the cost of Niagara consuming the NDC rows, GPU drawing, render target work, or UMG presentation. `MassBattleFrameMinimapNdcRenderTargetAvg` is the complete in-widget fallback path: it writes NDC, draws the gathered unit dots into the widget render target, and binds that render target to `MinimapImage` for end-to-end minimap testing.

## Scene Fog Direction

The scene fog path now has a MassBattle-prefixed actor:

```text
/Script/FogOfWar.MassBattleFrameFogOfWar
```

It is an independent Actor, not an `AFogOfWar` subclass. It keeps the same core public property/function names and the same post-process material parameter contract:

```text
FOW_SceneGpuVisionSourceTexture
FOW_SceneGpuVisionSourceCount
FOW_EnableSceneGpuVisionSources
```

The runtime path is:

1. Use the camera ground quad plus `CameraGroundQuadPadding` as the HashGrid broad phase.
2. Visit only occupied MassBattle HashGrid cells in that coordinate range.
3. Optionally compress per occupied HashGrid cell into one covering circle:

```text
CircleCenter = cell center
CircleRadius = max(distance(unit, cell center) + SightRadius)
```

4. Upload the resulting circle list to the same post-process material data texture used by `AFogOfWar`.

The scene CSV is:

```text
<Project>/Saved/Logs/FogOfWar_ScenePerf.csv
```

Use these channels together:

```text
SceneGpuVisionAvg
MassBattleFrameSceneGpuVisionAvg
```

Do not reuse minimap tile data as scene fog input. Minimap dots are a presentation layer; scene fog needs circular visibility sources and can use cell compression only as a conservative broad-phase reduction.
