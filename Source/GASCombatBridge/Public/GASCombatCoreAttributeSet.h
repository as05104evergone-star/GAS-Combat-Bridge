// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
// 必须完整包含：ATTRIBUTE_ACCESSORS_BASIC 宏展开后会调用
// UAbilitySystemComponent::SetNumericAttributeBase，只有前向声明会编译失败(C2027)。
#include "AbilitySystemComponent.h"
#include "GASCombatCoreAttributeSet.generated.h"

class UAbilitySystemComponent;

/**
 * 受伤事件。
 *
 * 注意区分两个「谁」：
 *   DamageInstigator(施加者) = 该为这次伤害负责的角色（通常是攻击者的角色本体）。
 *   DamageCauser(效果来源)   = 造成伤害的物理载体（可能是飞出去的投射物、武器 Actor）。
 * 做击杀归属、仇恨、连击计数时一般用「施加者」；做击退方向、命中特效位置时用「效果来源」。
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FGASCombatOnDamageTaken,
	AActor*, DamageInstigator,
	AActor*, DamageCauser,
	float, DamageAmount,
	float, RemainingHealth);

/** 死亡事件。只在生命值从「大于0」变为「0」时广播一次，被治疗复活后再次濒死会重新广播。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGASCombatOnDeath,
	AActor*, Killer,
	AActor*, DamageCauser);

/**
 * 通用战斗属性集 —— 这是「策划不用写 C++」的关键资产。
 *
 * 为什么必须有这个东西？
 *   GAS 的属性(Attribute)本质上是一个 C++ 的 FGameplayAttributeData 成员变量，
 *   而且引擎在收集属性列表时会把「蓝图生成的类」排除掉（引擎源码里的 !Class->ClassGeneratedBy），
 *   所以在蓝图里新建一个 AttributeSet 子类、加几个变量，是做不出真正能用的属性的：
 *   它不会出现在 GameplayEffect 的属性下拉框里，也没有合法的方式初始化。
 *   整个 GAS 里唯一绕不开 C++ 的地方就是这里，所以插件直接把它做好了。
 *
 * 这套属性覆盖了动作/ARPG 类游戏的常见需求，并且内置了一条完整的伤害管线，
 * 你可以直接拿来用，也可以照着它理解 GAS 的数值流程。
 */
