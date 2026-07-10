// Copyright Winyunq, 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MassEntityTypes.h"
#include "MassEntityQuery.h"
#include "Components/Image.h"
#include "MinimapWidget.generated.h"

UENUM(BlueprintType)
enum class EFogOfWarMinimapTeamColor : uint8
{
	Neutral = 0 UMETA(DisplayName = "Neutral"),
	Player = 1 UMETA(DisplayName = "Player"),
	Enemy = 2 UMETA(DisplayName = "Enemy"),
	AllyBlue = 3 UMETA(DisplayName = "Ally Blue"),
	Yellow = 4 UMETA(DisplayName = "Yellow"),
	Cyan = 5 UMETA(DisplayName = "Cyan"),
	Purple = 6 UMETA(DisplayName = "Purple"),
	Orange = 7 UMETA(DisplayName = "Orange")
};

class UImage;
class UTextureRenderTarget2D;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UMinimapDataSubsystem;
class AFogOfWar;

// 声明委托
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMinimapDrag, const FVector&, WorldLocationDelta);

/**
 * UMinimapWidget
 * 
 * 高度自动化、易于使用的小地图控件。
 * 自动从主战争迷雾Actor同步坐标系，自动绑定UI中的Image控件，并提供清晰的点击、拖动、释放事件回调。
 */
UCLASS(Config=MassBattle)
class FOGOFWAR_API UMinimapWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 初始化小地图系统。尝试同步 FogOfWar Actor，如果不存在则使用 Subsystem 配置。 */
	UFUNCTION(BlueprintCallable, Category = "Minimap")
	bool InitializeMinimapSystem();

	/** 将小地图上的UV坐标转换为世界坐标 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Minimap")
	FVector ConvertMinimapUVToWorldLocation(const FVector2D& UVPosition) const;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// 核心更新函数
	void UpdateMinimapTexture();

	// 根据单位数量选择不同的绘制路径
	void DrawInMassSize();  // 大于阈值时，使用缓存绘制
	void DrawInLessSize();  // 小于等于阈值时，直接查询绘制

protected:
	// --- 蓝图可配置属性 (Config) ---

	/** 用于绘制小地图的父材质 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap|Appearance")
	TObjectPtr<UMaterialInterface> MinimapMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap|Appearance")
	bool bDrawUnitsWithCanvasOverlay = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap|Appearance")
	bool bEncodeUnitsIntoMinimapMaterial = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap|Appearance", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float CanvasUnitDotSize = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap|Appearance", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float MinimapFogOpacity = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap|Appearance")
	FLinearColor MinimapRevealedColor = FLinearColor(0.08f, 0.11f, 0.12f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap|Team")
	FLinearColor DefaultTeamColor = FLinearColor(0.7f, 0.7f, 0.7f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap|Team")
	TArray<FLinearColor> RecommendedTeamColors = {
		FLinearColor(0.45f, 0.45f, 0.45f, 1.0f),
		FLinearColor(0.10f, 0.72f, 0.18f, 1.0f),
		FLinearColor(0.85f, 0.12f, 0.10f, 1.0f),
		FLinearColor(0.12f, 0.34f, 0.95f, 1.0f),
		FLinearColor(0.95f, 0.72f, 0.10f, 1.0f),
		FLinearColor(0.10f, 0.80f, 0.90f, 1.0f),
		FLinearColor(0.75f, 0.20f, 0.85f, 1.0f),
		FLinearColor(0.95f, 0.45f, 0.12f, 1.0f)
	};

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap|Team")
	bool bNormalizeTeamColorDirection = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap|Team", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float NormalUnitColorLength = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap|Team", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SelectedUnitColorLength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap|Team")
	FLinearColor CombatUnitColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap|Team")
	bool bEnableCombatColorFlash = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap|Team", meta = (ClampMin = "0.01", UIMin = "0.01"))
	float CombatColorFlashHz = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap|Team", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DefaultUnitPixelRadius = 1.5f;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Minimap|Team")
	FLinearColor GetRecommendedTeamColor(EFogOfWarMinimapTeamColor TeamColor) const;

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Minimap|Team")
	void NormalizeRecommendedTeamColors();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Minimap|Team")
	void ExportMinimapColorConfig();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Minimap|Team")
	FString GetMinimapColorConfigPath() const;

	/** 小地图纹理的分辨率；Zero 表示使用 256x256。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Minimap|Performance", meta = (DisplayName = "Texture Resolution (0 = Auto)"))
	FIntPoint TextureResolution = FIntPoint::ZeroValue;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap|Performance", meta = (ClampMin = "0"))
	int32 DirectQueryThreshold = 1024;

	/** 小地图更新的频率（秒）。0表示每帧更新。*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap|Performance", meta = (ClampMin = "0.0"))
	float UpdateInterval = 1.0f;
	/// 单位数量
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap|Performance", meta = (ClampMin = "0.0"))
	int32 MaxUnits = 8192;
	// --- 自动绑定和内部状态 (Internal State) ---

	/** 【自动绑定】请在UMG编辑器中，将您的Image控件命名为'MinimapImage' */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> MinimapImage;

	/** 渲染目标（RT）*/
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Minimap|Internal")
	TObjectPtr<UTextureRenderTarget2D> MinimapRenderTarget;

	/** 动态材质实例 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Minimap|Internal")
	TObjectPtr<UMaterialInstanceDynamic> MinimapMaterialInstance;

	/** [数据纹理] 缓存视野单位数据的动态纹理 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Minimap|Internal")
	TObjectPtr<UTexture2D> VisionDataTexture;

	/** [数据纹理] 缓存图标单位数据的动态纹理 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Minimap|Internal")
	TObjectPtr<UTexture2D> IconDataTexture;

	/** [数据纹理] 缓存图标颜色数据的动态纹理 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Minimap|Internal")
	TObjectPtr<UTexture2D> IconColorTexture;

	/** 指向数据仓库的指针 */
	UPROPERTY(Transient)
	TObjectPtr<UMinimapDataSubsystem> MinimapDataSubsystem;

	/** Query for counting entities with a minimap representation. Configured once on initialization. */
	FMassEntityQuery CountQuery;

	/** Query for drawing entities directly to the minimap. Configured once on initialization. */
	FMassEntityQuery DrawQuery;

	FIntPoint GetEffectiveTextureResolution() const;
	bool EnsureMinimapRenderTarget(const FIntPoint& DesiredResolution);

	// Tick更新频率控制器
	float TimeSinceLastUpdate = 0.0f;
	float TimeSinceLastInitRetry = 0.0f;

	// -- Internal Debug --
	bool bInitPermanentlyFailed = false;
	bool bIsSuccessfullyInitialized = false;
};
