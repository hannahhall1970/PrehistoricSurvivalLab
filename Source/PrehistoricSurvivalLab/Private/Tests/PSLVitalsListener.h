#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PSLCharacter.h"
#include "PSLVitalsListener.generated.h"

// Test-only receiver: dynamic multicast delegates need a UFUNCTION target. Records the last broadcast of each vital.
UCLASS()
class UPSLVitalsListener : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION() void OnStamina(float NewStamina) { ++StaminaCalls; LastStamina = NewStamina; }
    UFUNCTION() void OnHunger(float NewHunger) { ++HungerCalls; LastHunger = NewHunger; }
    UFUNCTION() void OnHealth(float NewHealth, float Delta) { ++HealthCalls; LastHealth = NewHealth; LastHealthDelta = Delta; }
    UFUNCTION() void OnDeath() { ++DeathCalls; }
    UFUNCTION() void OnDamage(float Amount, float NewHealth, EPSLDamageKind Kind, FVector CauserLocation, bool bHasCauser)
    {
        ++DamageCalls; LastDamage = Amount; LastDamageHealth = NewHealth; LastDamageKind = Kind; LastCauserLocation = CauserLocation; bLastHadCauser = bHasCauser;
    }

    int32 StaminaCalls = 0;
    int32 HungerCalls = 0;
    int32 HealthCalls = 0;
    int32 DeathCalls = 0;
    int32 DamageCalls = 0;
    float LastStamina = -1.0f;
    float LastHunger = -1.0f;
    float LastHealth = -1.0f;
    float LastHealthDelta = 0.0f;
    float LastDamage = 0.0f;
    float LastDamageHealth = -1.0f;
    EPSLDamageKind LastDamageKind = EPSLDamageKind::Starvation;
    FVector LastCauserLocation = FVector::ZeroVector;
    bool bLastHadCauser = false;
};
