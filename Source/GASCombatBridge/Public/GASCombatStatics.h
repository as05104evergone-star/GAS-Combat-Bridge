// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "GameplayEffectTypes.h"
#include "GameplayTagContainer.h"
#include "GASCombatTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GASCombatStatics.generated.h"

class UAbilitySystemComponent;
class UGASCombatComboAttributeSet;
class UGASCombatCoreAttributeSet;
class UGameplayAbility;
class UGameplayEffect;
class UAttributeSet;
struct FGameplayEventData;

/**
 * GAS 战斗桥接 —— 给战斗策划用的蓝图节点库。
 *
 * 设计原则（不是简单的「把 C++ 函数翻译一遍」）：
 *
 * 1. **输入用 Actor，不用 ASC。** 引擎原生节点大多要你先拿到「技能系统组件」再操作。
 *    对策划来说每次都要多连一根线、还要处理「拿到了空组件」的情况。
 *    这里所有节点的目标参数都直接收 Actor，内部自动解析组件；组件缺失会在日志里
 *    用中文说清楚下一步该做什么，而不是静默失败。
 *
 * 2. **内置防呆与中文报错。** GAS 最大的问题是「什么都没发生，也没有任何提示」。
 *    这里每个会失败的节点都会输出可读原因。
 *
 * 3. **术语按真实语义命名。** 例如 CommitAbility 叫「结算消耗与冷却」而不是「提交技能」，
 *    GameplayEffect 叫「效果」而不是「特效」，Magnitude 叫「数值」而不是「幅度」。
 */
