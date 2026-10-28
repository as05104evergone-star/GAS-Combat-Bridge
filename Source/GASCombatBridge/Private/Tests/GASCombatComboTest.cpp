// Copyright Epic Games, Inc. All Rights Reserved.

// ============================================================================
//  R1「连招属性集」的自动化验收测试
//
//  为什么要有这个文件？
//    连招段数的规则里有一条**极易被"顺手改坏"**的约定：属性集只守下界、上界刻意不钳。
//    如果哪天有人把它改成 FMath::Clamp(NewValue, 0, MaxComboIndex)，
//    编译一样通过、面板上看着也正常，但连招会**永久卡在第三段**再也接不出第一段 ——
//    这种回归靠人是看不出来的，必须由测试挡住。
//
//  怎么跑（关掉编辑器后执行）：
//    UnrealEditor-Cmd.exe <工程>.uproject -unattended -nullrhi -nosplash -NoSound -nopause ^
//      -ExecCmds="Automation RunTests GASCombat" -TestExit="Automation Test Queue Empty" ^
//      -ReportExportPath="<输出目录>" -log
//
//  整个文件被 WITH_DEV_AUTOMATION_TESTS 包住：发行版构建里会被完全移除，不进包体。
// ============================================================================

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameplayAbilitiesDeveloperSettings.h"
#include "GameplayEffect.h"
#include "GASCombatComboAttributeSet.h"
#include "GASCombatCoreAttributeSet.h"
#include "GASCombatStatics.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

namespace GASCombatComboTest
{
	/** 建一个最小可用的「世界 + 角色 + ASC + 连招属性集」，测完拆掉 */
	struct FFix
	{
		UWorld* World = nullptr;
		AActor* Avatar = nullptr;
		UAbilitySystemComponent* ASC = nullptr;

		bool Setup(FAutomationTestBase& Test)
		{
			World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
			if (!World)
			{
				Test.AddError(TEXT("无法创建测试世界"));
				return false;
			}

			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Avatar = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
			if (!Avatar)
			{
				Test.AddError(TEXT("无法生成测试角色"));
				return false;
			}

			ASC = NewObject<UAbilitySystemComponent>(Avatar, TEXT("GASCombatTestASC"));
			if (!ASC)
			{
				Test.AddError(TEXT("无法创建技能系统组件"));
				return false;
			}
			ASC->RegisterComponent();
			Avatar->AddInstanceComponent(ASC);

			// 属性集必须在 InitAbilityActorInfo 之前/之后都能挂，这里按正常流程走
			ASC->InitAbilityActorInfo(Avatar, Avatar);

			// 用插件自己的节点挂属性集 —— 顺带验证「添加属性集」这条路径
			UAttributeSet* Added = UGASCombatStatics::AddAttributeSet(Avatar, UGASCombatComboAttributeSet::StaticClass());
			if (!Added)
			{
				Test.AddError(TEXT("「添加属性集」没能挂上连招属性集"));
				return false;
			}

			return true;
		}

		void Teardown()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
				World = nullptr;
			}
		}

		float GetCombo() const
		{
			return ASC->GetNumericAttribute(UGASCombatComboAttributeSet::GetComboIndexAttribute());
		}

		float GetMaxCombo() const
		{
			return ASC->GetNumericAttribute(UGASCombatComboAttributeSet::GetMaxComboIndexAttribute());
		}

		/** 直接设基础值（相当于调试面板里手改） */
		void SetComboBase(float Value) const
		{
			ASC->SetNumericAttributeBase(UGASCombatComboAttributeSet::GetComboIndexAttribute(), Value);
		}
	};

	/** 造一个和教程里 GE_ComboAdvance 配置等价的瞬时效果：Add + SetByCaller(Data.Combo.Advance) */
	UGameplayEffect* MakeComboAdvanceEffect(const FGameplayTag& AdvanceTag)
	{
		UGameplayEffect* Effect = NewObject<UGameplayEffect>(GetTransientPackage(), NAME_None, RF_Transient);
		Effect->DurationPolicy = EGameplayEffectDurationType::Instant;

		FSetByCallerFloat SetByCaller;
		SetByCaller.DataTag = AdvanceTag;

		FGameplayModifierInfo Modifier;
		Modifier.Attribute = UGASCombatComboAttributeSet::GetComboIndexAttribute();
		Modifier.ModifierOp = EGameplayModOp::Additive;
		Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);

		Effect->Modifiers.Add(Modifier);
		return Effect;
	}
}

