# FogOfWar — Mass Battle GPU Minimap

当前 `Mass` 分支的小地图只有一条实现路径：`UMassBattleFrameMinimapWidget` 直接读取 Mass Battle Frame 已经维护的渲染批数组，低频上传到持久 GPU Buffer，再由 Slate 自定义绘制和 Global Shader 直接画入 Widget 的屏幕区域。

它不使用 Niagara、Niagara Data Channel、CPU 单位遍历、Mass Entity Query、小地图 HashGrid、材质小地图、SceneCapture、独立 RenderTarget 或世界空间特效。

## 使用

使用以下任一种方式放置小地图：

- 在 UMG 中放置原生 `Mass Battle Frame Minimap` 控件。
- 使用插件资产 `Content/Core/MassBattleFrameMiniMap.uasset`。

Widget 的布局位置和尺寸就是最终 GPU 绘制区域。`NativeConstruct` 会自动初始化并立即推送第一帧，之后由 `Update Rate` 控制定时更新。

默认 `Update Rate = 1/3 Hz`，即每 3 秒读取并上传一次新数据。两次更新之间不会重新读取单位；Slate 只用缓存的 GPU Buffer 重画，因此画面可以正常参与 UMG 合成而不会实时追踪单位数据。

## 唯一数据链

```text
UMassBattleFrameMinimapWidget
  -> UMassBattleSubsystem::AgentRenderers
  -> AMassBattleAgentRenderer::SpawnedRenderBatches
  -> 批量 Append 已有连续数组
       LocationArray
       DynamicParams0_Array
       IsHiddenArray
  -> 一次异步 Render Command
  -> 持久 GPU Buffer
  -> Slate Custom Element
  -> Global Shader 直接画入 Widget 区域
```

CPU 只遍历 Renderer 和 Render Batch，并对每个连续数组执行 `TArray::Append`。没有逐单位投影、过滤、重组或 Mass Entity 遍历。坐标换算、Team ID、尺寸取整、隐藏判断和绘制全部在 GPU 完成。

## GPU 绘制规则

### 坐标

地图范围来自当前关卡中的 `AMapRegion`。没有 Actor 时读取：

```text
<Project>/Config/MapRegion/<MapName>/MapRegion.ini
```

两者都没有有效自定义值时使用中心位于世界原点、大小为 `65536 × 65536 UU` 的缺省范围。

Shader 首先计算：

```text
UV = (WorldXY - MapMin) / MapSize
```

随后将世界坐标逆时针旋转 90°映射到小地图：

```text
ScreenUV = (UV.y, 1 - UV.x)
```

### 逻辑分辨率与向上取整

`Logical Resolution` 表示地图每个轴的逻辑像素数，不改变 Widget 的真实布局尺寸。默认值为 `256`。

单位或视野的世界直径先换算成逻辑像素，再逐轴向上取整：

```text
LogicalSize = max(ceil((2 * RadiusUU) * LogicalResolution / MapSize), 1)
```

最后按 Widget 的真实尺寸整体缩放。结果保证任何非零单位或视野至少占一个逻辑像素；当分辨率为 `256` 时，最小绘制尺度就是整张地图的 `1/256`。

- 单位绘制为正方形。
- 视野绘制为圆形。
- 非正方形地图或 Widget 下，单位仍取两轴结果中的较大值保持正方形，视野同样保持圆形。

### Team ID 与颜色

Mass Battle Frame 已把 Team ID 编码在 `DynamicParams0.W` 的低 10 位。Shader 直接取得：

```text
TeamID = asuint(DynamicParams0.W) & 1023
Color = TeamColors[TeamID]
```

颜色配置文件为：

```text
<Project>/Config/MapRegion/<MapName>/MinimapColors.ini
```

格式：

```ini
[MinimapUnitColors]
DefaultTeamColor=(R=0.7,G=0.7,B=0.7,A=1.0)
TeamColorCount=4
TeamColor0=(R=0.45,G=0.45,B=0.45,A=1.0)
TeamColor1=(R=0.10,G=0.72,B=0.18,A=1.0)
TeamColor2=(R=0.85,G=0.12,B=0.10,A=1.0)
TeamColor3=(R=0.12,G=0.34,B=0.95,A=1.0)
```

GPU 颜色表固定覆盖 `0..1023`。缺少 `TeamColorN` 的 Team ID 使用 `DefaultTeamColor`，当前缺省色是最亮白色的 `0.7` 倍。

### 单位、视野与战争迷雾

绘制顺序是：

1. 绘制所有 `IsHidden == false` 的单位色块。
2. 仅用 `TeamID == ViewingTeam` 且未隐藏的单位绘制圆形视野模板。
3. 最后绘制黑色战争迷雾，但在视野模板覆盖的像素跳过，因此雾会正确遮住不可见单位。

当前没有联盟/共享视野输入，所以不会猜测哪些 Team 是盟友。`Viewing Team` 只代表一个确切 Team ID；如果以后需要联盟共享视野，必须增加明确的 Team 关系输入。

