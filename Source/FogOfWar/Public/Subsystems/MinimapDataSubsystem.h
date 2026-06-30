// Copyright Winyunq, 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassSubsystemBase.h"
#include "Mass/ExternalSubsystemTraits.h"
#include "MinimapDataSubsystem.generated.h"

UENUM()
enum class EMinimapUnitDisplayFlags : uint8
{
	None = 0,
	Selected = 1 << 0,
	Combat = 1 << 1
};
ENUM_CLASS_FLAGS(EMinimapUnitDisplayFlags)

/**
 * 代表战争迷雾高精度网格中的单个瓦片。
 */
USTRUCT()
struct FOGOFWAR_API FTile
{
	GENERATED_BODY()

	/** 瓦片中心点的地形高度。初始化阶段由 AFogOfWar 在 GameThread 射线扫描写入。 */
	UPROPERTY()
	float Height = 0.0f;

	/** 当前看到此瓦片的视野贡献数量。Mass processors 在运行时维护。 */
	UPROPERTY()
	int32 VisibilityCounter = 0;
};

/**
 * 代表小地图网格上的单个瓦片数据。
 * Represents a single tile on the minimap grid.
 */
USTRUCT()
struct FOGOFWAR_API FMinimapTile
{
	GENERATED_BODY()

	/** 当前瓦片内的单位数量 */
	UPROPERTY()
	int32 UnitCount = 0;

	/** 最后一个进入该瓦片的单位颜色 (用于调试或显示) */
	UPROPERTY()
	FLinearColor Color = FLinearColor::Black;

	/** 该瓦片内单位的最大视野半径 */
	UPROPERTY()
	float MaxSightRadius = 0.0f;

	/** 该瓦片代表单位的小地图像素半径 */
	UPROPERTY()
	float MaxIconSize = 0.0f;

	/** 用于在同一瓦片内挑选代表单位；默认使用视野半径作为影响力 */
	UPROPERTY()
	float RepresentativeInfluence = -FLT_MAX;

	/** 上一次小地图更新中的颜色，用于按小地图更新频率做低成本过渡/闪烁。 */
	UPROPERTY()
	FLinearColor PreviousColor = FLinearColor::Transparent;

	/** 当前瓦片是否包含玩家框选单位。 */
	UPROPERTY()
	bool bHasSelectedUnit = false;

	/** 当前瓦片是否包含正在攻击/交战的单位。 */
	UPROPERTY()
	bool bHasCombatUnit = false;
};

USTRUCT(BlueprintType)
struct FOGOFWAR_API FMinimapHashGridPerfStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Performance")
	float TotalMs = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Performance")
	float ClearTilesMs = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Performance")
	float TraverseHashGridMs = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Performance")
	float EstimatedTraversalAndProjectionMs = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Performance")
	float EntityValidationMs = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Performance")
	float FragmentLookupMs = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Performance")
	int32 HashGridBlocks = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Performance")
	int32 ValidBlocks = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Performance")
	int32 OccupiedCells = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Performance")
	int32 NonEmptyCells = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Performance")
	int32 AgentsVisited = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Performance")
	int32 AgentsInBounds = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Performance")
	int32 SkippedOutOfBounds = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Performance")
	int32 EntityValidationChecks = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Performance")
	int32 InvalidEntities = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Performance")
	int32 FragmentDataPtrCalls = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Performance")
	int32 AgentsWithRepresentationFragment = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Performance")
	int32 AgentsWithVisionFragment = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Performance")
	int32 MinimapCellsWritten = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Performance")
	FVector AgentCellSize = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Performance")
	FIntVector AgentBlockDimensions = FIntVector(0, 0, 0);
};

USTRUCT(BlueprintType)
struct FOGOFWAR_API FMinimapDrawPerfStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Performance")
	float TotalMs = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Performance")
	float LockTexturesMs = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Performance")
	float ScanTilesMs = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Performance")
	float UploadTexturesMs = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Performance")
	float DrawRenderTargetMs = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Performance")
	int32 SourceTilesScanned = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Performance")
	int32 ActiveTiles = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Performance")
	int32 EncodedUnits = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Performance")
	int32 EncodedVisionSources = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Performance")
	int32 TotalUnitsRepresented = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Performance")
	int32 MaxUnitsInSingleTile = 0;
};

