// Copyright Epic Games, Inc. All Rights Reserved.

#include "GASCombatBridgeComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Abilities/GameplayAbility.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Engine/Engine.h"
#include "GameplayEffect.h"
#include "GASCombatAbilitySet.h"
#include "GASCombatAttributeWatcher.h"
#include "GASCombatBridge.h"
#include "GASCombatCoreAttributeSet.h"
#include "GASCombatStatics.h"

UGASCombatBridgeComponent::UGASCombatBridgeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// 纯事件驱动的组件，不需要每帧跑
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UGASCombatBridgeComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoInitializeOnBeginPlay)
	{
		InitializeCombat();
	}
}

void UGASCombatBridgeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 必须在销毁前解绑：否则角色先于组件被销毁时，残留的回调会打到已失效的对象上
	UnbindEngineDelegates();
	Super::EndPlay(EndPlayReason);
}

UAbilitySystemComponent* UGASCombatBridgeComponent::GetAbilitySystemComponent() const
{
	return UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
}

bool UGASCombatBridgeComponent::InitializeCombat()
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner))
	{
		UE_LOG(LogGASCombat, Warning, TEXT("【GAS战斗桥接】初始化战斗失败：组件没有挂在有效的 Actor 上。"));
		return false;
	}

	// 第一步：拿到 ASC。没有就按配置创建。
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!ASC && bCreateASCIfMissing)
	{
		ASC = UGASCombatStatics::GetOrCreateAbilitySystemComponent(Owner, bReplicateCreatedASC);
	}

	if (!ASC)
	{
		UE_LOG(LogGASCombat, Warning,
			TEXT("【GAS战斗桥接】初始化战斗失败：%s 没有技能系统组件(ASC)。\n")
			TEXT("  二选一：① 勾选本组件的「缺少组件时自动创建」；② 在角色蓝图里手动添加「技能系统组件」。"),
			*Owner->GetName());
		return false;
	}

	// 第二步：初始化。这一步漏掉的话，技能会因为「不是本地控制」而永远激活失败。
	UGASCombatStatics::InitAbilitySystem(Owner, Owner, /*bForceRefresh=*/false);

	// 第三步：挂属性集。必须在授予效果之前，否则效果改不到还不存在的属性。
	for (const TSubclassOf<UAttributeSet>& SetClass : AttributeSets)
	{
		if (SetClass)
		{
			UGASCombatStatics::AddAttributeSet(Owner, SetClass);
		}
	}

	// 第四步：绑定事件。放在授予技能之前，这样连「授予时立即激活」的被动技能也能被捕获到。
	BindEngineDelegates(ASC);

	// 第五步：授予技能集
	GrantAllAbilitySets(GrantedHandles);

	bCombatReady = true;
	OnCombatReady.Broadcast();

	UE_LOG(LogGASCombat, Log, TEXT("【GAS战斗桥接】%s 战斗系统就绪。"), *Owner->GetName());
	return true;
}

bool UGASCombatBridgeComponent::GrantAllAbilitySets(FGASCombatGrantedHandles& OutHandles)
{
	AActor* Owner = GetOwner();
	OutHandles = FGASCombatGrantedHandles();

	if (!IsValid(Owner))
	{
		return false;
	}

	if (AbilitySets.Num() == 0)
	{
		// 没配技能集是合法情况（例如纯靶子），不算失败
		return true;
	}

	bool bAnyGranted = false;
	for (UGASCombatAbilitySet* AbilitySet : AbilitySets)
	{
		if (!AbilitySet)
		{
			continue;
		}

		FGASCombatGrantedHandles SubHandles;
		if (AbilitySet->GrantTo(Owner, SubHandles))
		{
			bAnyGranted = true;
			// 把各份技能集的账合并成一本，回收时一次搞定
			OutHandles.AbilitySystemComponent = SubHandles.AbilitySystemComponent;
			OutHandles.AbilityHandles.Append(SubHandles.AbilityHandles);
			OutHandles.AttributeSets.Append(SubHandles.AttributeSets);
			OutHandles.EffectHandles.Append(SubHandles.EffectHandles);
			OutHandles.LooseTags.Append(SubHandles.LooseTags);
		}
	}

	OutHandles.bIsValid = OutHandles.AbilityHandles.Num() > 0
		|| OutHandles.EffectHandles.Num() > 0
		|| OutHandles.AttributeSets.Num() > 0
		|| OutHandles.LooseTags.Num() > 0;

	return bAnyGranted || OutHandles.bIsValid || AbilitySets.Num() == 0;
}

