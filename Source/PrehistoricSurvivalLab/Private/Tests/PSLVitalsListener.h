#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
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

    int32 StaminaCalls = 0;
    int32 HungerCalls = 0;
    int32 HealthCalls = 0;
    int32 DeathCalls = 0;
    float LastStamina = -1.0f;
    float LastHunger = -1.0f;
    float LastHealth = -1.0f;
    float LastHealthDelta = 0.0f;
};
