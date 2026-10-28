// Copyright Epic Games, Inc. All Rights Reserved.

#include "GASCombatStatics.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Abilities/GameplayAbility.h"
#include "Components/StateTreeComponent.h"
#include "GameplayCueFunctionLibrary.h"
#include "GameplayEffect.h"
#include "GASCombatBridge.h"
#include "GASCombatBridgeComponent.h"
#include "GASCombatComboAttributeSet.h"
#include "GASCombatCoreAttributeSet.h"
#include "Misc/EngineVersionComparison.h"
#include "StateTreeEvents.h"


namespace GASCombatBridge::Private
{
	/** 统一的「找不到组件」中文提示。所有失败路径都要说话，不能静默返回 */
	void WarnMissingASC(const AActor* Target, const TCHAR* Context)
	{
		UE_LOG(LogGASCombat, Warning,
			TEXT("【GAS战斗桥接】%s 失败：%s 身上没有找到技能系统组件(ASC)。\n")
			TEXT("  解决办法：① 给这个 Actor 蓝图添加「技能系统组件」；或者 ② 在 BeginPlay 里调用「获取或创建技能系统组件」节点。"),
			Context, Target ? *Target->GetName() : TEXT("(空目标)"));
	}

	/** 解析目标身上的技能系统组件（先走接口，再回退到组件查找，与引擎行为一致） */
	UAbilitySystemComponent* ResolveASC(const AActor* Target)
	{
		return Target ? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target) : nullptr;
	}

	/** 收集目标身上所有激活中的效果句柄 */
	TArray<FActiveGameplayEffectHandle> CollectActiveEffects(UAbilitySystemComponent* ASC)
	{
		return ASC ? ASC->GetActiveEffects(FGameplayEffectQuery()) : TArray<FActiveGameplayEffectHandle>();
	}
}

using namespace GASCombatBridge::Private;

// ============================================================================
//  初始化
// ============================================================================

UAbilitySystemComponent* UGASCombatStatics::GetAbilitySystemComponent(AActor* Target)
{
	// 这里不做警告：这是个纯查询节点，用「是否有效」判断是正常用法。
	return ResolveASC(Target);
}

UAbilitySystemComponent* UGASCombatStatics::GetOrCreateAbilitySystemComponent(AActor* Target, bool bReplicate)
{
	if (!IsValid(Target))
	{
		UE_LOG(LogGASCombat, Warning, TEXT("【GAS战斗桥接】获取或创建技能系统组件失败：目标为空。"));
		return nullptr;
	}

	if (UAbilitySystemComponent* Existing = ResolveASC(Target))
	{
		return Existing;
	}

	// 运行时的组件必须在世界已经存在之后创建，构造函数阶段调用会失败。
	if (!Target->GetWorld())
	{
		UE_LOG(LogGASCombat, Warning,
			TEXT("【GAS战斗桥接】获取或创建技能系统组件失败：%s 还没有进入世界。\n")
			TEXT("  请把这个节点放在 BeginPlay 或之后调用，不要放在构造函数里。"),
			*Target->GetName());
		return nullptr;
	}

	UAbilitySystemComponent* NewASC = NewObject<UAbilitySystemComponent>(
		Target, UAbilitySystemComponent::StaticClass(), TEXT("GASCombatBridge_ASC"));

	if (!NewASC)
	{
		UE_LOG(LogGASCombat, Error, TEXT("【GAS战斗桥接】创建技能系统组件失败：NewObject 返回空。"));
		return nullptr;
	}

	NewASC->SetIsReplicated(bReplicate);
	NewASC->RegisterComponent();

	UE_LOG(LogGASCombat, Log,
		TEXT("【GAS战斗桥接】已为 %s 创建技能系统组件（复制=%s）。记得紧接着调用「初始化技能系统」。"),
		*Target->GetName(), bReplicate ? TEXT("开") : TEXT("关"));

	return NewASC;
}

bool UGASCombatStatics::InitAbilitySystem(AActor* Target, AActor* AvatarActor, bool bForceRefresh)
{
	UAbilitySystemComponent* ASC = ResolveASC(Target);
	if (!ASC)
	{
		WarnMissingASC(Target, TEXT("初始化技能系统"));
		return false;
	}

	AActor* EffectiveAvatar = AvatarActor ? AvatarActor : Target;

	// 防御：引擎的 InitAbilityActorInfo 第一行是 check(AbilityActorInfo.IsValid())，
	// 而 AbilityActorInfo 只在组件的 OnRegister 里分配（也就是 RegisterComponent 之后才有）。
	// 不做这道检查的话，在错误时机调用会直接触发断言崩溃，而不是给出可读提示。
	if (!ASC->AbilityActorInfo.IsValid())
	{
		UE_LOG(LogGASCombat, Error,
			TEXT("【GAS战斗桥接】初始化技能系统失败：%s 的技能系统组件还没有完成注册（AbilityActorInfo 为空）。\n")
			TEXT("  原因通常是调用时机太早。请把这个节点放在 BeginPlay 或之后执行；\n")
			TEXT("  如果是运行时创建的组件，请确认已经调用过「获取或创建技能系统组件」并成功返回。"),
			*GetNameSafe(Target));
		return false;
	}

	// 已经初始化过且不要求刷新，就直接成功返回——本节点幂等，方便随手多调几次。
	const bool bAlreadyInitialized = ASC->AbilityActorInfo->OwnerActor.IsValid()
		&& ASC->AbilityActorInfo->AvatarActor.IsValid();

	if (bAlreadyInitialized && !bForceRefresh)
	{
		return true;
	}

	ASC->InitAbilityActorInfo(Target, EffectiveAvatar);

	UE_LOG(LogGASCombat, Log,
		TEXT("【GAS战斗桥接】技能系统已初始化：拥有者=%s，化身=%s。"),
		*GetNameSafe(Target), *GetNameSafe(EffectiveAvatar));

	return true;
}

bool UGASCombatStatics::IsAbilitySystemInitialized(AActor* Target)
{
	UAbilitySystemComponent* ASC = ResolveASC(Target);
	if (!ASC || !ASC->AbilityActorInfo.IsValid())
	{
		return false;
	}
	return ASC->AbilityActorInfo->OwnerActor.IsValid() && ASC->AbilityActorInfo->AvatarActor.IsValid();
}

UGASCombatCoreAttributeSet* UGASCombatStatics::GetCombatAttributeSet(AActor* Target)
{
	UAbilitySystemComponent* ASC = ResolveASC(Target);
	if (!ASC)
	{
		return nullptr;
	}

	// GetAttributeSet 返回 const 指针；这里转成非 const 是为了让蓝图能拿它去绑「受到伤害时」
	// 这类可写委托。属性集实例本身由 ASC 拥有，生命周期安全。
	const UAttributeSet* Found = ASC->GetAttributeSet(UGASCombatCoreAttributeSet::StaticClass());
	return const_cast<UGASCombatCoreAttributeSet*>(static_cast<const UGASCombatCoreAttributeSet*>(Found));
}

