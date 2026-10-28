// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActiveGameplayEffectHandle.h"
#include "AttributeSet.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameplayEffectTypes.h"
#include "GameplayTagContainer.h"
#include "GASCombatTypes.generated.h"

class UAbilitySystemComponent;
class UGameplayAbility;
class UGameplayEffect;
class UAttributeSet;

/**
 * 属性快照：某个属性在某一刻的「基础值」和「当前值」。
 *
 * 为什么需要两个值？
 *   基础值(BaseValue) = 永久值，只会被「瞬时(Instant)」效果改变。
 *   当前值(CurrentValue) = 基础值叠加所有「持续/无限(Duration/Infinite)」效果的实时结果。
 * 例如：生命上限 100（基础值），身上挂着一个 +20 的护盾 Buff 时，当前值显示 120。
 * 调试时如果发现「数值对不上」，先看是基础值错了还是当前值错了，能立刻定位问题在
 * 属性初始化、还是在持续效果上。
 */
USTRUCT(BlueprintType)
struct GASCOMBATBRIDGE_API FGASCombatAttributeSnapshot
{
	GENERATED_BODY()

	/** 属性句柄（可用于继续设置数值、绑监听） */
	UPROPERTY(BlueprintReadOnly, Category = "战斗|属性", meta = (DisplayName = "属性"))
	FGameplayAttribute Attribute;

	/** 属性名，例如 Health */
	UPROPERTY(BlueprintReadOnly, Category = "战斗|属性", meta = (DisplayName = "属性名"))
	FString AttributeName;

	/**
	 * 属性的中文名（插件内置映射表给出的，例如「生命」「连招段数」）。
	 * 做 UI 时优先显示它；没有对应映射的第三方属性会与「属性名」相同。
	 */
	UPROPERTY(BlueprintReadOnly, Category = "战斗|属性", meta = (DisplayName = "中文名"))
	FString DisplayName;

	/** 所属属性集类名，例如 GASCombatCoreAttributeSet */
	UPROPERTY(BlueprintReadOnly, Category = "战斗|属性", meta = (DisplayName = "所属属性集"))
	FString AttributeSetName;

	/** 基础值：不含临时增益的永久值 */
	UPROPERTY(BlueprintReadOnly, Category = "战斗|属性", meta = (DisplayName = "基础值"))
	float BaseValue = 0.f;

	/** 当前值：叠加了所有持续效果之后的最终值（UI 一般显示这个） */
	UPROPERTY(BlueprintReadOnly, Category = "战斗|属性", meta = (DisplayName = "当前值"))
	float CurrentValue = 0.f;
};

/**
 * 一条「授予技能」配置。
 *
 * 说明几个容易被误解的字段：
 *   - 等级(Level)：不是「技能等级」这个玩法概念本身，而是传给技能的一个整数。
 *     它最大的用处是配合「可缩放数值(Scalable Float)」让同一个 GameplayEffect
 *     在不同等级下给出不同数值。策划可以把它当成「技能强度档位」。
 *   - 输入标签(InputTag)：只是一个标签，用来把技能和输入绑定起来。
 *     真正的按键绑定仍然在你的角色蓝图里做（推荐用增强输入 Enhanced Input）。
 *   - 授予后立即激活(bActivateOnGrant)：对应 GAS 的「被动技能」做法。
 *     主动技能请不要勾它，否则一出生就会放技能。
 */
USTRUCT(BlueprintType)
struct GASCOMBATBRIDGE_API FGASCombatAbilityEntry
{
	GENERATED_BODY()

	/** 要授予的技能类（蓝图子类，父类为 GameplayAbility 或你项目里的技能基类） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗|技能", meta = (DisplayName = "技能类"))
	TSubclassOf<UGameplayAbility> Ability;

	/** 等级，会传给技能与效果，用于「可缩放数值」查表 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗|技能", meta = (DisplayName = "等级", ClampMin = "1"))
	int32 Level = 1;

	/** 输入标签：把技能和输入绑定用的标签，例如 Input.Combat.LightAttack */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗|技能", meta = (DisplayName = "输入标签"))
	FGameplayTag InputTag;

