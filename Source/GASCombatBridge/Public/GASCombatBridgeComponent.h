// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "GameplayEffectTypes.h"
#include "GameplayTagContainer.h"
#include "GASCombatTypes.h"
#include "GASCombatBridgeComponent.generated.h"

class UAbilitySystemComponent;
class UGASCombatAbilitySet;
class UGASCombatAttributeWatcher;
class UGASCombatCoreAttributeSet;
class UGameplayAbility;
class UAttributeSet;
struct FAbilityEndedData;

/**
 * 单个标签的变化记录。
 *
 * 纯 C++ 结构，不进蓝图也不需要 USTRUCT —— 它只是「标签最近一次变化时刻」这个
 * 调试查询的内部账本（R6）。
 */
struct FGASCombatTagChangeRecord
{
	/** 最近一次变化的时刻（世界秒，GetTimeSeconds）。-1 = 从未观察到变化 */
	float Time = -1.f;

	/** 最近一次是「挂上」(true) 还是「摘下」(false) */
	bool bAdded = false;

	/** 最近一次变化后的层数 */
	int32 Count = 0;
};

/** 战斗初始化完成 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGASCombatOnCombatReady);

/** 技能被激活 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGASCombatOnAbilityActivated, UGameplayAbility*, Ability);

/**
 * 技能结束（正常播完或被打断都会触发）。
 *
 * 【bWasCancelled 怎么读】
 *   false = 技能正常播完了
 *   true  = 这次结束属于「被打断 / 被取消」——来自「打断带标签的技能」「取消所有技能」
 *           「取消技能」，或者技能蓝图里用「结束技能」节点主动传了 true。
 *
 * 【为什么它值得单独存在】「攻击被打断后卡在攻击状态」是连招最常见的 bug。
 *   在技能**外部**能区分这两种结束，就能一眼看出「State.Attacking 没被摘掉」是
 *   因为技能根本没结束，还是结束路径走错了。
 *
 * 【保证】每个技能结束只广播**一次**。
 *   实现上只挂了引擎的 OnAbilityEnded（它自带 bWasCancelled），
 *   没有同时挂 AbilityEndedCallbacks，所以不存在重复广播。
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGASCombatOnAbilityEnded,
	UGameplayAbility*, Ability,
	bool, bWasCancelled);

/**
 * 技能激活失败 —— 本插件最有价值的一个事件。
 *
 * 【为什么它重要】原生 GAS 里「技能没放出来」是完全静默的：
 *   你按了键，什么都没发生，输出日志也没有任何提示。
 *   技能资产里那些「激活所需标签/消耗/冷却」到底哪一条没过，策划只能靠猜。
 *   这个事件把引擎内部的失败原因标签直接送到蓝图里，并且附上一句中文说明。
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FGASCombatOnAbilityFailed,
	UGameplayAbility*, Ability,
	FGameplayTagContainer, FailureTags,
	FString, ReadableReason);

/**
 * 身上的 Gameplay 标签发生变化。
 *
 * 【参数说明】引擎在「新增/移除」模式下只回调「新的层数」，不会给旧层数。
 *   这里不编造旧值，而是直接告诉你方向：
 *     bAdded = true  → 标签刚被挂上（Count 是挂上后的层数）
 *     bAdded = false → 标签刚被摘掉（Count 为 0）
 *   做「进入/退出某状态」的判定用 bAdded 即可，不需要自己记上一帧。
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FGASCombatOnTagChanged,
	FGameplayTag, Tag,
	int32, Count,
	bool, bAdded);

/** 被施加了一个效果 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGASCombatOnEffectApplied,
	FActiveGameplayEffectHandle, Handle,
	TSubclassOf<UGameplayEffect>, EffectClass);

/** 受到伤害（转发自通用战斗属性集） */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FGASCombatOnDamageTakenEvent,
	AActor*, DamageInstigator,
	AActor*, DamageCauser,
	float, DamageAmount,
	float, RemainingHealth);