/**
 * UMinimapDataSubsystem
 * 
 * 全局子系统，负责管理小地图和高精度迷雾网格的数据及坐标转换。
 * 它是所有网格计算的单一真理来源 (Single Source of Truth)。引用 AFogOfWar 在激活时注册的参数。
 */
UCLASS(Config = MassBattle, defaultconfig)
class FOGOFWAR_API UMinimapDataSubsystem : public UMassSubsystemBase
{
	GENERATED_BODY()

public:
	//~ Begin UMassSubsystemBase Interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End UMassSubsystemBase Interface

	/** 提供对着色器实例的静态直接访问，用于高性能代码路径。 */
	static FORCEINLINE UMinimapDataSubsystem* Get() { return SingletonInstance; }

	// Deprecated: UpdateVisionGridParameters removed for strict decoupling.

	/** 由小地图 UI 组件调用以设置其所需的分辨率。 */
	void SetMinimapResolution(const FIntPoint& NewResolution);

	/** 战争迷雾 Actor 激活时同步运行时选项，Mass 热路径只读这里的纯数据。 */
	void SyncFogOfWarRuntimeOptions(float InVisionBlockingDeltaHeightThreshold, float InVisionUpdateWorldDistanceThreshold, bool bInDebugStressTestIgnoreCache, bool bInDebugStressTestMinimap);

	/** 
	 * [Path B] 直接查询 MassBattleHashGrid 以更新小地图瓦片数据。
	 * 采用三层 LOD 遍历 (Map -> Block -> Cell) 以实现 O(Occupied) 性能。
	 */
	UFUNCTION(BlueprintCallable, Category = "FogOfWar|Minimap")
	void UpdateMinimapFromHashGrid(FVector CenterLocation, int32 BlockRadius = 8);

	void RecordMinimapDrawPerfStats(const FMinimapDrawPerfStats& Stats);

	/** 通过 TeamId 数组索引取得颜色，避免在热路径里散落 team if/switch。 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "FogOfWar|Team")
	FLinearColor GetTeamColor(int32 TeamIndex) const;

	void SyncMinimapDisplayOptions(
		const FLinearColor& InDefaultTeamColor,
		const TArray<FLinearColor>& InTeamColors,
		bool bInNormalizeTeamColorDirection,
		float InNormalUnitColorLength,
		float InSelectedUnitColorLength,
		const FLinearColor& InCombatUnitColor,
		bool bInEnableCombatColorFlash,
		float InCombatColorFlashHz,
		float InDefaultUnitPixelRadius);

	/**
	 * 同步战争迷雾高精度网格参数。
	 * 通常由 AFogOfWar 在初始化后调用，以保证静态坐标转换函数参数有效。
	 */
	UFUNCTION(BlueprintCallable, Category = "FogOfWar|Vision")
	void SyncVisionGridParameters(const FVector2D& InGridOrigin, const FVector2D& InGridSize, float InVisionTileSize, const FIntPoint& InVisionResolution);

	/** 同步世界范围给小地图/场景材质使用，不创建旧 CPU 视野 tile。 */
	UFUNCTION(BlueprintCallable, Category = "FogOfWar|Minimap")
	void SyncWorldBounds(const FVector2D& InGridOrigin, const FVector2D& InGridSize);

	void SetVisionGridActive(bool bInActive);
	bool IsVisionGridReady() const;
	bool IsMinimapGridReady() const;
	bool IsLocationVisible(const FVector& WorldLocation) const;
	FTile& GetVisionTile(int32 GlobalIndex);
	const FTile& GetVisionTile(int32 GlobalIndex) const;
	FTile& GetVisionTile(FIntPoint IJ);
	const FTile& GetVisionTile(FIntPoint IJ) const;
	bool IsBlockingVision(float ObserverHeight, float PotentialObstacleHeight) const;

	/**
	 * 手动初始化小地图网格参数 (通常由 AMinimapVolume 等 Actor 调用)。
	 * @param InGridOrigin 世界坐标原点 (GridBottomLeft)
	 * @param InGridSize 世界空间总尺寸
	 * @param InResolution 纹理分辨率 (Tile Count)
	 */
	UFUNCTION(BlueprintCallable, Category = "FogOfWar|Minimap")
	void InitMinimapGrid(const FVector2D& InGridOrigin, const FVector2D& InGridSize, const FIntPoint& InResolution);

public:
	/** 标志位，指示子系统是否已接收到有效的网格参数并准备就绪。 */
	
