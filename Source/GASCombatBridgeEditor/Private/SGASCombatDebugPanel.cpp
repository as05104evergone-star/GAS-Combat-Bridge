// Copyright Epic Games, Inc. All Rights Reserved.

#include "SGASCombatDebugPanel.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Abilities/GameplayAbility.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GASCombatStatics.h"
#include "GASCombatTypes.h"
#include "GameplayTagContainer.h"
#include "HAL/IConsoleManager.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "GASCombatDebugPanel"

namespace GASCombatDebugPanel::Private
{
	/** 列宽统一在这里定义，看起来才像一张表 */
	constexpr float ColName = 190.f;
	constexpr float ColValue = 100.f;
	constexpr float ColNote = 220.f;
	constexpr float ColAction = 130.f;

	/** 引擎的调试开关（由 Gameplay Abilities 设置注册为控制台变量） */
	const TCHAR* CVarIgnoreCooldowns = TEXT("AbilitySystem.IgnoreCooldowns");
	const TCHAR* CVarIgnoreCosts = TEXT("AbilitySystem.IgnoreCosts");

	bool ReadBoolCVar(const TCHAR* Name, bool& bOutFound)
	{
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			bOutFound = true;
			return CVar->GetInt() != 0;
		}
		bOutFound = false;
		return false;
	}

	void WriteBoolCVar(const TCHAR* Name, bool bEnabled)
	{
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			CVar->Set(bEnabled ? 1 : 0, ECVF_SetByGameSetting);
		}
	}

	FSlateFontInfo RegularFont() { return FCoreStyle::GetDefaultFontStyle("Regular", 9); }
	FSlateFontInfo BoldFont() { return FCoreStyle::GetDefaultFontStyle("Bold", 9); }

	/** 等宽字体：数值列用它对齐，扫一眼就能发现哪个数不对 */
	FSlateFontInfo MonoFont() { return FCoreStyle::GetDefaultFontStyle("Mono", 9); }

	/**
	 * 取当前 PIE 世界（含「在编辑器中模拟」）。
	 * 特意不看编辑器世界：那里的角色不会被初始化技能系统，属性全是默认值，容易误导。
	 */
	UWorld* GetPIEWorld()
	{
		if (!GEngine)
		{
			return nullptr;
		}

		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE && Context.World())
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	FText FormatFloat(float Value)
	{
		return FText::FromString(FString::Printf(TEXT("%.2f"), Value));
	}
}

using namespace GASCombatDebugPanel::Private;

// ============================================================================
//  构建
// ============================================================================

void SGASCombatDebugPanel::Construct(const FArguments& InArgs)
{
	// 必须显式开启 Tick：UE5.4 起 Slate 把 tick 改成了 EWidgetUpdateFlags::NeedsTick 标志位，
	// 控件默认不带这个标志。不写这一行的话，面板能显示但永远不会自动刷新（而且不报任何错）。
	SetCanTick(true);

	ChildSlot
	[
		SNew(SVerticalBox)

		// ---- 工具栏 ----
		+ SVerticalBox::Slot().AutoHeight().Padding(6.f, 6.f, 6.f, 2.f)
		[
			BuildToolbar()
		]

		// ---- 状态提示：没进 PIE / 没目标 / 没初始化时，直接告诉你下一步点什么 ----
		+ SVerticalBox::Slot().AutoHeight().Padding(6.f, 2.f, 6.f, 4.f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.Padding(6.f)
			[
				SAssignNew(StatusText, STextBlock)
				.Font(RegularFont())
				.AutoWrapText(true)
			]
		]

		// ---- 主体 ----
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(6.f, 0.f, 6.f, 6.f)
		[
			SNew(SScrollBox)

			+ SScrollBox::Slot().Padding(0.f, 2.f)
			[
				SNew(SExpandableArea)
				.AreaTitle(LOCTEXT("AttrSection", "属性"))
				.InitiallyCollapsed(false)
				.BodyContent()[ BuildAttributeSection() ]
			]

			+ SScrollBox::Slot().Padding(0.f, 2.f)
			[
				SNew(SExpandableArea)
				.AreaTitle(LOCTEXT("AbilitySection", "技能"))
				.InitiallyCollapsed(false)
				.BodyContent()[ BuildAbilitySection() ]
			]

			+ SScrollBox::Slot().Padding(0.f, 2.f)
			[
				SNew(SExpandableArea)
				.AreaTitle(LOCTEXT("EffectSection", "效果（Buff / Debuff）"))
				.InitiallyCollapsed(false)
				.BodyContent()[ BuildEffectSection() ]
			]

			+ SScrollBox::Slot().Padding(0.f, 2.f)
			[
				SNew(SExpandableArea)
				.AreaTitle(LOCTEXT("TagSection", "身上的标签（含层数）"))
				.InitiallyCollapsed(false)
				.BodyContent()[ BuildTagSection() ]
			]
		]
	];

	RefreshActorList();
	RefreshSnapshot();
}

void SGASCombatDebugPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	TimeSinceRefresh += InDeltaTime;
	if (TimeSinceRefresh < RefreshInterval)
	{
		return;
	}
	TimeSinceRefresh = 0.f;

	// 目标列表每次都扫（PIE 里 Actor 数量有限，开销可以忽略）
	RefreshActorList();

	// 状态提示始终更新，这样即使关了自动刷新，你也能看到「目标变了/被销毁了」
	if (StatusText.IsValid())
	{
		StatusText->SetText(GetStatusHint());
	}

	if (bAutoRefresh)
	{
		RefreshSnapshot();
	}
}

// ============================================================================
//  工具栏
// ============================================================================

TSharedRef<SWidget> SGASCombatDebugPanel::BuildToolbar()
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
		[
			SNew(STextBlock).Text(LOCTEXT("TargetLabel", "调试目标：")).Font(BoldFont())
		]

		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
		[
			SAssignNew(ActorComboBox, SComboBox<TSharedPtr<FActorEntry>>)
			.OptionsSource(&ActorEntries)
			.ToolTipText(LOCTEXT("TargetTip", "列出当前 PIE 世界里所有带「技能系统组件」的 Actor。\n点「播放」之后这里才会有内容。"))
			.OnGenerateWidget_Lambda([](TSharedPtr<FActorEntry> Item)
			{
				return SNew(STextBlock)
					.Font(RegularFont())
					.Text(Item.IsValid() ? FText::FromString(Item->Label) : FText::GetEmpty());
			})
			.OnSelectionChanged_Lambda([this](TSharedPtr<FActorEntry> Item, ESelectInfo::Type)
			{
				SelectedEntry = Item;
				// 换目标后结构一定变了，清掉签名强制重建
				LastAttributeSignature.Reset();
				LastAbilitySignature.Reset();
				LastEffectSignature.Reset();
				RefreshSnapshot();
			})
			[
				SNew(STextBlock)
				.Font(RegularFont())
				.Text_Lambda([this]()
				{
					return SelectedEntry.IsValid()
						? FText::FromString(SelectedEntry->Label)
						: LOCTEXT("NoTarget", "(没有可用目标)");
				})
			]
		]

		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([this]() { return bAutoRefresh ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bAutoRefresh = (State == ECheckBoxState::Checked); })
			.ToolTipText(LOCTEXT("AutoRefreshTip", "每 0.25 秒自动重新采集一次。\n想盯着某一瞬间的数值看时，把它关掉，再用「立即刷新」单步观察。"))
			[
				SNew(STextBlock).Text(LOCTEXT("AutoRefresh", "自动刷新")).Font(RegularFont())
			]
		]

		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 14.f, 0.f)
		[
			SNew(SButton)
			.Text(LOCTEXT("RefreshNow", "立即刷新"))
			.OnClicked_Lambda([this]() { RefreshSnapshot(); return FReply::Handled(); })
		]

		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
		[
			SNew(SCheckBox)
			.IsEnabled_Lambda([]() { bool bFound = false; ReadBoolCVar(CVarIgnoreCooldowns, bFound); return bFound; })
			.IsChecked_Lambda([]() { bool bFound = false; return ReadBoolCVar(CVarIgnoreCooldowns, bFound) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([](ECheckBoxState State) { WriteBoolCVar(CVarIgnoreCooldowns, State == ECheckBoxState::Checked); })
			.ToolTipText(LOCTEXT("IgnoreCDTip", "打开后所有技能不再检查冷却。\n验证连招衔接、反复放同一个技能时非常省事。\n（等价于控制台命令 AbilitySystem.IgnoreCooldowns 1）"))
			[
				SNew(STextBlock).Text(LOCTEXT("IgnoreCD", "忽略冷却")).Font(RegularFont())
			]
		]

		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 14.f, 0.f)
		[
			SNew(SCheckBox)
			.IsEnabled_Lambda([]() { bool bFound = false; ReadBoolCVar(CVarIgnoreCosts, bFound); return bFound; })
			.IsChecked_Lambda([]() { bool bFound = false; return ReadBoolCVar(CVarIgnoreCosts, bFound) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([](ECheckBoxState State) { WriteBoolCVar(CVarIgnoreCosts, State == ECheckBoxState::Checked); })
			.ToolTipText(LOCTEXT("IgnoreCostTip", "打开后所有技能不再检查消耗。\n蓝量不够也能放，方便单独验证技能效果本身。\n（等价于控制台命令 AbilitySystem.IgnoreCosts 1）"))
			[
				SNew(STextBlock).Text(LOCTEXT("IgnoreCost", "忽略消耗")).Font(RegularFont())
			]
		]

		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SButton)
			.Text(LOCTEXT("PrintLog", "打印到日志"))
			.ToolTipText(LOCTEXT("PrintLogTip", "把当前战斗状态完整输出到「输出日志」，方便复制给程序或存档对比"))
			.OnClicked_Lambda([this]()
			{
				if (AActor* Target = GetSelectedActor())
				{
					UE_LOG(LogTemp, Display, TEXT("%s"), *UGASCombatStatics::GetCombatSummaryText(Target));
				}
				return FReply::Handled();
			})
		];
}

