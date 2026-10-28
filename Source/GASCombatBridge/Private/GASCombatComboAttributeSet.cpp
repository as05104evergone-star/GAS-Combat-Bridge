// Copyright Epic Games, Inc. All Rights Reserved.

#include "GASCombatComboAttributeSet.h"

#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"

UGASCombatComboAttributeSet::UGASCombatComboAttributeSet()
{
	// 默认值只是「开箱可玩」的占位。正式项目建议在「技能集」资产的
	// 「属性初始值」列表里配，这样每个角色/每把武器能有不同的段数上限。
	InitComboIndex(0.f);     // 0 = 不在连招中
	InitMaxComboIndex(3.f);  // 默认三段
}

void UGASCombatComboAttributeSet::ClampComboAttribute(const FGameplayAttribute& Attribute, float& NewValue) const
{
	if (Attribute == GetComboIndexAttribute())
	{
		// ====================================================================
		//  【这一条规则必须与教程一字不差，改它会直接改坏连招手感】
		//
		//  只守下界，上界【刻意不钳】。
		//
		//  原因：如果写成 FMath::Clamp(NewValue, 0, MaxComboIndex)，那么打满第 3 段后
		//  再次推进时 3 + 1 会被夹成 3，连招会【永久停在第三段】，再也接不出第 1 段。
		//
		//  「打满之后绕回第 1 段」是**玩法决策**，写法是能力里的增量计算
		//  （Next - Current，绕回时 Next = 1、Current = 3，增量为 -2），
		//  不属于属性集的职责。
		//
		//  边界口径：
		//    属性集负责「数据约束」  —— 段数不会是负数
		//    能力负责  「玩法决策」  —— 起手 1 / 推进 +1 / 绕回 1 / 收尾归零
		//
		//  验收点：ComboIndex = 3 时施加增量 -2 必须得到 1。若这里钳了上界会得到 3 或 0。
		// ====================================================================
		NewValue = FMath::Max(NewValue, 0.f);
	}
	else if (Attribute == GetMaxComboIndexAttribute())
	{
		// 至少 1 段。0 段没有任何玩法含义，而且会让「打满绕回」的判定除零/死循环。
		NewValue = FMath::Max(NewValue, 1.f);
	}
	// 注意：这里**没有**「顺手钳到上限」的分支 —— 见上面的说明，那是有意为之。
}

void UGASCombatComboAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	ClampComboAttribute(Attribute, NewValue);
}

void UGASCombatComboAttributeSet::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const
{
	Super::PreAttributeBaseChange(Attribute, NewValue);
	ClampComboAttribute(Attribute, NewValue);
}

void UGASCombatComboAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// REPNOTIFY_Always：段数在客户端可能被预测改过同值，用 Always 保证 OnRep 一定触发
	// （连招绕回时「3 → 1」这种变化必须让客户端立刻跟上，否则 UI 会显示错段数）。
	DOREPLIFETIME_CONDITION_NOTIFY(UGASCombatComboAttributeSet, ComboIndex, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UGASCombatComboAttributeSet, MaxComboIndex, COND_None, REPNOTIFY_Always);
}

#define GASCOMBAT_DEFINE_COMBO_REP(PropertyName) \
	void UGASCombatComboAttributeSet::OnRep_##PropertyName(const FGameplayAttributeData& OldValue) \
	{ \
		GAMEPLAYATTRIBUTE_REPNOTIFY(UGASCombatComboAttributeSet, PropertyName, OldValue); \
	}

GASCOMBAT_DEFINE_COMBO_REP(ComboIndex)
GASCOMBAT_DEFINE_COMBO_REP(MaxComboIndex)

#undef GASCOMBAT_DEFINE_COMBO_REP
