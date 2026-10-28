// Copyright Epic Games, Inc. All Rights Reserved.

#include "GASCombatBridgeEditor.h"

#include "Framework/Docking/TabManager.h"
#include "GASCombatBridge.h"
#include "HAL/IConsoleManager.h"
#include "SGASCombatDebugPanel.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "FGASCombatBridgeEditorModule"

FName FGASCombatBridgeEditorModule::GetTabName()
{
	static const FName TabName(TEXT("GASCombatDebug"));
	return TabName;
}

void FGASCombatBridgeEditorModule::StartupModule()
{
	// 命令行工具（烘焙、自动化测试等）里没有 Slate，跳过所有界面注册
	if (IsRunningCommandlet())
	{
		return;
	}

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		GetTabName(),
		FOnSpawnTab::CreateRaw(this, &FGASCombatBridgeEditorModule::OnSpawnTab))
		.SetDisplayName(LOCTEXT("TabTitle", "GAS 战斗调试"))
		.SetTooltipText(LOCTEXT("TabTooltip",
			"实时查看并直接修改角色的战斗状态：属性、技能、效果、标签。\n"
			"需要先进入 PIE（点播放）才会采集数据。"))
		// 挂到「窗口 → 工具」分类下，和引擎自带工具放在一起
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Toolbar.Settings"));

	// 工具栏要在 ToolMenus 初始化完成之后再挂，所以走 startup callback
	ToolMenuStartupHandle = UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FGASCombatBridgeEditorModule::RegisterMenus));

	// 再给一个控制台入口：不依赖任何 UI 布局，而且敲命令比找菜单快
	DebugConsoleCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("GASCombat.Debug"),
		TEXT("打开 GAS 战斗调试面板（实时查看/修改属性、技能、效果、标签）"),
		FConsoleCommandDelegate::CreateRaw(this, &FGASCombatBridgeEditorModule::OpenPanel),
		ECVF_Default);

	// 打一条日志：排查「面板为什么找不到」时，在输出日志里搜 LogGASCombat 即可。
	// 没打这一行 = 编辑器模块没被加载（检查 .uplugin 里的 GASCombatBridgeEditor 模块项）。
	UE_LOG(LogGASCombat, Display,
		TEXT("【GAS战斗桥接】编辑器调试面板已注册。三种打开方式：① 菜单「窗口 → 工具 → GAS 战斗调试」；")
		TEXT("② 关卡编辑器工具栏「GAS 调试」按钮；③ 控制台命令 GASCombat.Debug。"));
}

void FGASCombatBridgeEditorModule::ShutdownModule()
{
	if (IsRunningCommandlet())
	{
		return;
	}

	if (ToolMenuStartupHandle.IsValid())
	{
		UToolMenus::UnRegisterStartupCallback(ToolMenuStartupHandle);
		ToolMenuStartupHandle.Reset();
	}

	// 用模块指针作为 owner 把自己挂的工具栏按钮摘掉（FToolMenuOwner 支持从 void* 构造）
	UToolMenus::UnregisterOwner(FToolMenuOwner(this));

	if (FGlobalTabmanager::Get()->HasTabSpawner(GetTabName()))
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(GetTabName());
	}

	if (DebugConsoleCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(DebugConsoleCommand);
		DebugConsoleCommand = nullptr;
	}
}

TSharedRef<SDockTab> FGASCombatBridgeEditorModule::OnSpawnTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(LOCTEXT("TabLabel", "GAS 战斗调试"))
		[
			SNew(SGASCombatDebugPanel)
		];
}

void FGASCombatBridgeEditorModule::OpenPanel()
{
	// 面板已经打开时 TryInvokeTab 会把它切到前台，不会重复开一个
	FGlobalTabmanager::Get()->TryInvokeTab(GetTabName());
}

void FGASCombatBridgeEditorModule::RegisterMenus()
{
	// OwnerScoped 让本次添加的所有条目归属本模块，ShutdownModule 里可以整体摘掉
	FToolMenuOwnerScoped OwnerScoped(FToolMenuOwner(this));

	// 放在关卡编辑器工具栏的「播放」旁边——调试战斗时最顺手的位置
	UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.LevelEditorToolBar.PlayToolBar"));
	if (!ToolbarMenu)
	{
		// 菜单名在某些编辑器配置下可能不存在。没关系：窗口菜单和控制台命令仍然可用
		return;
	}

	FToolMenuSection& Section = ToolbarMenu->FindOrAddSection(TEXT("Play"));
	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		TEXT("GASCombatDebugButton"),
		FUIAction(FExecuteAction::CreateRaw(this, &FGASCombatBridgeEditorModule::OpenPanel)),
		LOCTEXT("ToolbarButton", "GAS 调试"),
		LOCTEXT("ToolbarButtonTooltip",
			"打开 GAS 战斗调试面板：实时查看属性 / 技能 / 效果 / 标签，\n"
			"并且可以直接改数值、激活技能、移除效果来做快速验证。\n"
			"（也可以直接敲控制台命令 GASCombat.Debug）"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Toolbar.Settings")));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FGASCombatBridgeEditorModule, GASCombatBridgeEditor)
