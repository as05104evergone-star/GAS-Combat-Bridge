// Copyright Epic Games, Inc. All Rights Reserved.

#include "GASCombatAttributeWatcher.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "GameplayEffectTypes.h"
#include "GASCombatBridge.h"
#include "GASCombatStatics.h"

UGASCombatAttributeWatcher* UGASCombatAttributeWatcher::CreateWatcher(AActor* Target, FGameplayAttribute Attribute, bool bFireImmediately)
{
	if (!IsValid(Target))
	{
		UE_LOG(LogGASCombat, Warning, TEXT("【GAS战斗桥接】创建属性监听器失败：目标为空。"));
		return nullptr;
	}

	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
	if (!ASC)
	{
		UE_LOG(LogGASCombat, Warning,
			TEXT("【GAS战斗桥接】创建属性监听器失败：%s 身上没有技能系统组件(ASC)。"), *Target->GetName());
		return nullptr;
	}

	if (!Attribute.IsValid())
	{
		UE_LOG(LogGASCombat, Warning, TEXT("【GAS战斗桥接】创建属性监听器失败：属性句柄无效。"));
		return nullptr;
	}

	UGASCombatAttributeWatcher* Watcher = NewObject<UGASCombatAttributeWatcher>(Target);
	Watcher->WatchedComponent = ASC;
	Watcher->WatchedAttribute = Attribute;
	Watcher->StartWatching();

	if (bFireImmediately)
	{
		// 立即用当前值触发一次，做 UI 初始化时省掉一次手动刷新
		const float CurrentValue = ASC->GetNumericAttribute(Attribute);

		FGASCombatAttributeChangeEvent Event;
		Event.Attribute = Attribute;
		Event.AttributeName = Attribute.GetName();
		Event.OldValue = CurrentValue;
		Event.NewValue = CurrentValue;
		Event.Delta = 0.f;

		Watcher->bWasZero = FMath::IsNearlyZero(CurrentValue);
		Watcher->OnChanged.Broadcast(Event);
	}

	return Watcher;
}

UGASCombatAttributeWatcher* UGASCombatAttributeWatcher::CreateWatcherByName(AActor* Target, TSubclassOf<UAttributeSet> AttributeSetClass,
	FName AttributeName, bool bFireImmediately)
{
	// 复用「按名字取属性」的反射查找逻辑，避免两处实现不一致
	const FGameplayAttribute Attribute = UGASCombatStatics::MakeAttributeFromName(AttributeSetClass, AttributeName);
	if (!Attribute.IsValid())
	{
		UE_LOG(LogGASCombat, Warning,
			TEXT("【GAS战斗桥接】创建属性监听器（按名字）失败：在属性集上找不到属性 \"%s\"。"),
			*AttributeName.ToString());
		return nullptr;
	}

	return CreateWatcher(Target, Attribute, bFireImmediately);
}

bool UGASCombatAttributeWatcher::StartWatching()
{
	if (bWatching)
	{
		return true;
	}

	if (!WatchedComponent.IsValid())
	{
		UE_LOG(LogGASCombat, Warning,
			TEXT("【GAS战斗桥接】属性监听器无法开始监听：被监听的角色已经不存在了。"));
		return false;
	}

	return BindDelegate();
}

void UGASCombatAttributeWatcher::StopWatching()
{
	UnbindDelegate();
}

bool UGASCombatAttributeWatcher::IsWatching() const
{
	return bWatching && WatchedComponent.IsValid();
}

float UGASCombatAttributeWatcher::ReadCurrentValue() const
{
	UAbilitySystemComponent* ASC = WatchedComponent.Get();
	return (ASC && WatchedAttribute.IsValid()) ? ASC->GetNumericAttribute(WatchedAttribute) : 0.f;
}

bool UGASCombatAttributeWatcher::BindDelegate()
{
	UAbilitySystemComponent* ASC = WatchedComponent.Get();
	if (!ASC || !WatchedAttribute.IsValid())
	{
		return false;
	}

	// 这是引擎里唯一的属性变化入口。它返回的是原生多播委托，
	// 蓝图绑不了，所以必须由 C++ 中转一层。
	FOnGameplayAttributeValueChange& Delegate = ASC->GetGameplayAttributeValueChangeDelegate(WatchedAttribute);

	DelegateHandle = Delegate.AddUObject(this, &UGASCombatAttributeWatcher::HandleAttributeValueChanged);
	bWatching = true;

	// 记录初始的「是否为零」状态，避免第一次事件就把「归零」误报出来
	bWasZero = FMath::IsNearlyZero(ASC->GetNumericAttribute(WatchedAttribute));

	return true;
}

void UGASCombatAttributeWatcher::UnbindDelegate()
{
	if (!bWatching)
	{
		return;
	}

	// 目标可能已经销毁，此时委托自然也没了，只需要清状态
	if (UAbilitySystemComponent* ASC = WatchedComponent.Get())
	{
		if (DelegateHandle.IsValid())
		{
			FOnGameplayAttributeValueChange& Delegate = ASC->GetGameplayAttributeValueChangeDelegate(WatchedAttribute);
			Delegate.Remove(DelegateHandle);
		}
	}

	DelegateHandle.Reset();
	bWatching = false;
}

void UGASCombatAttributeWatcher::HandleAttributeValueChanged(const FOnAttributeChangeData& ChangeData)
{
	FGASCombatAttributeChangeEvent Event;
	Event.Attribute = ChangeData.Attribute;
	Event.AttributeName = ChangeData.Attribute.GetName();
	Event.OldValue = ChangeData.OldValue;
	Event.NewValue = ChangeData.NewValue;
	// 统一在这里算好差值，策划不用自己去记上一帧的值
	Event.Delta = ChangeData.NewValue - ChangeData.OldValue;

	OnChanged.Broadcast(Event);

	// 「归零」只在跨越 0 的那一次触发，而不是每次数值为 0 都触发。
	// 例如生命值已经是 0 时再受到伤害，不应该重复播死亡表现。
	const bool bIsZeroNow = FMath::IsNearlyZero(ChangeData.NewValue);
	if (bIsZeroNow && !bWasZero)
	{
		OnZeroed.Broadcast(Event);
	}
	bWasZero = bIsZeroNow;
}

void UGASCombatAttributeWatcher::BeginDestroy()
{
	// 必须在这里解绑：否则角色销毁后引擎仍会回调到这个已经被回收的对象，导致崩溃。
	UnbindDelegate();
	Super::BeginDestroy();
}

UWorld* UGASCombatAttributeWatcher::GetWorld() const
{
	if (const UAbilitySystemComponent* ASC = WatchedComponent.Get())
	{
		return ASC->GetWorld();
	}
	return nullptr;
}
