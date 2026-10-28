// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/** 插件统一日志分类。所有防呆提示都走这里，方便在 Output Log 里按 "LogGASCombat" 过滤 */
GASCOMBATBRIDGE_API DECLARE_LOG_CATEGORY_EXTERN(LogGASCombat, Log, All);

/**
 * GASCombatBridge 模块
 *
 * 这是一个「桥接层」：把虚幻引擎 Gameplay Ability System（GAS）里原本只有 C++ 才能碰到的东西，
 * 包装成中文蓝图节点，让不会写 C++ 的战斗策划也能独立完成战斗系统的搭建与验证。
 *
 * 本模块只做三件事：
 *   1. 补齐引擎没暴露给蓝图的 GAS 功能（属性变化监听、运行时属性按名字读写、技能组批量授予……）
 *   2. 把已有节点用中文重新命名并加上防呆提示，避免直译误导
 *   3. 提供运行时战斗信息可视化，让「为什么没生效」变得可回答
 */
class FGASCombatBridgeModule : public IModuleInterface
{
public:
	/** IModuleInterface 实现 */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/**
	 * 单例访问。插件内部用，蓝图不需要。
	 */
	static inline FGASCombatBridgeModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FGASCombatBridgeModule>("GASCombatBridge");
	}

	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("GASCombatBridge");
	}

private:
	/** GASCombat.ListAttributes 控制台命令的句柄（仅非发行版构建里存在） */
	struct IConsoleCommand* ListAttributesCommand = nullptr;
};
