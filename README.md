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

新增原生 Actor：

```text
AMassBattleFrameFogOfWar
```

这是一个独立的 `AActor`，不继承 `AFogOfWar`，不使用旧后处理源列表，也不修改 MassBattleFrame 源码。Actor 只负责创建和配置 Niagara；视野数据由 Niagara/NDC 路径在 GPU 侧消费。

```text
现成 NDC / Niagara 输入
  -> GPU 视野点按半径绘制成圆
  -> GPU 取反
  -> 世界空间 Fog Mesh / Decal
```

### 可调参数

| 参数 | 默认值 | 作用 |
| :-- | --: | :-- |
| `FogNiagaraSystem` | 无 | 新的世界空间战争迷雾 Niagara 系统；未配置时直接禁用，不回退 CPU。 |
| `VisionDataChannel` | 无 | 现成视野 NDC 输入，可由 Niagara User 参数读取。 |
| `TemporaryVisionRadius` | `1024 cm` | 当前临时默认视野半径；后续接入单位独立半径时替换。 |
| `ViewingTeamIndex` | `0` | GPU 侧参与揭雾的队伍。 |
| `FogOpacity` | `0.85` | 不可见世界空间 Fog 的不透明度。 |
| `bFogDebug` | `false` | Niagara 调试显示开关。 |
| `FogUpdateRateHz` | `0` | `0` 表示不锁帧、每个引擎 Tick 更新；大于 `0` 时按指定频率更新。 |
| `bAutoActivate` | `true` | BeginPlay 自动启用 Niagara Fog。 |

Niagara User 参数契约：

```text
User.FogVisionDataChannel
User.FogVisionRadius
User.FogViewingTeam
User.FogOpacity
User.FogDebug
User.FogUpdateRateHz
User.FogEnabled
```

### 性能接口

```text
GetLastMassBattleFrameFogPerfStats()
[FogOfWarPerf][MassBattleFrameFog]
```

当前接口记录 Niagara 参数推送耗时和 Niagara 路径是否激活。GPU 视野绘制时间应使用 Niagara/GPU profiler 统计；FogOfWar 不引入单位级 CPU 统计。

未配置 `FogNiagaraSystem` 时只记录错误并禁用功能，不执行 CPU fallback。

## GitHub Pages

网页文档由独立 `Document` 分支根目录发布：

<https://winyunq.github.io/FogOfWar/>
