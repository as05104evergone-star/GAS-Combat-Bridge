// Copyright Epic Games, Inc. All Rights Reserved.

#include "GASCombatAbilitySet.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h"
#include "GASCombatBridge.h"
#include "GASCombatStatics.h"

bool UGASCombatAbilitySet::GrantTo(AActor* Target, FGASCombatGrantedHandles& OutHandles)
{
	OutHandles = FGASCombatGrantedHandles();

	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
	if (!ASC)
	{
		UE_LOG(LogGASCombat, Warning,
			TEXT("【GAS战斗桥接】授予技能集失败：%s 身上没有技能系统组件(ASC)。"),
			*GetNameSafe(Target));
		return false;
	}

	if (!ASC->IsOwnerActorAuthoritative())
	{
		UE_LOG(LogGASCombat, Warning,
			TEXT("【GAS战斗桥接】授予技能集 %s 被忽略：当前不是服务器权威端。"),
			*GetName());
		return false;
	}

	OutHandles.AbilitySystemComponent = ASC;

	// ------------------------------------------------------------------
	// 1) 先挂属性集。
	//    这一步必须最先做：后面施加的效果可能要修改这些属性，
	//    属性还不存在的话，效果会静默失效（这是 GAS 里最隐蔽的坑之一）。
	// ------------------------------------------------------------------
	for (const TSubclassOf<UAttributeSet>& SetClass : AttributeSets)
	{
		if (!SetClass)
		{
			continue;
		}

		if (UAttributeSet* AddedSet = UGASCombatStatics::AddAttributeSet(Target, SetClass))
		{
			OutHandles.AttributeSets.Add(AddedSet);
		}
	}

	// ------------------------------------------------------------------
	// 2) 设属性初始值。放在效果之前，保证基准值是干净的。
	// ------------------------------------------------------------------
	for (const FGASCombatAttributeInitEntry& Entry : AttributeOverrides)
	{
		if (!Entry.Attribute.IsValid())
		{
			UE_LOG(LogGASCombat, Warning,
				TEXT("【GAS战斗桥接】技能集 %s：有一条属性初始值没有选择属性，已跳过。"), *GetName());
			continue;
		}

		UGASCombatStatics::SetAttributeBaseValue(Target, Entry.Attribute, Entry.BaseValue);
	}

	// ------------------------------------------------------------------
	// 3) 授予技能。
	//    用 FGameplayAbilitySpec 而不是简化接口，是为了能带上「输入标签」——
	//    这是把技能和增强输入(Enhanced Input)挂钩的正规做法。
	// ------------------------------------------------------------------
	for (const FGASCombatAbilityEntry& Entry : GrantedAbilities)
	{
		if (!Entry.Ability)
		{
			UE_LOG(LogGASCombat, Warning, TEXT("【GAS战斗桥接】技能集 %s：有一条技能条目没有选择技能类，已跳过。"), *GetName());
			continue;
		}

		const int32 SafeLevel = FMath::Max(Entry.Level, 1);

		FGameplayAbilitySpec Spec(Entry.Ability, SafeLevel, INDEX_NONE, this);

		if (Entry.InputTag.IsValid())
		{
			// 5.5 起 DynamicAbilityTags 已废弃，改用 GetDynamicSpecSourceTags()
			Spec.GetDynamicSpecSourceTags().AddTag(Entry.InputTag);
		}

		const FGameplayAbilitySpecHandle Handle = ASC->GiveAbility(Spec);
		if (!Handle.IsValid())
		{
			UE_LOG(LogGASCombat, Warning, TEXT("【GAS战斗桥接】技能集 %s：技能 %s 授予失败。"), *GetName(), *Entry.Ability->GetName());
			continue;
		}

		OutHandles.AbilityHandles.Add(Handle);

		// 被动技能：授予后立即激活一次。
		// 注意这些技能应该把「实例化策略」设为 Per Actor，并且不要在里面调用 EndAbility，
		// 否则它只会跑一帧就结束了。
		if (Entry.bActivateOnGrant)
		{
			ASC->TryActivateAbility(Handle);
		}
	}

	// ------------------------------------------------------------------
	// 4) 施加效果（常驻 Buff/Debuff）
	// ------------------------------------------------------------------
	for (const FGASCombatEffectEntry& Entry : GrantedEffects)
	{
		if (!Entry.Effect)
		{
			UE_LOG(LogGASCombat, Warning, TEXT("【GAS战斗桥接】技能集 %s：有一条效果条目没有选择效果类，已跳过。"), *GetName());
			continue;
		}

		const FActiveGameplayEffectHandle Handle = UGASCombatStatics::ApplyEffect(
			Target, Entry.Effect, FMath::Max(Entry.Level, 1), Target, Target);

		if (Handle.IsValid())
		{
			OutHandles.EffectHandles.Add(Handle);
		}
	}

	// ------------------------------------------------------------------
	// 5) 游离标签
	// ------------------------------------------------------------------
	if (!GrantedLooseTags.IsEmpty())
	{
		// 复制 = true：技能集通常在服务器上授予，客户端需要看到这些标签
		if (UAbilitySystemBlueprintLibrary::AddLooseGameplayTags(Target, GrantedLooseTags, /*bShouldReplicate=*/true))
		{
			// 记下账，回收时按这份列表原样摘掉
			GrantedLooseTags.GetGameplayTagArray(OutHandles.LooseTags);
		}
	}

	OutHandles.bIsValid = OutHandles.AbilityHandles.Num() > 0
		|| OutHandles.EffectHandles.Num() > 0
		|| OutHandles.AttributeSets.Num() > 0
		|| OutHandles.LooseTags.Num() > 0;

	UE_LOG(LogGASCombat, Log,
		TEXT("【GAS战斗桥接】技能集 %s 已授予 %s：技能 %d 个，效果 %d 个，属性集 %d 个，游离标签 %d 个。"),
		*GetName(), *GetNameSafe(Target),
		OutHandles.AbilityHandles.Num(), OutHandles.EffectHandles.Num(),
		OutHandles.AttributeSets.Num(), OutHandles.LooseTags.Num());

	return true;
}

