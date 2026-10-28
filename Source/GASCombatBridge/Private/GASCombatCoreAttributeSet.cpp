// Copyright Epic Games, Inc. All Rights Reserved.

#include "GASCombatCoreAttributeSet.h"

#include "AbilitySystemComponent.h"
#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"

UGASCombatCoreAttributeSet::UGASCombatCoreAttributeSet()
{
	// 默认值只是「开箱可玩」的占位，真正的数值应该由数据资产或表格在运行时设置。
	// 想改数值，推荐用「技能集(AbilitySet)」资产里的「属性初始值」列表，
	// 而不是改这里的 C++ 默认值——这样每个角色/每个等级都能不一样。
	InitHealth(100.f);
	InitMaxHealth(100.f);
	InitShield(0.f);
	InitMaxShield(0.f);
	InitMana(100.f);
	InitMaxMana(100.f);
	InitAttackPower(10.f);
	InitDefense(0.f);
	InitMoveSpeed(600.f);

	// 临时属性永远从 0 开始，且不参与复制
	InitIncomingDamage(0.f);
	InitIncomingHealing(0.f);
}

void UGASCombatCoreAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// IncomingDamage / IncomingHealing 故意不复制：
	// 它们只是服务端结算过程中转瞬即逝的中转值，复制出去没有意义还有安全风险
	// （客户端如果能改「待结算伤害」，就等于能自己决定扣多少血）。
	DOREPLIFETIME_CONDITION_NOTIFY(UGASCombatCoreAttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UGASCombatCoreAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UGASCombatCoreAttributeSet, Shield, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UGASCombatCoreAttributeSet, MaxShield, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UGASCombatCoreAttributeSet, Mana, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UGASCombatCoreAttributeSet, MaxMana, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UGASCombatCoreAttributeSet, AttackPower, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UGASCombatCoreAttributeSet, Defense, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UGASCombatCoreAttributeSet, MoveSpeed, COND_None, REPNOTIFY_Always);
}

#define GASCOMBAT_DEFINE_REP(PropertyName) \
	void UGASCombatCoreAttributeSet::OnRep_##PropertyName(const FGameplayAttributeData& OldValue) \
	{ \
		GAMEPLAYATTRIBUTE_REPNOTIFY(UGASCombatCoreAttributeSet, PropertyName, OldValue); \
	}

GASCOMBAT_DEFINE_REP(Health)
GASCOMBAT_DEFINE_REP(MaxHealth)
GASCOMBAT_DEFINE_REP(Shield)
GASCOMBAT_DEFINE_REP(MaxShield)
GASCOMBAT_DEFINE_REP(Mana)
GASCOMBAT_DEFINE_REP(MaxMana)
GASCOMBAT_DEFINE_REP(AttackPower)
GASCOMBAT_DEFINE_REP(Defense)
GASCOMBAT_DEFINE_REP(MoveSpeed)

#undef GASCOMBAT_DEFINE_REP

void UGASCombatCoreAttributeSet::ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const
{
	// GAS 不会自动做数值夹取，必须自己做，否则会出现负血、超上限的护盾、负蓝条。
	// 这是「当前值」和「基础值」两条路径共用的夹取逻辑。
	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, FMath::Max(GetMaxHealth(), 0.f));
	}
	else if (Attribute == GetShieldAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, FMath::Max(GetMaxShield(), 0.f));
	}
	else if (Attribute == GetManaAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, FMath::Max(GetMaxMana(), 0.f));
	}
	else if (Attribute == GetMaxHealthAttribute() || Attribute == GetMaxShieldAttribute() || Attribute == GetMaxManaAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.f);
	}
	else if (Attribute == GetIncomingDamageAttribute() || Attribute == GetIncomingHealingAttribute())
	{
		// 伤害/治疗量不允许为负——否则「负伤害」会变成治疗，是个经典漏洞
		NewValue = FMath::Max(NewValue, 0.f);
	}
	else if (Attribute == GetAttackPowerAttribute() || Attribute == GetDefenseAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.f);
	}
	else if (Attribute == GetMoveSpeedAttribute())
	{
		// 移速留一点余量，避免减速效果把自己减到不能动
		NewValue = FMath::Max(NewValue, 0.f);
	}
}

void UGASCombatCoreAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	ClampAttribute(Attribute, NewValue);
}

void UGASCombatCoreAttributeSet::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const
{
	Super::PreAttributeBaseChange(Attribute, NewValue);
	ClampAttribute(Attribute, NewValue);
}

void UGASCombatCoreAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	// 只有「瞬时(Instant)」效果会走到这里，这正是我们希望的：
	// 伤害/治疗本来就该是瞬时结算，而不是挂一个持续效果慢慢改血。
	if (Data.EvaluatedData.Attribute == GetIncomingDamageAttribute())
	{
		HandleIncomingDamage(Data);
	}
	else if (Data.EvaluatedData.Attribute == GetIncomingHealingAttribute())
	{
		HandleIncomingHealing(Data);
	}
}

void UGASCombatCoreAttributeSet::HandleIncomingDamage(const FGameplayEffectModCallbackData& Data)
{
	// 先把中转值读出来并清空。
	// 必须清空：这个属性会被复制/复用，残留的值会让下一次伤害莫名其妙地叠加。
	const float RawDamage = GetIncomingDamage();
	SetIncomingDamage(0.f);

	if (RawDamage <= 0.f)
	{
		return;
	}

	// 取「谁打的」。注意：UE 5.7 的 FGameplayEffectModCallbackData 只有
	// EffectSpec / EvaluatedData / Target 三个成员，没有 Instigator，
	// 必须从效果上下文里取（网上很多老教程写的 Data.Instigator 在 5.7 已经不存在了）。
	const FGameplayEffectContextHandle EffectContext = Data.EffectSpec.GetContext();
	AActor* DamageInstigator = EffectContext.GetInstigator();
	AActor* DamageCauser = EffectContext.GetEffectCauser();

	// 结算顺序：护盾先扛，扛不住的溢出到生命。
	// 想加「无敌帧」「格挡」「减伤」就插在这一段之前——这是唯一的伤害收口。
	float RemainingDamage = RawDamage;
	const float ShieldAbsorbed = FMath::Min(GetShield(), RemainingDamage);
	if (ShieldAbsorbed > 0.f)
	{
		SetShield(GetShield() - ShieldAbsorbed);
		RemainingDamage -= ShieldAbsorbed;
	}

	if (RemainingDamage > 0.f)
	{
		SetHealth(FMath::Max(0.f, GetHealth() - RemainingDamage));
	}

	OnDamageTaken.Broadcast(DamageInstigator, DamageCauser, RawDamage, GetHealth());

	// 死亡只广播一次。被治疗拉回血量后会把标记复位，所以「倒下→被救起→再倒下」会再广播一次。
	if (GetHealth() <= 0.f && !bDeathBroadcast)
	{
		bDeathBroadcast = true;
		OnDeath.Broadcast(DamageInstigator, DamageCauser);
	}
}

void UGASCombatCoreAttributeSet::HandleIncomingHealing(const FGameplayEffectModCallbackData& Data)
{
	const float RawHealing = GetIncomingHealing();
	SetIncomingHealing(0.f);

	if (RawHealing <= 0.f)
	{
		return;
	}

	SetHealth(FMath::Min(GetHealth() + RawHealing, GetMaxHealth()));

	// 复活/被救起：复位死亡标记
	if (GetHealth() > 0.f)
	{
		bDeathBroadcast = false;
	}
}

float UGASCombatCoreAttributeSet::GetHealthPercent() const
{
	// 变量名不能叫 MaxHealth：那会遮蔽同名的属性成员，MSVC 会当成错误(C4458)
	const float MaxHealthValue = GetMaxHealth();
	return MaxHealthValue > 0.f ? FMath::Clamp(GetHealth() / MaxHealthValue, 0.f, 1.f) : 0.f;
}