/** 生命归零 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGASCombatOnDeathEvent,
	AActor*, Killer,
	AActor*, DamageCauser);

/**
 * GAS 战斗桥接组件 —— 挂在角色上就能跑战斗系统。
 *
 * 【它替你做了哪些事】
 *   1. 找（或按需创建）技能系统组件
 *   2. 调用「初始化技能系统」——漏掉这一步技能永远放不出来，是新手第一大坑
 *   3. 挂上属性集、设好初始数值
 *   4. 授予技能集里的技能与效果
 *   5. 把引擎里「蓝图绑不到」的事件转发成蓝图事件
 *
 * 【最小用法】
 *   在角色蓝图里添加本组件 → 把「技能集」资产拖进「技能集」数组 → 完成。
 *   剩下的在 BeginPlay 自动发生，完成后会广播「战斗就绪」。
 *
 * 【不想用自动初始化？】把「开始游戏时自动初始化」取消勾选，
 * 然后在合适的时机（例如选完角色、进入关卡后）手动调「初始化战斗」。
 */
UCLASS(BlueprintType, ClassGroup = (Combat),
	meta = (BlueprintSpawnableComponent, DisplayName = "GAS战斗桥接组件"))
class GASCOMBATBRIDGE_API UGASCombatBridgeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGASCombatBridgeComponent();

	// ========================================================================
	//  配置
	// ========================================================================

	/** 开始游戏时自动完成整套战斗初始化。绝大多数情况保持勾选 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗|初始化",
		meta = (DisplayName = "开始游戏时自动初始化"))
	bool bAutoInitializeOnBeginPlay = true;

	/** 找不到技能系统组件时自动创建一个。联机项目建议改为在角色蓝图里手动添加组件，然后取消勾选此项 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗|初始化",
		meta = (DisplayName = "缺少组件时自动创建"))
	bool bCreateASCIfMissing = true;

	/** 自动创建的技能系统组件是否参与网络复制（仅联机项目需要） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗|初始化",
		meta = (DisplayName = "自动创建时启用复制", EditCondition = "bCreateASCIfMissing"))
	bool bReplicateCreatedASC = false;

	/** 要挂上的属性集。默认填「通用战斗属性集」 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗|初始化",
		meta = (DisplayName = "挂载的属性集", AllowAbstract = "false"))
	TArray<TSubclassOf<UAttributeSet>> AttributeSets;

	/**
	 * 要授予的技能集列表。
	 * 一个角色 = 若干份技能集，换装备/换职业就是换这里的资产，不用改蓝图连线。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗|初始化",
		meta = (DisplayName = "授予的技能集"))
	TArray<TObjectPtr<UGASCombatAbilitySet>> AbilitySets;

	/** 只在这些标签变化时通知（留空则完全不通知标签变化，避免刷屏） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗|事件",
		meta = (DisplayName = "监听的标签"))
	FGameplayTagContainer WatchedTags;

	/**
	 * 通知「任何」标签变化。
	 * 调试时很有用（能看到状态机到底挂了什么标签），但战斗中会非常吵，正式项目请关掉。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "战斗|事件",
		meta = (DisplayName = "通知所有标签变化（调试用）"))
	bool bBroadcastAllTagChanges = false;

	// ========================================================================
	//  事件
	// ========================================================================

	/** 战斗系统初始化完成（技能已授予、属性已就位）。UI 应该在这个事件之后再去读数值 */
	UPROPERTY(BlueprintAssignable, Category = "战斗|事件", meta = (DisplayName = "战斗就绪"))
	FGASCombatOnCombatReady OnCombatReady;

	/** 技能开始激活（做技能栏高亮、打断提示用） */
	UPROPERTY(BlueprintAssignable, Category = "战斗|事件", meta = (DisplayName = "技能激活时"))
	FGASCombatOnAbilityActivated OnAbilityActivated;

	/** 技能结束。bWasCancelled 为真表示是被打断/取消的，为假表示正常播完 */
	UPROPERTY(BlueprintAssignable, Category = "战斗|事件", meta = (DisplayName = "技能结束时"))
	FGASCombatOnAbilityEnded OnAbilityEnded;

	/**
	 * 技能激活失败，附带中文原因。
	 * 强烈建议在开发期绑上它并打印出来——这是排查「技能为什么放不出来」的最快路径。
	 */
	UPROPERTY(BlueprintAssignable, Category = "战斗|事件", meta = (DisplayName = "技能激活失败时"))
	FGASCombatOnAbilityFailed OnAbilityFailed;

	/** 监听的标签发生变化 */
	UPROPERTY(BlueprintAssignable, Category = "战斗|事件", meta = (DisplayName = "标签变化时"))
	FGASCombatOnTagChanged OnGameplayTagChanged;

	/** 被施加了效果 */
	UPROPERTY(BlueprintAssignable, Category = "战斗|事件", meta = (DisplayName = "被施加效果时"))
	FGASCombatOnEffectApplied OnEffectApplied;

	/** 受到伤害。等价于属性集上的同名事件，放这里是为了让事件集中在一处 */
	UPROPERTY(BlueprintAssignable, Category = "战斗|事件", meta = (DisplayName = "受到伤害时"))
	FGASCombatOnDamageTakenEvent OnDamageTaken;

	/** 生命归零 */
	UPROPERTY(BlueprintAssignable, Category = "战斗|事件", meta = (DisplayName = "死亡时"))
	FGASCombatOnDeathEvent OnDeath;

	// ========================================================================
	//  接口
	// ========================================================================

	/** 取技能系统组件 */
	UFUNCTION(BlueprintPure, Category = "战斗|初始化", meta = (DisplayName = "获取技能系统组件（ASC）"))
	UAbilitySystemComponent* GetAbilitySystemComponent() const;

	/**
	 * 手动执行整套初始化。会自动跳过已经做过的步骤，重复调用安全。
	 * @return 是否成功（失败原因见输出日志的中文提示）
	 */
	UFUNCTION(BlueprintCallable, Category = "战斗|初始化", meta = (DisplayName = "初始化战斗", Keywords = "初始化 开始 setup init combat"))
	bool InitializeCombat();

	/** 授予「技能集」列表里的全部内容，返回凭据供之后回收 */
	UFUNCTION(BlueprintCallable, Category = "战斗|初始化", meta = (DisplayName = "授予全部技能集"))
	bool GrantAllAbilitySets(FGASCombatGrantedHandles& OutHandles);

	/** 回收本组件授予的全部技能集 */
	UFUNCTION(BlueprintCallable, Category = "战斗|初始化", meta = (DisplayName = "回收全部技能集"))
	bool RevokeAllAbilitySets();

	/** 生成一份中文战斗状态报告（属性、技能、效果、标签全在里面） */
	UFUNCTION(BlueprintCallable, Category = "战斗|调试", meta = (DisplayName = "获取战斗信息文本", Keywords = "调试 报告 dump debug summary"))
	FString GetCombatSummary() const;

	/** 把战斗状态报告打印到屏幕和输出日志 */
	UFUNCTION(BlueprintCallable, Category = "战斗|调试", meta = (DisplayName = "打印战斗信息", Keywords = "调试 打印 报告 print debug"))
	void PrintCombatSummary() const;

	/** 为某个属性创建监听器（便捷入口，省一次找组件的连线） */
	UFUNCTION(BlueprintCallable, Category = "战斗|调试", meta = (DisplayName = "监听属性变化", Keywords = "监听 属性 watcher"))
	UGASCombatAttributeWatcher* WatchAttribute(FGameplayAttribute Attribute, bool bFireImmediately = true);

	/** 初始化是否已完成 */
	UFUNCTION(BlueprintPure, Category = "战斗|初始化", meta = (DisplayName = "是否已初始化"))
	bool IsCombatReady() const { return bCombatReady; }

	/**
	 * 取某个标签最近一次「从无到有 / 从有到无」发生的世界时刻（秒）。没记录过返回 -1。
	 *
	 * 【只对「监听的标签」里配过的标签有效】勾了「通知所有标签变化」则所有标签都会被记录。
	 * 蓝图里更方便的入口是节点库里的「标签最近变化时刻」。
	 */
	float GetTagLastChangeTime(const FGameplayTag& Tag) const;

	/** 某个标签最近一次变化是「挂上」(true) 还是「摘下」(false)。没记录过返回 false */
	bool WasTagLastChangeAdded(const FGameplayTag& Tag) const;

	/** 取某个标签最近一次变化后的层数。没记录过返回 0 */
	int32 GetTagLastChangeCount(const FGameplayTag& Tag) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// --- 引擎原生委托的中转 ---
	void HandleAbilityActivated(UGameplayAbility* Ability);

	/**
	 * 技能结束的中转。
	 *
	 * 参数用引擎的 FAbilityEndedData 而不是裸的 UGameplayAbility*：
	 * 引擎在同一处先后广播了两个委托 ——
	 *   AbilityEndedCallbacks(UGameplayAbility*)                      没有 bWasCancelled
	 *   OnAbilityEnded(const FAbilityEndedData&)                      带 bWasCancelled
	 * 这里只挂后者，所以「技能结束时」事件能拿到是否被打断，且每个技能只广播一次。
	 */
	void HandleAbilityEnded(const FAbilityEndedData& Data);

	void HandleAbilityFailed(const UGameplayAbility* Ability, const FGameplayTagContainer& FailureTags);
	void HandleGameplayTagChanged(const FGameplayTag Tag, int32 NewCount);
	void HandleEffectApplied(UAbilitySystemComponent* Source, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle Handle);

	// 这两个必须标 UFUNCTION：属性集上的是「动态多播委托」，
	// 只能通过 AddDynamic / RemoveDynamic 绑定，而那套宏要求目标函数是 UFUNCTION。
	UFUNCTION()
	void HandleDamageTaken(AActor* DamageInstigator, AActor* DamageCauser, float DamageAmount, float RemainingHealth);

	UFUNCTION()
	void HandleDeath(AActor* Killer, AActor* DamageCauser);

	/** 绑定引擎里那些「蓝图绑不到」的原生委托 */
	void BindEngineDelegates(UAbilitySystemComponent* ASC);

	/** 解绑全部委托。必须在 EndPlay 做，否则组件销毁后仍会收到回调 */
	void UnbindEngineDelegates();

	/** 把引擎的失败标签翻译成策划看得懂的中文 */
	static FString MakeReadableFailureReason(const FGameplayTagContainer& FailureTags);

	/** 从失败标签里挑出「最主要的那一条」原因 */
	static FString TranslateSingleFailureTag(const FGameplayTag& Tag);

private:
	/** 初始化是否已完成 */
	bool bCombatReady = false;

	/** 本组件授予出去的凭据，用于「回收全部技能集」 */
	FGASCombatGrantedHandles GrantedHandles;

	/** 已注册的标签事件句柄，解绑时要用 */
	TMap<FGameplayTag, FDelegateHandle> TagEventHandles;

	/** 「通知所有标签变化」模式的句柄 */
	FDelegateHandle GenericTagEventHandle;

	/** 已绑定事件的属性集（弱引用，避免组件反过来延长属性集寿命） */
	TWeakObjectPtr<UGASCombatCoreAttributeSet> BoundAttributeSet;

	/** 是否已绑定过引擎委托 */
	bool bEngineDelegatesBound = false;

	/**
	 * 标签最近一次变化的小账本，供「标签最近变化时刻」调试节点查询。
	 * 只有「监听的标签」里配过的标签（或勾了「通知所有标签变化」时的所有标签）会有记录。
	 */
	TMap<FGameplayTag, FGASCombatTagChangeRecord> TagLastChangeRecords;
};