## 主要参数

| 参数 | 默认值 | 含义 |
| --- | ---: | --- |
| `Logical Resolution` | `256` | 地图每个轴的逻辑像素数；控制最小向上取整尺度。 |
| `Update Rate` | `1/3 Hz` | 数据缓存更新频率；默认每 3 秒一次。 |
| `Unit Radius` | `100 UU` | 单位正方形的世界半径。 |
| `Vision Radius` | `4000 UU` | 当前 Viewing Team 的圆形视野半径。 |
| `Fog Opacity` | `0.5` | 未揭示区域的黑色遮罩透明度。 |
| `Viewing Team` | `0` | 唯一能揭示当前小地图迷雾的 Team ID。 |

运行时可调用对应的 `SetUpdateRateHz`、`SetMinimapResolution`、`SetUnitRadiusUU`、`SetVisionRadiusUU`、`SetFogDarkenOpacity` 和 `SetViewingTeamIndex`。除更新频率外，参数修改会立即重新推送缓存。

## 性能日志

启用小地图后会输出三类日志：

```text
MassBattleMinimapPerf GT: Agents=... Batches=... BulkMergeAndSchedule=...ms UploadBytes=...
MassBattleMinimapPerf RT: Agents=... BufferCreateAndUpload=...ms
MassBattleMinimapPerf GPU: Agents=... UnitsAvg=...ms VisionAvg=...ms FogAvg=...ms TotalAvg=...ms
```

- GT：批数组合并和提交 Render Command 的耗时，`CpuAgentTraversalCount` 固定为 `0`。
- RT：创建/替换 GPU Buffer 并上传缓存的耗时，只在低频更新时发生。
- GPU：每 15 次绘制异步采样一次，累计后输出单位、视野和雾三个 Pass 的平均值。

已记录的 `9,944` 单位样例为：GT `0.088 ms`、RT `0.150 ms`、GPU Units `0.033 ms`、Vision `0.037 ms`、Fog `0.004 ms`、GPU Total `0.097 ms`。这是一次场景测量，不是硬件无关保证。

## 当前保留资产与源码

小地图运行时核心只有：

```text
Content/Core/MassBattleFrameMiniMap.uasset
Shaders/Private/MassBattleMinimap.usf
Source/FogOfWar/Public/UI/MassBattleFrameMinimapWidget.h
Source/FogOfWar/Private/UI/MassBattleFrameMinimapWidget.cpp
Source/FogOfWar/Private/UI/MassBattleFrameMinimapSlate.h
Source/FogOfWar/Private/UI/MassBattleFrameMinimapSlate.cpp
Source/FogOfWar/Public/Minimap/MapRegion.h
Source/FogOfWar/Private/Minimap/MapRegion.cpp
```

`Content/Core/Materials/MinimapTarget.uasset` 仍被当前 Widget Blueprint 的基础地图图层引用，但不参与单位或战争迷雾计算。

## 明确不存在的旧路径

仓库不再保留以下小地图实现，以免它们被误认为可选方案：

- `UMinimapWidget` CPU/HashGrid/RenderTarget 路径
- `MassMinimapProcessors` 与 `MinimapCellObserver`
- Niagara / NDC 小地图资产
- 小地图材质数据纹理上传
- 世界空间 Niagara 预览或摄像机前移动方案
- 旧测试地图和旧小地图 Widget 示例

场景主画面的战争迷雾是另一条独立功能，不作为小地图的数据入口，也不与上述小地图 GPU Buffer 耦合。

## MassBattleFrame 场景战争迷雾

先明确一个设计纠正：Niagara 只能作为视野圆形的 GPU 光栅化器，不能凭空把已经渲染完成的 `SceneColor` 反相变暗；真正的战争迷雾必须有一个最终合成步骤。因此本实现不再把 Niagara 当作场景输出，而是直接使用独立 SceneView GPU pass 完成“视野遮罩 + SceneColor 合成”。

新增原生 Actor：

```text
AMassBattleFrameFogOfWar
```

一键放置资产：

```text
Content/Core/BP_MassBattleFrameFogOfWar.uasset
```

该 Blueprint 仅继承 `AMassBattleFrameFogOfWar`，没有 Niagara、材质或网格依赖；拖入关卡后使用 C++ 默认参数即可运行。原有的 `Content/Core/MassBattleFogOfWar.uasset` 仍属于旧 `AFogOfWar` 路径，不是本功能的默认资产。

这是一个独立的 `AActor`，不继承 `AFogOfWar`，不使用旧后处理源列表，也不修改 MassBattleFrame 源码。Actor 直接读取 `UMassBattleSubsystem` 已经维护的 `AgentRenderers -> SpawnedRenderBatches`，按 batch 整块转发位置、队伍和隐藏状态；不会查询 Mass Entity、遍历单位、访问 HashGrid 或重新投影数据。