	//~ Common Grid Properties (Shared by Vision and Minimap)
	
	// 世界空间下的网格总尺寸 (World Size)
	// 如果不使用 AFogOfWar，需手动配置此项以定义小地图覆盖范围。
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category="Minimap|Config")
	FVector2D GridSize = FVector2D(409600.0f, 409600.0f); // Default 4km x 4km

	// 网格左下角的世界坐标 (World Origin)
	// 通常为 -(GridSize / 2) 以使 (0,0) 为中心。
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category="Minimap|Config")
	FVector2D GridBottomLeftWorldLocation = FVector2D(-204800.0f, -204800.0f);
	
	//~ Vision Grid Properties (High-Resolution for Fog of War calculation)
	
	// 高精度视野网格的单瓦片尺寸
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category="Minimap|Config")
	float VisionTileSize = 100.0f;

	// 高精度视野网格的分辨率
	UPROPERTY(Transient)
	FIntPoint VisionGridResolution = FIntPoint::ZeroValue;

	UPROPERTY(Transient)
	bool bVisionGridActive = false;

	UPROPERTY(Transient)
	float VisionBlockingDeltaHeightThreshold = 200.0f;

	UPROPERTY(Transient)
	float VisionUpdateWorldDistanceThreshold = 0.0f;

	UPROPERTY(Transient)
	bool bDebugStressTestIgnoreCache = false;

	UPROPERTY(Transient)
	bool bDebugStressTestMinimap = false;

	TArray<FTile> VisionTiles;
	
	//~ Minimap Grid Properties (Low-Resolution for UI)
	
	// 小地图网格的分辨率
    UPROPERTY(Transient)
    FIntPoint MinimapGridResolution = FIntPoint(256, 256);

	// 小地图单瓦片尺寸
	UPROPERTY(Transient)
	FVector2D MinimapTileSize = FVector2D::Zero();

	// 小地图瓦片数据数组
	TArray<FMinimapTile> MinimapTiles;

	//~ MassBattle auto binding defaults

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category="Minimap|MassBattle")
	bool bAutoBindMassBattleAgents = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category="Minimap|MassBattle", meta=(ClampMin="0.0", UIMin="0.0"))
	float DefaultMassBattleSightRadius = 1024.0f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category="Minimap|MassBattle", meta=(ClampMin="0.0", UIMin="0.0"))
	float DefaultMinimapUnitPixelRadius = 1.5f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category="Minimap|Performance")
	bool bEnableMinimapPerformanceStats = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category="Minimap|Performance")
	bool bEnableDetailedMinimapPerformanceStats = false;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category="Minimap|Performance")
	bool bLogMinimapPerformanceToOutputLog = false;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category="Minimap|Performance")
	bool bWriteMinimapPerformanceCsv = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category="Minimap|Performance", meta=(ClampMin="0.0", UIMin="0.0"))
	float MinimapPerformanceLogInterval = 2.0f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category="Minimap|Rendering")
	bool bEncodeMinimapVisionSources = true;

	UPROPERTY(Transient, BlueprintReadOnly, Category="Minimap|Performance")
	FMinimapHashGridPerfStats LastHashGridPerfStats;

	UPROPERTY(Transient, BlueprintReadOnly, Category="Minimap|Performance")
	FMinimapDrawPerfStats LastDrawPerfStats;

	UPROPERTY(Transient, BlueprintReadOnly, Category="Minimap|Team")
	FLinearColor DefaultTeamColor = FLinearColor::White;

	UPROPERTY(Transient, BlueprintReadOnly, Category="Minimap|Team")
	TArray<FLinearColor> TeamColors = {
		FLinearColor(0.45f, 0.45f, 0.45f, 1.0f),
		FLinearColor(0.10f, 0.72f, 0.18f, 1.0f),
		FLinearColor(0.85f, 0.12f, 0.10f, 1.0f),
		FLinearColor(0.12f, 0.34f, 0.95f, 1.0f),
		FLinearColor(0.95f, 0.72f, 0.10f, 1.0f),
		FLinearColor(0.10f, 0.80f, 0.90f, 1.0f),
		FLinearColor(0.75f, 0.20f, 0.85f, 1.0f),
		FLinearColor(0.95f, 0.45f, 0.12f, 1.0f)
	};

	UPROPERTY(Transient, BlueprintReadOnly, Category="Minimap|Team")
	TArray<FLinearColor> NormalTeamDisplayColors;

	UPROPERTY(Transient, BlueprintReadOnly, Category="Minimap|Team")
	TArray<FLinearColor> SelectedTeamDisplayColors;

	UPROPERTY(Transient, BlueprintReadOnly, Category="Minimap|Team")
	TArray<FLinearColor> TeamDisplayColorsByState;

	UPROPERTY(Transient, BlueprintReadOnly, Category="Minimap|Team")
	FLinearColor DefaultNormalTeamDisplayColor = FLinearColor(0.25f, 0.25f, 0.25f, 1.0f);

	UPROPERTY(Transient, BlueprintReadOnly, Category="Minimap|Team")
	FLinearColor DefaultSelectedTeamDisplayColor = FLinearColor(0.58f, 0.58f, 0.58f, 1.0f);

	UPROPERTY(Transient, BlueprintReadOnly, Category="Minimap|Team")
	TArray<FLinearColor> DefaultTeamDisplayColorsByState;

	UPROPERTY(Transient, BlueprintReadOnly, Category="Minimap|Team")
	bool bNormalizeTeamColorDirection = true;

	UPROPERTY(Transient, BlueprintReadOnly, Category="Minimap|Team")
	float NormalUnitColorLength = 0.5f;

	UPROPERTY(Transient, BlueprintReadOnly, Category="Minimap|Team")
	float SelectedUnitColorLength = 1.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category="Minimap|Team")
	FLinearColor CombatUnitColor = FLinearColor::White;

	UPROPERTY(Transient, BlueprintReadOnly, Category="Minimap|Team")
	bool bEnableCombatColorFlash = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category="Minimap|Team")
	float CombatColorFlashHz = 3.0f;

	UPROPERTY(Transient)
	bool bDrawCombatColorThisUpdate = true;

	void RebuildTeamDisplayColorCache();
	FLinearColor BuildCachedTeamDisplayColor(const FLinearColor& TeamColor, float TargetLength) const;
	