// ============================================================================
//  区块骨架
// ============================================================================

TSharedRef<SWidget> SGASCombatDebugPanel::MakeCell(TSharedPtr<STextBlock>& OutBlock, float Width,
	const FText& InitialText, EHorizontalAlignment Alignment)
{
	TSharedPtr<STextBlock> Block;
	TSharedRef<SWidget> Result =
		SNew(SBox)
		.WidthOverride(Width)
		.Padding(FMargin(2.f, 1.f))
		.VAlign(VAlign_Center)
		[
			SAssignNew(Block, STextBlock)
			.Font(MonoFont())
			.Text(InitialText)
			.Justification(Alignment)
			.AutoWrapText(false)
		];

	OutBlock = Block;
	return Result;
}

TSharedRef<SWidget> SGASCombatDebugPanel::MakeHeaderRow(const TArray<TPair<FString, float>>& Columns)
{
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	for (const TPair<FString, float>& Column : Columns)
	{
		Row->AddSlot().AutoWidth()
		[
			SNew(SBox)
			.WidthOverride(Column.Value)
			.Padding(FMargin(2.f, 2.f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(Column.Key))
				.Font(BoldFont())
				.ColorAndOpacity(FLinearColor(0.5f, 0.72f, 1.0f))
			]
		];
	}
	return Row;
}

TSharedRef<SWidget> SGASCombatDebugPanel::BuildAttributeSection()
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock)
			.Font(RegularFont())
			.ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f))
			.AutoWrapText(true)
			.Text(LOCTEXT("AttrHint",
				"「当前值」是 UI 应该显示的值；「基础值」是不含临时增益的永久值（注意：基础值不是最大值）。\n"
				"右侧输入框可以改基础值——调数值、造濒死、清空蓝量都能直接在这里做，不用改蓝图。"))
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
		[
			MakeHeaderRow({
				TPair<FString, float>(TEXT("属性"), ColName),
				TPair<FString, float>(TEXT("当前值"), ColValue),
				TPair<FString, float>(TEXT("基础值"), ColValue),
				TPair<FString, float>(TEXT("改基础值"), 80.f),
				TPair<FString, float>(TEXT("说明"), ColNote) })
		]

		+ SVerticalBox::Slot().AutoHeight()[ SNew(SSeparator) ]

		+ SVerticalBox::Slot().AutoHeight()
		[
			SAssignNew(AttributeContainer, SVerticalBox)
		];
}

TSharedRef<SWidget> SGASCombatDebugPanel::BuildAbilitySection()
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock)
			.Font(RegularFont())
			.ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f))
			.AutoWrapText(true)
			.Text(LOCTEXT("AbilityHint",
				"「激活」= 尝试释放技能，失败原因会打到输出日志（等价于蓝图里的「尝试激活技能」）。\n"
				"冷却进度条的填充比例 = 剩余冷却 / 总时长，所以满格表示刚进冷却。"))
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
		[
			MakeHeaderRow({
				TPair<FString, float>(TEXT("技能"), ColName + 80.f),
				TPair<FString, float>(TEXT("等级"), 44.f),
				TPair<FString, float>(TEXT("状态"), 72.f),
				TPair<FString, float>(TEXT("冷却进度"), 130.f),
				TPair<FString, float>(TEXT("剩余"), 64.f),
				TPair<FString, float>(TEXT("操作"), ColAction) })
		]

		+ SVerticalBox::Slot().AutoHeight()[ SNew(SSeparator) ]

		+ SVerticalBox::Slot().AutoHeight()
		[
			SAssignNew(AbilityContainer, SVerticalBox)
		];
}