bool UGASCombatBridgeComponent::RevokeAllAbilitySets()
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !GrantedHandles.bIsValid)
	{
		return false;
	}

	const bool bResult = UGASCombatAbilitySet::RemoveFrom(Owner, GrantedHandles);
	return bResult;
}

UGASCombatAttributeWatcher* UGASCombatBridgeComponent::WatchAttribute(FGameplayAttribute Attribute, bool bFireImmediately)
{
	return UGASCombatAttributeWatcher::CreateWatcher(GetOwner(), Attribute, bFireImmediately);
}

// ============================================================================
//  引擎委托绑定
// ============================================================================

void UGASCombatBridgeComponent::BindEngineDelegates(UAbilitySystemComponent* ASC)
{
	if (!ASC || bEngineDelegatesBound)
	{
		return;
	}

	// 下面这些委托在引擎里都是「原生多播委托」，没有 BlueprintAssignable 标记，
	// 所以策划在蓝图里是绑不到的。这里统一中转成蓝图事件。
	ASC->AbilityActivatedCallbacks.AddUObject(this, &UGASCombatBridgeComponent::HandleAbilityActivated);

	// 【为什么只挂 OnAbilityEnded，不挂 AbilityEndedCallbacks】
	// 引擎在同一个位置连着广播这两个委托（AbilitySystemComponent_Abilities.cpp 里
	// NotifyAbilityEnded 的相邻两行），信息量却是包含关系：
	//   AbilityEndedCallbacks(UGameplayAbility*)   —— 没有 bWasCancelled
	//   OnAbilityEnded(const FAbilityEndedData&)   —— 带 bWasCancelled
	// 两个都挂会让「技能结束时」事件每个技能广播两次；只挂后者则一次且信息更全。
	ASC->OnAbilityEnded.AddUObject(this, &UGASCombatBridgeComponent::HandleAbilityEnded);
	ASC->AbilityFailedCallbacks.AddUObject(this, &UGASCombatBridgeComponent::HandleAbilityFailed);
	ASC->OnGameplayEffectAppliedDelegateToSelf.AddUObject(this, &UGASCombatBridgeComponent::HandleEffectApplied);

	// 按需注册指定标签的变化事件
	for (const FGameplayTag& Tag : WatchedTags)
	{
		if (!Tag.IsValid())
		{
			continue;
		}

		FOnGameplayEffectTagCountChanged& TagEvent = ASC->RegisterGameplayTagEvent(Tag, EGameplayTagEventType::NewOrRemoved);
		const FDelegateHandle Handle = TagEvent.AddUObject(this, &UGASCombatBridgeComponent::HandleGameplayTagChanged);
		TagEventHandles.Add(Tag, Handle);
	}

	if (bBroadcastAllTagChanges)
	{
		FOnGameplayEffectTagCountChanged& GenericEvent = ASC->RegisterGenericGameplayTagEvent();
		GenericTagEventHandle = GenericEvent.AddUObject(this, &UGASCombatBridgeComponent::HandleGameplayTagChanged);
	}

	// 受伤/死亡来自属性集（动态多播委托，所以用 AddDynamic）
	if (UGASCombatCoreAttributeSet* CoreSet = UGASCombatStatics::GetCombatAttributeSet(GetOwner()))
	{
		CoreSet->OnDamageTaken.AddDynamic(this, &UGASCombatBridgeComponent::HandleDamageTaken);
		CoreSet->OnDeath.AddDynamic(this, &UGASCombatBridgeComponent::HandleDeath);
		BoundAttributeSet = CoreSet;
	}

	bEngineDelegatesBound = true;
}