UCLASS(Blueprintable, meta = (DisplayName = "通用战斗属性集"))
class GASCOMBATBRIDGE_API UGASCombatCoreAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UGASCombatCoreAttributeSet();

	// ============================================================================
	//  属性定义
	// ============================================================================
	// 每个属性都成对出现：实际值 + 上限值。
	// 之所以要「上限」，是因为 GAS 不会自动帮你夹取数值，夹取逻辑写在下面 PreAttributeChange 里。

	/** 当前生命值。归零即视为死亡（由本属性集广播死亡事件） */
	UPROPERTY(BlueprintReadOnly, Category = "战斗属性|生存", ReplicatedUsing = OnRep_Health)
	FGameplayAttributeData Health;
	ATTRIBUTE_ACCESSORS_BASIC(UGASCombatCoreAttributeSet, Health)

	/** 生命值上限 */
	UPROPERTY(BlueprintReadOnly, Category = "战斗属性|生存", ReplicatedUsing = OnRep_MaxHealth)
	FGameplayAttributeData MaxHealth;
	ATTRIBUTE_ACCESSORS_BASIC(UGASCombatCoreAttributeSet, MaxHealth)

	/**
	 * 当前护盾值。伤害会先扣护盾、扣完溢出部分才进生命。
	 * 上限为 0 时就等于「没有护盾系统」，不影响其他逻辑。
	 */
	UPROPERTY(BlueprintReadOnly, Category = "战斗属性|生存", ReplicatedUsing = OnRep_Shield)
	FGameplayAttributeData Shield;
	ATTRIBUTE_ACCESSORS_BASIC(UGASCombatCoreAttributeSet, Shield)

	/** 护盾上限 */
	UPROPERTY(BlueprintReadOnly, Category = "战斗属性|生存", ReplicatedUsing = OnRep_MaxShield)
	FGameplayAttributeData MaxShield;
	ATTRIBUTE_ACCESSORS_BASIC(UGASCombatCoreAttributeSet, MaxShield)

	/** 当前法力/能量值。典型用法是当作技能的「消耗(Cost)」资源 */
	UPROPERTY(BlueprintReadOnly, Category = "战斗属性|资源", ReplicatedUsing = OnRep_Mana)
	FGameplayAttributeData Mana;
	ATTRIBUTE_ACCESSORS_BASIC(UGASCombatCoreAttributeSet, Mana)

	/** 法力值上限 */
	UPROPERTY(BlueprintReadOnly, Category = "战斗属性|资源", ReplicatedUsing = OnRep_MaxMana)
	FGameplayAttributeData MaxMana;
	ATTRIBUTE_ACCESSORS_BASIC(UGASCombatCoreAttributeSet, MaxMana)

	/** 攻击力。伤害结算时按 (攻击力 - 防御力) 之类的公式取值，具体公式由你决定 */
	UPROPERTY(BlueprintReadOnly, Category = "战斗属性|攻防", ReplicatedUsing = OnRep_AttackPower)
	FGameplayAttributeData AttackPower;
	ATTRIBUTE_ACCESSORS_BASIC(UGASCombatCoreAttributeSet, AttackPower)

	/** 防御力。可以是减法减免，也可以在「属性计算」类效果里当乘法系数 */
	UPROPERTY(BlueprintReadOnly, Category = "战斗属性|攻防", ReplicatedUsing = OnRep_Defense)
	FGameplayAttributeData Defense;
	ATTRIBUTE_ACCESSORS_BASIC(UGASCombatCoreAttributeSet, Defense)

	/**
	 * 移动速度。GAS 不会自动把它接到移动组件上，
	 * 需要在角色蓝图里读这个属性再设置 Character Movement 的 Max Walk Speed。
	 * 这么做的好处是：减速/加速类效果只要改属性，移动逻辑无需改动。
	 */
	UPROPERTY(BlueprintReadOnly, Category = "战斗属性|移动", ReplicatedUsing = OnRep_MoveSpeed)
	FGameplayAttributeData MoveSpeed;
	ATTRIBUTE_ACCESSORS_BASIC(UGASCombatCoreAttributeSet, MoveSpeed)

	// ---------------------------------------------------------------------------
	//  临时属性（Meta Attribute）
	// ---------------------------------------------------------------------------
	/**
	 * 待结算伤害。
	 *
	 * 这是 GAS 里最重要的一个设计模式，值得单独理解：
	 *   伤害不是直接去减 Health 的，而是先往这个「中转属性」上加一个数值，
	 *   然后在 PostGameplayEffectExecute 里被消费掉：扣护盾 → 扣血 → 广播事件 → 归零。
	 *
	 * 为什么要这么绕？因为这样伤害就有了一个统一的收口：
	 *   所有伤害来源（普攻、技能、DOT、陷阱、反伤）都走同一条结算路径，
	 *   你只在一个地方写「护盾优先」「暴击」「无敌判定」「伤害数字飘字」，
	 *   不用在每个技能里重复一遍。改平衡时也只需要动这一处。
	 *
	 * 它不会被保留：结算完立刻被设为 0，所以不要在 UI 上显示它。
	 */
	UPROPERTY(BlueprintReadOnly, Category = "战斗属性|临时")
	FGameplayAttributeData IncomingDamage;
	ATTRIBUTE_ACCESSORS_BASIC(UGASCombatCoreAttributeSet, IncomingDamage)

	/** 待结算治疗。与伤害同理，用于让所有治疗走统一收口（治疗加成、治疗减免都在一处处理） */
	UPROPERTY(BlueprintReadOnly, Category = "战斗属性|临时")
	FGameplayAttributeData IncomingHealing;
	ATTRIBUTE_ACCESSORS_BASIC(UGASCombatCoreAttributeSet, IncomingHealing)

	// ============================================================================
	//  事件
	// ============================================================================
	// 在蓝图里这样拿到本属性集实例来绑事件：
	//   「获取通用战斗属性集」节点 → 拖出引脚 → 绑定事件

	/** 受到伤害时广播。伤害为 0 或治疗时不会广播 */
	UPROPERTY(BlueprintAssignable, Category = "战斗|事件", meta = (DisplayName = "受到伤害时"))
	FGASCombatOnDamageTaken OnDamageTaken;

	/** 生命值归零时广播一次 */
	UPROPERTY(BlueprintAssignable, Category = "战斗|事件", meta = (DisplayName = "死亡时"))
	FGASCombatOnDeath OnDeath;

	// ============================================================================
	//  UAttributeSet 回调（GAS 的数值管线钩子）
	// ============================================================================

	/** 任何「当前值」变化前调用。这是官方推荐的数值夹取位置 */
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;

	/** 任何「基础值」变化前调用。基础值也要夹取，否则上限降低时会出现负血 */
	virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;

	/** 瞬时(Instant)效果结算完成后调用。伤害/治疗管线就挂在这里 */
	virtual void PostGameplayEffectExecute(const struct FGameplayEffectModCallbackData& Data) override;

	/** 网络复制 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ============================================================================
	//  给蓝图用的辅助查询
	// ============================================================================

	/** 当前生命是否已归零 */
	UFUNCTION(BlueprintPure, Category = "战斗|属性", meta = (DisplayName = "是否已死亡"))
	bool IsDead() const { return GetHealth() <= 0.f; }

	/** 生命百分比 0~1，可直接喂给血条 */
	UFUNCTION(BlueprintPure, Category = "战斗|属性", meta = (DisplayName = "生命百分比"))
	float GetHealthPercent() const;

protected:
	/** 把数值夹到合法区间。当前值和基础值两条路径共用 */
	void ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const;

	/** 伤害结算：护盾 → 生命 → 广播事件 */
	void HandleIncomingDamage(const struct FGameplayEffectModCallbackData& Data);

	/** 治疗结算 */
	void HandleIncomingHealing(const struct FGameplayEffectModCallbackData& Data);

	/** 死亡只广播一次，这里记录状态，被治疗拉回血量后复位 */
	bool bDeathBroadcast = false;

	// --- 复制回调 ---
	UFUNCTION()
	void OnRep_Health(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_MaxHealth(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_Shield(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_MaxShield(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_Mana(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_MaxMana(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_AttackPower(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_Defense(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_MoveSpeed(const FGameplayAttributeData& OldValue);
};