TSharedRef<SWidget> SGASCombatDebugPanel::BuildEffectSection()
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock)
			.Font(RegularFont())
			.ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f))
			.AutoWrapText(true)
			.Text(LOCTEXT("EffectHint",
				"「层数」大于 1 说明这个效果在叠加。无限持续的效果不会被自动移除，只能手动摘掉。\n"
				"点「移除」可以直接验证驱散、以及 Buff 消失后数值与表现是否正确。"))
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
		[
			MakeHeaderRow({
				TPair<FString, float>(TEXT("效果"), ColName + 80.f),
				TPair<FString, float>(TEXT("层数"), 50.f),
				TPair<FString, float>(TEXT("剩余时间"), 90.f),
				TPair<FString, float>(TEXT("总时长"), 90.f),
				TPair<FString, float>(TEXT("操作"), ColAction) })
		]

		+ SVerticalBox::Slot().AutoHeight()[ SNew(SSeparator) ]

		+ SVerticalBox::Slot().AutoHeight()
		[
			SAssignNew(EffectContainer, SVerticalBox)
		];
}

TSharedRef<SWidget> SGASCombatDebugPanel::BuildTagSection()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(2.f, 2.f)
		[
			SAssignNew(TagText, STextBlock)
			.Font(MonoFont())
			.AutoWrapText(true)
			.Text(LOCTEXT("NoTags", "(无)"))
		];
}

// ============================================================================
//  行控件
// ============================================================================

TSharedRef<SWidget> SGASCombatDebugPanel::MakeAttributeRow(const TSharedPtr<FAttributeRow>& Row)
{
	TSharedPtr<SEditableTextBox> Setter;
	TSharedRef<SWidget> SetterWidget =
		SNew(SBox)
		.WidthOverride(80.f)
		.Padding(FMargin(2.f, 1.f))
		.VAlign(VAlign_Center)
		[
			SAssignNew(Setter, SEditableTextBox)
			.Font(MonoFont())
			.HintText(LOCTEXT("SetValueHint", "改值"))
			.ToolTipText(LOCTEXT("SetValueTip",
				"输入数字后按回车：把这个属性的「基础值」改成该数值。\n"
				"注意改的是基础值（永久值），不会触发「受到伤害 / 受到治疗」事件，\n"
				"所以适合调数值、造濒死、清空蓝量这类验证场景。"))
			.OnTextCommitted_Lambda([this, Row](const FText& NewText, ETextCommit::Type CommitType)
			{
				if (CommitType == ETextCommit::OnEnter)
				{
					const FString Trimmed = NewText.ToString().TrimStartAndEnd();
					if (!Trimmed.IsEmpty())
					{
						UGASCombatStatics::SetAttributeBaseValue(GetSelectedActor(), Row->Attribute, FCString::Atof(*Trimmed));
					}
				}

				// 清空输入框，方便连续改多个属性
				if (Row->SetterBox.IsValid())
				{
					Row->SetterBox->SetText(FText::GetEmpty());
				}

				RefreshSnapshot();
			})
		];
	Row->SetterBox = Setter;

	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox).WidthOverride(ColName).Padding(FMargin(2.f, 1.f)).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Font(RegularFont()).Text(FText::FromString(Row->Name))
			]
		]

		+ SHorizontalBox::Slot().AutoWidth()[ MakeCell(Row->CurrentText, ColValue, FText::FromString(TEXT("0.00")), HAlign_Right) ]
		+ SHorizontalBox::Slot().AutoWidth()[ MakeCell(Row->BaseText, ColValue, FText::FromString(TEXT("0.00")), HAlign_Right) ]
		+ SHorizontalBox::Slot().AutoWidth()[ SetterWidget ]
		+ SHorizontalBox::Slot().AutoWidth()[ MakeCell(Row->NoteText, ColNote, FText::GetEmpty()) ];
}

