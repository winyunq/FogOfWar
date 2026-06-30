# MassBattle / FogOfWar Architecture Memory

This file records integration decisions and pitfalls that should survive across sessions.

## Confirmed MassBattle Facts

- MassBattleFrame agents do not use `FTransformFragment` as their authoritative runtime location.
- The authoritative MassBattle location fragment is `FLocating` in `Plugins/MassBattleFrame/Source/MassBattle/Public/Fragments/Transform.h`.
- `FLocating` carries `Location`, `PreLocation`, and `InitialLocation`. It is updated by MassBattle processors and components.
- FogOfWar must read `FLocating` by default. Reading `FTransformFragment` silently misses normal MassBattle agents.

## FogOfWar Binding Rule

- FogOfWar uses `FogOfWarMassBinding.h` as the single mapping point for Mass data.
- Default project binding:
  - `FOW_LOCATION_FRAGMENT` -> `FLocating`
  - `FOW_TEAM_FRAGMENT` -> `FTeam`
  - `FOW_GET_LOCATION(Fragment)` -> `Fragment.Location`
  - `FOW_GET_TEAM_INDEX(Fragment)` -> `Fragment.index`
- Fallback fragments exist inside FogOfWar for non-MassBattle builds:
  - `FFogOfWarLocationFragment`
  - `FFogOfWarTeamFragment`
- Do not scatter direct references to `FLocating`, `FTeam`, or `FTransformFragment` through FogOfWar processors. Route them through the binding header.

## Vision Update Pitfalls

- `UMassLocationChangedObserver` must not blindly mark all vision providers every frame in normal mode.
- The Mass branch does not use `AFogOfWar` as the debug-force owner for CPU tile scene fog. Scene fog is GPU circle-source based; any remaining CPU tile debug flags belong to legacy/minimap code paths.
- Normal mode should use FogOfWar's cached `FMassPreviousVisionFragment::PreviousVisionData` to decide whether an entity needs a vision refresh.
- The observer intentionally avoids relying on MassBattle `FLocating::PreLocation` timing. MassBattle updates `PreLocation` inside its own movement/grid registration flow, so cross-plugin ordering can make it a poor external change detector.
- `AFogOfWar::VisionUpdateWorldDistanceThreshold` is optional and is synced into `UMinimapDataSubsystem::VisionUpdateWorldDistanceThreshold`. `0` means grid/cache boundary comparison only.
- Plain MassBattle agents do not pass through `UMassVisionTrait`, so FogOfWar must auto-bind them. `UMassBattleFogOfWarBootstrapProcessor` is the bridge that adds `FMassVisionFragment`, `FMassPreviousVisionFragment`, and `FMassMinimapRepresentationFragment` once per Battle agent.
- Initial vision must perform an actual reveal pass. A processor that only adds `FMassVisionInitializedTag` without calling the vision calculation leaves the map black until some later movement happens.
- Do not gate RTS fog updates on render/frustum/distance culling tags. Off-camera friendly units still need to reveal the strategic fog state.
- Do not fix Mass worker-thread crashes by leaving hot processors on GameThread. The correct boundary is:
  - `AFogOfWar` handles GameThread-only setup/render work: Volume bounds, LineTrace height scan, texture/MID/RT updates, and post-process submission.
  - Mass processors handle runtime vision logic from fragments plus `UMinimapDataSubsystem` data.
  - `UMinimapDataSubsystem` is declared with `TMassExternalSubsystemTraits<GameThreadOnly=false>` and processors must declare both query requirements and processor-level `ProcessorRequirements` when accessing the subsystem outside `ForEachEntityChunk`.
- Optional/plugin-style Mass processors must not use `GetSubsystemChecked` for FogOfWar/Minimap subsystems. Use `Context.GetSubsystem` or `Context.GetMutableSubsystem` and return when null, so a missing minimap/fog module does not crash the core MassBattle simulation.
- Mass processors must not call `UGameplayStatics`, `GetWorld()`, actor iteration, or `AFogOfWar` in their hot path. If a processor truly needs Actor/render access, it is a bridge processor and should be isolated from the per-entity loop.
- Current visibility counter updates are Mass-owned but serial within the processor because many entities can touch the same tile. Do not switch this to chunk-parallel until the shared `VisibilityCounter` write hazard is solved with a reduction pass or explicit atomics.

## Minimap Fog

- The minimap should be low-frequency and UI-driven or subsystem-driven. It does not need per-frame precision.
- The minimap is a low-resolution presentation problem. A 256x256 minimap means 65536 presentation samples; calling them tiles/cells is an implementation detail, not the gameplay fog model.
- The preferred data source is `UMassBattleHashGridSubsystem::AgentGrid`, not a duplicate FogOfWar unit list.
- `UMinimapWidget` is the place to expose user-facing minimap options such as texture resolution, update interval, max encoded units, and rendering material.
- Team filtering is not mandatory for the first minimap implementation, but visibility ownership will eventually require a team/alliance resolver.
- Grid size and origin should be centralized. The intended direction is configuration-driven linkage between MassBattle HashGrid, minimap bounds, and FogOfWar bounds.
- Team color should be resolved with `TeamColors[TeamId]` through `UMinimapDataSubsystem::GetTeamColor`, not with repeated if/switch chains in hot paths.
- `FMassMinimapRepresentationFragment::IconSize` is a minimap pixel radius. Do not derive it from `FCollider.Radius * FScaling.Scale` unless an explicit conversion policy exists. Collision radius is a world/physics concept; minimap dots are UI symbols.
- The minimap material data contract is:
  - `UnitLocationDataTexture` / `IconDataTexture`: `(WorldX, WorldY, IconPixelRadius, Reserved)`.
  - `UnitColorDataTexture` / `IconColorTexture`: unit display color.
  - `VisionSourceDataTexture` / `VisionDataTexture`: `(WorldX, WorldY, Reserved, SightRadiusWorld)`.
  - `GridWorldSize` / `GridSize` and `GridBottomLeftWorldLocation` define the minimap world rectangle.