UGASCombatComboAttributeSet* UGASCombatStatics::GetComboAttributeSet(AActor* Target)
{
	UAbilitySystemComponent* ASC = ResolveASC(Target);
	if (!ASC)
	{
		return nullptr;
	}

	// 和上面 GetCombatAttributeSet 一样：GetAttributeSet 返回 const，
	// 这里转成非 const 是为了让蓝图能拿它去绑属性监听、或在调试面板里改段数。
	// 实例归 ASC 所有，生命周期安全。
	const UAttributeSet* Found = ASC->GetAttributeSet(UGASCombatComboAttributeSet::StaticClass());
	return const_cast<UGASCombatComboAttributeSet*>(static_cast<const UGASCombatComboAttributeSet*>(Found));
}

UAttributeSet* UGASCombatStatics::AddAttributeSet(AActor* Target, TSubclassOf<UAttributeSet> AttributeSetClass)
{
	UAbilitySystemComponent* ASC = ResolveASC(Target);
	if (!ASC)
	{
		WarnMissingASC(Target, TEXT("添加属性集"));
		return nullptr;
	}

	if (!AttributeSetClass)
	{
		UE_LOG(LogGASCombat, Warning, TEXT("【GAS战斗桥接】添加属性集失败：没有指定属性集类。"));
		return nullptr;
	}

	// 同一个类只挂一份，重复调用直接返回已有的那份
	if (const UAttributeSet* Existing = ASC->GetAttributeSet(AttributeSetClass))
	{
		return const_cast<UAttributeSet*>(Existing);
	}

	// 与引擎 UAbilitySystemComponent::GetOrCreateAttributeSubobject 的做法保持一致：
	// 以 OwnerActor 为 Outer 创建，然后注册进 ASC 的生成属性列表。
	AActor* OwningActor = ASC->GetOwner();
	UAttributeSet* NewSet = NewObject<UAttributeSet>(OwningActor ? static_cast<UObject*>(OwningActor) : static_cast<UObject*>(ASC), AttributeSetClass);
	if (!NewSet)
	{
		UE_LOG(LogGASCombat, Error, TEXT("【GAS战斗桥接】添加属性集失败：创建 %s 实例失败。"), *AttributeSetClass->GetName());
		return nullptr;
	}

	ASC->AddSpawnedAttribute(NewSet);

	UE_LOG(LogGASCombat, Log, TEXT("【GAS战斗桥接】已为 %s 添加属性集 %s。"), *GetNameSafe(Target), *AttributeSetClass->GetName());
	return NewSet;
}

// ============================================================================
//  属性
// ============================================================================

float UGASCombatStatics::GetAttributeCurrentValue(AActor* Target, FGameplayAttribute Attribute, bool& bSuccessfullyFoundAttribute)
{
	bSuccessfullyFoundAttribute = false;

	UAbilitySystemComponent* ASC = ResolveASC(Target);
	if (!ASC)
	{
		WarnMissingASC(Target, TEXT("取属性当前值"));
		return 0.f;
	}

	if (!Attribute.IsValid())
	{
		UE_LOG(LogGASCombat, Warning,
			TEXT("【GAS战斗桥接】取属性当前值失败：属性句柄无效。\n")
			TEXT("  多半是节点上的「属性」下拉框没选，或者用「按名字取属性」时属性名拼错了。"));
		return 0.f;
	}

	// HasAttributeSetForAttribute 是判断「这个属性到底在不在目标身上」的正规方式。
	// 少了这一步，属性集没挂时引擎会安静地返回 0，使用者会以为是「真的是 0」。
	if (!ASC->HasAttributeSetForAttribute(Attribute))
	{
		UE_LOG(LogGASCombat, Warning,
			TEXT("【GAS战斗桥接】取属性当前值失败：%s 身上没有包含属性 %s 的属性集。\n")
			TEXT("  两种可能：① 属性集没挂——在桥接组件的「挂载的属性集」里把它加上；\n")
			TEXT("           ② 属性名写错了——检查大小写。"),
			*GetNameSafe(Target), *Attribute.GetName());
		return 0.f;
	}

	bSuccessfullyFoundAttribute = true;
	return ASC->GetNumericAttribute(Attribute);
}

float UGASCombatStatics::GetAttributeBaseValue(AActor* Target, FGameplayAttribute Attribute, bool& bSuccessfullyFoundAttribute)
{
	bSuccessfullyFoundAttribute = false;

	UAbilitySystemComponent* ASC = ResolveASC(Target);
	if (!ASC)
	{
		WarnMissingASC(Target, TEXT("取属性基础值"));
		return 0.f;
	}

	if (!Attribute.IsValid())
	{
		UE_LOG(LogGASCombat, Warning, TEXT("【GAS战斗桥接】取属性基础值失败：属性句柄无效。"));
		return 0.f;
	}

	if (!ASC->HasAttributeSetForAttribute(Attribute))
	{
		UE_LOG(LogGASCombat, Warning,
			TEXT("【GAS战斗桥接】取属性基础值失败：%s 身上没有包含属性 %s 的属性集。\n")
			TEXT("  请先用「添加属性集」把对应的属性集挂上去。"),
			*GetNameSafe(Target), *Attribute.GetName());
		return 0.f;
	}

	bSuccessfullyFoundAttribute = true;
	return ASC->GetNumericAttributeBase(Attribute);
}

bool UGASCombatStatics::SetAttributeBaseValue(AActor* Target, FGameplayAttribute Attribute, float NewBaseValue)
{
	UAbilitySystemComponent* ASC = ResolveASC(Target);
	if (!ASC)
	{
		WarnMissingASC(Target, TEXT("设置属性基础值"));
		return false;
	}

	if (!Attribute.IsValid())
	{
		UE_LOG(LogGASCombat, Warning, TEXT("【GAS战斗桥接】设置属性基础值失败：属性句柄无效。"));
		return false;
	}

	if (!ASC->HasAttributeSetForAttribute(Attribute))
	{
		UE_LOG(LogGASCombat, Warning,
			TEXT("【GAS战斗桥接】设置属性基础值失败：%s 身上没有包含属性 %s 的属性集。\n")
			TEXT("  请先用「添加属性集」把对应的属性集挂上去。"),
			*GetNameSafe(Target), *Attribute.GetName());
		return false;
	}

	ASC->SetNumericAttributeBase(Attribute, NewBaseValue);
	return true;
}

FGameplayAttribute UGASCombatStatics::MakeAttributeFromName(TSubclassOf<UAttributeSet> AttributeSetClass, FName AttributeName)
{
	if (!AttributeSetClass || AttributeName.IsNone())
	{
		return FGameplayAttribute();
	}

	// 反射查字段：这是绕过「引擎属性下拉框不列出蓝图属性集」的关键一步。
	FProperty* Property = FindFProperty<FProperty>(AttributeSetClass.Get(), AttributeName);
	if (!Property || !FGameplayAttribute::IsSupportedProperty(Property))
	{
		UE_LOG(LogGASCombat, Warning,
			TEXT("【GAS战斗桥接】按名字取属性失败：在 %s 上找不到名为 \"%s\" 的可用属性。\n")
			TEXT("  请检查属性名拼写与大小写（例如 Health、MaxHealth），且该字段必须是 GameplayAttributeData 类型。"),
			*AttributeSetClass->GetName(), *AttributeName.ToString());
		return FGameplayAttribute();
	}

	return FGameplayAttribute(Property);
}

bool UGASCombatStatics::IsAttributeValid(FGameplayAttribute Attribute)
{
	return Attribute.IsValid();
}