TSharedRef<SWidget> SGASCombatDebugPanel::MakeAbilityRow(const TSharedPtr<FAbilityRow>& Row)
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox).WidthOverride(ColName + 80.f).Padding(FMargin(2.f, 1.f)).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Font(RegularFont()).Text(FText::FromString(Row->Name))
			]
		]

		+ SHorizontalBox::Slot().AutoWidth()[ MakeCell(Row->LevelText, 44.f, FText::FromString(TEXT("1")), HAlign_Center) ]
		+ SHorizontalBox::Slot().AutoWidth()[ MakeCell(Row->StateText, 72.f, FText::FromString(TEXT("就绪"))) ]

		// 冷却进度条：Percent 用 lambda 读 Row 里的实时字段，
		// 所以刷新时只改数据就会自动重绘，不需要重建控件（也就不会闪）
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(130.f).Padding(FMargin(2.f, 3.f))
			[
				SNew(SProgressBar)
				.Percent_Lambda([Row]()
				{
					return Row->CooldownDuration > 0.f
						? FMath::Clamp(Row->CooldownRemaining / Row->CooldownDuration, 0.f, 1.f)
						: 0.f;
				})
				.FillColorAndOpacity(FLinearColor(0.95f, 0.55f, 0.12f))
			]
		]

		+ SHorizontalBox::Slot().AutoWidth()[ MakeCell(Row->CooldownText, 64.f, FText::GetEmpty(), HAlign_Right) ]

		+ SHorizontalBox::Slot().AutoWidth().Padding(2.f, 1.f).VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 3.f, 0.f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Activate", "激活"))
				.ToolTipText(LOCTEXT("ActivateTip", "尝试激活这个技能。失败了会在输出日志里给出原因——这是验证技能配置最快的方式"))
				.OnClicked_Lambda([this, Row]()
				{
					AActor* Target = GetSelectedActor();
					if (Target && Row->AbilityClass)
					{
						const bool bStarted = UGASCombatStatics::TryActivateAbility(Target, Row->AbilityClass, /*bAllowRemoteActivation=*/true);
						UE_LOG(LogTemp, Display,
							TEXT("【GAS战斗调试面板】尝试激活 %s → %s"),
							*Row->AbilityClass->GetName(),
							bStarted ? TEXT("已开始激活") : TEXT("未通过检查，原因见上方警告或「打印到日志」"));
					}
					RefreshSnapshot();
					return FReply::Handled();
				})
			]

			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("CancelAbility", "取消"))
				.ToolTipText(LOCTEXT("CancelAbilityTip", "立刻取消这个正在运行的技能。用来验证「被打断时表现是否正确」"))
				.OnClicked_Lambda([this, Row]()
				{
					if (AActor* Target = GetSelectedActor())
					{
						if (Row->AbilityClass)
						{
							UGASCombatStatics::CancelAbility(Target, Row->AbilityClass);
						}
					}
					RefreshSnapshot();
					return FReply::Handled();
				})
			]
		];
}

TSharedRef<SWidget> SGASCombatDebugPanel::MakeEffectRow(const TSharedPtr<FEffectRow>& Row)
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox).WidthOverride(ColName + 80.f).Padding(FMargin(2.f, 1.f)).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Font(RegularFont()).Text(FText::FromString(Row->Name))
			]
		]

		+ SHorizontalBox::Slot().AutoWidth()[ MakeCell(Row->StacksText, 50.f, FText::FromString(TEXT("1")), HAlign_Center) ]
		+ SHorizontalBox::Slot().AutoWidth()[ MakeCell(Row->RemainingText, 90.f, FText::GetEmpty(), HAlign_Right) ]
		+ SHorizontalBox::Slot().AutoWidth()[ MakeCell(Row->DurationText, 90.f, FText::GetEmpty(), HAlign_Right) ]

		+ SHorizontalBox::Slot().AutoWidth().Padding(2.f, 1.f).VAlign(VAlign_Center)
		[
			SNew(SButton)
			.Text(LOCTEXT("RemoveEffect", "移除"))
			.ToolTipText(LOCTEXT("RemoveEffectTip", "立刻移除此效果。用来验证驱散、以及 Buff 消失后数值与表现是否正确"))
			.OnClicked_Lambda([this, Row]()
			{
				if (AActor* Target = GetSelectedActor())
				{
					if (Row->Handle.IsValid())
					{
						UGASCombatStatics::RemoveEffect(Target, Row->Handle);
					}
				}
				RefreshSnapshot();
				return FReply::Handled();
			})
		];
}

// ============================================================================
//  数据刷新
// ============================================================================

