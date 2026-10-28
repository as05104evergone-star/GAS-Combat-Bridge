// Copyright Epic Games, Inc. All Rights Reserved.

#include "GASCombatBridge.h"

#include "AbilitySystemGlobals.h"
#include "AttributeSet.h"
#include "GASCombatStatics.h"
#include "GameplayAbilitiesDeveloperSettings.h"
#include "GameplayTagsManager.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "FGASCombatBridgeModule"

DEFINE_LOG_CATEGORY(LogGASCombat);

// ============================================================================
//  插件自带标签 —— 用 C++ 原生注册
//
//  【为什么不用 Config/DefaultGameplayTags.ini】
//  插件的 Config 目录**不会**被并进引擎 GameplayTags 模块的配置层级里：
//  UGameplayTagsSettings 是 UCLASS(config=GameplayTags) 且位于引擎的 GameplayTags 模块中，
//  它读 DefaultGameplayTags.ini 时走的是标准层级（引擎/工程/用户），
//  只有「插件自己的 settings 类」才会额外走 {PLUGIN}/Config/Default{TYPE}.ini 那一层。
//  也就是说：把标签写进插件 Config 目录，表面上很合理，实际**一条都不会生效**，
//  而且完全没有报错 —— 这类"静默失效"正是本插件最想消灭的东西，所以这里改成原生注册。
//
//  （Config/DefaultGameplayTags.ini 仍保留在仓库里，作为「想把这些标签并进你自己工程配置」
//    时的复制来源，但插件运行不再依赖它。）
// ============================================================================
namespace GASCombatBridge::Private
{
	struct FCombatTagDef
	{
		const TCHAR* Name;
		const TCHAR* Comment;
	};

	/** 插件自带的全部标签 */
	static const FCombatTagDef GCombatTags[] =
	{
		// 数值传递标签（配合 GameplayEffect 的「调用方赋值 / SetByCaller」使用）
		{ TEXT("Data.Damage"),       TEXT("伤害数值。效果里把修饰符数值类型设为 SetByCaller、键选这个标签，「对目标造成伤害」节点会自动把伤害值填进来") },
		{ TEXT("Data.Healing"),      TEXT("治疗数值。用法同 Data.Damage") },
		{ TEXT("Data.Duration"),     TEXT("持续时长。用于运行时决定一个效果持续几秒，例如按蓄力时间决定灼烧时长") },
		{ TEXT("Data.Combo.Advance"),TEXT("连招段数增量。把 GameplayEffect 的修饰符数值类型设为 SetByCaller、键选这个标签，即可用同一个效果承担 起手+1 / 推进+1 / 打满绕回-2 / 收尾归零-N 四种语义") },

		// 技能激活失败原因
		{ TEXT("Activation.Fail.CanActivateAbility"), TEXT("技能自身的 CanActivateAbility 检查未通过") },
		{ TEXT("Activation.Fail.CantAffordCost"),     TEXT("消耗不足（蓝量/资源不够）") },
		{ TEXT("Activation.Fail.OnCooldown"),         TEXT("技能正在冷却中") },
		{ TEXT("Activation.Fail.BlockedByTags"),      TEXT("被「阻止他人技能」类标签挡下（眩晕/沉默/硬直）") },
		{ TEXT("Activation.Fail.MissingTags"),        TEXT("缺少技能要求的「激活所需标签」") },
		{ TEXT("Activation.Fail.Networking"),         TEXT("网络执行策略不允许在当前端激活") },
		{ TEXT("Activation.Fail.IsDead"),             TEXT("角色已死亡") },

		// 常用战斗状态（示例，可按需删改）
		{ TEXT("State.Dead"),         TEXT("已死亡") },
		{ TEXT("State.Stunned"),      TEXT("被眩晕，通常配合「阻止他人技能」使用") },
		{ TEXT("State.Invulnerable"), TEXT("无敌帧") },
		{ TEXT("State.Attacking"),    TEXT("正在攻击，常用于「攻击中不能再次攻击」") },
		{ TEXT("Cooldown.Skill"),     TEXT("技能冷却标签的父标签。每个技能的冷却效果应授予 Cooldown.Skill.<技能名> 这样的唯一子标签") },
	};

	/**
	 * 注册插件自带标签。
	 * 项目里已经自己配过同名标签时跳过，不覆盖、也不刷「重复注册」警告 ——
	 * 使用者想接管这些标签的注释文案时不会被插件干扰。
	 */
	void RegisterCombatTags()
	{
		UGameplayTagsManager& TagManager = UGameplayTagsManager::Get();

		int32 RegisteredCount = 0;
		for (const FCombatTagDef& Def : GCombatTags)
		{
			const FName TagName(Def.Name);
			if (TagManager.RequestGameplayTag(TagName, /*ErrorIfNotFound=*/false).IsValid())
			{
				continue;
			}

			TagManager.AddNativeGameplayTag(TagName, FString(Def.Comment));
			++RegisteredCount;
		}

		UE_LOG(LogGASCombat, Log,
			TEXT("【GAS战斗桥接】已注册 %d 个自带标签（另有 %d 个在项目里已存在，保持不动）。"),
			RegisteredCount, UE_ARRAY_COUNT(GCombatTags) - RegisteredCount);
	}