// ---------------------------------------------------------------------------
//  测试：连招段数管线
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGASCombatComboPipelineTest,
	"GASCombat.R1.ComboAttributeSetPipeline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGASCombatComboPipelineTest::RunTest(const FString& Parameters)
{
	using namespace GASCombatComboTest;

	FFix Fix;
	if (!Fix.Setup(*this))
	{
		Fix.Teardown();
		return false;
	}

	const FGameplayAttribute ComboAttr = UGASCombatComboAttributeSet::GetComboIndexAttribute();
	const FGameplayAttribute MaxComboAttr = UGASCombatComboAttributeSet::GetMaxComboIndexAttribute();

	// -----------------------------------------------------------------------
	// 1. 属性集挂上了、能取到
	// -----------------------------------------------------------------------
	TestNotNull<UGASCombatComboAttributeSet>(TEXT("「获取连招属性集」应返回实例"),
		UGASCombatStatics::GetComboAttributeSet(Fix.Avatar));

	// -----------------------------------------------------------------------
	// 2. 默认值
	// -----------------------------------------------------------------------
	TestEqual(TEXT("ComboIndex 默认应为 0（不在连招中）"), Fix.GetCombo(), 0.f);
	TestEqual(TEXT("MaxComboIndex 默认应为 3"), Fix.GetMaxCombo(), 3.f);

	// -----------------------------------------------------------------------
	// 3. 下界钳制：段数不会变成负数
	// -----------------------------------------------------------------------
	Fix.SetComboBase(1.f);
	Fix.ASC->ApplyModToAttribute(ComboAttr, EGameplayModOp::Additive, -5.f);
	TestEqual(TEXT("段数 1 传 -5 应被钳到 0，不出现负段数"), Fix.GetCombo(), 0.f);

	// -----------------------------------------------------------------------
	// 4. 【最关键】绕回语义：上界刻意不钳
	//
	//    打满第 3 段后靠「增量 -2」绕回第 1 段（Next=1, Current=3）。
	//    如果哪天有人把钳制改成 Clamp(0, MaxComboIndex)，
	//    3 + (-2) = 1 会被"算"成没问题，但 3 + 1 = 3 会被夹住 ——
	//    于是连招永久停在第三段。下面两条一起断言，把两种改坏方式都挡住。
	// -----------------------------------------------------------------------
	Fix.SetComboBase(3.f);
	Fix.ASC->ApplyModToAttribute(ComboAttr, EGameplayModOp::Additive, -2.f);
	TestEqual(TEXT("【绕回】段数 3 传 -2 应得到 1（若这里得到 3 或 0，说明上界被钳或下界过严）"),
		Fix.GetCombo(), 1.f);

	// 反向证明：3 传 +1 得到 4，而不是被夹成 3。
	// 这一条正是「上界不钳」的直接证据 —— 绕回是能力的增量计算，不是属性集的夹取。
	Fix.SetComboBase(3.f);
	Fix.ASC->ApplyModToAttribute(ComboAttr, EGameplayModOp::Additive, 1.f);
	TestEqual(TEXT("【上界不钳】段数 3 传 +1 应得到 4（属性集刻意不做上界夹取）"),
		Fix.GetCombo(), 4.f);

	// -----------------------------------------------------------------------
	// 5. MaxComboIndex 至少为 1（0 段没有玩法含义，且会让绕回判定除零）
	// -----------------------------------------------------------------------
	Fix.ASC->SetNumericAttributeBase(MaxComboAttr, 0.f);
	TestEqual(TEXT("MaxComboIndex 传 0 应被钳到 1"), Fix.GetMaxCombo(), 1.f);

	Fix.ASC->SetNumericAttributeBase(MaxComboAttr, -3.f);
	TestEqual(TEXT("MaxComboIndex 传 -3 应被钳到 1"), Fix.GetMaxCombo(), 1.f);

	// -----------------------------------------------------------------------
	// 6. 数值传递标签已注册（GE 的 SetByCaller 键就靠它）
	// -----------------------------------------------------------------------
	const FGameplayTag AdvanceTag = FGameplayTag::RequestGameplayTag(TEXT("Data.Combo.Advance"), /*ErrorIfNotFound=*/false);
	TestTrue(TEXT("标签 Data.Combo.Advance 应已注册（由插件模块启动时原生注册）"), AdvanceTag.IsValid());

	// -----------------------------------------------------------------------
	// 7. 走真实 GameplayEffect 的完整链路
	//    配置与教程的 GE_ComboAdvance 等价：Instant + Add + SetByCaller
	//    依次传 +1 / +1 / +1 / -2 / -1，期望 1 / 2 / 3 / 1 / 0
	// -----------------------------------------------------------------------
	if (AdvanceTag.IsValid())
	{
		UGameplayEffect* ComboAdvance = MakeComboAdvanceEffect(AdvanceTag);
		Fix.SetComboBase(0.f);

		struct FStep { float Delta; float Expect; const TCHAR* Note; };
		const FStep Steps[] =
		{
			{ +1.f, 1.f, TEXT("起手：0 → 1") },
			{ +1.f, 2.f, TEXT("推进：1 → 2") },
			{ +1.f, 3.f, TEXT("推进：2 → 3（打满）") },
			{ -2.f, 1.f, TEXT("【绕回】3 → 1") },
			{ -1.f, 0.f, TEXT("收尾：1 → 0（归零）") },
		};

		for (const FStep& Step : Steps)
		{
			FGameplayEffectContextHandle Context = Fix.ASC->MakeEffectContext();
			FGameplayEffectSpec Spec(ComboAdvance, Context, 1.f);
			Spec.SetSetByCallerMagnitude(AdvanceTag, Step.Delta);
			Fix.ASC->ApplyGameplayEffectSpecToSelf(Spec);

			TestEqual(FString::Printf(TEXT("GE_ComboAdvance 传 %+.0f → %s"), Step.Delta, Step.Note),
				Fix.GetCombo(), Step.Expect);
		}

		// 归零后继续减，仍不能出现负段数
		{
			FGameplayEffectContextHandle Context = Fix.ASC->MakeEffectContext();
			FGameplayEffectSpec Spec(ComboAdvance, Context, 1.f);
			Spec.SetSetByCallerMagnitude(AdvanceTag, -5.f);
			Fix.ASC->ApplyGameplayEffectSpecToSelf(Spec);
			TestEqual(TEXT("GE_ComboAdvance 在 0 段时传 -5 仍应为 0"), Fix.GetCombo(), 0.f);
		}
	}

	// -----------------------------------------------------------------------
	// 8. 中文名映射（调试面板与状态报告显示的就是它）
	// -----------------------------------------------------------------------
	TestEqual(TEXT("ComboIndex 的中文名应为「连招段数」"),
		UGASCombatStatics::GetAttributeDisplayName(TEXT("ComboIndex")), FString(TEXT("连招段数")));
	TestEqual(TEXT("MaxComboIndex 的中文名应为「连招段数上限」"),
		UGASCombatStatics::GetAttributeDisplayName(TEXT("MaxComboIndex")), FString(TEXT("连招段数上限")));

	// -----------------------------------------------------------------------
	// 9. 采集全部属性时两套属性集都在，且带中文名
	// -----------------------------------------------------------------------
	{
		// 再挂一套通用战斗属性集，验证两套可以共存（R1 验收点之一）
		UGASCombatStatics::AddAttributeSet(Fix.Avatar, UGASCombatCoreAttributeSet::StaticClass());

		const TArray<FGASCombatAttributeSnapshot> Snapshots = UGASCombatStatics::CaptureAllAttributes(Fix.Avatar);

		bool bFoundCombo = false;
		bool bFoundHealth = false;
		for (const FGASCombatAttributeSnapshot& Snapshot : Snapshots)
		{
			if (Snapshot.AttributeName == TEXT("ComboIndex"))
			{
				bFoundCombo = true;
				TestEqual(TEXT("快照里 ComboIndex 的中文名应为「连招段数」"),
					Snapshot.DisplayName, FString(TEXT("连招段数")));
			}
			if (Snapshot.AttributeName == TEXT("Health"))
			{
				bFoundHealth = true;
			}
		}

		TestTrue(TEXT("两套属性集同时挂载时，快照里应有连招段数"), bFoundCombo);
		TestTrue(TEXT("两套属性集同时挂载时，快照里应有生命"), bFoundHealth);
	}

	Fix.Teardown();
	return true;
}

