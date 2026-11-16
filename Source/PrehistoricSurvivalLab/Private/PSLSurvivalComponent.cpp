#include "PSLSurvivalComponent.h"

#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "PSLAudio.h"
#include "PSLStatsComponent.h"
#include "Sound/SoundBase.h"

UPSLSurvivalComponent::UPSLSurvivalComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    MaxStamina = 100.0f;
    SprintDrainPerSecond = 20.0f;
    StaminaRegenPerSecond = 12.0f;
    Stamina = MaxStamina;
    bSprintRequested = false;
    bSprintLocked = false;
    bIsSprinting = false;
    MaxHunger = 100.0f;
    Hunger = MaxHunger;
    HungerDrainPerSecond = 0.15f;
    StarveDamagePerSecond = 4.0f;
}

void UPSLSurvivalComponent::BeginPlay()
{
    Super::BeginPlay();
    ResetVitalsForPlay();
}

void UPSLSurvivalComponent::ResetVitalsForPlay()
{
    MaxStamina = FMath::Max(MaxStamina, 1.0f);
    MaxHunger = FMath::Max(MaxHunger, 1.0f);
    if (bStateRestored)
    {
        return;
    }
    Stamina = MaxStamina;
    Hunger = MaxHunger;
}

void UPSLSurvivalComponent::RestoreState(float InStamina, float InHunger)
{
    MaxStamina = FMath::Max(MaxStamina, 1.0f);
    MaxHunger = FMath::Max(MaxHunger, 1.0f);
    Stamina = FMath::Clamp(InStamina, 0.0f, MaxStamina);
    Hunger = FMath::Clamp(InHunger, 0.0f, MaxHunger);
    OnStaminaChanged.Broadcast(Stamina);
    OnHungerChanged.Broadcast(Hunger);
    bStateRestored = true;
}

void UPSLSurvivalComponent::SetSprintRequested(bool bRequested)
{
    bSprintRequested = bRequested;
    if (!bRequested)
    {
        bSprintLocked = false;
    }
}

void UPSLSurvivalComponent::UpdateStamina(float DeltaSeconds, bool bIsMoving)
{
    if (DeltaSeconds <= 0.0f)
    {
        return;
    }

    const bool bWasSprinting = bIsSprinting;
    bIsSprinting = bSprintRequested && bIsMoving && Stamina > 0.0f && !bSprintLocked;

    const float PreviousStamina = Stamina;
    if (bIsSprinting)
    {
        Stamina = FMath::Max(0.0f, Stamina - SprintDrainPerSecond * DeltaSeconds);
        if (Stamina <= 0.0f)
        {
            bSprintLocked = true;
            bIsSprinting = false;
        }
    }
    else
    {
        const float Regen = IsStarving() ? StaminaRegenPerSecond * 0.5f : StaminaRegenPerSecond;
        Stamina = FMath::Min(MaxStamina, Stamina + Regen * DeltaSeconds);
    }

    if (!FMath::IsNearlyEqual(PreviousStamina, Stamina))
    {
        OnStaminaChanged.Broadcast(Stamina);
    }

    if (bWasSprinting != bIsSprinting)
    {
        OnSprintStateChanged.Broadcast(bIsSprinting);
    }
}

void UPSLSurvivalComponent::UpdateHunger(float DeltaSeconds)
{
    UPSLStatsComponent* Stats = GetOwner() ? GetOwner()->FindComponentByClass<UPSLStatsComponent>() : nullptr;
    if (DeltaSeconds <= 0.0f || (Stats && Stats->IsDead()))
    {
        return;
    }

    const float Previous = Hunger;
    Hunger = FMath::Max(0.0f, Hunger - HungerDrainPerSecond * (bIsSprinting ? 2.0f : 1.0f) * DeltaSeconds);
    // Starving cue on the edge into Hunger 0 only; needs a world (transient test components have none).
    if (Previous > 0.0f && Hunger <= 0.0f && GetOwner() && GetWorld())
    {
        if (USoundBase* S = PSLAudio::Load(PSLAudio::Starving))
        {
            UGameplayStatics::PlaySoundAtLocation(GetWorld(), S, GetOwner()->GetActorLocation());
        }
    }
    if (!FMath::IsNearlyEqual(Previous, Hunger))
    {
        OnHungerChanged.Broadcast(Hunger);
    }
    if (IsStarving() && Stats)
    {
        bStarvationDamageDealt = true;
        Stats->ApplyDamage(StarveDamagePerSecond * DeltaSeconds);
    }
}

void UPSLSurvivalComponent::AddFood(float Amount)
{
    if (Amount <= 0.0f)
    {
        return;
    }
    const float Previous = Hunger;
    Hunger = FMath::Min(MaxHunger, Hunger + Amount);
    if (!FMath::IsNearlyEqual(Previous, Hunger))
    {
        OnHungerChanged.Broadcast(Hunger);
    }
}

void UPSLSurvivalComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    const AActor* Owner = GetOwner();
    const bool bIsMoving = Owner && Owner->GetVelocity().Size2D() > KINDA_SMALL_NUMBER;
    UpdateStamina(DeltaTime, bIsMoving);
    UpdateHunger(DeltaTime);
}
