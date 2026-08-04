# 场景迷雾 GPU 合成与单位过滤管线

场景迷雾是相机画面的 GPU 后处理合成，不是 Landscape 材质、世界贴花或地形接收器。场景中是否存在 Landscape、地形使用 Lit 还是 Unlit，都不会改变这条显示路径。

```text
FogOfWar 只替代 MassBattleFrame Agent Render Processor
  -> 低频 HashGrid occupied-cell gather 维护 ActiveProxyIds
  -> 普通渲染帧仅执行 Active Mass collection
  -> Fog Agent Render Query 在 Archetype 层排除 ISKM，只处理最终可见 VAT / Actor
       -> VAT：直接写最终稠密 Niagara 帧数组
       -> Actor：只生成必要的 Spawn / Recycle 增量命令
  -> 独立 MassBattleISKM 最小语义状态 Query（仅逻辑 tick、只含最终可见 ISKM）
       -> 独立 ISKM 后端提交 Transform / CustomData / Provider
  -> 24 Hz 请求时从同一次局部 HashGrid gather 收集友军/盟军 XY + XY 速度
  -> 一次紧凑 float4 GPU Buffer 上传
  -> A. 24 Hz：一个真实半径实例化圆 Draw 写高分辨率 PF_G8 Visual Mask
  -> B. 每个渲染帧：一个实例投影缓存的 Visual Mask 到屏幕 PF_G8 VisibilityMask
       -> 一个全屏 Composite Pass 合成 SceneColor
  -> C. 3 Hz：一个既有真实半径实例化圆 Draw 写 HashGrid 对齐的 PF_G8 逻辑图
       -> 最多一个异步回读供下一代 Niagara 提交判定
```

A 与 C 共用同一次 `24 Hz` 源 Buffer 上传；C 只在 `3 Hz` 到期时额外提交低分辨率 GPU Draw，不重新遍历 CPU 单位或重新上传源。B 只消费 A 已生成的缓存 Visual Mask。每个源收集请求都以一遍镜头窗口与视野窗口的并集 HashGrid 扫描同时更新 Active 工作集，不存在第二条 source-only 路径。重表现 Query 在普通渲染帧和模拟子帧都只执行 Active collection。Active collection 由 `UE::Mass::FEntityCollection` 缓存并检查 Archetype entity-order version；成员稳定时不重写 handles。

ISKM 不属于 MassBattleFrame 原版，也不嵌入 Fog Processor。独立 `MassBattleISKM` 的状态阶段和后端在 Fog 开启时复用同一份最终可见 `FEntityCollection`，Fog 的 VAT Query 则直接排除 ISKM Archetype；Fog 关闭时 ISKM 走自身未过滤管线。不存在旧后端、兼容层或双写路径。

## 场景显示

所有视野源只在 `24 Hz` Visual Mask 更新中以一个实例化 Draw 参与计算。普通渲染帧固定生成一个六顶点实例，把缓存 Visual Mask 投到当前相机；逐帧投影与合成的 GPU 工作量不随视野源数量增长。场景合成公式是：

```text
Fogged = SceneColor * (1 - FogOpacity)
Final = lerp(Fogged, SceneColor, VisibilityMask)
```

场景默认 `FogOpacity=0.30`：迷雾区保留原画面 `70%` 亮度，真视野区严格保留 `100%` 原画面。`bFogDebug` 显示黑白 VisibilityMask；`bDebugRevealAll` 直接返回原 SceneColor。

这条管线订阅 Tonemap 后处理节点。它直接采样渲染线程已经缓存的 Visual Mask，不等待 CPU 异步回读；回读只供下一代单位过滤使用。

## 高度与投影

紧凑源数据只有 `XY + XY 速度`。世界图更新时，源统一按 `SceneFogProjectionPlaneZ` 的平面语义处理，不会因为提供视野的飞机自身 Z 值改变覆盖位置；速度只用于该次低频世界图生成时的有上限预测。普通渲染帧不再外推或逐源计算。

这是平面投影，不是 SceneDepth 世界坐标重建。因此高空粒子的屏幕位置与地面 XY 投影仍可能有视差；这是相机后处理方案本身的已知限制，而不是 Landscape 或场景材质问题。若以后修正，应在屏幕遮罩投影中单独处理高度语义，不能重新引入地形扫描、贴花或第二次单位遍历。

## Visual Mask 与 HashGrid 逻辑图分工

高分辨率 PF_G8 Visual Mask 只保存 `0/1`，按真实源位置和真实半径绘制，供场景后处理直接投影。HashGrid 对齐 PF_G8 逻辑图只保存 `0/3`，按 `3 Hz` 异步回读供表现过滤使用：

- `0`：深雾，单位不产生有效 VAT/Actor/ISKM 提交；
- `1`：仅表示异步布局交接时的未知格，与 `0` 一样不进入 Active 或任何表现后端；
- `2`：仅用于攻击暴露、永久可见或 VAT 建筑最后快照等最终单单位例外，不写入世界图；
- `3`：真视野，提交最终选定的表现后端。

状态 `2` 的单位存在于最终选定的表现后端，但它位于场景 VisibilityMask 外，所以最终画面自然保留迷雾亮度；状态 `3` 位于揭开区域，显示原亮度。攻击暴露只提升攻击者本身，不添加视野源，也不会揭开地形或附近单位。

两次圆 Draw 都关闭 Blend，因为所有覆盖片元写入相同常量，最后一次写入与二值 OR 等价。Visual Mask 默认 `24 Hz`，使用真实半径；逻辑图只把既有绘制频率独立为 `3 Hz`，不改变原来的半径或 HashGrid 判定语义。两者共用一份源 Buffer；回读队列最多保留一个在途 staging copy，避免积压。

## 单位连贯性

替代 MBF Processor 在 Active collection 中消费上一代格状态，并在逻辑/状态计算完成后才调用表现后端：

- `0/1` 都不激活代理，也不建立隐藏 VAT 项或其他后端实例；
- 只有最终状态为 `2/3` 才把单位交给 VAT、Actor 或独立 ISKM 后端；
- 重新显示时从当前权威位置开始，不从隐藏前旧位置补插值路径；
- `RememberLastSeen` 的冻结建筑 payload 当前只由 VAT 后端重放；Actor/ISKM 类型隐藏，避免读取不可见实体的实时状态。

## 小地图

小地图是独立的 `3 Hz` 全图 GPU 快照与材质显示，迷雾强度独立默认为 `0.30`。全图单位记录由一个窄字段只读 Query 收集，不触发表现 Processor 的全量 LOD/VAT/插值/打包。场景与小地图共享玩家/盟友判定及逻辑视野源，但不共享显示纹理或刷新频率。

本地 Team 来自 RTSInput；盟友集合来自 `URTSDiplomacySubsystem` Snapshot，并与手填 `AlliedTeamIndices` 合并。外交扫描只读取当前玩家的一行 Team 关系，不遍历单位。

## 明确禁止

- 普通渲染帧的全单位 Mass/HashGrid/历史槽位遍历；
- 让 VAT、Actor 或 ISKM 后端重新决定 Fog；
- 每像素循环全部视野源；
- `VisionMapGrid` / `PreviousVision`；
- `FMassVisibilityFragment`；
- CPU 圆覆盖、CPU 网格膨胀；
- Landscape 扫描、组件重注册、Deferred Decal、MPC 或地形材质接入；
- 修改 MassBattleFrame 原始源码。

完整的 Processor 接管与配置见 [README](../README.md)。