// ---------------------------------------------------------------------------
//  测试：属性集类必须是「原生 C++ 类」
//
//  这是整个 R1 存在的前提：引擎的 FGameplayAttribute::GetAllAttributeProperties
//  会把蓝图生成的类过滤掉（!Class->ClassGeneratedBy），所以属性集必须是 C++ 类。
//  如果哪天有人把 UGASCombatComboAttributeSet 变成蓝图类，这条会立刻失败。
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGASCombatComboNativeClassTest,
	"GASCombat.R1.AttributeSetIsNative",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGASCombatComboNativeClassTest::RunTest(const FString& Parameters)
{
	const UClass* ComboClass = UGASCombatComboAttributeSet::StaticClass();
	TestNotNull(TEXT("连招属性集类应存在"), ComboClass);

	if (ComboClass)
	{
		// ClassGeneratedBy 只在带编辑器数据的构建里存在（WITH_EDITORONLY_DATA）。
		// 引擎那条过滤（!Class->ClassGeneratedBy）本身也是 WITH_EDITORONLY_DATA 包着的，
		// 所以这里用同样的条件包住，两种构建下都是对的。
#if WITH_EDITORONLY_DATA
		TestTrue(TEXT("连招属性集必须是原生 C++ 类（ClassGeneratedBy 为空），否则不会出现在 GE 的属性下拉框里"),
			ComboClass->ClassGeneratedBy == nullptr);
#endif

		// 引擎收集属性时要求父类是 UAttributeSet
		TestTrue(TEXT("连招属性集应继承自 UAttributeSet"),
			ComboClass->IsChildOf(UAttributeSet::StaticClass()));
	}

	// 两个属性都必须能被引擎的「可修饰属性」收集函数看到
	TArray<FProperty*> Properties;
	FGameplayAttribute::GetAllAttributeProperties(Properties);

	bool bFoundCombo = false;
	bool bFoundMaxCombo = false;
	for (const FProperty* Property : Properties)
	{
		if (!Property || Property->GetOwnerClass() != ComboClass)
		{
			continue;
		}
		if (Property->GetFName() == TEXT("ComboIndex"))
		{
			bFoundCombo = true;
		}
		if (Property->GetFName() == TEXT("MaxComboIndex"))
		{
			bFoundMaxCombo = true;
		}
	}

	TestTrue(TEXT("引擎的 GetAllAttributeProperties（= GE 属性下拉框的数据源）应包含 ComboIndex"), bFoundCombo);
	TestTrue(TEXT("引擎的 GetAllAttributeProperties（= GE 属性下拉框的数据源）应包含 MaxComboIndex"), bFoundMaxCombo);

	return true;
}