UCLASS(meta = (ScriptName = "GASCombat"))
class GASCOMBATBRIDGE_API UGASCombatStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ============================================================================
	//  初始化 —— 这里解决「纯蓝图工程怎么把 GAS 跑起来」
	// ============================================================================

	/**
	 * 取目标身上的技能系统组件（ASC）。
	 * 找不到就返回空。这只是查询，不会创建任何东西。
	 */
	UFUNCTION(BlueprintPure, Category = "战斗|初始化",
		meta = (DisplayName = "获取技能系统组件（ASC）", Keywords = "ASC 技能系统组件 获取 get ability system component",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static UAbilitySystemComponent* GetAbilitySystemComponent(AActor* Target);

	/**
	 * 取技能系统组件，没有就当场创建一个并挂到目标身上。
	 *
	 * 【什么时候用】纯蓝图工程里不想写 C++，又需要一个 ASC。
	 * 【创建时机】必须在游戏开始后（BeginPlay 之后）调用，不能在构造函数里调。
	 * 【多人注意】运行时创建的组件默认不复制。做联机项目时建议改成在蓝图 Actor 上
	 *             预先用「添加组件」加上技能系统组件（5.7 完全可以这么做），
	 *             再让本节点去取它。
	 */
	UFUNCTION(BlueprintCallable, Category = "战斗|初始化",
		meta = (DisplayName = "获取或创建技能系统组件", Keywords = "创建 ASC 初始化 create ability system component",
			DefaultToSelf = "Target", AdvancedDisplay = "bReplicate"))
	static UAbilitySystemComponent* GetOrCreateAbilitySystemComponent(AActor* Target, bool bReplicate = false);

	/**
	 * 初始化技能系统（最关键的一步，漏掉它会「技能激活不了」「属性读不到」）。
	 *
	 * 【它到底做了什么】GAS 需要知道两件事：这个组件「归谁」（拥有者）、
	 * 以及它「作用在谁身上」（化身）。单机游戏里这两个通常都是同一个 Actor。
	 * 如果不调用它，技能会报 "Can't activate ... when not local"，
	 * 属性变化也不会生效。
	 *
	 * 【什么时候调用】角色 BeginPlay 时调用一次。
	 * 【多人注意】服务器在 PossessedBy 时、客户端在 OnRep_PlayerState / AcknowledgePossession
	 *             时各要调用一次，否则客户端上技能放不出来。
	 *             本节点是幂等的，重复调用没有坏处。
	 *
	 * @param Target         目标 Actor（拥有者）
	 * @param AvatarActor    化身 Actor。留空表示就是 Target 自己
	 * @param bForceRefresh  勾上则强制重新绑定（角色重生、换 Pawn 时用）
	 */
	UFUNCTION(BlueprintCallable, Category = "战斗|初始化",
		meta = (DisplayName = "初始化技能系统", Keywords = "初始化 挂载 init ability actor info 化神",
			DefaultToSelf = "Target", AdvancedDisplay = "AvatarActor,bForceRefresh"))
	static bool InitAbilitySystem(AActor* Target, AActor* AvatarActor = nullptr, bool bForceRefresh = false);

	/**
	 * 技能系统是否已经初始化过。
	 * 拿到组件却不能用，十有八九是这个返回了 false。
	 */
	UFUNCTION(BlueprintPure, Category = "战斗|初始化",
		meta = (DisplayName = "技能系统是否已初始化", Keywords = "初始化 检查 is initialized",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static bool IsAbilitySystemInitialized(AActor* Target);

	/**
	 * 取本插件内置的「通用战斗属性集」实例。
	 * 拿到它就能绑「受到伤害时」「死亡时」这些事件，也能直接读生命/护盾等数值。
	 */
	UFUNCTION(BlueprintPure, Category = "战斗|初始化",
		meta = (DisplayName = "获取通用战斗属性集", Keywords = "属性集 生命 血量 attribute set health",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static UGASCombatCoreAttributeSet* GetCombatAttributeSet(AActor* Target);

	/**
	 * 取本插件内置的「连招属性集」实例。
	 *
	 * 【前提】目标身上必须挂过这个属性集 —— 在桥接组件的「挂载的属性集」里加上
	 *   「连招属性集」，或者在「技能集」资产的「挂载的属性集」列表里加。
	 *   没挂的话这里返回空（这不是错误，只是说明这个角色不参与连招玩法）。
	 *
	 * 【拿到它能做什么】
	 *   1. 读当前段数：用它拖出「连招段数」属性 → 接「取属性当前值」；
	 *   2. 绑段数变化：用它拖出属性 → 接「创建属性监听器」，段数一变就推给动画蓝图/UI；
	 *   3. 调试改段数：接「设置属性基础值」直接改（注意这只用于调试，
	 *      正式推进段数请用「施加效果（动态数值）」配 GE_ComboAdvance）。
	 */
	UFUNCTION(BlueprintPure, Category = "战斗|初始化",
		meta = (DisplayName = "获取连招属性集", Keywords = "连招 段数 属性集 combo index attribute set 第几段",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static UGASCombatComboAttributeSet* GetComboAttributeSet(AActor* Target);

	/**
	 * 给目标挂上一个属性集（在运行时创建并注册）。
	 *
	 * 【为什么需要】GAS 的属性必须在 C++ 里定义（蓝图定义的属性不会被引擎识别），
	 * 所以这一步通常用来挂本插件内置的通用战斗属性集，
	 * 或者你项目里其他 C++ 属性集。同一个属性集类只会挂一份。
	 *
	 * @return 挂上的属性集实例；失败返回空（日志里会说明原因）
	 */
	UFUNCTION(BlueprintCallable, Category = "战斗|初始化",
		meta = (DisplayName = "添加属性集", Keywords = "属性集 注册 add attribute set",
			DefaultToSelf = "Target", AdvancedDisplay = "Target,AttributeSetClass"))
	static UAttributeSet* AddAttributeSet(AActor* Target, TSubclassOf<UAttributeSet> AttributeSetClass);

	// ============================================================================
	//  属性 —— 读、写、按名字查
	// ============================================================================

	/**
	 * 取属性当前值（叠加了所有增益/减益之后的最终值）。UI 显示的一般是它。
	 * 注意：这个值不含「预测」结果，联机时客户端可能短暂不准。
	 *
	 * 【为什么有「是否找到」这个输出引脚】
	 *   拿不到属性时引擎会安静地给你 0，你根本分不清「真的是 0」还是「属性压根不存在」。
	 *   所以这里把判定结果单独给出来，并且为 false 时在输出日志里说明原因。
	 *   **返回值不会被伪造成 0 来掩盖问题**——Found 为 false 时请把它当无效值，不要直接拿去算伤害。
	 */
	UFUNCTION(BlueprintPure, Category = "战斗|属性",
		meta = (DisplayName = "取属性当前值", Keywords = "属性 当前值 血量 数值 health current value",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static float GetAttributeCurrentValue(AActor* Target, FGameplayAttribute Attribute, bool& bSuccessfullyFoundAttribute);

	/**
	 * 取属性基础值（不含临时增益的永久值）。
	 * 【策划常问】「基础值不是最大值！」生命上限是另一个属性(MaxHealth)，
	 * 基础值只是「没有 Buff 时它本来是多少」。
	 */
	UFUNCTION(BlueprintPure, Category = "战斗|属性",
		meta = (DisplayName = "取属性基础值", Keywords = "属性 基础值 base value",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static float GetAttributeBaseValue(AActor* Target, FGameplayAttribute Attribute, bool& bSuccessfullyFoundAttribute);

	/**
	 * 直接设置属性基础值。
	 *
	 * 【重要】引擎没有把这个能力开放给蓝图——蓝图原生只能读属性、不能写。
	 *         所以想做「出生时按等级给血量」「调试时一键满血」，就必须用这个节点。
	 *
	 * 【策划常问】它和「施加效果」有什么区别？
	 *   本节点改的是基础值，是永久改动，不走 GameplayEffect 管线，
	 *   因此不会触发「受到伤害/受到治疗」事件，也不会被免疫、减伤之类规则拦截。
	 *   适合初始化与调试；正式的战斗数值变动请用「施加效果」。
	 *
	 * @return 找到并设置成功返回 true
	 */
	UFUNCTION(BlueprintCallable, Category = "战斗|属性",
		meta = (DisplayName = "设置属性基础值", Keywords = "属性 设置 修改 初始化 set base value 满血",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static bool SetAttributeBaseValue(AActor* Target, FGameplayAttribute Attribute, float NewBaseValue);

	/**
	 * 按「属性集类 + 属性名」拼出一个属性句柄。
	 *
	 * 【为什么需要】属性句柄(FGameplayAttribute)在编辑器里只能从下拉框选，
	 * 而引擎的下拉框**不会列出蓝图定义的属性集**。
	 * 有了这个节点，就能用字符串（例如 "Health"）在运行时定位属性，
	 * 方便做配置表驱动的逻辑。
	 *
	 * @param AttributeSetClass 属性集类，例如通用战斗属性集
	 * @param AttributeName     属性名，例如 Health（不要带引号，注意大小写）
	 * @return 属性句柄。属性名写错时返回无效句柄，可用「属性是否有效」检查
	 */
	UFUNCTION(BlueprintPure, Category = "战斗|属性",
		meta = (DisplayName = "按名字取属性", Keywords = "属性 名字 字符串 查找 attribute by name 反射"))
	static FGameplayAttribute MakeAttributeFromName(TSubclassOf<UAttributeSet> AttributeSetClass, FName AttributeName);

	/** 属性句柄是否有效（用来检查名字有没有写错） */
	UFUNCTION(BlueprintPure, Category = "战斗|属性",
		meta = (DisplayName = "属性是否有效", Keywords = "属性 校验 有效 is valid"))
	static bool IsAttributeValid(FGameplayAttribute Attribute);

	/** 按名字取属性当前值（MakeAttributeFromName + GetAttributeCurrentValue 的合并版，少连一根线） */
	UFUNCTION(BlueprintPure, Category = "战斗|属性",
		meta = (DisplayName = "按名字取属性当前值", Keywords = "属性 名字 当前值 get by name",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static float GetAttributeCurrentValueByName(AActor* Target, TSubclassOf<UAttributeSet> AttributeSetClass, FName AttributeName);

	/** 按名字设置属性基础值 */
	UFUNCTION(BlueprintCallable, Category = "战斗|属性",
		meta = (DisplayName = "按名字设置属性基础值", Keywords = "属性 名字 设置 set by name",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static bool SetAttributeBaseValueByName(AActor* Target, TSubclassOf<UAttributeSet> AttributeSetClass, FName AttributeName, float NewBaseValue);

	/**
	 * 采集目标身上所有属性的快照（当前值 + 基础值）。
	 * 做 Buff 面板、做调试打印、排查「数值为什么不对」都靠它。
	 */
	UFUNCTION(BlueprintCallable, Category = "战斗|属性",
		meta = (DisplayName = "采集全部属性", Keywords = "属性 列表 快照 调试 dump attributes",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static TArray<FGASCombatAttributeSnapshot> CaptureAllAttributes(AActor* Target);

	// ============================================================================
	//  技能（GameplayAbility）
	// ============================================================================

	/**
	 * 把一个技能授予目标（学会这个技能）。
	 *
	 * 【授予 ≠ 激活】授予只是「把它放进技能栏」，不会自动释放。
	 * 【只在服务器有效】联机时客户端调用会被忽略——这是 GAS 的规则，不是 bug。
	 * 【策划常问】为什么给了技能却按不出来？依次检查：
	 *   1) 是否调用了「初始化技能系统」
	 *   2) 技能资产里的「激活所需标签 / 激活排除标签」是否满足
	 *   3) 消耗够不够（蓝不够会静默失败）
	 *   4) 是否在冷却中
	 *   用「打印战斗信息」能看到当前状态。
	 *
	 * @param Level 技能等级，会传给效果用于「可缩放数值」查表；填 1 即可
	 */
	UFUNCTION(BlueprintCallable, Category = "战斗|技能",
		meta = (DisplayName = "授予技能", Keywords = "技能 授予 学习 添加 give ability grant",
			DefaultToSelf = "Target", AdvancedDisplay = "Target,Level"))
	static FGameplayAbilitySpecHandle GrantAbility(AActor* Target, TSubclassOf<UGameplayAbility> AbilityClass, int32 Level = 1);

	/** 移除一个已授予的技能（用授予时返回的句柄） */
	UFUNCTION(BlueprintCallable, Category = "战斗|技能",
		meta = (DisplayName = "移除技能", Keywords = "技能 移除 遗忘 remove ability revoke",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static bool RemoveAbility(AActor* Target, FGameplayAbilitySpecHandle Handle);

	/** 目标是否已学会某个技能 */
	UFUNCTION(BlueprintCallable, Category = "战斗|技能",
		meta = (DisplayName = "是否拥有技能", Keywords = "技能 拥有 检查 has ability",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static bool HasAbility(AActor* Target, TSubclassOf<UGameplayAbility> AbilityClass);

	/**
	 * 尝试激活（释放）技能。
	 *
	 * 【返回值怎么理解】返回 true 只代表「通过了初步检查并开始激活」，
	 * 技能仍可能在之后失败。返回 false 说明连检查都没过
	 * （标签不满足 / 蓝不够 / 冷却中 / 网络权限）。
	 * 想知道具体原因，用「打印战斗信息」或控制台 showdebug abilitysystem。
	 */
	UFUNCTION(BlueprintCallable, Category = "战斗|技能",
		meta = (DisplayName = "尝试激活技能", Keywords = "技能 激活 释放 施放 放技能 try activate cast",
			DefaultToSelf = "Target", AdvancedDisplay = "Target,bAllowRemoteActivation"))
	static bool TryActivateAbility(AActor* Target, TSubclassOf<UGameplayAbility> AbilityClass, bool bAllowRemoteActivation = true);

	/** 按标签批量激活技能（例如一次触发所有带 Ability.Combo.Next 的技能） */
	UFUNCTION(BlueprintCallable, Category = "战斗|技能",
		meta = (DisplayName = "按标签激活技能", Keywords = "技能 标签 激活 批量 try activate by tag",
			DefaultToSelf = "Target", AdvancedDisplay = "Target,bAllowRemoteActivation"))
	static bool TryActivateAbilitiesByTag(AActor* Target, FGameplayTagContainer Tags, bool bAllowRemoteActivation = true);

	/**
	 * 取消技能（自己把自己停下来）。
	 * 【和「打断带标签的技能」的区别】这个是「我不想放了」，那个是「我要掐掉别人」。
	 */
	UFUNCTION(BlueprintCallable, Category = "战斗|技能",
		meta = (DisplayName = "取消技能", Keywords = "技能 取消 停止 cancel ability",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static bool CancelAbility(AActor* Target, TSubclassOf<UGameplayAbility> AbilityClass);

	/**
	 * 打断所有带指定标签的技能（掐别人的技能）。
	 * 典型用法：受击硬直时打断所有带 Ability.Attack 的技能。
	 * 【策划常问】和技能资产里的「阻止他人技能」有什么区别？
	 *   「阻止」只是不许新开，已经在跑的技能不受影响；
	 *   「打断」是立刻掐掉正在跑的。
	 * @return 实际被打断的技能数量
	 */
	UFUNCTION(BlueprintCallable, Category = "战斗|技能",
		meta = (DisplayName = "打断带标签的技能", Keywords = "技能 打断 中断 硬直 cancel abilities with tag 打断他人",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static int32 CancelAbilitiesWithTags(AActor* Target, FGameplayTagContainer Tags);

	/**
	 * 取消目标身上**所有**正在运行的技能。
	 *
	 * 【和「打断带标签的技能」的区别 —— 这条很重要，别用错】
	 *   本节点是「一刀切」，不挑技能。适用场景：死亡收尾、关卡重置、复活前清理、过场演出。
	 *   正常玩法逻辑（受击打断攻击、闪避取消攻击）请用「打断带标签的技能」做精确打击。
	 *
	 * 【为什么需要它】没有这个节点的话，死亡收尾只能写成
	 *   「打断带标签的技能」+ 手工枚举 [Ability.Attack, Ability.HitReact, Ability.Dodge]……
	 *   以后每加一个新技能族（格挡、蓄力、处决）都要回来补一个标签，
	 *   这正是 GAS 想消灭的「每加一个动作就要改多处」。
	 *
	 * @return 实际被取消的技能数量（没有技能在跑时返回 0，这是正常情况不是错误）
	 */
	UFUNCTION(BlueprintCallable, Category = "战斗|技能",
		meta = (DisplayName = "取消所有技能", Keywords = "技能 全部取消 收尾 死亡 重置 cancel all abilities",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static int32 CancelAllAbilities(AActor* Target);

	/**
	 * 取某个已授予技能身上的**动态标签**。
	 *
	 * 【动态标签是什么】GAS 的规范做法是把「当前是第几段」挂成**技能实例(Spec)的**动态标签
	 *   （例如 Ability.Attack.A / B / C），语义是「这个技能实例现在是第几段」，
	 *   而不是「这个角色带着第几段的标记」。UI 与状态树可以直接查标签就知道第几段。
	 * 【为什么需要这个节点】引擎里动态标签的读写接口（5.5+ 的 GetDynamicSpecSourceTags /
	 *   5.4 的 DynamicAbilityTags）都不是 BlueprintCallable，纯蓝图拿不到。
	 * 【和普通标签的区别】动态标签只存在于技能实例上，不会出现在
	 *   「获取全部标签」的结果里，也不影响技能的 Ability Tags（识别标签）。
	 */
	UFUNCTION(BlueprintCallable, Category = "战斗|技能",
		meta = (DisplayName = "取技能的动态标签", Keywords = "技能 动态标签 实例 第几段 spec dynamic tags",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static FGameplayTagContainer GetAbilitySpecDynamicTags(AActor* Target, FGameplayAbilitySpecHandle Handle);

	/**
	 * 设置某个已授予技能身上的动态标签。
	 *
	 * 【重要】这里改的只是「这个技能实例的标记」，**不会**影响技能的 Ability Tags，
	 *   所以按标签激活 / 打断仍然按原来的技能族标签工作。
	 *
	 * @param bAppend false（默认）= 先清空再写入（推荐：每段开始前重写一次，语义干净）；
	 *                true = 追加，适合「同时带多个窗口标记」
	 */
	UFUNCTION(BlueprintCallable, Category = "战斗|技能",
		meta = (DisplayName = "设置技能的动态标签", Keywords = "技能 动态标签 设置 第几段 spec dynamic tags",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static bool SetAbilitySpecDynamicTags(AActor* Target, FGameplayAbilitySpecHandle Handle,
		FGameplayTagContainer Tags, bool bAppend = false);

	/**
	 * 结算技能的消耗与冷却。
	 *
	 * 【什么时候调】技能蓝图里「确认这次真的要放出去了」的那一刻——通常是蒙太奇
	 *   已经确定能播、前置判定都过了之后。提前调会出现「技能被打断但蓝已经扣了」。
	 * 【它做了什么】扣消耗（Cost效果）+ 开始冷却（Cooldown效果）+ 广播提交事件，三件事一起做。
	 * 【失败怎么办】返回 false 表示消耗不够或正在冷却。具体原因看桥接组件的
	 *   「技能激活失败时」事件——它会给出中文说明。
	 */
	UFUNCTION(BlueprintCallable, Category = "战斗|技能",
		meta = (DisplayName = "结算消耗与冷却", Keywords = "消耗 冷却 提交 commit ability cost cooldown",
			DefaultToSelf = "Ability"))
	static bool CommitAbilityFor(UGameplayAbility* Ability);

	/**
	 * 结束技能。
	 *
	 * @param bWasCancelled false（默认）= 正常播完；true = 这次结束属于「被打断 / 被取消」。
	 *   这个值会一路传到桥接组件的「技能结束时」事件上，用来区分
	 *   「打完了」和「被打断了」（后者是「攻击中卡住」这类连招 bug 的头号线索）。
	 *   ★ 引擎原生节点做不到这件事：它写死了 false，所以技能蓝图无法主动上报「我被中断了」。
	 *   ★ true 走的是引擎的「取消」路径，所以会尊重技能的「可被取消」设置——
	 *     如果技能自己声明了不可取消，这个调用不会生效（这是引擎的既定规则，不是 bug）。
	 */
	UFUNCTION(BlueprintCallable, Category = "战斗|技能",
		meta = (DisplayName = "结束技能", Keywords = "结束 停止 完成 end ability 打断 取消",
			DefaultToSelf = "Ability"))
	static void EndAbilityFor(UGameplayAbility* Ability, bool bWasCancelled = false);

	/** 某个技能当前是否正在运行中 */
	UFUNCTION(BlueprintCallable, Category = "战斗|技能",
		meta = (DisplayName = "技能是否在运行", Keywords = "技能 激活中 运行 is active",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static bool IsAbilityActive(AActor* Target, TSubclassOf<UGameplayAbility> AbilityClass);

	/**
	 * 列出目标身上所有已授予的技能，附带等级、是否运行中、冷却进度。
	 * 做技能栏、做调试面板直接用这个。
	 */
	UFUNCTION(BlueprintCallable, Category = "战斗|技能",
		meta = (DisplayName = "获取已授予技能列表", Keywords = "技能 列表 技能栏 dump abilities",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static TArray<FGASCombatAbilityInfo> GetGrantedAbilities(AActor* Target);

	/**
	 * 查询某个技能的剩余冷却秒数。0 表示没在冷却。
	 *
	 * 【为什么需要】引擎只在技能**内部**提供冷却查询，
	 * 从外部（比如 UI）拿着技能类去问「还有几秒」，蓝图是做不到的。
	 */
	UFUNCTION(BlueprintCallable, Category = "战斗|技能",
		meta = (DisplayName = "技能剩余冷却", Keywords = "技能 冷却 CD 剩余时间 cooldown remaining",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static float GetAbilityCooldownRemaining(AActor* Target, TSubclassOf<UGameplayAbility> AbilityClass);

	/**
	 * 冷却进度 0~1：1 表示刚进冷却，0 表示已冷却完毕。
	 * 直接接到 UI 进度条的 Percent 上即可，不需要自己算倒数。
	 */
	UFUNCTION(BlueprintCallable, Category = "战斗|技能",
		meta = (DisplayName = "技能冷却进度", Keywords = "技能 冷却 进度条 遮罩 cooldown percent UI",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static float GetAbilityCooldownPercent(AActor* Target, TSubclassOf<UGameplayAbility> AbilityClass);

	// ============================================================================
	//  效果（GameplayEffect）
	// ============================================================================

	/**
	 * 对目标施加一个效果。
	 *
	 * 【效果不是特效】GameplayEffect 里只有数值和标签，没有任何粒子音效。
	 *   飘血、震屏、音效属于「表现(GameplayCue)」，请用「播放表现」节点。
	 *
	 * 【施加者/效果施加者怎么填】
	 *   Instigator(发起者)   = 该为这次伤害负责的角色（击杀归属看它）
	 *   EffectCauser(效果施加者) = 造成伤害的物体（武器/投射物/陷阱）
	 *   普攻场景两者填同一个角色即可；投射物场景 EffectCauser 填投射物 Actor。
	 *
	 * @return 效果句柄。瞬时效果结算完即失效；持续/无限效果可用句柄移除
	 */
	UFUNCTION(BlueprintCallable, Category = "战斗|效果",
		meta = (DisplayName = "施加效果", Keywords = "效果 buff debuff 伤害 施加 apply gameplay effect",
			DefaultToSelf = "Target", AdvancedDisplay = "Target,Level,Instigator,EffectCauser"))
	static FActiveGameplayEffectHandle ApplyEffect(AActor* Target, TSubclassOf<UGameplayEffect> EffectClass,
		int32 Level = 1, AActor* Instigator = nullptr, AActor* EffectCauser = nullptr);

	/**
	 * 施加效果，并动态传入数值（调用方赋值 / SetByCaller）。
	 *
	 * 【什么时候用】数值在运行时才知道的情况：蓄力伤害、按攻击力算出的伤害、
	 *   技能等级决定的减速幅度。
	 * 【怎么配】效果资产里把修饰符的数值类型选成「SetByCaller」，
	 *   然后填一个标签（例如 Data.Damage），这里的 Map 用同一个标签当键。
	 * 【常见坑】如果效果里用了 SetByCaller 但这里没传对应标签，
	 *   引擎会报错并当作 0 处理——做「除法」修饰符时会直接算出 0 或无穷大。
	 *
	 * @param SetByCallerValues 键=标签，值=数值
	 */
	UFUNCTION(BlueprintCallable, Category = "战斗|效果",
		meta = (DisplayName = "施加效果（动态数值）", Keywords = "效果 伤害 数值 SetByCaller 蓄力 apply effect magnitude",
			DefaultToSelf = "Target", AdvancedDisplay = "Target,Level,Instigator,EffectCauser"))
	static FActiveGameplayEffectHandle ApplyEffectWithValues(AActor* Target, TSubclassOf<UGameplayEffect> EffectClass,
		const TMap<FGameplayTag, float>& SetByCallerValues, int32 Level = 1, AActor* Instigator = nullptr, AActor* EffectCauser = nullptr);

	/**
	 * 让两个角色之间发生一次伤害（最常用的组合拳）。
	 * 内部 = 用来源当发起者，把伤害数值作为「调用方赋值」塞进效果，再施加给目标。
	 *
	 * 【前提】伤害效果必须往「待结算伤害(IncomingDamage)」这个临时属性上加值，
	 *   而且修饰符的数值类型要选「SetByCaller」，键填 Data.Damage
	 *   （本插件已在配置里注册好这个标签，直接用即可）。
	 *   通用战斗属性集内置了后续管线：护盾先扛 → 溢出扣血 → 广播事件 → 死亡判定。
	 *
	 * @param DamageAmount 这次伤害的数值。策划调平衡时改的就是它
	 */
	UFUNCTION(BlueprintCallable, Category = "战斗|效果",
		meta = (DisplayName = "对目标造成伤害", Keywords = "伤害 攻击 打 扣血 damage deal",
			DefaultToSelf = "Target", AdvancedDisplay = "Target,Level,EffectCauser"))
	static FActiveGameplayEffectHandle ApplyDamageToTarget(AActor* Source, AActor* Target,
		TSubclassOf<UGameplayEffect> DamageEffectClass, float DamageAmount, int32 Level = 1, AActor* EffectCauser = nullptr);

	/** 移除一个激活中的效果（用句柄） */
	UFUNCTION(BlueprintCallable, Category = "战斗|效果",
		meta = (DisplayName = "移除效果", Keywords = "效果 移除 驱散 remove gameplay effect",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static bool RemoveEffect(AActor* Target, FActiveGameplayEffectHandle Handle);

	/**
	 * 移除所有「授予了指定标签」的效果——这就是「驱散」。
	 * 例：移除身上所有带 Debuff.Burn 标签的效果 = 灭火。
	 * @return 被移除的效果数量
	 */
	UFUNCTION(BlueprintCallable, Category = "战斗|效果",
		meta = (DisplayName = "按标签移除效果（驱散）", Keywords = "效果 移除 驱散 净化 dispel cleanse remove by tag",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static int32 RemoveEffectsWithGrantedTags(AActor* Target, FGameplayTagContainer Tags);

	/** 某个效果当前叠了几层。0 表示身上没有这个效果（这就是做「弹药/次数」的正规方式） */
	UFUNCTION(BlueprintCallable, Category = "战斗|效果",
		meta = (DisplayName = "查询效果层数", Keywords = "效果 层数 叠层 弹药 次数 stack count",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static int32 GetEffectStackCount(AActor* Target, TSubclassOf<UGameplayEffect> EffectClass);

	/** 目标身上是否有某个效果 */
	UFUNCTION(BlueprintCallable, Category = "战斗|效果",
		meta = (DisplayName = "是否拥有效果", Keywords = "效果 检查 拥有 has effect",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static bool HasEffect(AActor* Target, TSubclassOf<UGameplayEffect> EffectClass);

	/** 列出所有激活中的效果，附带层数与剩余时间。做 Buff 图标栏用这个 */
	UFUNCTION(BlueprintCallable, Category = "战斗|效果",
		meta = (DisplayName = "获取激活中效果列表", Keywords = "效果 列表 buff栏 dump effects",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static TArray<FGASCombatActiveEffectInfo> GetActiveEffectInfos(AActor* Target);

	/** 某个效果的剩余时间（秒）。无限持续的效果返回 -1 */
	UFUNCTION(BlueprintCallable, Category = "战斗|效果",
		meta = (DisplayName = "效果剩余时间", Keywords = "效果 剩余时间 倒计时 remaining duration",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static float GetEffectRemainingTime(AActor* Target, FActiveGameplayEffectHandle Handle);

	// ============================================================================
	//  标签（GameplayTag）—— 战斗状态的判定条件
	// ============================================================================

	/** 目标是否拥有某个标签（含父标签匹配：拥有 A.B 时问 A 也会返回 true） */
	UFUNCTION(BlueprintPure, Category = "战斗|标签",
		meta = (DisplayName = "拥有标签", Keywords = "标签 判定 has tag 状态",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static bool HasTag(AActor* Target, FGameplayTag Tag);

	/** 拥有其中任意一个标签 */
	UFUNCTION(BlueprintPure, Category = "战斗|标签",
		meta = (DisplayName = "拥有任意标签", Keywords = "标签 判定 任意 has any tag",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static bool HasAnyTags(AActor* Target, FGameplayTagContainer Tags);

	/** 拥有全部标签 */
	UFUNCTION(BlueprintPure, Category = "战斗|标签",
		meta = (DisplayName = "拥有全部标签", Keywords = "标签 判定 全部 has all tags",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static bool HasAllTags(AActor* Target, FGameplayTagContainer Tags);

	/** 取目标身上所有标签 */
	UFUNCTION(BlueprintPure, Category = "战斗|标签",
		meta = (DisplayName = "获取全部标签", Keywords = "标签 列表 全部 get owned tags dump",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static FGameplayTagContainer GetAllTags(AActor* Target);

	/** 某个标签当前的层数（同一标签可以被叠加多次，例如「易伤 3 层」） */
	UFUNCTION(BlueprintPure, Category = "战斗|标签",
		meta = (DisplayName = "标签层数", Keywords = "标签 层数 计数 tag count",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static int32 GetTagCount(AActor* Target, FGameplayTag Tag);

	/**
	 * 添加游离标签（凭空挂在身上、不来自任何效果或技能的标签）。
	 *
	 * 【策划常问】为什么不用「施加效果」加标签？
	 *   效果加的标签在效果结束时会自动摘掉，适合「Buff 期间免疫硬直」。
	 *   游离标签不会自动消失，适合「剧情状态」「关卡开关」这类长期标记。
	 * 【多人注意】默认不复制。联机时勾上「是否复制」，否则服务器加了客户端看不到。
	 */
	UFUNCTION(BlueprintCallable, Category = "战斗|标签",
		meta = (DisplayName = "添加游离标签", Keywords = "标签 添加 游离 loose tag add",
			DefaultToSelf = "Target", AdvancedDisplay = "Target,bReplicate"))
	static bool AddLooseTags(AActor* Target, FGameplayTagContainer Tags, bool bReplicate = false);

	/** 移除游离标签 */
	UFUNCTION(BlueprintCallable, Category = "战斗|标签",
		meta = (DisplayName = "移除游离标签", Keywords = "标签 移除 游离 loose tag remove",
			DefaultToSelf = "Target", AdvancedDisplay = "Target,bReplicate"))
	static bool RemoveLooseTags(AActor* Target, FGameplayTagContainer Tags, bool bReplicate = false);

	// ============================================================================
	//  表现与事件
	// ============================================================================

	/**
	 * 播放一次性表现（GameplayCue 的 Execute 事件）。
	 *
	 * 【表现 ≠ 效果】表现只做看得见听得见的事：粒子、音效、震屏、顿帧。
	 *   它不改变任何数值。数值请用「施加效果」。
	 * 【命名要求】表现标签必须以 GameplayCue 开头（例如 GameplayCue.Hit.Impact），
	 *   否则引擎会忽略且不报错——这是「我配了特效但没播」的第一大原因。
	 */
	UFUNCTION(BlueprintCallable, Category = "战斗|表现",
		meta = (DisplayName = "播放表现", Keywords = "表现 特效 音效 震屏 cue 打击感 play gameplay cue",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static void PlayGameplayCue(AActor* Target, FGameplayTag CueTag);

	/** 添加持续表现（Looping 表现，例如燃烧状态的火光）。结束时记得调「移除持续表现」 */
	UFUNCTION(BlueprintCallable, Category = "战斗|表现",
		meta = (DisplayName = "添加持续表现", Keywords = "表现 持续 循环 cue looping add",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static void AddLoopingGameplayCue(AActor* Target, FGameplayTag CueTag);

	/** 移除持续表现 */
	UFUNCTION(BlueprintCallable, Category = "战斗|表现",
		meta = (DisplayName = "移除持续表现", Keywords = "表现 持续 循环 移除 cue looping remove",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static void RemoveLoopingGameplayCue(AActor* Target, FGameplayTag CueTag);

	/**
	 * 给目标发送一个 Gameplay 事件。
	 *
	 * 【用途】动画通知 → 判定窗口 的标准做法：
	 *   动画里放一个 AnimNotify，发送 GameplayEvent.Hit 事件，
	 *   技能资产里用「等待Gameplay事件」技能任务接住它，再去做伤害判定。
	 *   这样打击判定就跟着动画走，而不是靠计时器硬猜。
	 * 【注意】Gameplay事件(GameplayEvent) 不是 效果(GameplayEffect)，两者缩写都是 GE，别搞混。
	 */
	UFUNCTION(BlueprintCallable, Category = "战斗|表现",
		meta = (DisplayName = "发送Gameplay事件", Keywords = "事件 通知 判定窗口 gameplay event send animnotify",
			DefaultToSelf = "Target", AdvancedDisplay = "Target,Payload"))
	static void SendGameplayEvent(AActor* Target, FGameplayTag EventTag, FGameplayEventData Payload);

	/**
	 * 给目标身上的状态树组件发送一个状态树事件。
	 *
	 * 【为什么值得单独包一层】纯蓝图发状态树事件是**两步**：先用「Make State Tree Event」
	 *   造一个结构体（Tag / Payload / Origin 三个字段），再调状态树组件的
	 *   「Send State Tree Event」。初学者十有八九会漏掉第一步，或者把 Payload 留成空导致报错。
	 *   这里合并成一步：填事件标签 + 来源说明即可。
	 *
	 * 【状态树事件 ≠ Gameplay事件】状态树事件（FStateTreeEvent）是给 StateTree 用的；
	 *   Gameplay事件是给 GAS 技能任务用的，两者互不相通。想给技能发事件请用「发送Gameplay事件」。
	 *
	 * 【前提】目标 Actor 上要有「State Tree」组件，并且树正在运行
	 *   （没在跑的话事件发进去也没人接）。
	 *
	 * @param EventTag 事件标签。状态树里用「On Event」触发器配上同一个标签来接
	 * @param Origin   来源说明，会带进状态树里用于分辨事件是谁发的；留空则自动填本插件名
	 * @return 找到状态树组件并投递成功返回 true
	 */
	UFUNCTION(BlueprintCallable, Category = "战斗|表现",
		meta = (DisplayName = "发送状态树事件", Keywords = "状态树 StateTree 事件 行为树 切换 send state tree event",
			DefaultToSelf = "Target", AdvancedDisplay = "Target,Origin"))
	static bool SendStateTreeEventToTarget(AActor* Target, FGameplayTag EventTag,
		FName Origin = NAME_None);

	// ============================================================================
	//  调试
	// ============================================================================

	/**
	 * 生成一份「战斗状态报告」文本：属性、已授予技能、激活中效果、身上的标签，全在里面。
	 *
	 * 【怎么用】接到 Text 控件上做实时面板，或者用「打印到屏幕/日志」节点输出。
	 * 排查「数值为什么不对」「技能为什么没生效」时，先看这一份报告通常就能定位。
	 *
	 * 【更直观的方式】编辑器里打开「GAS 战斗调试」页签（窗口菜单），
	 * 那是一个会自动刷新的可视化面板，并且能直接改数值/激活技能/移除效果。
	 */
	UFUNCTION(BlueprintCallable, Category = "战斗|调试",
		meta = (DisplayName = "获取战斗状态报告", Keywords = "调试 报告 快照 面板 dump debug summary report",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static FString GetCombatSummaryText(AActor* Target);

	/**
	 * 取属性的中文显示名（插件内置映射表，例如 Health → 「生命」、ComboIndex → 「连招段数」）。
	 *
	 * 【用途】自己做 UI 时想要中文属性名，直接查这个，不用再维护一份对照表。
	 * 【没命中怎么办】原样返回传入的英文名，不会返回空——所以可以放心直接显示。
	 * 【自己项目的属性】插件不认识你项目里自定义的属性集，会返回英文名。
	 *   这是有意的：插件不去猜业务含义，避免出现「看着像中文其实译错」的名字。
	 */
	UFUNCTION(BlueprintPure, Category = "战斗|调试",
		meta = (DisplayName = "取属性中文名", Keywords = "属性 中文名 显示名 翻译 display name 对照表"))
	static FString GetAttributeDisplayName(FName AttributeName);

	/**
	 * 取某个标签最近一次「从无到有 / 从有到无」发生的世界时刻（秒）。没记录过返回 -1。
	 *
	 * 【用途】调连招窗口时最常问的是「窗口到底是在哪一帧开的」。
	 *   把窗口标签挂上时的时刻记下来，就能确认它是不是真的踩在动画的 35%~80% 区间，
	 *   也能定位「窗口提前关了」这类时序问题。
	 *
	 * 【重要限制】只对**桥接组件「监听的标签」列表里配过的标签**有效。
	 *   没配过的标签根本没有回调，自然也没有时刻记录，一律返回 -1。
	 *   想让某个窗口标签可查，就把它加进「监听的标签」。
	 *   （勾了「通知所有标签变化」的话，所有标签都会被记录。）
	 *
	 * 【返回值怎么读】这是个**时刻**不是**时长**。想知道「窗口开了多久」，
	 *   用「Get Game Time in Seconds」减去它。
	 */
	UFUNCTION(BlueprintPure, Category = "战斗|调试",
		meta = (DisplayName = "标签最近变化时刻", Keywords = "标签 时刻 窗口 调试 tag change time 连招窗口",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static float GetTagLastChangeTime(AActor* Target, FGameplayTag Tag);

	/** 某个标签最近一次变化是不是「挂上」（true）还是「摘下」（false）。没记录过返回 false */
	UFUNCTION(BlueprintPure, Category = "战斗|调试",
		meta = (DisplayName = "标签最近变化方向", Keywords = "标签 方向 挂上 摘下 调试 tag last change direction",
			DefaultToSelf = "Target", AdvancedDisplay = "Target"))
	static bool WasTagLastChangeAdded(AActor* Target, FGameplayTag Tag);
};