bool UGASCombatAbilitySet::RemoveFrom(AActor* Target, FGASCombatGrantedHandles& Handles)
{
	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
	if (!ASC)
	{
		return false;
	}

	// 回收顺序与授予相反：先摘标签，再撤效果，最后移除技能与属性集。
	// 反着来的原因是：技能可能依赖属性，效果可能依赖标签，先抽掉底层会让上层报错。

	if (Handles.LooseTags.Num() > 0)
	{
		FGameplayTagContainer TagContainer;
		for (const FGameplayTag& Tag : Handles.LooseTags)
		{
			TagContainer.AddTag(Tag);
		}
		UAbilitySystemBlueprintLibrary::RemoveLooseGameplayTags(Target, TagContainer, /*bShouldReplicate=*/true);
	}

	for (const FActiveGameplayEffectHandle& Handle : Handles.EffectHandles)
	{
		if (Handle.IsValid())
		{
			// -1 = 移除全部层数
			ASC->RemoveActiveGameplayEffect(Handle, -1);
		}
	}

	for (const FGameplayAbilitySpecHandle& Handle : Handles.AbilityHandles)
	{
		if (Handle.IsValid())
		{
			ASC->ClearAbility(Handle);
		}
	}

	for (const TObjectPtr<UAttributeSet>& Set : Handles.AttributeSets)
	{
		if (Set)
		{
			ASC->RemoveSpawnedAttribute(Set);
		}
	}

	Handles = FGASCombatGrantedHandles();
	return true;
}

FString UGASCombatAbilitySet::GetSummary() const
{
	TStringBuilder<1024> Builder;
	Builder.Appendf(TEXT("技能集：%s\n"), *GetName());
	Builder.Appendf(TEXT("  技能 %d 个：\n"), GrantedAbilities.Num());
	for (const FGASCombatAbilityEntry& Entry : GrantedAbilities)
	{
		Builder.Appendf(TEXT("    - %s（等级 %d%s%s）\n"),
			Entry.Ability ? *Entry.Ability->GetName() : TEXT("(未设置)"),
			Entry.Level,
			Entry.InputTag.IsValid() ? *FString::Printf(TEXT("，输入 %s"), *Entry.InputTag.ToString()) : TEXT(""),
			Entry.bActivateOnGrant ? TEXT("，被动") : TEXT(""));
	}
	Builder.Appendf(TEXT("  效果 %d 个：\n"), GrantedEffects.Num());
	for (const FGASCombatEffectEntry& Entry : GrantedEffects)
	{
		Builder.Appendf(TEXT("    - %s（等级 %d）\n"),
			Entry.Effect ? *Entry.Effect->GetName() : TEXT("(未设置)"), Entry.Level);
	}
	Builder.Appendf(TEXT("  属性集 %d 个，属性初始值 %d 条，游离标签 %d 个\n"),
		AttributeSets.Num(), AttributeOverrides.Num(), GrantedLooseTags.Num());

	return Builder.ToString();
}