float UGASCombatStatics::GetAttributeCurrentValueByName(AActor* Target, TSubclassOf<UAttributeSet> AttributeSetClass, FName AttributeName)
{
	// 按名字取值的版本不带「是否找到」引脚：属性名写错时 MakeAttributeFromName
	// 会返回无效句柄，上面那个函数已经在日志里说清楚了。这里只需要一个结果值。
	bool bFound = false;
	return GetAttributeCurrentValue(Target, MakeAttributeFromName(AttributeSetClass, AttributeName), bFound);
}

bool UGASCombatStatics::SetAttributeBaseValueByName(AActor* Target, TSubclassOf<UAttributeSet> AttributeSetClass, FName AttributeName, float NewBaseValue)
{
	return SetAttributeBaseValue(Target, MakeAttributeFromName(AttributeSetClass, AttributeName), NewBaseValue);
}

TArray<FGASCombatAttributeSnapshot> UGASCombatStatics::CaptureAllAttributes(AActor* Target)
{
	TArray<FGASCombatAttributeSnapshot> Snapshots;

	UAbilitySystemComponent* ASC = ResolveASC(Target);
	if (!ASC)
	{
		return Snapshots;
	}

	TArray<FGameplayAttribute> Attributes;
	ASC->GetAllAttributes(Attributes);

	Snapshots.Reserve(Attributes.Num());
	for (const FGameplayAttribute& Attribute : Attributes)
	{
		FGASCombatAttributeSnapshot Snapshot;
		Snapshot.Attribute = Attribute;
		Snapshot.AttributeName = Attribute.GetName();
		// 中文名：调试面板与状态报告都显示它，避免使用者在报告里对着英文属性名多绕一层
		Snapshot.DisplayName = GetAttributeDisplayName(FName(*Snapshot.AttributeName));
		Snapshot.BaseValue = ASC->GetNumericAttributeBase(Attribute);
		Snapshot.CurrentValue = ASC->GetNumericAttribute(Attribute);

		if (Attribute.IsValid())
		{
			if (const UClass* OwnerClass = Attribute.GetAttributeSetClass())
			{
				Snapshot.AttributeSetName = OwnerClass->GetName();
			}
		}

		Snapshots.Add(MoveTemp(Snapshot));
	}

	return Snapshots;
}

// ============================================================================
//  技能
// ============================================================================

FGameplayAbilitySpecHandle UGASCombatStatics::GrantAbility(AActor* Target, TSubclassOf<UGameplayAbility> AbilityClass, int32 Level)
{
	UAbilitySystemComponent* ASC = ResolveASC(Target);
	if (!ASC)
	{
		WarnMissingASC(Target, TEXT("授予技能"));
		return FGameplayAbilitySpecHandle();
	}

	if (!AbilityClass)
	{
		UE_LOG(LogGASCombat, Warning, TEXT("【GAS战斗桥接】授予技能失败：没有指定技能类。"));
		return FGameplayAbilitySpecHandle();
	}

	if (!ASC->IsOwnerActorAuthoritative())
	{
		UE_LOG(LogGASCombat, Warning,
			TEXT("【GAS战斗桥接】授予技能 %s 被忽略：当前不是服务器权威端。\n")
			TEXT("  GAS 规定只有服务器能授予技能。联机时请在服务器上调用，客户端让它自动同步。"),
			*AbilityClass->GetName());
		return FGameplayAbilitySpecHandle();
	}

	// Level 至少为 1：填 0 会让「可缩放数值」查到第 0 行，通常不是策划想要的结果。
	const int32 SafeLevel = FMath::Max(Level, 1);

	// K2_GiveAbility 内部会构造 FGameplayAbilitySpec 并调用 GiveAbility。
	return ASC->K2_GiveAbility(AbilityClass, SafeLevel, -1);
}

bool UGASCombatStatics::RemoveAbility(AActor* Target, FGameplayAbilitySpecHandle Handle)
{
	UAbilitySystemComponent* ASC = ResolveASC(Target);
	if (!ASC || !Handle.IsValid())
	{
		return false;
	}

	if (!ASC->FindAbilitySpecFromHandle(Handle))
	{
		return false;
	}

	ASC->ClearAbility(Handle);
	return true;
}

bool UGASCombatStatics::HasAbility(AActor* Target, TSubclassOf<UGameplayAbility> AbilityClass)
{
	UAbilitySystemComponent* ASC = ResolveASC(Target);
	return ASC && AbilityClass ? (ASC->FindAbilitySpecFromClass(AbilityClass) != nullptr) : false;
}

bool UGASCombatStatics::TryActivateAbility(AActor* Target, TSubclassOf<UGameplayAbility> AbilityClass, bool bAllowRemoteActivation)
{
	UAbilitySystemComponent* ASC = ResolveASC(Target);
	if (!ASC)
	{
		WarnMissingASC(Target, TEXT("尝试激活技能"));
		return false;
	}

	if (!AbilityClass)
	{
		return false;
	}

	const bool bActivated = ASC->TryActivateAbilityByClass(AbilityClass, bAllowRemoteActivation);
	if (!bActivated)
	{
		UE_LOG(LogGASCombat, Verbose,
			TEXT("【GAS战斗桥接】技能 %s 激活未通过检查。常见原因：未授予、标签不满足、消耗不足、冷却中、非本地控制端。\n")
			TEXT("  用「打印战斗信息」查看当前标签与属性，或控制台执行 showdebug abilitysystem（翻页键切换分类）。"),
			*AbilityClass->GetName());
	}
	return bActivated;
}

bool UGASCombatStatics::TryActivateAbilitiesByTag(AActor* Target, FGameplayTagContainer Tags, bool bAllowRemoteActivation)
{
	UAbilitySystemComponent* ASC = ResolveASC(Target);
	if (!ASC)
	{
		WarnMissingASC(Target, TEXT("按标签激活技能"));
		return false;
	}
	return ASC->TryActivateAbilitiesByTag(Tags, bAllowRemoteActivation);
}

bool UGASCombatStatics::CancelAbility(AActor* Target, TSubclassOf<UGameplayAbility> AbilityClass)
{
	UAbilitySystemComponent* ASC = ResolveASC(Target);
	if (!ASC || !AbilityClass)
	{
		return false;
	}

	FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromClass(AbilityClass);
	if (!Spec)
	{
		return false;
	}

	if (Spec->ActiveCount <= 0)
	{
		return false;
	}

	ASC->CancelAbilityHandle(Spec->Handle);
	return true;
}

int32 UGASCombatStatics::CancelAbilitiesWithTags(AActor* Target, FGameplayTagContainer Tags)
{
	UAbilitySystemComponent* ASC = ResolveASC(Target);
	if (!ASC || Tags.IsEmpty())
	{
		return 0;
	}

	TArray<FGameplayAbilitySpecHandle> MatchingHandles;
	// bExactMatch=false：父标签也能匹配，方便一次打断一整类技能（例如 Ability.Attack）
	ASC->FindAllAbilitiesWithTags(MatchingHandles, Tags, /*bExactMatch=*/false);

	int32 CancelledCount = 0;
	for (const FGameplayAbilitySpecHandle& Handle : MatchingHandles)
	{
		const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(Handle);
		if (Spec && Spec->ActiveCount > 0)
		{
			ASC->CancelAbilityHandle(Handle);
			++CancelledCount;
		}
	}

	return CancelledCount;
}

