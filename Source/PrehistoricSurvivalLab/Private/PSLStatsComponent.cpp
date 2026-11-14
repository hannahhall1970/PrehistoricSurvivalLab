#include "PSLStatsComponent.h"

UPSLStatsComponent::UPSLStatsComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    bWantsInitializeComponent = true;
    MaxHealth = 100.0f;
    Health = MaxHealth;
    bIsDead = false;
}

void UPSLStatsComponent::InitializeComponent()
{
    Super::InitializeComponent();
    MaxHealth = FMath::Max(MaxHealth, 1.0f);
    Health = MaxHealth;
    bIsDead = false;
}

float UPSLStatsComponent::ApplyDamage(float Amount)
{
    if (Amount <= 0.0f || bIsDead)
    {
        return 0.0f;
    }

    const float PreviousHealth = Health;
    Health = FMath::Clamp(Health - Amount, 0.0f, MaxHealth);
    const float AppliedDamage = PreviousHealth - Health;

    if (AppliedDamage > 0.0f)
    {
        OnHealthChanged.Broadcast(Health, Health - PreviousHealth);
    }

    if (Health <= 0.0f)
    {
        bIsDead = true;
        OnDeath.Broadcast();
    }

    return AppliedDamage;
}

void UPSLStatsComponent::SetHealth(float NewHealth)
{
    const float PreviousHealth = Health;
    Health = FMath::Clamp(NewHealth, 0.0f, MaxHealth);
    if (Health != PreviousHealth)
    {
        OnHealthChanged.Broadcast(Health, Health - PreviousHealth);
    }
}
