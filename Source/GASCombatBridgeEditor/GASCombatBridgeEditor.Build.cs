// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

/**
 * GASCombatBridgeEditor —— 编辑器专用模块。
 *
 * 只做一件事：提供一个可停靠的「GAS 战斗调试」面板。
 * 这个模块不会被编译进打包后的游戏（Type = Editor），所以对游戏包体没有任何影响。
 */
public class GASCombatBridgeEditor : ModuleRules
{
	public GASCombatBridgeEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				// 运行时模块：面板要靠它的节点库采集战斗数据，保证「面板显示」与「蓝图能拿到」完全一致
				"GASCombatBridge",
				"GameplayAbilities",
				"GameplayTags",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore",
				"InputCore",
				// 编辑器框架：停靠页签、工具栏按钮、World 枚举
				"UnrealEd",
				"EditorFramework",
				"ToolMenus",
				"WorkspaceMenuStructure",
				"LevelEditor",
				"Projects",
			}
		);
	}
}