void UGASCombatBridgeComponent::UnbindEngineDelegates()
{
	if (!bEngineDelegatesBound)
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->AbilityActivatedCallbacks.RemoveAll(this);
		ASC->OnAbilityEnded.RemoveAll(this);
		ASC->AbilityFailedCallbacks.RemoveAll(this);
		ASC->OnGameplayEffectAppliedDelegateToSelf.RemoveAll(this);

		for (const TPair<FGameplayTag, FDelegateHandle>& Pair : TagEventHandles)
		{
			if (Pair.Value.IsValid())
			{
				FOnGameplayEffectTagCountChanged& TagEvent = ASC->RegisterGameplayTagEvent(Pair.Key, EGameplayTagEventType::NewOrRemoved);
				TagEvent.Remove(Pair.Value);
			}
		}

		if (GenericTagEventHandle.IsValid())
		{
			ASC->RegisterGenericGameplayTagEvent().Remove(GenericTagEventHandle);
		}
	}

	if (UGASCombatCoreAttributeSet* CoreSet = BoundAttributeSet.Get())
	{
		CoreSet->OnDamageTaken.RemoveDynamic(this, &UGASCombatBridgeComponent::HandleDamageTaken);
		CoreSet->OnDeath.RemoveDynamic(this, &UGASCombatBridgeComponent::HandleDeath);
	}

	TagEventHandles.Reset();
	GenericTagEventHandle.Reset();
	BoundAttributeSet.Reset();
	bEngineDelegatesBound = false;
}

// ============================================================================
//  事件中转实现
// ============================================================================

void UGASCombatBridgeComponent::HandleAbilityActivated(UGameplayAbility* Ability)
{
	OnAbilityActivated.Broadcast(Ability);
}

void UGASCombatBridgeComponent::HandleAbilityEnded(const FAbilityEndedData& Data)
{
	// 字段名是 AbilityThatEnded（不是 Ability），bWasCancelled 区分「正常播完」与「被打断/取消」。
	// 句柄 Data.AbilitySpecHandle 这里用不上（蓝图侧按技能实例办事），保留在注释里备查。
	OnAbilityEnded.Broadcast(Data.AbilityThatEnded, Data.bWasCancelled);
}

void UGASCombatBridgeComponent::HandleAbilityFailed(const UGameplayAbility* Ability, const FGameplayTagContainer& FailureTags)
{
	const FString Reason = MakeReadableFailureReason(FailureTags);

	UE_LOG(LogGASCombat, Warning,
		TEXT("【GAS战斗桥接】技能 %s 激活失败：%s"),
		Ability ? *Ability->GetClass()->GetName() : TEXT("(未知技能)"), *Reason);

	// 事件是给蓝图的，但 Ability 参数是 const，这里转成非 const 交给蓝图
	OnAbilityFailed.Broadcast(const_cast<UGameplayAbility*>(Ability), FailureTags, Reason);
}

void UGASCombatBridgeComponent::HandleGameplayTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	// 我们注册的是 NewOrRemoved 事件类型：只在「从无到有」和「从有到无」两个跨越点回调。
	// 所以 NewCount > 0 就是刚挂上，NewCount == 0 就是刚摘掉，不需要（也拿不到）旧层数。
	const bool bAdded = NewCount > 0;

	// 顺手记一笔时刻，供「标签最近变化时刻」调试节点查询（R6）。
	// 这个账本只对注册过事件的标签有内容 —— 也就是「监听的标签」里配过的那些，
	// 或者勾了「通知所有标签变化」时的全部标签。节点文档里写明了这个限制。
	if (const UWorld* World = GetWorld())
	{
		FGASCombatTagChangeRecord& Record = TagLastChangeRecords.FindOrAdd(Tag);
		Record.Time = World->GetTimeSeconds();
		Record.bAdded = bAdded;
		Record.Count = NewCount;
	}

	OnGameplayTagChanged.Broadcast(Tag, NewCount, bAdded);
}

float UGASCombatBridgeComponent::GetTagLastChangeTime(const FGameplayTag& Tag) const
{
	const FGASCombatTagChangeRecord* Record = TagLastChangeRecords.Find(Tag);
	return Record ? Record->Time : -1.f;
}

bool UGASCombatBridgeComponent::WasTagLastChangeAdded(const FGameplayTag& Tag) const
{
	const FGASCombatTagChangeRecord* Record = TagLastChangeRecords.Find(Tag);
	return Record ? Record->bAdded : false;
}