	/** 勾选后：授予时立刻尝试激活一次。被动技能（光环、持续回复）才勾 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗|技能", meta = (DisplayName = "授予后立即激活"))
	bool bActivateOnGrant = false;

	/** 备注：只写给策划自己看，不影响逻辑 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗|技能", meta = (DisplayName = "备注"))
	FString Note;
};

/** 一条「施加效果」配置。等级同样用于「可缩放数值」查表。 */
USTRUCT(BlueprintType)
struct GASCOMBATBRIDGE_API FGASCombatEffectEntry
{
	GENERATED_BODY()

	/** 要施加的效果类 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗|效果", meta = (DisplayName = "效果类"))
	TSubclassOf<UGameplayEffect> Effect;

	/** 效果等级 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗|效果", meta = (DisplayName = "等级", ClampMin = "1"))
	int32 Level = 1;

	/** 备注 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗|效果", meta = (DisplayName = "备注"))
	FString Note;
};

/** 一条「属性初始值」配置：把某个属性的基础值设为指定数值 */
USTRUCT(BlueprintType)
struct GASCOMBATBRIDGE_API FGASCombatAttributeInitEntry
{
	GENERATED_BODY()

	/** 目标属性 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗|属性", meta = (DisplayName = "属性"))
	FGameplayAttribute Attribute;

	/** 要设置的基础值 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗|属性", meta = (DisplayName = "基础值"))
	float BaseValue = 0.f;

	/** 备注 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗|属性", meta = (DisplayName = "备注"))
	FString Note;
};

/**
 * 一个已授予技能的只读信息（用于调试面板 / 技能栏 UI）。
 */
USTRUCT(BlueprintType)
struct GASCOMBATBRIDGE_API FGASCombatAbilityInfo
{
	GENERATED_BODY()

	/** 技能类 */
	UPROPERTY(BlueprintReadOnly, Category = "战斗|技能", meta = (DisplayName = "技能类"))
	TSubclassOf<UGameplayAbility> AbilityClass;

	/** 技能类名（好读的字符串） */
	UPROPERTY(BlueprintReadOnly, Category = "战斗|技能", meta = (DisplayName = "技能名"))
	FString AbilityName;

	/** 技能句柄 */
	UPROPERTY(BlueprintReadOnly, Category = "战斗|技能", meta = (DisplayName = "句柄"))
	FGameplayAbilitySpecHandle Handle;

	/** 等级 */
	UPROPERTY(BlueprintReadOnly, Category = "战斗|技能", meta = (DisplayName = "等级"))
	int32 Level = 1;

	/** 当前是否正在激活中 */
	UPROPERTY(BlueprintReadOnly, Category = "战斗|技能", meta = (DisplayName = "已激活"))
	bool bIsActive = false;

	/** 剩余冷却秒数。0 表示没在冷却 */
	UPROPERTY(BlueprintReadOnly, Category = "战斗|技能", meta = (DisplayName = "剩余冷却"))
	float CooldownRemaining = 0.f;

	/** 冷却总时长秒数。0 表示该技能没配冷却 */
	UPROPERTY(BlueprintReadOnly, Category = "战斗|技能", meta = (DisplayName = "冷却总时长"))
	float CooldownDuration = 0.f;

	/** 冷却进度 0~1，1 表示刚进冷却，0 表示冷却完毕。可直接喂给 UI 的进度条 */
	UPROPERTY(BlueprintReadOnly, Category = "战斗|技能", meta = (DisplayName = "冷却进度"))
	float CooldownPercent = 0.f;
};

/**
 * 一个「技能集」被授予后返回的凭据。
 *
 * 为什么需要它？
 *   授予技能/效果之后如果想撤销（换武器、洗点、角色重置），必须记住当初给了什么。
 *   这个结构体就是那本账。把它存到蓝图变量里，需要收回时调「回收技能集」。
 */
USTRUCT(BlueprintType)
struct GASCOMBATBRIDGE_API FGASCombatGrantedHandles
{
	GENERATED_BODY()

	/** 被授予的目标（技能系统组件） */
	UPROPERTY(BlueprintReadOnly, Category = "战斗|技能集", meta = (DisplayName = "目标组件"))
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent = nullptr;

