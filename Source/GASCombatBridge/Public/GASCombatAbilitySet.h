// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "GASCombatTypes.h"
#include "GASCombatAbilitySet.generated.h"

class UAttributeSet;

/**
 * 技能集 —— 一份「角色战斗配置」打包资产。
 *
 * 【它解决什么问题】
 *   原版 GAS 要配一个角色的战斗技能，得在蓝图里连着调好几次「授予技能」「施加效果」
 *   「挂属性集」，而且顺序错了就会出各种怪问题（比如效果改不到还不存在的属性）。
 *   有了这个资产，一个角色 = 一份技能集：换武器/换职业/换难度，换一份资产就行，
 *   不需要改任何蓝图连线。这也是 Lyra 项目里策划最喜欢的做法。
 *
 * 【怎么创建】
 *   内容浏览器右键 → 杂项 → 数据资产 → 选择「技能集」。
 *
 * 【执行顺序是有讲究的，不能乱】
 *   1. 挂属性集     —— 必须先有属性，后面的效果才有地方改
 *   2. 设属性初始值 —— 在没有任何效果污染数值之前定下基准
 *   3. 授予技能     —— 只授予不激活（除了勾了「授予后立即激活」的被动技能）
 *   4. 施加效果     —— 常驻 Buff/Debuff，例如出生自带的护盾
 *   5. 加游离标签   —— 状态标记，例如「已解锁二段跳」
 */
UCLASS(BlueprintType, meta = (DisplayName = "技能集"))
class GASCOMBATBRIDGE_API UGASCombatAbilitySet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 要授予的技能列表 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "技能集",
		meta = (DisplayName = "授予的技能", TitleProperty = "Note"))
	TArray<FGASCombatAbilityEntry> GrantedAbilities;

	/** 要施加的效果列表（一般放常驻的被动增益） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "技能集",
		meta = (DisplayName = "施加的效果", TitleProperty = "Note"))
	TArray<FGASCombatEffectEntry> GrantedEffects;

	/** 要挂上的属性集。默认填插件的「通用战斗属性集」即可 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "技能集",
		meta = (DisplayName = "挂载的属性集", AllowAbstract = "false"))
	TArray<TSubclassOf<UAttributeSet>> AttributeSets;

	/**
	 * 属性初始值。
	 * 【为什么要在这里设】属性集的 C++ 默认值只是占位；同一个属性集要服务不同角色
	 * （小怪 100 血、Boss 5000 血），差异就应该靠这份列表来配，而不是改代码。
	 * 注意：这里设的是「基础值」，不是最大值——最大值是 MaxHealth 那个属性。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "技能集",
		meta = (DisplayName = "属性初始值", TitleProperty = "Note"))
	TArray<FGASCombatAttributeInitEntry> AttributeOverrides;

	/** 要添加的游离标签（不会自动消失的长期状态标记） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "技能集", meta = (DisplayName = "游离标签"))
	FGameplayTagContainer GrantedLooseTags;

	/**
	 * 把整份技能集授予目标。
	 *
	 * 【只在服务器有效】GAS 的规则：授予技能与施加效果都必须在权威端做。
	 * 【策划常问】为什么角色出生没技能？先确认这个节点是在服务器上调的，
	 *             并且已经调用过「初始化技能系统」。
	 *
	 * @param Target      目标 Actor
	 * @param OutHandles  输出凭据，存到蓝图变量里，之后可以整份回收
	 * @return 是否成功执行（部分条目失败不会中断整体，日志里会有中文说明）
	 */
	UFUNCTION(BlueprintCallable, Category = "战斗|技能集",
		meta = (DisplayName = "授予技能集", Keywords = "技能集 授予 装备 换武器 换职业 ability set grant",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	bool GrantTo(AActor* Target, FGASCombatGrantedHandles& OutHandles);

	/**
	 * 回收整份技能集：撤销当初授予的一切，让角色回到「没学过这套」的状态。
	 * 典型用法：换武器、洗点、角色死亡重置。
	 */
	UFUNCTION(BlueprintCallable, Category = "战斗|技能集",
		meta = (DisplayName = "回收技能集", Keywords = "技能集 回收 撤销 卸下 ability set remove clear"))
	static bool RemoveFrom(AActor* Target, FGASCombatGrantedHandles& Handles);

	/** 用于调试：把这份技能集的内容摘要成一段中文文本 */
	UFUNCTION(BlueprintPure, Category = "战斗|技能集", meta = (DisplayName = "技能集摘要"))
	FString GetSummary() const;
};
