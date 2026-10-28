// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ActiveGameplayEffectHandle.h"
#include "AttributeSet.h"
#include "CoreMinimal.h"
#include "GameplayAbilitySpecHandle.h"
#include "Types/SlateEnums.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class AActor;
class SEditableTextBox;
class SProgressBar;
class SScrollBox;
class STextBlock;
class SVerticalBox;
class UAbilitySystemComponent;
class UGameplayAbility;

/**
 * GAS 战斗调试面板。
 *
 * 【为什么需要它】
 *   引擎自带的 `showdebug abilitysystem` 有三个痛点：
 *     1) 要手打控制台命令，还要翻页（AbilitySystem.Debug.NextCategory）
 *     2) 它是画在游戏视口上的文字，会挡住画面，也没法复制
 *     3) 只能看，不能改——想验证「把血量设成 1 会不会触发濒死」得先去改蓝图
 *
 *   这个面板是编辑器里一个可停靠的页签，PIE 运行时每 0.25 秒自动刷新：
 *     - 属性：当前值 / 基础值，并且**可以直接改基础值**（调数值、造濒死、清空蓝量）
 *     - 技能：等级 / 是否运行中 / 冷却进度条，并且**可以直接激活或取消**
 *     - 效果：层数 / 剩余时间，并且**可以直接移除**（验证驱散、验证 Buff 消失后的表现）
 *     - 标签：当前身上所有标签及层数
 *     - 两个全局开关：忽略冷却 / 忽略消耗（做技能连招验证时非常省事）
 *
 * 【设计原则】面板只读运行时模块的同一套节点库（UGASCombatStatics），
 * 所以面板里看到的一定等于蓝图里能拿到的，不会出现「面板显示正常但蓝图取不到」的割裂。
 */
class SGASCombatDebugPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SGASCombatDebugPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** 每帧调用；内部按固定间隔节流刷新，不是每帧都采集数据 */
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	// ========================================================================
	//  行数据
	// ========================================================================
	// 每一行都是一个独立的数据对象 + 一组指向它内部字段的控件。
	// 这样刷新时只需要改字段值，不用重建控件——滚动位置不会跳，进度条也不会闪。

	struct FAttributeRow
	{
		FGameplayAttribute Attribute;
		FString Name;
		float CurrentValue = 0.f;
		float BaseValue = 0.f;

		TSharedPtr<STextBlock> CurrentText;
		TSharedPtr<STextBlock> BaseText;
		TSharedPtr<STextBlock> NoteText;
		TSharedPtr<SEditableTextBox> SetterBox;
	};

	struct FAbilityRow
	{
		TSubclassOf<UGameplayAbility> AbilityClass;
		FGameplayAbilitySpecHandle Handle;
		FString Name;
		int32 Level = 1;
		bool bActive = false;
		float CooldownRemaining = 0.f;
		float CooldownDuration = 0.f;

		TSharedPtr<STextBlock> LevelText;
		TSharedPtr<STextBlock> StateText;
		TSharedPtr<STextBlock> CooldownText;
	};

	struct FEffectRow
	{
		FActiveGameplayEffectHandle Handle;
		FString Name;
		int32 StackCount = 1;
		float RemainingTime = -1.f;
		float TotalDuration = -1.f;
		bool bInfinite = false;

		TSharedPtr<STextBlock> StacksText;
		TSharedPtr<STextBlock> RemainingText;
		TSharedPtr<STextBlock> DurationText;
	};

	/** 目标选择下拉框里的一项 */
	struct FActorEntry
	{
		TWeakObjectPtr<AActor> Actor;
		FString Label;
	};

	// ========================================================================
	//  界面构建
	// ========================================================================

	TSharedRef<SWidget> BuildToolbar();

	/** 属性区：读数值 + 直接改基础值 */
	TSharedRef<SWidget> BuildAttributeSection();

	/** 技能区：状态 + 冷却条 + 激活/取消按钮 */
	TSharedRef<SWidget> BuildAbilitySection();

	/** 效果区：层数 + 剩余时间 + 移除按钮 */
	TSharedRef<SWidget> BuildEffectSection();

	/** 标签区：全部标签与层数 */
	TSharedRef<SWidget> BuildTagSection();

	TSharedRef<SWidget> MakeSectionHeader(const FString& Title, const FString& Hint);

	/**
	 * 生成一个固定宽度的文本单元格，并把控件指针存回 OutBlock。
	 * 之后刷新时只要 SetText 即可，不需要重建控件树——这样滚动位置不会跳动，
	 * 进度条也不会因为重建而闪烁。
	 */
	TSharedRef<SWidget> MakeCell(TSharedPtr<STextBlock>& OutBlock, float Width, const FText& InitialText,
		EHorizontalAlignment Alignment = HAlign_Left);

	/** 生成一行表头（列名） */
	TSharedRef<SWidget> MakeHeaderRow(const TArray<TPair<FString, float>>& Columns);

	TSharedRef<SWidget> MakeAttributeRow(const TSharedPtr<FAttributeRow>& Row);
	TSharedRef<SWidget> MakeAbilityRow(const TSharedPtr<FAbilityRow>& Row);
	TSharedRef<SWidget> MakeEffectRow(const TSharedPtr<FEffectRow>& Row);

	// ========================================================================
	//  数据刷新
	// ========================================================================

	/** 重新扫描 PIE 世界里所有带技能系统组件的 Actor */
	void RefreshActorList();

	/** 采集当前选中目标的数据并更新界面 */
	void RefreshSnapshot();

	/** 取当前选中的 Actor（可能为空） */
	AActor* GetSelectedActor() const;

	/** 技能系统组件是否已就绪（决定面板显示数据还是显示提示） */
	bool IsTargetReady() const;

	/** 顶部提示文本：告诉策划现在该做什么 */
	FText GetStatusHint() const;

	// ---- 数据源 ----
	TArray<TSharedPtr<FActorEntry>> ActorEntries;
	TSharedPtr<FActorEntry> SelectedEntry;
	TSharedPtr<SComboBox<TSharedPtr<FActorEntry>>> ActorComboBox;

	TArray<TSharedPtr<FAttributeRow>> AttributeRows;
	TArray<TSharedPtr<FAbilityRow>> AbilityRows;
	TArray<TSharedPtr<FEffectRow>> EffectRows;

	TSharedPtr<SVerticalBox> AttributeContainer;
	TSharedPtr<SVerticalBox> AbilityContainer;
	TSharedPtr<SVerticalBox> EffectContainer;
	TSharedPtr<STextBlock> TagText;
	TSharedPtr<STextBlock> StatusText;

	// ---- 刷新节流 ----
	float TimeSinceRefresh = 0.f;
	static constexpr float RefreshInterval = 0.25f;

	/** 自动刷新开关 */
	bool bAutoRefresh = true;

	/** 上一次采集时的「结构指纹」，用来判断是否需要重建行控件 */
	FString LastAttributeSignature;
	FString LastAbilitySignature;
	FString LastEffectSignature;
	FString LastActorSignature;
};
