// Copyright Epic Games, Inc. All Rights Reserved.

#include "GASCombatAbilityTaskLibrary.h"

#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"

UAbilityTask_WaitGameplayEvent* UGASCombatAbilityTaskLibrary::WaitGameplayEventKeepListening(
	UGameplayAbility* OwningAbility,
	FGameplayTag EventTag,
	AActor* OptionalExternalTarget,
	bool OnlyMatchExact)
{
	// 完全委托给引擎原生工厂，不复制它的任何内部状态 ——
	// 这样引擎升级改了内部实现（多了字段、换了构造函数）时本节点不需要跟着改。
	//
	// 唯一固定下来的参数是 OnlyTriggerOnce = false：
	// 连招每一段都要重新等同一个判定窗口事件，只触发一次会直接卡死第二段。
	return UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		OwningAbility,
		EventTag,
		OptionalExternalTarget,
		/*OnlyTriggerOnce=*/false,
		OnlyMatchExact);
}
