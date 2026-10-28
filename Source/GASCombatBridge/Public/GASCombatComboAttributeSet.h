// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
// 必须完整包含：ATTRIBUTE_ACCESSORS_BASIC 宏展开后会调用
// UAbilitySystemComponent::SetNumericAttributeBase，只有前向声明会编译失败(C2027)。
#include "AbilitySystemComponent.h"
#include "GASCombatComboAttributeSet.generated.h"

/**
 * 连招属性集 —— 服务「多段连招」这一玩法的段数数值。
 *
 * 【为什么单独一个属性集，而不是加进「通用战斗属性集」】
 *   通用战斗属性集放的是**所有战斗玩法都要用**的数值（生命/护盾/法力/攻防/移速）。
 *   连招段数是**玩法专属**数值：平台跳跃、载具、潜行类项目根本用不到它。
 *   分开之后有三个好处：
 *     1. 用不到的工程不必挂载它，属性列表里也不会多出两个莫名其妙的属性；
 *     2. 有项目会把 Mana 当「架势值」、Shield 当「护甲值」（插件文档鼓励这种复用），
 *        把连招塞进核心集会限制这种灵活性；
 *     3. 连招玩法以后要加「是否在窗口内」「本套累计伤害」等属性时，
 *        都放进这个集里，不会污染核心集。
 *
 * 【为什么段数必须是「属性」而不是普通蓝图变量】
 *   1. **唯一写入口**：GE_ComboAdvance（Instant + Add + 调用方赋值）用同一个效果
 *      承担「起手 +1 / 推进 +1 / 打满绕回 -2 / 收尾归零 -N」四种语义。没有属性，这个效果无处可写。
 *   2. **可复制**：属性自带 ReplicatedUsing + DOREPLIFETIME，段数由服务器权威决定；
 *      蓝图变量要你自己造一套。
 *   3. **可监听**：用「创建属性监听器」把段数推给动画蓝图与 UI；蓝图变量做不到「一变就通知」。
 *   4. **可观测**：编辑器调试面板与「获取战斗状态报告」能直接显示并改段数。
 *
 * 【语义契约】（与教程口径一致，改动它等于改坏连招手感）
 *   ComboIndex    : 0 = 不在连招中；1/2/3 = 当前正播第几段
 *   MaxComboIndex : 最大段数（例如 3）。它只服务「打满绕回」的判定，**不是上界钳制**
 */
UCLASS(Blueprintable, meta = (DisplayName = "连招属性集"))
class GASCOMBATBRIDGE_API UGASCombatComboAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UGASCombatComboAttributeSet();

	/**
	 * 当前连招段数。0 = 不在连招中。
	 *
	 * 【唯一写入口】GE_ComboAdvance（Duration Policy = Instant、Modifiers 一条 Add、
	 * 数值类型 = SetByCaller、键 = Data.Combo.Advance）。
	 * 用「施加效果（动态数值）」节点传 {Data.Combo.Advance : 增量} 即可推进段数。
	 *
	 * 【不要用「设置属性基础值」在正式逻辑里改它】那会绕开效果管线、不触发属性变化委托、
	 * 也不会被复制。它只在调试面板里改数值时使用。
	 */
	UPROPERTY(BlueprintReadOnly, Category = "战斗属性|连招", ReplicatedUsing = OnRep_ComboIndex)
	FGameplayAttributeData ComboIndex;
	ATTRIBUTE_ACCESSORS_BASIC(UGASCombatComboAttributeSet, ComboIndex)

	/**
	 * 最大段数（例如三段连招填 3）。
	 *
	 * 【它只用来判断「打满了没有」】不做上界钳制 —— 见下面 ClampComboAttribute 的长注释。
	 * 【初始值】默认 3。正式项目建议在「技能集」资产的「属性初始值」里配，
	 * 这样不同角色/不同武器可以有不同段数，而不用改 C++ 默认值。
	 */
	UPROPERTY(BlueprintReadOnly, Category = "战斗属性|连招", ReplicatedUsing = OnRep_MaxComboIndex)
	FGameplayAttributeData MaxComboIndex;
	ATTRIBUTE_ACCESSORS_BASIC(UGASCombatComboAttributeSet, MaxComboIndex)

	/** 任何「当前值」变化前调用 */
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;

	/** 任何「基础值」变化前调用 */
	virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;

	/** 网络复制 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	/** 当前值与基础值两条路径共用的钳制 */
	void ClampComboAttribute(const FGameplayAttribute& Attribute, float& NewValue) const;

	// --- 复制回调 ---
	UFUNCTION()
	void OnRep_ComboIndex(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_MaxComboIndex(const FGameplayAttributeData& OldValue);
};
