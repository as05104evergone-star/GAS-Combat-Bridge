// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

struct IConsoleCommand;
class SDockTab;
class FSpawnTabArgs;

/**
 * GASCombatBridgeEditor 模块。
 *
 * 只负责「把面板挂到编辑器上」，不包含任何战斗逻辑。
 * 提供三个入口，确保你总能找到它：
 *   1. 菜单「窗口」→ 工具 → GAS 战斗调试
 *   2. 关卡编辑器工具栏上的「GAS 调试」按钮（在「播放」按钮附近）
 *   3. 控制台命令 `GASCombat.Debug`
 */
class FGASCombatBridgeEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** 面板页签的稳定 ID */
	static FName GetTabName();

private:
	/** 生成面板页签 */
	TSharedRef<SDockTab> OnSpawnTab(const FSpawnTabArgs& Args);

	/** 打开（或把已打开的面板切到前台） */
	void OpenPanel();

	/** ToolMenus 就绪后挂工具栏按钮 */
	void RegisterMenus();

	/** UToolMenus::RegisterStartupCallback 返回的句柄，注销时要用 */
	FDelegateHandle ToolMenuStartupHandle;

	/** 注册的控制台命令，退出时要注销 */
	IConsoleCommand* DebugConsoleCommand = nullptr;
};