AActor* SGASCombatDebugPanel::GetSelectedActor() const
{
	return SelectedEntry.IsValid() ? SelectedEntry->Actor.Get() : nullptr;
}

bool SGASCombatDebugPanel::IsTargetReady() const
{
	AActor* Target = GetSelectedActor();
	return Target && UGASCombatStatics::IsAbilitySystemInitialized(Target);
}

FText SGASCombatDebugPanel::GetStatusHint() const
{
	if (!GetPIEWorld())
	{
		return LOCTEXT("HintNoPIE",
			"⚠ 还没开始运行。点击关卡工具栏的「播放」进入 PIE 之后，这个面板会自动开始采集数据。\n"
			"（编辑器状态下角色还没有初始化技能系统，属性都是默认值，看起来会很奇怪，所以面板不显示。");
	}

	if (ActorEntries.Num() == 0)
	{
		return LOCTEXT("HintNoActor",
			"⚠ 当前关卡里没有找到带「技能系统组件」的 Actor。\n"
			"请给角色蓝图添加「GAS战斗桥接」组件，并在组件的「挂载的属性集」里加上「通用战斗属性集」。");
	}

	AActor* Target = GetSelectedActor();
	if (!Target)
	{
		return LOCTEXT("HintPickTarget", "请在上方下拉框里选择一个调试目标。");
	}

	if (!UGASCombatStatics::IsAbilitySystemInitialized(Target))
	{
		return FText::FromString(FString::Printf(
			TEXT("⚠ %s 的技能系统还没有初始化 —— 这种情况下技能永远激活不了（会提示 not local）。\n")
			TEXT("   请确认调用了「初始化战斗」或「初始化技能系统」，并且调用时机在 BeginPlay 之后。"),
			*Target->GetName()));
	}

	return FText::FromString(FString::Printf(
		TEXT("正在监视：%s    属性 %d 项 · 技能 %d 个 · 效果 %d 个    （%s）"),
		*Target->GetName(),
		AttributeRows.Num(), AbilityRows.Num(), EffectRows.Num(),
		bAutoRefresh ? TEXT("每 0.25 秒自动刷新") : TEXT("自动刷新已关闭")));
}

void SGASCombatDebugPanel::RefreshActorList()
{
	UWorld* World = GetPIEWorld();

	TArray<TSharedPtr<FActorEntry>> Found;
	if (World)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!IsValid(Actor) || Actor->IsActorBeingDestroyed())
			{
				continue;
			}

			// 这里用的是和蓝图节点完全相同的查找方式，保证「面板能看到的」= 「蓝图能操作的」
			if (!UGASCombatStatics::GetAbilitySystemComponent(Actor))
			{
				continue;
			}

			TSharedPtr<FActorEntry> Entry = MakeShared<FActorEntry>();
			Entry->Actor = Actor;
			Entry->Label = FString::Printf(TEXT("%s  [%s]"), *Actor->GetName(), *Actor->GetClass()->GetName());
			Found.Add(Entry);
		}
	}

	FString Signature;
	for (const TSharedPtr<FActorEntry>& Entry : Found)
	{
		Signature += Entry->Label;
		Signature += TEXT(";");
	}

	if (Signature == LastActorSignature && ActorEntries.Num() == Found.Num())
	{
		// 名单没变，但 Actor 指针可能被重建过（例如角色重生），逐项刷新一下
		for (int32 Index = 0; Index < ActorEntries.Num(); ++Index)
		{
			ActorEntries[Index]->Actor = Found[Index]->Actor;
		}
		return;
	}

	LastActorSignature = Signature;
	ActorEntries = MoveTemp(Found);

	// 原来的选中项如果还在名单里就保留，否则退回第一个
	TSharedPtr<FActorEntry> NewSelection = nullptr;
	if (SelectedEntry.IsValid())
	{
		for (const TSharedPtr<FActorEntry>& Entry : ActorEntries)
		{
			if (Entry->Actor == SelectedEntry->Actor)
			{
				NewSelection = Entry;
				break;
			}
		}
	}
	if (!NewSelection && ActorEntries.Num() > 0)
	{
		NewSelection = ActorEntries[0];
	}
	SelectedEntry = NewSelection;

	if (ActorComboBox.IsValid())
	{
		ActorComboBox->RefreshOptions();
		ActorComboBox->SetSelectedItem(SelectedEntry);
	}

	// 目标可能变了，强制重建所有区块
	LastAttributeSignature.Reset();
	LastAbilitySignature.Reset();
	LastEffectSignature.Reset();
}