	/**
	 * 把「技能激活失败原因」接上。
	 *
	 * 引擎默认**不配**这几个标签（UGameplayAbilitiesDeveloperSettings 里它们默认是空的），
	 * 所以技能放不出来时 GAS 不会附带任何原因 —— 只能靠猜。
	 * 这里在标签为空时补上默认值；使用者已经在项目设置里配过的，一律不覆盖。
	 *
	 * 两处都要写：
	 *   DeveloperSettings —— 项目设置界面显示的就是它
	 *   UAbilitySystemGlobals 的成员 —— 引擎运行时真正读的是它（见 GameplayAbility.cpp）
	 * 后者不能省：InitGlobalData 通常在模块加载前就跑过了，只改 settings 不会同步过去。
	 */
	void ApplyActivateFailTagDefaults()
	{
		auto ResolveTag = [](const TCHAR* TagName) -> FGameplayTag
		{
			return FGameplayTag::RequestGameplayTag(FName(TagName), /*ErrorIfNotFound=*/false);
		};

		UGameplayAbilitiesDeveloperSettings* Settings = GetMutableDefault<UGameplayAbilitiesDeveloperSettings>();
		UAbilitySystemGlobals& Globals = UAbilitySystemGlobals::Get();

		int32 AppliedCount = 0;
		auto Ensure = [&AppliedCount, &ResolveTag](FGameplayTag& Target, const TCHAR* TagName)
		{
			if (Target.IsValid())
			{
				return; // 使用者已配置，尊重它
			}
			Target = ResolveTag(TagName);
			if (Target.IsValid())
			{
				++AppliedCount;
			}
		};

		if (Settings)
		{
			Ensure(Settings->ActivateFailCanActivateAbilityTag, TEXT("Activation.Fail.CanActivateAbility"));
			Ensure(Settings->ActivateFailCooldownTag,         TEXT("Activation.Fail.OnCooldown"));
			Ensure(Settings->ActivateFailCostTag,             TEXT("Activation.Fail.CantAffordCost"));
			Ensure(Settings->ActivateFailNetworkingTag,       TEXT("Activation.Fail.Networking"));
			Ensure(Settings->ActivateFailTagsBlockedTag,      TEXT("Activation.Fail.BlockedByTags"));
			Ensure(Settings->ActivateFailTagsMissingTag,      TEXT("Activation.Fail.MissingTags"));
		}

		Ensure(Globals.ActivateFailCooldownTag,      TEXT("Activation.Fail.OnCooldown"));
		Ensure(Globals.ActivateFailCostTag,          TEXT("Activation.Fail.CantAffordCost"));
		Ensure(Globals.ActivateFailNetworkingTag,    TEXT("Activation.Fail.Networking"));
		Ensure(Globals.ActivateFailTagsBlockedTag,   TEXT("Activation.Fail.BlockedByTags"));
		Ensure(Globals.ActivateFailTagsMissingTag,   TEXT("Activation.Fail.MissingTags"));

		if (AppliedCount > 0)
		{
			UE_LOG(LogGASCombat, Log,
				TEXT("【GAS战斗桥接】已接上技能激活失败原因（%d 处）。")
				TEXT("技能放不出来时，「技能激活失败时」事件会给出中文原因。"),
				AppliedCount);
		}
	}
}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
namespace
{
	/**
	 * GASCombat.ListAttributes 的实现。
	 *
	 * 回答一个几乎每个 GAS 使用者都会问的问题：
	 *   「为什么我在 GameplayEffect 的『属性』下拉框里找不到某个属性？」
	 *
	 * 判定规则直接照搬引擎（AttributeSet.cpp 的 FGameplayAttribute::GetAllAttributeProperties）：
	 * 只收录「父类是 AttributeSet」且「不是蓝图生成的类」的属性。
	 * 蓝图里新建的 AttributeSet 子类会被引擎直接排除掉 —— 这就是那个问题的答案。
	 * 所以这个命令列出的内容 = 下拉框里能选到的内容。
	 */
	void ListAllAttributes()
	{
		TArray<FProperty*> Properties;
		FGameplayAttribute::GetAllAttributeProperties(Properties);

		// 按所属属性集分组，读起来才像一份清单
		TMap<UClass*, TArray<FProperty*>> GroupedBySet;
		int32 AttributeCount = 0;
		for (FProperty* Property : Properties)
		{
			if (!Property)
			{
				continue;
			}

			// 下拉框里真正能选的只有 FGameplayAttributeData 类型的属性，
			// 属性集上的其他成员（比如缓存的临时变量）不算，这里一并滤掉。
			const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
			if (!StructProperty || StructProperty->Struct != FGameplayAttributeData::StaticStruct())
			{
				continue;
			}

			GroupedBySet.FindOrAdd(Property->GetOwnerClass()).Add(Property);
			++AttributeCount;
		}

		TArray<UClass*> SetClasses;
		GroupedBySet.GetKeys(SetClasses);
		SetClasses.Sort([](UClass& A, UClass& B)
		{
			return A.GetName() < B.GetName();
		});

		UE_LOG(LogGASCombat, Display,
			TEXT("【GAS战斗桥接】引擎「属性」下拉框里可选：%d 个属性集 / %d 个属性。"),
			SetClasses.Num(), AttributeCount);

		for (UClass* SetClass : SetClasses)
		{
			// 类的中文显示名（UCLASS 的 DisplayName 元数据）。
			// UObject 的元数据只在带编辑器数据的构建里存在（WITH_METADATA），
			// 所以打包后的 Development 构建里会退回类名 —— 这不是缺陷，是引擎的既定规则。
			FString SetLabel;
#if WITH_METADATA
			if (SetClass->HasMetaData(TEXT("DisplayName")))
			{
				SetLabel = SetClass->GetMetaData(TEXT("DisplayName"));
			}
#endif
			if (SetLabel.IsEmpty())
			{
				SetLabel = SetClass->GetName();
			}

			TArray<FString> AttributeNames;
			for (const FProperty* Property : GroupedBySet[SetClass])
			{
				AttributeNames.Add(FString::Printf(TEXT("%s(%s)"),
					*UGASCombatStatics::GetAttributeDisplayName(Property->GetFName()),
					*Property->GetName()));
			}

			UE_LOG(LogGASCombat, Display, TEXT("  · %s：%s"),
				*SetLabel, *FString::Join(AttributeNames, TEXT("、")));
		}

		UE_LOG(LogGASCombat, Display,
			TEXT("  提示：蓝图里新建的属性集不会出现在这个列表里（引擎会排除蓝图生成的类），")
			TEXT("所以「需要被效果修改的数值」必须在 C++ 里声明。"));
	}
}
#endif