```text
MassBattleFrame SpawnedRenderBatches
  -> GPU-facing Location / DynamicParams0 / IsHidden buffers
  -> SceneView GPU pass: 每个视野源实例化一个世界半径圆形
  -> R8 VisibilityMask（ViewingTeamIndex + IsHidden 在 GPU 过滤）
  -> 一次 SceneColor 合成：SceneColor × FogFactor
```

最终场景输出由独立 SceneView GPU pass 完成；`bFogDebug=true` 时直接把 GPU 可见性遮罩输出到场景，便于确认圆形视野和队伍过滤。Actor 拖入关卡后不需要 Niagara、材质、网格、SceneCapture、RenderTarget 或手工绑定后处理材质。

### 可调参数

| 参数 | 默认值 | 作用 |
| :-- | --: | :-- |
| `TemporaryVisionRadius` | `1024 cm` | 当前临时默认视野半径；后续接入单位独立半径时替换。 |
| `ViewingTeamIndex` | `0` | GPU 侧参与揭雾的队伍。 |
| `FogOpacity` | `0.85` | 不可见场景区域的暗化强度。 |
| `bFogDebug` | `false` | 是否直接显示 GPU 可见性遮罩（白=已揭示，黑=战争迷雾）。 |
| `bDebugRevealAll` | `false` | Debug 开关；开启后所有已上传单位都揭开视野，绕过 Team 和 `IsHidden` 过滤，仅用于排查数据。 |
| `FogUpdateRateHz` | `0` | `0` 表示不锁帧、每个引擎 Tick 更新；大于 `0` 时按指定频率更新。 |
| `bAutoActivate` | `true` | BeginPlay 自动启用场景 GPU 战争迷雾。 |

### 性能接口

```text
GetLastMassBattleFrameFogPerfStats()
[FogOfWarPerf][MassBattleFrameFog]
```

当前接口记录参数推送、批量数组上传、来源数量、batch 数量，以及 Scene GPU 路径状态。RenderDoc/Unreal GPU profiler 中可直接查看：

```text
MassBattleFrameFog Vision Mask
MassBattleFrameFog Composite
```

这两个 GPU pass 的复杂度是 `O(可见性源实例数 + 当前视图像素数)`，不是 `O(单位数 × 屏幕像素数)`；CPU 侧只有 batch 级 `Append` 和 GPU buffer 更新，不执行单位级 fallback。

Actor 拖入场景后无需手动指定任何资产；C++ 会自动注册 SceneView GPU pass，不执行 CPU fallback。

### 自动查看最新日志

仓库提供一个不依赖额外 Python 包的日志查看脚本：

```powershell
python Scripts\ReportLatestMassBattleFrameFogPerf.py
```

脚本会自动选择项目 `Saved/Logs` 下最近修改的 `.log`，也可以显式指定：

```powershell
python Scripts\ReportLatestMassBattleFrameFogPerf.py --log D:\UE5Project\Winyunq\Saved\Logs\Winyunq.log --tail 30
```

脚本中的“串行总时间”定义为：`CPU ParameterPush + GPU VisionMask + GPU Composite`。
`ArrayUpload` 已包含在 `ParameterPush` 内，只作为子计时展示，不能再次相加；CPU/GPU 可能重叠，所以串行总时间是便于比较的核算上界，不冒充严格墙钟帧时间。
如果日志没有 `MassBattleFrameFogGPU` 记录，脚本会明确报告无法得到完整场景 Fog 总时间，此时只有 CPU 推送数据，不能拿小地图 GPU 时间代替。

### 9944 单位实测

测试地图为 `/Game/Map/EastAsia/64`，Win64 Development、D3D12，`FogUpdateRateHz=0`。日志确认本次场景战争迷雾实际收到全部 `9944` 个来源、`24` 个 batch，并且 `SceneGPU=yes`；不是只测试了 728 个单位，也不是空数据路径。

| 指标 | 实测结果 | 说明 |
| :-- | --: | :-- |
| Fog `ParameterPush` | 平均 `0.158 ms`，最大 `0.279 ms` | 场景 Fog Actor 单次参数推送。 |
| Fog `ArrayUpload` | 平均 `0.157 ms`，最大 `0.278 ms` | batch 数组合并和提交 GPU buffer 的 CPU 侧耗时。 |
| Mass 来源 | `9944` sources / `24` batches | 全部来源进入场景 GPU 视野 pass；Team 和 `IsHidden` 在 GPU 过滤。 |
| 数据量 | `424088 bytes` | 同一批 9944 单位的小地图日志记录的三组来源数组及颜色表上传量。 |

同一批 9944 单位的小地图 GPU 参考采样为：Units `0.061 ms`、Vision `0.081 ms`、Fog `0.006 ms`、Total `0.189 ms`（3 个样本平均）。这组数值是小地图 GPU pass 的参考，不冒充场景 Fog 的 GPU 时间；场景 Fog 的确切 GPU 时间应在 profiler 中查看 `MassBattleFrameFog Vision Mask` 和 `MassBattleFrameFog Composite`。

## GitHub Pages

网页文档由独立 `Document` 分支根目录发布：

<https://winyunq.github.io/FogOfWar/>
