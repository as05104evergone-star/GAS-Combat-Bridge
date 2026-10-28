// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "GameplayEffectTypes.h"
#include "GASCombatTypes.h"
#include "UObject/Object.h"
#include "GASCombatAttributeWatcher.generated.h"

class UAbilitySystemComponent;

/** 被监听的属性发生变化时广播 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGASCombatOnAttributeChanged, const FGASCombatAttributeChangeEvent&, Event);

/** 被监听的属性掉到 0 时广播（做「护盾破碎」「蓝量耗尽」这类逻辑用） */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGASCombatOnAttributeZeroed, const FGASCombatAttributeChangeEvent&, Event);

/**
 * 属性监听器 —— 让蓝图能「属性一变就自动收到通知」。
 *
 * 【为什么需要这个东西】
 *   引擎内部确实有属性变化回调，但它是 C++ 的多播委托(Native Multicast Delegate)，
 *   不是蓝图能绑的那种动态委托，所以策划在蓝图里根本绑不到。
 *   引擎自带的替代品是「Wait For Attribute Changed」异步节点，但有两个限制：
 *     1) 一个节点只能监听一个属性，而且只认 Actor 不认具体的组件实例；
 *     2) 异步节点必须手动 EndAction，忘了就会泄漏。
 *   本监听器是一个普通的 UObject：存进蓝图变量，只要变量还在就一直有效，
 *   蓝图变量被丢弃时它会被自动回收并解绑，不需要你操心清理。
 *
 * 【怎么用】
 *   1. 「创建属性监听器」节点，传入目标角色 + 属性句柄（或用「按名字」版本）
 *   2. 把返回值存到一个蓝图变量里（重要！不存的话会被立刻回收，事件不会触发）
 *   3. 从变量拖出引脚，绑定「数值变化时」事件
 *   4. 不想听了就调「停止监听」，或者直接把变量置空
 *
 * 【它和「受伤事件」的分工】
 *   监听器是「数值层面」的：任何原因（受伤、回血、换装备、调试改值）导致数值变化都会触发。
 *   通用战斗属性集的「受到伤害时」是「战斗语义层面」的：只在真的被打了才触发。
 *   做血条用监听器；做受击动画/伤害飘字用受伤事件。
 */
UCLASS(BlueprintType, meta = (DisplayName = "属性监听器"))
class GASCOMBATBRIDGE_API UGASCombatAttributeWatcher : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * 创建监听器（用属性句柄）。
	 * @param Target            被监听的角色
	 * @param Attribute         要监听的属性
	 * @param bFireImmediately  立即用当前值触发一次事件（做 UI 初始化很方便，省一次手动刷新）
	 * @return 监听器对象。**一定要存到蓝图变量里**，否则会被垃圾回收，事件永远不会触发
	 */
	UFUNCTION(BlueprintCallable, Category = "战斗|监听",
		meta = (DisplayName = "创建属性监听器", Keywords = "监听 属性 变化 UI 血条 watcher attribute changed bind",
			DefaultToSelf = "Target", AdvancedDisplay = "Target,bFireImmediately"))
	static UGASCombatAttributeWatcher* CreateWatcher(AActor* Target, FGameplayAttribute Attribute, bool bFireImmediately = true);

	/**
	 * 创建监听器（按「属性集类 + 属性名」定位属性）。
	 * 适合配置表驱动的场景，例如从表里读出 "Health" 字符串再去监听。
	 */
	UFUNCTION(BlueprintCallable, Category = "战斗|监听",
		meta = (DisplayName = "创建属性监听器（按名字）", Keywords = "监听 属性 名字 watcher by name",
			DefaultToSelf = "Target", AdvancedDisplay = "Target,bFireImmediately"))
	static UGASCombatAttributeWatcher* CreateWatcherByName(AActor* Target, TSubclassOf<UAttributeSet> AttributeSetClass,
		FName AttributeName, bool bFireImmediately = true);

	/** 数值变化时触发（受伤、回血、装备加成生效……任何原因都算） */
	UPROPERTY(BlueprintAssignable, Category = "战斗|监听", meta = (DisplayName = "数值变化时"))
	FGASCombatOnAttributeChanged OnChanged;

	/** 数值掉到 0 时触发。用于护盾破碎、蓝量耗尽、弹药打空 */
	UPROPERTY(BlueprintAssignable, Category = "战斗|监听", meta = (DisplayName = "数值归零时"))
	FGASCombatOnAttributeZeroed OnZeroed;

	/** 停止监听并解绑。之后可以再调「重新开始监听」 */
	UFUNCTION(BlueprintCallable, Category = "战斗|监听", meta = (DisplayName = "停止监听", Keywords = "监听 停止 解绑 stop unbind"))
	void StopWatching();

	/** 重新开始监听（配合「停止监听」做临时静音，例如 UI 动画播放期间不想被刷新） */
	UFUNCTION(BlueprintCallable, Category = "战斗|监听", meta = (DisplayName = "重新开始监听", Keywords = "监听 开始 resume"))
	bool StartWatching();

	/** 是否正在监听 */
	UFUNCTION(BlueprintPure, Category = "战斗|监听", meta = (DisplayName = "正在监听"))
	bool IsWatching() const;

	/** 当前被监听的属性 */
	UFUNCTION(BlueprintPure, Category = "战斗|监听", meta = (DisplayName = "被监听的属性"))
	FGameplayAttribute GetWatchedAttribute() const { return WatchedAttribute; }

	/** 手动读一次当前值（不想等事件时可以主动拉） */
	UFUNCTION(BlueprintPure, Category = "战斗|监听", meta = (DisplayName = "读取当前值"))
	float ReadCurrentValue() const;

	/** 对象被回收时自动解绑，防止野指针回调 */
	virtual void BeginDestroy() override;

	/** 让蓝图节点能正确识别世界上下文 */
	virtual UWorld* GetWorld() const override;

private:
	/** 底层回调：把引擎的原生委托转成蓝图能收的动态委托 */
	void HandleAttributeValueChanged(const FOnAttributeChangeData& ChangeData);

	/** 真正执行绑定 */
	bool BindDelegate();

	/** 解绑 */
	void UnbindDelegate();

	/** 被监听的目标组件（弱引用，目标被销毁时自动失效） */
	TWeakObjectPtr<UAbilitySystemComponent> WatchedComponent;

	/** 被监听的属性 */
	FGameplayAttribute WatchedAttribute;

	/** 引擎返回的委托句柄，解绑时要还回去 */
	FDelegateHandle DelegateHandle;

	/** 上一次的值是否已经是 0，用来保证「归零」事件只在越过 0 的那一次触发 */
	bool bWasZero = false;

	/** 是否处于监听状态 */
	bool bWatching = false;
};