void FGASCombatBridgeModule::StartupModule()
{
	// 顺序有讲究：
	//   1. 先把标签注册好 —— 后面两步都要用到它们
	//   2. 再把「激活失败原因」接上（依赖第 1 步的标签已存在）
	//   3. 最后才 InitGlobalData —— 它会从 DeveloperSettings 把标签同步进 UAbilitySystemGlobals
	GASCombatBridge::Private::RegisterCombatTags();
	GASCombatBridge::Private::ApplyActivateFailTagDefaults();

	// 明确打一条启动日志：排查「插件到底有没有加载」时，在输出日志里搜 GASCombatBridge 即可。
	// 插件没打印这一行 = 模块没被加载（检查 .uplugin 是否启用、二进制是否匹配引擎版本）。
	UE_LOG(LogGASCombat, Display,
		TEXT("【GAS战斗桥接】插件已加载（引擎 5.7.4 适配版，v1.1.0）。")
		TEXT("蓝图里右键搜索「战斗」或「技能」可见节点；调试面板见「窗口 → 工具 → GAS 战斗调试」。"));
	UE_LOG(LogGASCombat, Display,
		TEXT("【GAS战斗桥接】内置属性集：通用战斗属性集（生命/护盾/法力/攻防/移速等 11 项）、")
		TEXT("连招属性集（连招段数 / 连招段数上限）。")
		TEXT("想确认某个属性能不能被效果修改，敲控制台命令 GASCombat.ListAttributes。"));

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	ListAttributesCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("GASCombat.ListAttributes"),
		TEXT("列出 GameplayEffect 的「属性」下拉框里能选到的全部属性（含中文名）。用来排查「属性为什么找不到」"),
		FConsoleCommandDelegate::CreateStatic(&ListAllAttributes),
		ECVF_Default);

	UE_LOG(LogGASCombat, Display,
		TEXT("【GAS战斗桥接】控制台命令已注册：GASCombat.ListAttributes（随时重查属性清单）。"));
#endif

	// GAS 的全局数据（TargetData / GameplayCue / 预测）需要显式初始化。
	// 引擎通常会在 UAbilitySystemGlobals::PostInitProperties 里自动调用，但纯蓝图工程
	// 在没有 C++ 模块的启动流程里偶尔会晚一步，导致第一次用 TargetData 时报错。
	// 这里做一次幂等兜底调用：已经初始化过就直接返回，不会有副作用。
	UAbilitySystemGlobals& AbilitySystemGlobals = UAbilitySystemGlobals::Get();
	if (!AbilitySystemGlobals.IsAbilitySystemGlobalsInitialized())
	{
		AbilitySystemGlobals.InitGlobalData();
	}
}

void FGASCombatBridgeModule::ShutdownModule()
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (ListAttributesCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(ListAttributesCommand);
		ListAttributesCommand = nullptr;
	}
#endif
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FGASCombatBridgeModule, GASCombatBridge)
