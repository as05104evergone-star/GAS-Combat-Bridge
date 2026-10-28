// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class GASCombatBridge : ModuleRules
{
	public GASCombatBridge(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				// GAS 本体：ASC / GameplayAbility / GameplayEffect / AttributeSet
				"GameplayAbilities",
				// 标签系统：FGameplayTag / FGameplayTagContainer
				"GameplayTags",
				// ASC 的父类所在模块（UGameplayTasksComponent）
				"GameplayTasks",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"InputCore",
				// 「发送状态树事件」节点用（FStateTreeEvent 在 StateTreeModule，
				// UStateTreeComponent 在 GameplayStateTreeModule）。
				// 这两个插件在 .uplugin 里已声明为依赖，所以模块一定存在。
				// 注意：StateTree / GameplayStateTree 在引擎里默认是关闭的，
				// 是靠本插件的依赖声明把它们打开的 —— 详见 使用说明.md 的「依赖与影响」一节。
				"StateTreeModule",
				"GameplayStateTreeModule",
			}
		);
	}
}