int32 UGASCombatStatics::CancelAllAbilities(AActor* Target)
{
	UAbilitySystemComponent* ASC = ResolveASC(Target);
	if (!ASC)
	{
		WarnMissingASC(Target, TEXT("取消所有技能"));
		return 0;
	}

	// 取消前后各数一次「有几个技能在跑」，差值才是本次真正取消掉的数量。
	// 直接返回遍历到的数量是不准的：有的技能可能自己已经结束了。
	auto CountActiveAbilities = [ASC]()
	{
		int32 Count = 0;
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			if (Spec.IsActive())
			{
				++Count;
			}
		}
		return Count;
	};

	const int32 ActiveBefore = CountActiveAbilities();

	// 三个过滤器全传空 = 不做任何筛选，取消每一个激活中的技能。
	ASC->CancelAbilities(nullptr, nullptr, nullptr);

	const int32 CancelledCount = ActiveBefore - CountActiveAbilities();

	// 没有任何技能在跑是**正常情况**（例如死亡流程被重复触发），不报错、不打警告。
	if (CancelledCount > 0)
	{
		UE_LOG(LogGASCombat, Log, TEXT("【GAS战斗桥接】已取消 %s 身上 %d 个正在运行的技能。"),
			*GetNameSafe(Target), CancelledCount);
	}

	return CancelledCount;
}

FGameplayTagContainer UGASCombatStatics::GetAbilitySpecDynamicTags(AActor* Target, FGameplayAbilitySpecHandle Handle)
{
	FGameplayTagContainer Result;

	UAbilitySystemComponent* ASC = ResolveASC(Target);
	if (!ASC)
	{
		WarnMissingASC(Target, TEXT("取技能的动态标签"));
		return Result;
	}

	if (!Handle.IsValid())
	{
		UE_LOG(LogGASCombat, Warning,
			TEXT("【GAS战斗桥接】取技能的动态标签失败：技能句柄无效。\n")
			TEXT("  句柄要接「授予技能」的返回值；如果技能是技能集授予的，请从「获取已授予技能列表」里取。"));
		return Result;
	}

	FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(Handle);
	if (!Spec)
	{
		UE_LOG(LogGASCombat, Warning,
			TEXT("【GAS战斗桥接】取技能的动态标签失败：这个技能句柄不在 %s 的技能系统组件上。\n")
			TEXT("  两种可能：① 这个技能还没授予；② 它已经被移除了（换武器/换职业时常见）。"),
			*GetNameSafe(Target));
		return Result;
	}

	// 5.5 起 DynamicAbilityTags 被标记为废弃，改叫 GetDynamicSpecSourceTags()。
	// 用 UE_VERSION_OLDER_THAN 而不是 NEWER_THAN(5,5,0)：后者在正好 5.5.0 上会走旧分支，
	// 那样会吃到一条废弃警告。
#if UE_VERSION_OLDER_THAN(5, 5, 0)
	return Spec->DynamicAbilityTags;
#else
	return Spec->GetDynamicSpecSourceTags();
#endif
}

bool UGASCombatStatics::SetAbilitySpecDynamicTags(AActor* Target, FGameplayAbilitySpecHandle Handle,
	FGameplayTagContainer Tags, bool bAppend)
{
	UAbilitySystemComponent* ASC = ResolveASC(Target);
	if (!ASC)
	{
		WarnMissingASC(Target, TEXT("设置技能的动态标签"));
		return false;
	}

	if (!Handle.IsValid())
	{
		UE_LOG(LogGASCombat, Warning,
			TEXT("【GAS战斗桥接】设置技能的动态标签失败：技能句柄无效。\n")
			TEXT("  句柄要接「授予技能」的返回值。"));
		return false;
	}

	FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(Handle);
	if (!Spec)
	{
		UE_LOG(LogGASCombat, Warning,
			TEXT("【GAS战斗桥接】设置技能的动态标签失败：这个技能句柄不在 %s 的技能系统组件上。\n")
			TEXT("  两种可能：① 这个技能还没授予；② 它已经被移除了。"),
			*GetNameSafe(Target));
		return false;
	}

#if UE_VERSION_OLDER_THAN(5, 5, 0)
	FGameplayTagContainer& DynamicTags = Spec->DynamicAbilityTags;
#else
	FGameplayTagContainer& DynamicTags = Spec->GetDynamicSpecSourceTags();
#endif

	if (!bAppend)
	{
		// 先清空再写入。注意清的只是**动态标签**——
		// 技能的 Ability Tags（Spec->Ability->GetAssetTags()）是只读的身份标签，
		// 这里碰不到它，所以按标签激活 / 打断仍然按原能力族标签工作。
		DynamicTags.Reset();
	}

	DynamicTags.AppendTags(Tags);

	return true;
}

bool UGASCombatStatics::CommitAbilityFor(UGameplayAbility* Ability)
{
	if (!IsValid(Ability))
	{
		UE_LOG(LogGASCombat, Warning,
			TEXT("【GAS战斗桥接】结算消耗与冷却失败：没有传入技能。\n")
			TEXT("  这个节点要在技能蓝图里用，把 Self 接到「技能」引脚上（默认会自己填当前技能）。"));
		return false;
	}

	const bool bCommitted = Ability->K2_CommitAbility();

	if (!bCommitted)
	{
		// 引擎只会安静地返回 false，不说为什么。这里补一句，并指向能给出原因的地方。
		UE_LOG(LogGASCombat, Warning,
			TEXT("【GAS战斗桥接】技能 %s 结算消耗与冷却失败（消耗不足或正在冷却）。\n")
			TEXT("  具体是哪一条没过，看桥接组件的「技能激活失败时」事件，它会给出中文原因。"),
			*Ability->GetClass()->GetName());
	}

	return bCommitted;
}

