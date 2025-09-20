// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * @file FogOfWarModule.h
 * @brief 声明了战争迷雾插件模块的主类。
 */

/**
 * @class FFogOfWarModule
 * @brief 战争迷雾插件的主模块类。
 * @details 实现了IModuleInterface接口，用于在引擎启动和关闭时挂载和卸载本插件。
 */
class FFogOfWarModule : public IModuleInterface
{
public:

	/**
	 * @brief       在模块加载时调用。
	 * @details     当插件被引擎加载时，此函数会被执行，用于执行必要的初始化操作。
	 */
	virtual void StartupModule() override;

	/**
	 * @brief       在模块卸载时调用。
	 * @details     当插件被引擎卸载或引擎关闭时，此函数会被执行，用于执行清理操作。
	 */
	virtual void ShutdownModule() override;
};