void SGASCombatDebugPanel::RefreshSnapshot()
{
	AActor* Target = GetSelectedActor();

	// ------------------------------------------------------------------
	//  属性
	// ------------------------------------------------------------------
	if (AttributeContainer.IsValid())
	{
		const TArray<FGASCombatAttributeSnapshot> Snapshots = UGASCombatStatics::CaptureAllAttributes(Target);

		FString Signature;
		for (const FGASCombatAttributeSnapshot& Snapshot : Snapshots)
		{
			Signature += Snapshot.AttributeName;
			Signature += TEXT(";");
		}

		if (Signature != LastAttributeSignature)
		{
			LastAttributeSignature = Signature;
			AttributeRows.Reset();
			AttributeContainer->ClearChildren();

			for (const FGASCombatAttributeSnapshot& Snapshot : Snapshots)
			{
				TSharedPtr<FAttributeRow> Row = MakeShared<FAttributeRow>();
				Row->Attribute = Snapshot.Attribute;
				// 显示用中文名（「生命」「连招段数」）；结构签名仍用上面的英文 AttributeName，
				// 因为那才是稳定的身份标识 —— 中文名只影响显示，不影响「要不要重建控件」的判断。
				Row->Name = Snapshot.DisplayName.IsEmpty() ? Snapshot.AttributeName : Snapshot.DisplayName;
				AttributeRows.Add(Row);
				AttributeContainer->AddSlot().AutoHeight()[ MakeAttributeRow(Row) ];
			}
		}

		// 逐行更新数值。顺带把「有持续效果」的提示算出来——
		// 这是排查「数值为什么和预期不一样」时第一眼要看的东西。
		for (int32 Index = 0; Index < AttributeRows.Num() && Index < Snapshots.Num(); ++Index)
		{
			const TSharedPtr<FAttributeRow>& Row = AttributeRows[Index];
			const FGASCombatAttributeSnapshot& Snapshot = Snapshots[Index];

			Row->Attribute = Snapshot.Attribute;
			Row->CurrentValue = Snapshot.CurrentValue;
			Row->BaseValue = Snapshot.BaseValue;

			if (Row->CurrentText.IsValid())
			{
				Row->CurrentText->SetText(FormatFloat(Snapshot.CurrentValue));
			}
			if (Row->BaseText.IsValid())
			{
				Row->BaseText->SetText(FormatFloat(Snapshot.BaseValue));
			}
			if (Row->NoteText.IsValid())
			{
				const float Delta = Snapshot.CurrentValue - Snapshot.BaseValue;
				const bool bModified = !FMath::IsNearlyEqual(Delta, 0.f, 0.001f);
				Row->NoteText->SetText(bModified
					? FText::FromString(FString::Printf(TEXT("有持续效果在起作用（%+.2f）"), Delta))
					: FText::GetEmpty());
				Row->NoteText->SetColorAndOpacity(bModified
					? FLinearColor(1.0f, 0.75f, 0.3f)
					: FLinearColor::White);
			}
		}
	}

	// ------------------------------------------------------------------
	//  技能
	// ------------------------------------------------------------------
	if (AbilityContainer.IsValid())
	{
		const TArray<FGASCombatAbilityInfo> Abilities = UGASCombatStatics::GetGrantedAbilities(Target);

		FString Signature;
		for (const FGASCombatAbilityInfo& Info : Abilities)
		{
			Signature += Info.AbilityName;
			Signature += TEXT(";");
		}

		if (Signature != LastAbilitySignature)
		{
			LastAbilitySignature = Signature;
			AbilityRows.Reset();
			AbilityContainer->ClearChildren();

			for (const FGASCombatAbilityInfo& Info : Abilities)
			{
				TSharedPtr<FAbilityRow> Row = MakeShared<FAbilityRow>();
				Row->Name = Info.AbilityName;
				AbilityRows.Add(Row);
				AbilityContainer->AddSlot().AutoHeight()[ MakeAbilityRow(Row) ];
			}
		}

		for (int32 Index = 0; Index < AbilityRows.Num() && Index < Abilities.Num(); ++Index)
		{
			const TSharedPtr<FAbilityRow>& Row = AbilityRows[Index];
			const FGASCombatAbilityInfo& Info = Abilities[Index];

			// 每次都把类与句柄刷新一遍，这样「激活/取消」按钮永远指向当前这一行的真实技能
			Row->AbilityClass = Info.AbilityClass;
			Row->Handle = Info.Handle;
			Row->Level = Info.Level;
			Row->bActive = Info.bIsActive;
			Row->CooldownRemaining = Info.CooldownRemaining;
			Row->CooldownDuration = Info.CooldownDuration;

			if (Row->LevelText.IsValid())
			{
				Row->LevelText->SetText(FText::FromString(FString::FromInt(Info.Level)));
			}
			if (Row->StateText.IsValid())
			{
				Row->StateText->SetText(Info.bIsActive
					? LOCTEXT("AbilityRunning", "运行中")
					: LOCTEXT("AbilityReady", "就绪"));
				Row->StateText->SetColorAndOpacity(Info.bIsActive
					? FLinearColor(0.4f, 1.0f, 0.4f)
					: FLinearColor::White);
			}
			if (Row->CooldownText.IsValid())
			{
				Row->CooldownText->SetText(Info.CooldownRemaining > 0.f
					? FText::FromString(FString::Printf(TEXT("%.2fs"), Info.CooldownRemaining))
					: FText::GetEmpty());
			}
		}
	}

	// ------------------------------------------------------------------
	//  效果
	// ------------------------------------------------------------------
	if (EffectContainer.IsValid())
	{
		const TArray<FGASCombatActiveEffectInfo> Effects = UGASCombatStatics::GetActiveEffectInfos(Target);

		FString Signature;
		for (const FGASCombatActiveEffectInfo& Info : Effects)
		{
			Signature += Info.EffectName;
			Signature += TEXT(";");
		}

		if (Signature != LastEffectSignature)
		{
			LastEffectSignature = Signature;
			EffectRows.Reset();
			EffectContainer->ClearChildren();

			for (const FGASCombatActiveEffectInfo& Info : Effects)
			{
				TSharedPtr<FEffectRow> Row = MakeShared<FEffectRow>();
				Row->Name = Info.EffectName;
				EffectRows.Add(Row);
				EffectContainer->AddSlot().AutoHeight()[ MakeEffectRow(Row) ];
			}
		}

		for (int32 Index = 0; Index < EffectRows.Num() && Index < Effects.Num(); ++Index)
		{
			const TSharedPtr<FEffectRow>& Row = EffectRows[Index];
			const FGASCombatActiveEffectInfo& Info = Effects[Index];

			Row->Handle = Info.Handle;
			Row->StackCount = Info.StackCount;
			Row->RemainingTime = Info.RemainingTime;
			Row->TotalDuration = Info.TotalDuration;
			Row->bInfinite = Info.bIsInfinite;

			if (Row->StacksText.IsValid())
			{
				Row->StacksText->SetText(FText::FromString(FString::FromInt(Info.StackCount)));
			}
			if (Row->RemainingText.IsValid())
			{
				Row->RemainingText->SetText(Info.bIsInfinite
					? LOCTEXT("Infinite", "无限")
					: FText::FromString(FString::Printf(TEXT("%.2fs"), Info.RemainingTime)));
			}
			if (Row->DurationText.IsValid())
			{
				Row->DurationText->SetText(Info.bIsInfinite
					? FText::GetEmpty()
					: FText::FromString(FString::Printf(TEXT("%.2fs"), Info.TotalDuration)));
			}
		}
	}

	// ------------------------------------------------------------------
	//  标签
	// ------------------------------------------------------------------
	if (TagText.IsValid())
	{
		UAbilitySystemComponent* ASC = UGASCombatStatics::GetAbilitySystemComponent(Target);
		const FGameplayTagContainer Tags = UGASCombatStatics::GetAllTags(Target);

		if (!ASC || Tags.IsEmpty())
		{
			TagText->SetText(LOCTEXT("NoTagsNow", "(当前没有任何标签)"));
		}
		else
		{
			TArray<FString> Parts;
			for (const FGameplayTag& OwnedTag : Tags)
			{
				const int32 Count = ASC->GetGameplayTagCount(OwnedTag);
				Parts.Add(Count > 1
					? FString::Printf(TEXT("%s ×%d"), *OwnedTag.ToString(), Count)
					: OwnedTag.ToString());
			}
			TagText->SetText(FText::FromString(FString::Join(Parts, TEXT("   "))));
		}
	}

	// 状态提示跟着一起刷新，保证数字是同一时刻的
	if (StatusText.IsValid())
	{
		StatusText->SetText(GetStatusHint());
	}
}

#undef LOCTEXT_NAMESPACE