int32 UGASCombatBridgeComponent::GetTagLastChangeCount(const FGameplayTag& Tag) const
{
	const FGASCombatTagChangeRecord* Record = TagLastChangeRecords.Find(Tag);
	return Record ? Record->Count : 0;
}

void UGASCombatBridgeComponent::HandleEffectApplied(UAbilitySystemComponent* Source, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle Handle)
{
	OnEffectApplied.Broadcast(Handle, Spec.Def ? Spec.Def->GetClass() : nullptr);
}

void UGASCombatBridgeComponent::HandleDamageTaken(AActor* DamageInstigator, AActor* DamageCauser, float DamageAmount, float RemainingHealth)
{
	OnDamageTaken.Broadcast(DamageInstigator, DamageCauser, DamageAmount, RemainingHealth);
}

void UGASCombatBridgeComponent::HandleDeath(AActor* Killer, AActor* DamageCauser)
{
	OnDeath.Broadcast(Killer, DamageCauser);
}

// ============================================================================
//  失败原因翻译
// ============================================================================

FString UGASCombatBridgeComponent::TranslateSingleFailureTag(const FGameplayTag& Tag)
{
	if (!Tag.IsValid())
	{
		return FString();
	}

	// 这些标签名由本插件在模块启动时原生注册（AddNativeGameplayTag），
	// 并已接到 UGameplayAbilitiesDeveloperSettings / UAbilitySystemGlobals 的
	// ActivateFail*Tag 设置上（见 GASCombatBridge.cpp 的 ApplyActivateFailTagDefaults）。
	// 如果项目改过这些设置，翻译会失效并回退到显示原始标签名。
	static const TMap<FString, FString> FailureTagToReason =
	{
		{ TEXT("Activation.Fail.CantAffordCost"),  TEXT("消耗不足——蓝量/资源不够，检查技能资产的「消耗效果」以及当前资源属性") },
		{ TEXT("Activation.Fail.OnCooldown"),      TEXT("技能正在冷却中——检查「冷却效果」的时长与冷却标签") },
		{ TEXT("Activation.Fail.BlockedByTags"),   TEXT("被状态标签阻止——角色身上有「阻止他人技能」类标签（眩晕/沉默/硬直等）") },
		{ TEXT("Activation.Fail.MissingTags"),     TEXT("缺少必需的激活标签——技能资产里配了「激活所需标签」，但角色身上没有") },
		{ TEXT("Activation.Fail.IsDead"),          TEXT("角色已死亡——生命值归零后大部分技能会被拒绝") },
		{ TEXT("Activation.Fail.Networking"),      TEXT("网络权限不足——技能的网络执行策略不允许在当前端激活") },
	};

	if (const FString* Found = FailureTagToReason.Find(Tag.ToString()))
	{
		return *Found;
	}

	return FString::Printf(TEXT("未识别的失败标签：%s"), *Tag.ToString());
}

FString UGASCombatBridgeComponent::MakeReadableFailureReason(const FGameplayTagContainer& FailureTags)
{
	if (FailureTags.IsEmpty())
	{
		return TEXT("引擎没有给出具体原因。常见补充排查方向：技能是否已授予、是否调用了「初始化技能系统」、技能资产里的标签条件是否互相矛盾。");
	}

	TArray<FString> Reasons;
	for (const FGameplayTag& Tag : FailureTags)
	{
		Reasons.Add(TranslateSingleFailureTag(Tag));
	}

	return FString::Join(Reasons, TEXT("；"));
}

// ============================================================================
//  调试报告
// ============================================================================

FString UGASCombatBridgeComponent::GetCombatSummary() const
{
	// 实现放在节点库里（UGASCombatStatics::GetCombatSummaryText），
	// 这样编辑器的「GAS 战斗调试」面板、蓝图节点、本组件三处用的是同一份报告，
	// 不会出现「面板显示的内容和蓝图打印出来的不一样」。
	return UGASCombatStatics::GetCombatSummaryText(GetOwner());
}

void UGASCombatBridgeComponent::PrintCombatSummary() const
{
	const FString Summary = GetCombatSummary();

	UE_LOG(LogGASCombat, Log, TEXT("%s"), *Summary);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 15.f, FColor::Cyan, Summary);
	}
#endif
}