void UGASCombatStatics::EndAbilityFor(UGameplayAbility* Ability, bool bWasCancelled)
{
	if (!IsValid(Ability))
	{
		UE_LOG(LogGASCombat, Warning,
			TEXT("【GAS战斗桥接】结束技能失败：没有传入技能。\n")
			TEXT("  这个节点要在技能蓝图里用，把 Self 接到「技能」引脚上。"));
		return;
	}

	// 【为什么不能直接调 Ability->EndAbility(...) 或 Ability->K2_EndAbility()】
	// UGameplayAbility 把 EndAbility、K2_EndAbility、K2_EndAbilityLocally 三个都放在
	// **protected** 区段里：引擎只允许**蓝图**调它们，C++ 从外部调不到。
	// 这是编译期的可见性限制，不是运行时契约——蓝图节点自己走的就是这个 UFunction。
	//
	// 所以两条路径分别走各自正规的公开入口：
	//   正常结束(bWasCancelled=false) → 按 UFunction 调用 K2_EndAbility，
	//        与蓝图里直接连引擎的「End Ability」节点效果完全一致。
	//   被打断  (bWasCancelled=true)  → ASC->CancelAbilityHandle()，
	//        这才是引擎里「结束并标记为取消」的正规做法：内部会用
	//        bWasCancelled = true 收尾，并且会正确地把取消复制到客户端。
	if (bWasCancelled)
	{
		UAbilitySystemComponent* ASC = Ability->GetAbilitySystemComponentFromActorInfo();
		if (!ASC)
		{
			UE_LOG(LogGASCombat, Warning,
				TEXT("【GAS战斗桥接】结束技能（标记为被打断）失败：技能 %s 拿不到技能系统组件。\n")
				TEXT("  技能可能还没真正激活，或者它的 ActorInfo 已经失效。"),
				*Ability->GetClass()->GetName());
			return;
		}

		ASC->CancelAbilityHandle(Ability->GetCurrentAbilitySpecHandle());
		return;
	}

	// 按名字找 UFunction。找不到时给出明确警告，绝不静默什么都不做 ——
	// 否则使用者会遇到「调了结束技能但技能还在跑」这种最难查的情况。
	UFunction* EndAbilityFunction = UGameplayAbility::StaticClass()->FindFunctionByName(TEXT("K2_EndAbility"));
	if (!EndAbilityFunction)
	{
		UE_LOG(LogGASCombat, Error,
			TEXT("【GAS战斗桥接】结束技能失败：在这个引擎版本里找不到 UGameplayAbility::K2_EndAbility。\n")
			TEXT("  这是引擎接口变更导致的，请在技能蓝图里改用引擎原生的「End Ability」节点。"));
		return;
	}

	Ability->ProcessEvent(EndAbilityFunction, nullptr);
}

bool UGASCombatStatics::IsAbilityActive(AActor* Target, TSubclassOf<UGameplayAbility> AbilityClass)
{
	UAbilitySystemComponent* ASC = ResolveASC(Target);
	if (!ASC || !AbilityClass)
	{
		return false;
	}

	const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromClass(AbilityClass);
	return Spec && Spec->ActiveCount > 0;
}

TArray<FGASCombatAbilityInfo> UGASCombatStatics::GetGrantedAbilities(AActor* Target)
{
	TArray<FGASCombatAbilityInfo> Result;

	UAbilitySystemComponent* ASC = ResolveASC(Target);
	if (!ASC)
	{
		return Result;
	}

	const FGameplayAbilityActorInfo* ActorInfo = ASC->AbilityActorInfo.Get();

	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		FGASCombatAbilityInfo Info;
		Info.Handle = Spec.Handle;
		Info.Level = Spec.Level;
		Info.bIsActive = Spec.ActiveCount > 0;

		if (const UGameplayAbility* Ability = Spec.Ability)
		{
			Info.AbilityClass = Ability->GetClass();
			Info.AbilityName = Ability->GetClass()->GetName();

			// 冷却查询必须用「技能类默认对象 + 句柄 + ActorInfo」，
			// 因为冷却本质上是一枚挂在身上的标签，只有技能自己知道该查哪些标签。
			float TimeRemaining = 0.f;
			float Duration = 0.f;
			Ability->GetCooldownTimeRemainingAndDuration(Spec.Handle, ActorInfo, TimeRemaining, Duration);
			Info.CooldownRemaining = TimeRemaining;
			Info.CooldownDuration = Duration;
			Info.CooldownPercent = Duration > 0.f ? FMath::Clamp(TimeRemaining / Duration, 0.f, 1.f) : 0.f;
		}

		Result.Add(MoveTemp(Info));
	}

	return Result;
}

float UGASCombatStatics::GetAbilityCooldownRemaining(AActor* Target, TSubclassOf<UGameplayAbility> AbilityClass)
{
	UAbilitySystemComponent* ASC = ResolveASC(Target);
	if (!ASC || !AbilityClass)
	{
		return 0.f;
	}

	const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromClass(AbilityClass);
	if (!Spec || !Spec->Ability)
	{
		return 0.f;
	}

	float TimeRemaining = 0.f;
	float Duration = 0.f;
	Spec->Ability->GetCooldownTimeRemainingAndDuration(Spec->Handle, ASC->AbilityActorInfo.Get(), TimeRemaining, Duration);
	return TimeRemaining;
}

float UGASCombatStatics::GetAbilityCooldownPercent(AActor* Target, TSubclassOf<UGameplayAbility> AbilityClass)
{
	UAbilitySystemComponent* ASC = ResolveASC(Target);
	if (!ASC || !AbilityClass)
	{
		return 0.f;
	}

	const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromClass(AbilityClass);
	if (!Spec || !Spec->Ability)
	{
		return 0.f;
	}

	float TimeRemaining = 0.f;
	float Duration = 0.f;
	Spec->Ability->GetCooldownTimeRemainingAndDuration(Spec->Handle, ASC->AbilityActorInfo.Get(), TimeRemaining, Duration);
	return Duration > 0.f ? FMath::Clamp(TimeRemaining / Duration, 0.f, 1.f) : 0.f;
}

// ============================================================================
//  效果
// ============================================================================

FActiveGameplayEffectHandle UGASCombatStatics::ApplyEffect(AActor* Target, TSubclassOf<UGameplayEffect> EffectClass,
	int32 Level, AActor* Instigator, AActor* EffectCauser)
{
	UAbilitySystemComponent* ASC = ResolveASC(Target);
	if (!ASC)
	{
		WarnMissingASC(Target, TEXT("施加效果"));
		return FActiveGameplayEffectHandle();
	}

	if (!EffectClass)
	{
		UE_LOG(LogGASCombat, Warning, TEXT("【GAS战斗桥接】施加效果失败：没有指定效果类。"));
		return FActiveGameplayEffectHandle();
	}

	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddInstigator(Instigator ? Instigator : Target, EffectCauser ? EffectCauser : (Instigator ? Instigator : Target));

	const FActiveGameplayEffectHandle Handle = ASC->BP_ApplyGameplayEffectToSelf(EffectClass, static_cast<float>(FMath::Max(Level, 1)), Context);

	if (!Handle.WasSuccessfullyApplied())
	{
		UE_LOG(LogGASCombat, Verbose,
			TEXT("【GAS战斗桥接】效果 %s 施加后没有生效。常见原因：效果被免疫/条件标签挡下、目标已拥有同名效果且不支持叠层、目标缺少对应属性集。"),
			*EffectClass->GetName());
	}

	return Handle;
}

FActiveGameplayEffectHandle UGASCombatStatics::ApplyEffectWithValues(AActor* Target, TSubclassOf<UGameplayEffect> EffectClass,
	const TMap<FGameplayTag, float>& SetByCallerValues, int32 Level, AActor* Instigator, AActor* EffectCauser)
{
	UAbilitySystemComponent* ASC = ResolveASC(Target);
	if (!ASC)
	{
		WarnMissingASC(Target, TEXT("施加效果（动态数值）"));
		return FActiveGameplayEffectHandle();
	}

	if (!EffectClass)
	{
		return FActiveGameplayEffectHandle();
	}

	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddInstigator(Instigator ? Instigator : Target, EffectCauser ? EffectCauser : (Instigator ? Instigator : Target));

	FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(EffectClass, static_cast<float>(FMath::Max(Level, 1)), Context);
	if (!SpecHandle.IsValid())
	{
		UE_LOG(LogGASCombat, Warning, TEXT("【GAS战斗桥接】创建效果实例失败：%s。"), *EffectClass->GetName());
		return FActiveGameplayEffectHandle();
	}

	int32 AssignedCount = 0;
	for (const TPair<FGameplayTag, float>& Pair : SetByCallerValues)
	{
		if (Pair.Key.IsValid())
		{
			// 注意：这里必须用标签版本的赋值。用 FName 的旧接口在 5.x 已不推荐，
			// 而且和效果资产里配置的「SetByCaller 标签」对不上时会静默失效。
			SpecHandle = UAbilitySystemBlueprintLibrary::AssignTagSetByCallerMagnitude(SpecHandle, Pair.Key, Pair.Value);
			++AssignedCount;
		}
	}

	if (AssignedCount == 0)
	{
		UE_LOG(LogGASCombat, Warning,
			TEXT("【GAS战斗桥接】施加效果（动态数值）：传入的标签-数值映射为空或标签无效，效果将按资产里的默认数值结算。"));
	}

	return ASC->BP_ApplyGameplayEffectSpecToSelf(SpecHandle);
}