	/** 已授予的技能句柄 */
	UPROPERTY(BlueprintReadOnly, Category = "战斗|技能集", meta = (DisplayName = "技能句柄"))
	TArray<FGameplayAbilitySpecHandle> AbilityHandles;

	/** 已挂上的属性集实例 */
	UPROPERTY(BlueprintReadOnly, Category = "战斗|技能集", meta = (DisplayName = "已挂属性集"))
	TArray<TObjectPtr<UAttributeSet>> AttributeSets;

	/** 已施加效果的句柄（只对持续/无限效果有意义） */
	UPROPERTY(BlueprintReadOnly, Category = "战斗|技能集", meta = (DisplayName = "效果句柄"))
	TArray<FActiveGameplayEffectHandle> EffectHandles;

	/** 已添加的游离标签（凭空挂在角色身上、不来自任何效果或技能的标签） */
	UPROPERTY(BlueprintReadOnly, Category = "战斗|技能集", meta = (DisplayName = "游离标签"))
	TArray<FGameplayTag> LooseTags;

	/** 是否有效（至少有授予过东西） */
	UPROPERTY(BlueprintReadOnly, Category = "战斗|技能集", meta = (DisplayName = "有效"))
	bool bIsValid = false;
};

/** 一条「激活中的效果」信息（用于调试面板 / Buff 图标栏） */
USTRUCT(BlueprintType)
struct GASCOMBATBRIDGE_API FGASCombatActiveEffectInfo
{
	GENERATED_BODY()

	/** 效果类 */
	UPROPERTY(BlueprintReadOnly, Category = "战斗|效果", meta = (DisplayName = "效果类"))
	TSubclassOf<UGameplayEffect> EffectClass;

	/** 效果类名 */
	UPROPERTY(BlueprintReadOnly, Category = "战斗|效果", meta = (DisplayName = "效果名"))
	FString EffectName;

	/** 效果句柄 */
	UPROPERTY(BlueprintReadOnly, Category = "战斗|效果", meta = (DisplayName = "句柄"))
	FActiveGameplayEffectHandle Handle;

	/** 当前层数 */
	UPROPERTY(BlueprintReadOnly, Category = "战斗|效果", meta = (DisplayName = "层数"))
	int32 StackCount = 1;

	/** 剩余时间（秒）。无限效果为 -1 */
	UPROPERTY(BlueprintReadOnly, Category = "战斗|效果", meta = (DisplayName = "剩余时间"))
	float RemainingTime = -1.f;

	/** 总时长（秒）。无限效果为 -1 */
	UPROPERTY(BlueprintReadOnly, Category = "战斗|效果", meta = (DisplayName = "总时长"))
	float TotalDuration = -1.f;

	/** 是否无限持续（不会被自动移除） */
	UPROPERTY(BlueprintReadOnly, Category = "战斗|效果", meta = (DisplayName = "无限持续"))
	bool bIsInfinite = false;
};

/**
 * 「属性变化」事件数据。
 * 注意 Delta 的计算方式：使用 新值 - 旧值，所以你不需要自己再记录上一帧的值。
 */
USTRUCT(BlueprintType)
struct GASCOMBATBRIDGE_API FGASCombatAttributeChangeEvent
{
	GENERATED_BODY()

	/** 变化的属性 */
	UPROPERTY(BlueprintReadOnly, Category = "战斗|属性", meta = (DisplayName = "属性"))
	FGameplayAttribute Attribute;

	/** 属性名 */
	UPROPERTY(BlueprintReadOnly, Category = "战斗|属性", meta = (DisplayName = "属性名"))
	FString AttributeName;

	/** 旧值 */
	UPROPERTY(BlueprintReadOnly, Category = "战斗|属性", meta = (DisplayName = "旧值"))
	float OldValue = 0.f;

	/** 新值 */
	UPROPERTY(BlueprintReadOnly, Category = "战斗|属性", meta = (DisplayName = "新值"))
	float NewValue = 0.f;

	/** 变化量 = 新值 - 旧值。受伤时为负数，治疗时为正数 */
	UPROPERTY(BlueprintReadOnly, Category = "战斗|属性", meta = (DisplayName = "变化量"))
	float Delta = 0.f;
};