public:
	//~ Begin Static Vision Grid Conversion Functions
	// 静态视野网格转换函数
	
	static FORCEINLINE FVector2f ConvertWorldSpaceLocationToVisionGridSpace_Static(const FVector2D& WorldLocation);
	static FORCEINLINE FIntPoint ConvertVisionGridLocationToTileIJ_Static(const FVector2f& GridLocation);
	static FORCEINLINE FIntPoint ConvertWorldLocationToVisionTileIJ_Static(const FVector2D& WorldLocation);
	static FORCEINLINE FVector2D ConvertVisionTileIJToTileCenterWorldLocation_Static(const FIntPoint& IJ);
	static FORCEINLINE int32 GetVisionGridGlobalIndex_Static(FIntPoint IJ);
	static FORCEINLINE FIntPoint GetVisionGridTileIJ_Static(int32 GlobalIndex);
	static FORCEINLINE bool IsVisionGridIJValid_Static(FIntPoint IJ);
	
	//~ Begin Static Minimap Grid Conversion Functions
	// 静态小地图网格转换函数
	
	static FORCEINLINE FIntPoint ConvertWorldLocationToMinimapTileIJ_Static(const FVector2D& WorldLocation);
	static FORCEINLINE FVector2D ConvertMinimapTileIJToWorldLocation_Static(const FIntPoint& TileIJ);
	
private:
	// 小地图转换的私有辅助函数
	static FORCEINLINE FVector2f ConvertWorldSpaceLocationToMinimapGridSpace_Static(const FVector2D& WorldLocation);
	static FORCEINLINE FIntPoint ConvertMinimapGridLocationToTileIJ_Static(const FVector2f& GridLocation);

private:
	/** 单例实例指针 */
	static UMinimapDataSubsystem* SingletonInstance;

	double LastHashGridPerfLogTime = 0.0;
	double LastDrawPerfLogTime = 0.0;

	int32 HashGridPerfSampleCount = 0;
	FMinimapHashGridPerfStats HashGridPerfAccum;
	int32 DrawPerfSampleCount = 0;
	FMinimapDrawPerfStats DrawPerfAccum;

	void RecordHashGridPerfStats(const FMinimapHashGridPerfStats& Stats);
	void FlushHashGridPerfStats(double CurrentTime);
	void FlushDrawPerfStats(double CurrentTime);
	void AppendPerformanceCsvLine(const FString& Channel, const FString& CsvColumns) const;
};