- Minimap tile output is one representative unit per occupied tile. Current representative influence is sight radius; this keeps the current material path bounded by occupied minimap/hash cells rather than raw unit count.
- The minimap draw layer must not silently clamp every unit icon to a hard-coded minimum size. If a 1px/3px readability floor is needed, expose it as an explicit minimap option; otherwise it hides bad unit-size data and makes MassBattle-derived icon sizes look ineffective.
- Minimap tile indexing is `Index = X * ResolutionY + Y`. Keep writers and readers aligned with `ConvertMinimapTileIJToWorldLocation_Static`.
- UMG can initialize before `AFogOfWar::Activate()` syncs final grid bounds. Refresh minimap material grid parameters before drawing instead of assuming one-time initialization order is correct.

## Scene Fog

- The Mass branch scene fog implementation is GPU circle-source based. The old CPU tile/DDA scene pipeline is not the main path.
- `AFogOfWar` must not run the old `SnapshotTexture -> Interpolation -> AfterInterpolation -> SuperSampling` render-target chain for scene fog.
- `AFogOfWar` does not activate `UMinimapDataSubsystem::VisionTiles` for scene fog. The CPU tile grid may remain as legacy data/API, but it must not be used for the main camera fog path.
- Camera-visible scene fog is GPU-driven, using visible allied/friendly units as reveal sources inside the camera region.
- Scene fog and minimap fog are separate rendering problems. Do not reuse minimap unit/color/tile data as the source for scene post-process fog.
- `AFogOfWar` owns the scene post-process bridge. It can upload `FOW_SceneGpuVisionSourceTexture` with texels `(WorldX, WorldY, SightRadius, Reserved)`, plus `FOW_SceneGpuVisionSourceCount` and `FOW_EnableSceneGpuVisionSources`. The post-process material can use those sources to decide per scene pixel whether the pixel is covered by a reveal radius.
- The final scene fog visibility test is circular. AABB/bounds are allowed only as CPU broad-phase query windows for HashGrid and camera-frustum candidate collection; they must not become the final reveal shape.
- The scene material should evaluate each screen pixel/world position against the uploaded circle sources. Smooth edge/temporal fade belongs in the post-process material or a dedicated GPU history pass, not in minimap compression data.
- The UMGMCP `hlsl_*` protocol is UI-material-only and must not be used for scene fog post-process assets. Use `material_setup_scene_fog_postprocess` for `/FogOfWar/Core/Materials/M_FogOfWarPostProcessing`, because it creates a PostProcess material and wires `PostProcessInput0`, `WorldPosition`, `FOW_*` parameters, and one Custom HLSL node to `EmissiveColor`.
- Scene GPU vision source collection should prefer `URTSCamera::minimapFrustumPoints[4]` as the camera ground quadrilateral. HashGrid query bounds must be expanded by a configured max/source search padding so off-camera large vision sources that cover the camera are not missed.
- Scene GPU source compression is per HashGrid agent cell. Merge all vision sources in a cell into one covering circle centered on the cell center with radius `max(distance(unit, cellCenter) + SightRadius)`. This reduces source count without under-revealing. Do not use the minimap tile cache for scene fog, and do not fall back to a full Mass entity query unless HashGrid is unavailable by design.
- The CPU tile grid remains useful for minimap/explored state and gameplay queries, but it should not be mistaken for the final high-detail scene fog model.

## Do Not Rebuild

- Do not create a second authoritative unit registry for FogOfWar.
- Do not traverse all Mass entities for minimap hot paths when MassBattle HashGrid already contains spatially organized agent data.
- Do not add/remove Mass tags as a high-frequency state mechanism unless it is genuinely structural; prefer cached fragments or MassAPI flags for high-frequency state.

## Plugin Descriptor Pitfalls

- If `FogOfWar.Build.cs` depends on modules from another plugin, `FogOfWar.uplugin` must also list that plugin in its `Plugins` array. Otherwise UBT can compile inside the project but the plugin is fragile when packaged, migrated, or enabled independently.
- Current explicit plugin dependencies: `MassBattle`, `MassGameplay`, `MassBattleMinimap`, `EnhancedInput`, and `OpenRTSCamera`.
- Default command tags used by UI assets/subsystems must exist in `Config/DefaultGameplayTags.ini`. Runtime code that calls `RequestGameplayTag` before native registration can still trigger editor ensures.
