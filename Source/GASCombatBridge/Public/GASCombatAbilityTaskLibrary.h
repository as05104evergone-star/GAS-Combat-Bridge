// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/Tasks/AbilityTask.h"
#include "GameplayTagContainer.h"
#include "GASCombatAbilityTaskLibrary.generated.h"

class UAbilityTask_WaitGameplayEvent;
class UGameplayAbility;

/**
 * 「技能任务」类中文节点的宿主。
 *
 * 【为什么需要这么一个类】
 *   技能任务（AbilityTask，例如「等待Gameplay事件」「播放蒙太奇」）在蓝图里的节点
 *   不是普通函数节点：它带异步执行引脚（那排「已完成 / 已中断」的输出引脚）。
 *   引擎是靠**工厂函数所在的类是不是 UGameplayTask 的子类**来识别这件事的
 *   （源码：GameplayTasksEditor 的 UK2Node_LatentGameplayTaskCall::GetMenuActions
 *    里 RegisterClassFactoryActions<UGameplayTask>）。
 *   所以想包一层中文的技能任务节点，工厂函数就必须挂在一个 UAbilityTask 子类上——
 *   挂在 UGASCombatStatics（普通函数库）上会退化成一个没有异步引脚的普通节点。
 *
 * 【本类不会被实例化】
 *   它只是工厂函数的宿主，所有实际逻辑都交给引擎原生的技能任务去做。
 *   这样做的代价最小：引擎升级时不需要跟着改任何内部状态。
 */
UCLASS()
class GASCOMBATBRIDGE_API UGASCombatAbilityTaskLibrary : public UAbilityTask
{
	GENERATED_BODY()

public:
	/**
	 * 等待指定的 Gameplay 事件；技能蓝图里用它来接「动画通知发出来的判定窗口」。
	 *
	 * 【和引擎原生「Wait Gameplay Event」的区别】
	 *   只有两点：① 中文名，② 把「只触发一次」固定在 false。
	 *   「只触发一次」是这套流程里最容易踩的坑：如果它是 true，事件只会接住一次，
	 *   第二段连招就再也等不到判定窗口，表现为「第一段能打、第二段按了没反应」。
	 *   连招必然需要反复接收同一个事件，所以这里直接钉死为 false。
	 *
	 * 【和「发送Gameplay事件」配套】动画通知里用「发送Gameplay事件」发出标签，
	 *   这里接住它，然后去做伤害判定——这样打击判定跟着动画走，而不是靠计时器硬猜。
	 *
	 * 【注意】Gameplay事件(GameplayEvent) 不是 效果(GameplayEffect)，两者缩写都是 GE，别搞混。
	 *
	 * @param OwningAbility        拥有本任务的技能（自动填当前技能，隐藏引脚）
	 * @param EventTag             要等待的事件标签，例如 GameplayEvent.Hit
	 * @param OptionalExternalTarget 留空 = 监听技能拥有者自己的事件；
	 *                             填写 = 监听另一个 Actor 身上的事件（例如监听取受击方的事件）
	 * @param OnlyMatchExact       true（默认）= 只认完全相同的标签；
	 *                             false = 子标签也算命中（监听 GameplayEvent.Hit 时，
	 *                                     GameplayEvent.Hit.Heavy 也会触发）
	 */
	UFUNCTION(BlueprintCallable, Category = "战斗|技能",
		meta = (DisplayName = "由事件激活技能", Keywords = "事件 等待 判定窗口 GameplayEvent 动画通知 wait gameplay event 连招",
			HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"))
	static UAbilityTask_WaitGameplayEvent* WaitGameplayEventKeepListening(
		UGameplayAbility* OwningAbility,
		FGameplayTag EventTag,
		AActor* OptionalExternalTarget = nullptr,
		bool OnlyMatchExact = true);
};