// ---------------------------------------------------------------------------
//  测试：插件自带标签与「激活失败原因」配置
//
//  这两个东西以前是靠插件 Config 目录里的 ini 提供的，但那条路**根本不通**
//  （插件的 Config 不会被并进引擎 GameplayTags 模块的配置层级，且没有任何报错）。
//  现在改成 C++ 原生注册。这个测试就是防止它再悄悄退化 ——
//  标签没注册的症状是「SetByCaller 键选不到」「失败原因永远是空的」，全都不报错。
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGASCombatTagsAndSettingsTest,
	"GASCombat.R1.TagsAndActivateFailSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGASCombatTagsAndSettingsTest::RunTest(const FString& Parameters)
{
	// --- 自带标签必须全部存在 ---
	static const TCHAR* RequiredTags[] =
	{
		TEXT("Data.Damage"),
		TEXT("Data.Healing"),
		TEXT("Data.Duration"),
		TEXT("Data.Combo.Advance"),
		TEXT("Activation.Fail.CanActivateAbility"),
		TEXT("Activation.Fail.CantAffordCost"),
		TEXT("Activation.Fail.OnCooldown"),
		TEXT("Activation.Fail.BlockedByTags"),
		TEXT("Activation.Fail.MissingTags"),
		TEXT("Activation.Fail.Networking"),
		TEXT("Activation.Fail.IsDead"),
		TEXT("State.Dead"),
		TEXT("State.Stunned"),
		TEXT("State.Invulnerable"),
		TEXT("State.Attacking"),
		TEXT("Cooldown.Skill"),
	};

	for (const TCHAR* TagName : RequiredTags)
	{
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TagName), /*ErrorIfNotFound=*/false);
		TestTrue(FString::Printf(TEXT("插件自带标签 %s 应已注册"), TagName), Tag.IsValid());
	}

	// --- 「激活失败原因」必须接上 ---
	// 引擎运行时真正读的是 UAbilitySystemGlobals 上的这几个成员
	// （见 GameplayAbility.cpp 里的 UAbilitySystemGlobals::Get().ActivateFailCostTag）。
	const UAbilitySystemGlobals& Globals = UAbilitySystemGlobals::Get();

	TestTrue(TEXT("ActivateFailCostTag 应已接上（否则「消耗不足」不会上报原因）"),
		Globals.ActivateFailCostTag.IsValid());
	TestTrue(TEXT("ActivateFailCooldownTag 应已接上（否则「冷却中」不会上报原因）"),
		Globals.ActivateFailCooldownTag.IsValid());
	TestTrue(TEXT("ActivateFailTagsBlockedTag 应已接上（否则「被状态阻止」不会上报原因）"),
		Globals.ActivateFailTagsBlockedTag.IsValid());
	TestTrue(TEXT("ActivateFailTagsMissingTag 应已接上（否则「缺少所需标签」不会上报原因）"),
		Globals.ActivateFailTagsMissingTag.IsValid());
	TestTrue(TEXT("ActivateFailNetworkingTag 应已接上（否则「网络权限」不会上报原因）"),
		Globals.ActivateFailNetworkingTag.IsValid());

	// 项目设置界面显示的是 DeveloperSettings，也要一致
	const UGameplayAbilitiesDeveloperSettings* Settings = GetDefault<UGameplayAbilitiesDeveloperSettings>();
	TestNotNull(TEXT("UGameplayAbilitiesDeveloperSettings 应可访问"), Settings);
	if (Settings)
	{
		TestTrue(TEXT("项目设置里的 ActivateFailCostTag 应已接上"),
			Settings->ActivateFailCostTag.IsValid());
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
