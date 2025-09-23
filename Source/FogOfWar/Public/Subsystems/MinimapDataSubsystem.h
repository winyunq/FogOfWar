// Copyright Winyunq, 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassSubsystemBase.h"
#include "MassEntityTypes.h" // Required for FMassEntityHandle
#include "MinimapDataSubsystem.generated.h"

/**
 * UMinimapDataSubsystem
 * 
 * 一个全局的子系统，用作小地图数据的中央缓存或“仓库”。
 * UMinimapDataCollectorProcessor 负责向这里写入最新的数据，
 * UMinimapWidget 负责从这里读取数据用于渲染。
 */
UCLASS()
class FOGOFWAR_API UMinimapDataSubsystem : public UMassSubsystemBase
{
	GENERATED_BODY()

public:

	/** 所有视野提供者的缓存数据 (位置X, Y, Z, 视野半径W) */
	TMap<FMassEntityHandle, FVector4> VisionSources;

	/** 所有小地图图标的位置数据 (位置X, Y, 尺寸Z, 强度W) */
	TMap<FMassEntityHandle, FVector4> IconLocations;

	/** 所有小地图图标的颜色数据 (R, G, B, A) */
	TMap<FMassEntityHandle, FLinearColor> IconColors;
};