FActiveGameplayEffectHandle UGASCombatStatics::ApplyDamageToTarget(AActor* Source, AActor* Target,
	TSubclassOf<UGameplayEffect> DamageEffectClass, float DamageAmount, int32 Level, AActor* EffectCauser)
{
	UAbilitySystemComponent* TargetASC = ResolveASC(Target);
	if (!TargetASC)
	{
		WarnMissingASC(Target, TEXT("对目标造成伤害"));
		return FActiveGameplayEffectHandle();
	}

	if (!DamageEffectClass)
	{
		UE_LOG(LogGASCombat, Warning, TEXT("【GAS战斗桥接】对目标造成伤害失败：没有指定伤害效果类。"));
		return FActiveGameplayEffectHandle();
	}

	// 来源可以为空（陷阱、掉落伤害、环境伤害），此时不给发起者。
	UAbilitySystemComponent* SourceASC = ResolveASC(Source);

	FGameplayEffectContextHandle Context = SourceASC ? SourceASC->MakeEffectContext() : TargetASC->MakeEffectContext();
	if (Source)
	{
		// 发起者(谁打的)与效果施加者(用什么打的)分开传，后续做击杀归属/击退方向时都需要。
		Context.AddInstigator(Source, EffectCauser ? EffectCauser : Source);
	}
	else if (!EffectCauser)
	{
		// 环境伤害：把目标自己记为施加者，避免上下文里出现空指针导致部分引擎逻辑异常。
		Context.AddInstigator(Target, Target);
	}

	UAbilitySystemComponent* SpecSourceASC = SourceASC ? SourceASC : TargetASC;
	FGameplayEffectSpecHandle SpecHandle = SpecSourceASC->MakeOutgoingSpec(DamageEffectClass, static_cast<float>(FMath::Max(Level, 1)), Context);
	if (!SpecHandle.IsValid())
	{
		return FActiveGameplayEffectHandle();
	}

	// 约定：伤害数值通过标签 Data.Damage 以「调用方赋值」传入。
	// 该标签由本插件在模块启动时用 AddNativeGameplayTag 原生注册
	// （不是靠 Config/DefaultGameplayTags.ini —— 插件 Config 不会被并进引擎
	//  GameplayTags 模块的配置层级，写在那里是静默失效的，详见 GASCombatBridge.cpp）。
	const FGameplayTag DamageTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Data.Damage")), /*ErrorIfNotFound=*/false);
	if (DamageTag.IsValid())
	{
		SpecHandle = UAbilitySystemBlueprintLibrary::AssignTagSetByCallerMagnitude(SpecHandle, DamageTag, DamageAmount);
	}
	else
	{
		UE_LOG(LogGASCombat, Warning,
			TEXT("【GAS战斗桥接】找不到标签 Data.Damage，伤害数值没有被传进效果。\n")
			TEXT("  这个标签由插件模块启动时注册。出现本提示说明插件模块的 StartupModule 没有跑起来——")
			TEXT("请检查 .uplugin 是否启用、以及二进制是否与当前引擎版本匹配。"));
	}

	return TargetASC->BP_ApplyGameplayEffectSpecToSelf(SpecHandle);
}

bool UGASCombatStatics::RemoveEffect(AActor* Target, FActiveGameplayEffectHandle Handle)
{
	UAbilitySystemComponent* ASC = ResolveASC(Target);
	if (!ASC || !Handle.IsValid())
	{
		return false;
	}
	return ASC->RemoveActiveGameplayEffect(Handle, -1);
}

int32 UGASCombatStatics::RemoveEffectsWithGrantedTags(AActor* Target, FGameplayTagContainer Tags)
{
	UAbilitySystemComponent* ASC = ResolveASC(Target);
	if (!ASC || Tags.IsEmpty())
	{
		return 0;
	}
	// 「授予标签」= 效果给目标挂上的标签。按它来移除，就是策划说的「驱散这类负面状态」。
	return ASC->RemoveActiveEffectsWithGrantedTags(Tags);
}

int32 UGASCombatStatics::GetEffectStackCount(AActor* Target, TSubclassOf<UGameplayEffect> EffectClass)
{
	UAbilitySystemComponent* ASC = ResolveASC(Target);
	if (!ASC || !EffectClass)
	{
		return 0;
	}

	// GAS 没有独立的「充能/次数」概念，叠层就是官方的实现方式：
	// 一个无限持续 + 可叠层的效果，层数就是剩余次数。
	int32 TotalStacks = 0;
	for (const FActiveGameplayEffectHandle& Handle : CollectActiveEffects(ASC))
	{
		const UGameplayEffect* ActiveEffect = UAbilitySystemBlueprintLibrary::GetGameplayEffectFromActiveEffectHandle(Handle);
		if (ActiveEffect && ActiveEffect->GetClass() == EffectClass.Get())
		{
			TotalStacks += UAbilitySystemBlueprintLibrary::GetActiveGameplayEffectStackCount(Handle);
		}
	}

	return TotalStacks;
}

bool UGASCombatStatics::HasEffect(AActor* Target, TSubclassOf<UGameplayEffect> EffectClass)
{
	return GetEffectStackCount(Target, EffectClass) > 0;
}

TArray<FGASCombatActiveEffectInfo> UGASCombatStatics::GetActiveEffectInfos(AActor* Target)
{
	TArray<FGASCombatActiveEffectInfo> Result;

	UAbilitySystemComponent* ASC = ResolveASC(Target);
	if (!ASC)
	{
		return Result;
	}

	for (const FActiveGameplayEffectHandle& Handle : CollectActiveEffects(ASC))
	{
		const UGameplayEffect* ActiveEffect = UAbilitySystemBlueprintLibrary::GetGameplayEffectFromActiveEffectHandle(Handle);
		if (!ActiveEffect)
		{
			continue;
		}

		FGASCombatActiveEffectInfo Info;
		Info.EffectClass = ActiveEffect->GetClass();
		Info.EffectName = ActiveEffect->GetClass()->GetName();
		Info.Handle = Handle;
		Info.StackCount = UAbilitySystemBlueprintLibrary::GetActiveGameplayEffectStackCount(Handle);

		const float TotalDuration = UAbilitySystemBlueprintLibrary::GetActiveGameplayEffectTotalDuration(Handle);
		// 总时长为 0 或负数 = 无限持续效果（不会被自动移除，只能手动驱散）
		Info.bIsInfinite = TotalDuration <= 0.f;
		Info.TotalDuration = Info.bIsInfinite ? -1.f : TotalDuration;
		Info.RemainingTime = Info.bIsInfinite
			? -1.f
			: UAbilitySystemBlueprintLibrary::GetActiveGameplayEffectRemainingDuration(Target, Handle);

		Result.Add(MoveTemp(Info));
	}

	return Result;
}