template<>
struct TMassExternalSubsystemTraits<UMinimapDataSubsystem> final
{
	enum
	{
		GameThreadOnly = false
	};
};

//~ Begin Inline Implementations of Static Functions

FORCEINLINE FVector2f UMinimapDataSubsystem::ConvertWorldSpaceLocationToVisionGridSpace_Static(const FVector2D& WorldLocation)
{
	check(SingletonInstance);
	return FVector2f((WorldLocation - SingletonInstance->GridBottomLeftWorldLocation) / SingletonInstance->VisionTileSize);
}

FORCEINLINE FIntPoint UMinimapDataSubsystem::ConvertVisionGridLocationToTileIJ_Static(const FVector2f& GridLocation)
{
	return FIntPoint(FMath::FloorToInt(GridLocation.X), FMath::FloorToInt(GridLocation.Y));
}

FORCEINLINE FIntPoint UMinimapDataSubsystem::ConvertWorldLocationToVisionTileIJ_Static(const FVector2D& WorldLocation)
{
	check(SingletonInstance);
	const FVector2f GridLocation = FVector2f((WorldLocation - SingletonInstance->GridBottomLeftWorldLocation) / SingletonInstance->VisionTileSize);
	return FIntPoint(FMath::FloorToInt(GridLocation.X), FMath::FloorToInt(GridLocation.Y));
}

FORCEINLINE FVector2D UMinimapDataSubsystem::ConvertVisionTileIJToTileCenterWorldLocation_Static(const FIntPoint& IJ)
{
	check(SingletonInstance);
	return SingletonInstance->GridBottomLeftWorldLocation + (FVector2D(IJ) + 0.5f) * SingletonInstance->VisionTileSize;
}

FORCEINLINE int32 UMinimapDataSubsystem::GetVisionGridGlobalIndex_Static(FIntPoint IJ)
{
	check(SingletonInstance);
	return IJ.X * SingletonInstance->VisionGridResolution.Y + IJ.Y;
}

FORCEINLINE FIntPoint UMinimapDataSubsystem::GetVisionGridTileIJ_Static(int32 GlobalIndex)
{
	check(SingletonInstance);
	return { GlobalIndex / SingletonInstance->VisionGridResolution.Y, GlobalIndex % SingletonInstance->VisionGridResolution.Y };
}

FORCEINLINE bool UMinimapDataSubsystem::IsVisionGridIJValid_Static(FIntPoint IJ)
{
	check(SingletonInstance);
	return IJ.X >= 0 && IJ.Y >= 0 && IJ.X < SingletonInstance->VisionGridResolution.X && IJ.Y < SingletonInstance->VisionGridResolution.Y;
}

FORCEINLINE FIntPoint UMinimapDataSubsystem::ConvertWorldLocationToMinimapTileIJ_Static(const FVector2D& WorldLocation)
{
	check(SingletonInstance);
	const FVector2f GridLocation = ConvertWorldSpaceLocationToMinimapGridSpace_Static(WorldLocation);
	return ConvertMinimapGridLocationToTileIJ_Static(GridLocation);
}

FORCEINLINE FVector2D UMinimapDataSubsystem::ConvertMinimapTileIJToWorldLocation_Static(const FIntPoint& TileIJ)
{
	check(SingletonInstance);
	return SingletonInstance->GridBottomLeftWorldLocation + (FVector2D(TileIJ) + 0.5f) * SingletonInstance->MinimapTileSize;
}

FORCEINLINE FVector2f UMinimapDataSubsystem::ConvertWorldSpaceLocationToMinimapGridSpace_Static(const FVector2D& WorldLocation)
{
	check(SingletonInstance);
	return FVector2f((WorldLocation - SingletonInstance->GridBottomLeftWorldLocation) / SingletonInstance->MinimapTileSize);
}

FORCEINLINE FIntPoint UMinimapDataSubsystem::ConvertMinimapGridLocationToTileIJ_Static(const FVector2f& GridLocation)
{
	return FIntPoint(FMath::FloorToInt(GridLocation.X), FMath::FloorToInt(GridLocation.Y));
}