float UGASCombatStatics::GetEffectRemainingTime(AActor* Target, FActiveGameplayEffectHandle Handle)
{
	if (!Handle.IsValid())
	{
		return 0.f;
	}
	return UAbilitySystemBlueprintLibrary::GetActiveGameplayEffectRemainingDuration(Target, Handle);
}

// ============================================================================
//  标签
// ============================================================================

bool UGASCombatStatics::HasTag(AActor* Target, FGameplayTag Tag)
{
	UAbilitySystemComponent* ASC = ResolveASC(Target);
	return ASC ? ASC->HasMatchingGameplayTag(Tag) : false;
}

bool UGASCombatStatics::HasAnyTags(AActor* Target, FGameplayTagContainer Tags)
{
	UAbilitySystemComponent* ASC = ResolveASC(Target);
	return ASC ? ASC->HasAnyMatchingGameplayTags(Tags) : false;
}

bool UGASCombatStatics::HasAllTags(AActor* Target, FGameplayTagContainer Tags)
{
	UAbilitySystemComponent* ASC = ResolveASC(Target);
	return ASC ? ASC->HasAllMatchingGameplayTags(Tags) : false;
}

FGameplayTagContainer UGASCombatStatics::GetAllTags(AActor* Target)
{
	UAbilitySystemComponent* ASC = ResolveASC(Target);
	return ASC ? ASC->GetOwnedGameplayTags() : FGameplayTagContainer();
}

int32 UGASCombatStatics::GetTagCount(AActor* Target, FGameplayTag Tag)
{
	UAbilitySystemComponent* ASC = ResolveASC(Target);
	return ASC ? ASC->GetGameplayTagCount(Tag) : 0;
}

bool UGASCombatStatics::AddLooseTags(AActor* Target, FGameplayTagContainer Tags, bool bReplicate)
{
	if (!IsValid(Target) || Tags.IsEmpty())
	{
		return false;
	}
	return UAbilitySystemBlueprintLibrary::AddLooseGameplayTags(Target, Tags, bReplicate);
}

bool UGASCombatStatics::RemoveLooseTags(AActor* Target, FGameplayTagContainer Tags, bool bReplicate)
{
	if (!IsValid(Target) || Tags.IsEmpty())
	{
		return false;
	}
	return UAbilitySystemBlueprintLibrary::RemoveLooseGameplayTags(Target, Tags, bReplicate);
}

// ============================================================================
//  表现与事件
// ============================================================================

void UGASCombatStatics::PlayGameplayCue(AActor* Target, FGameplayTag CueTag)
{
	if (!IsValid(Target) || !CueTag.IsValid())
	{
		return;
	}

	if (!CueTag.ToString().StartsWith(TEXT("GameplayCue")))
	{
		UE_LOG(LogGASCombat, Warning,
			TEXT("【GAS战斗桥接】表现标签 %s 不以 GameplayCue 开头，引擎会直接忽略它（且不报错）。\n")
			TEXT("  正确写法例如：GameplayCue.Hit.Impact"),
			*CueTag.ToString());
		return;
	}

	UGameplayCueFunctionLibrary::ExecuteGameplayCueOnActor(Target, CueTag, FGameplayCueParameters());
}

void UGASCombatStatics::AddLoopingGameplayCue(AActor* Target, FGameplayTag CueTag)
{
	if (!IsValid(Target) || !CueTag.IsValid())
	{
		return;
	}
	UGameplayCueFunctionLibrary::AddGameplayCueOnActor(Target, CueTag, FGameplayCueParameters());
}

void UGASCombatStatics::RemoveLoopingGameplayCue(AActor* Target, FGameplayTag CueTag)
{
	if (!IsValid(Target) || !CueTag.IsValid())
	{
		return;
	}
	UGameplayCueFunctionLibrary::RemoveGameplayCueOnActor(Target, CueTag, FGameplayCueParameters());
}

void UGASCombatStatics::SendGameplayEvent(AActor* Target, FGameplayTag EventTag, FGameplayEventData Payload)
{
	if (!IsValid(Target) || !EventTag.IsValid())
	{
		return;
	}

	if (!ResolveASC(Target))
	{
		WarnMissingASC(Target, TEXT("发送Gameplay事件"));
		return;
	}

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Target, EventTag, Payload);
}

bool UGASCombatStatics::SendStateTreeEventToTarget(AActor* Target, FGameplayTag EventTag, FName Origin)
{
	if (!IsValid(Target))
	{
		UE_LOG(LogGASCombat, Warning, TEXT("【GAS战斗桥接】发送状态树事件失败：目标为空。"));
		return false;
	}

	if (!EventTag.IsValid())
	{
		UE_LOG(LogGASCombat, Warning,
			TEXT("【GAS战斗桥接】发送状态树事件失败：事件标签为空。\n")
			TEXT("  状态树里要用「On Event」触发器配上同一个标签才能接住它。"));
		return false;
	}

	// UStateTreeComponent 继承自 UBrainComponent（不是 UActorComponent 的直接子类），
	// 用 FindComponentByClass 能连子类一起找到。
	UStateTreeComponent* StateTreeComp = Target->FindComponentByClass<UStateTreeComponent>();
	if (!StateTreeComp)
	{
		UE_LOG(LogGASCombat, Warning,
			TEXT("【GAS战斗桥接】发送状态树事件失败：%s 身上没有 State Tree 组件。\n")
			TEXT("  加一个「State Tree」组件，并在它的「State Tree」属性里指定要跑的状态树资产。"),
			*GetNameSafe(Target));
		return false;
	}

	// 状态树的逻辑状态（Running / Paused / Stopped）不在这个类上，无法在这里可靠地判断
	// 「树到底跑起来没有」，所以事件发出去就算成功；树没跑的话事件会被丢掉。
	// 这一点在节点 ToolTip 和文档里都写明了，避免使用者误以为「发了就一定会被处理」。
	if (Origin.IsNone())
	{
		Origin = FName(TEXT("GASCombatBridge"));
	}

	StateTreeComp->SendStateTreeEvent(FStateTreeEvent(EventTag, FConstStructView(), Origin));
	return true;
}

// ============================================================================
//  调试
// ============================================================================

FString UGASCombatStatics::GetAttributeDisplayName(FName AttributeName)
{
	// 插件内置属性的中文名。
	// 这里**只**收录插件自己声明的属性；使用者项目里的自定义属性集会原样返回英文名 ——
	// 插件不去猜业务含义，免得出现「看着像中文其实译错了」的名字。
	static const TMap<FName, FString> DisplayNames =
	{
		// 通用战斗属性集
		{ TEXT("Health"),          TEXT("生命") },
		{ TEXT("MaxHealth"),       TEXT("生命上限") },
		{ TEXT("Shield"),          TEXT("护盾") },
		{ TEXT("MaxShield"),       TEXT("护盾上限") },
		{ TEXT("Mana"),            TEXT("法力") },
		{ TEXT("MaxMana"),         TEXT("法力上限") },
		{ TEXT("AttackPower"),     TEXT("攻击力") },
		{ TEXT("Defense"),         TEXT("防御力") },
		{ TEXT("MoveSpeed"),       TEXT("移动速度") },
		{ TEXT("IncomingDamage"),  TEXT("待结算伤害") },
		{ TEXT("IncomingHealing"), TEXT("待结算治疗") },

		// 连招属性集。这一条看着小，但决定了「用调试面板/状态报告一眼看出第几段」能不能做到 ——
		// 报告里显示英文 ComboIndex 会让使用者多绕一层。
		{ TEXT("ComboIndex"),      TEXT("连招段数") },
		{ TEXT("MaxComboIndex"),   TEXT("连招段数上限") },
	};

	if (const FString* Found = DisplayNames.Find(AttributeName))
	{
		return *Found;
	}
	return AttributeName.ToString();
}

FString UGASCombatStatics::GetCombatSummaryText(AActor* Target)
{
	TStringBuilder<4096> Builder;
	Builder.Append(TEXT("=========== 战斗状态报告 ===========\n"));
	Builder.Appendf(TEXT("角色：%s\n"), *GetNameSafe(Target));

	UAbilitySystemComponent* ASC = ResolveASC(Target);
	if (!ASC)
	{
		Builder.Append(TEXT("技能系统组件：**缺失**\n"));
		Builder.Append(TEXT("  → 给角色蓝图添加「GAS战斗桥接」组件，或在 BeginPlay 调用「获取或创建技能系统组件」。\n"));
		Builder.Append(TEXT("===================================="));
		return Builder.ToString();
	}

	Builder.Appendf(TEXT("技能系统组件：已找到   初始化：%s   权威端：%s\n"),
		IsAbilitySystemInitialized(Target) ? TEXT("已初始化") : TEXT("**未初始化**"),
		ASC->IsOwnerActorAuthoritative() ? TEXT("是") : TEXT("否"));

	if (!IsAbilitySystemInitialized(Target))
	{
		Builder.Append(TEXT("  → 没有初始化的话技能永远激活不了。请调用「初始化技能系统」（放在 BeginPlay 之后）。\n"));
	}

	// --- 属性 ---
	Builder.Append(TEXT("\n--- 属性 ---\n"));
	const TArray<FGASCombatAttributeSnapshot> Attributes = CaptureAllAttributes(Target);
	if (Attributes.Num() == 0)
	{
		Builder.Append(TEXT("  (没有任何属性)\n"));
		Builder.Append(TEXT("  → 是不是忘了挂「通用战斗属性集」？在桥接组件的「挂载的属性集」里加一项。\n"));
	}
	for (const FGASCombatAttributeSnapshot& Snapshot : Attributes)
	{
		const bool bHasModifiers = !FMath::IsNearlyEqual(Snapshot.BaseValue, Snapshot.CurrentValue, 0.001f);
		Builder.Appendf(TEXT("  %-12s 当前 %9.2f  基础 %9.2f%s\n"),
			*GetAttributeDisplayName(FName(*Snapshot.AttributeName)),
			Snapshot.CurrentValue,
			Snapshot.BaseValue,
			bHasModifiers ? TEXT("   ← 有持续效果在起作用") : TEXT(""));
	}

	// --- 技能 ---
	Builder.Append(TEXT("\n--- 已授予技能 ---\n"));
	const TArray<FGASCombatAbilityInfo> Abilities = GetGrantedAbilities(Target);
	if (Abilities.Num() == 0)
	{
		Builder.Append(TEXT("  (没有已授予的技能)\n"));
		Builder.Append(TEXT("  → 检查桥接组件的「授予的技能集」，或者是不是在客户端调用了「授予技能」（只有服务器能授予）。\n"));
	}
	for (const FGASCombatAbilityInfo& Info : Abilities)
	{
		Builder.Appendf(TEXT("  %-30s 等级%-3d %s"),
			*Info.AbilityName, Info.Level,
			Info.bIsActive ? TEXT("[运行中]") : TEXT("[就绪]  "));
		if (Info.CooldownRemaining > 0.f)
		{
			Builder.Appendf(TEXT("  冷却中 %.2f / %.2f 秒（%.0f%%）"),
				Info.CooldownRemaining, Info.CooldownDuration, Info.CooldownPercent * 100.f);
		}
		Builder.Append(TEXT("\n"));
	}

	// --- 效果 ---
	Builder.Append(TEXT("\n--- 激活中的效果 ---\n"));
	const TArray<FGASCombatActiveEffectInfo> Effects = GetActiveEffectInfos(Target);
	if (Effects.Num() == 0)
	{
		Builder.Append(TEXT("  (无)\n"));
	}
	for (const FGASCombatActiveEffectInfo& Info : Effects)
	{
		Builder.Appendf(TEXT("  %-30s 层数%-3d %s\n"),
			*Info.EffectName, Info.StackCount,
			Info.bIsInfinite ? TEXT("无限持续（需手动移除）") : *FString::Printf(TEXT("剩余 %.2f / %.2f 秒"), Info.RemainingTime, Info.TotalDuration));
	}

	// --- 标签 ---
	Builder.Append(TEXT("\n--- 身上的标签（含层数） ---\n  "));
	const FGameplayTagContainer Tags = GetAllTags(Target);
	if (Tags.IsEmpty())
	{
		Builder.Append(TEXT("(无)\n"));
	}
	else
	{
		TArray<FString> TagStrings;
		for (const FGameplayTag& Tag : Tags)
		{
			TagStrings.Add(FString::Printf(TEXT("%s(%d)"), *Tag.ToString(), ASC->GetGameplayTagCount(Tag)));
		}
		Builder.Append(FString::Join(TagStrings, TEXT("  ")));
		Builder.Append(TEXT("\n"));
	}

	Builder.Append(TEXT("===================================="));
	return Builder.ToString();
}

float UGASCombatStatics::GetTagLastChangeTime(AActor* Target, FGameplayTag Tag)
{
	if (!IsValid(Target) || !Tag.IsValid())
	{
		return -1.f;
	}

	// 记录点在桥接组件里（它才是注册标签事件的地方），这里只是转个手。
	// 找不到组件是**正常情况**：没挂桥接组件的角色本来就不记录标签时刻，
	// 所以这里不报警告，直接返回 -1，并把「为什么是 -1」写进节点说明里。
	const UGASCombatBridgeComponent* Bridge = Target->FindComponentByClass<UGASCombatBridgeComponent>();
	if (!Bridge)
	{
		return -1.f;
	}

	return Bridge->GetTagLastChangeTime(Tag);
}

bool UGASCombatStatics::WasTagLastChangeAdded(AActor* Target, FGameplayTag Tag)
{
	if (!IsValid(Target) || !Tag.IsValid())
	{
		return false;
	}

	const UGASCombatBridgeComponent* Bridge = Target->FindComponentByClass<UGASCombatBridgeComponent>();
	if (!Bridge)
	{
		return false;
	}

	return Bridge->WasTagLastChangeAdded(Tag);
}
