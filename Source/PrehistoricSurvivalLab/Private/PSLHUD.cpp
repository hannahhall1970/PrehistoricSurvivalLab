#include "PSLHUD.h"

#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "PSLCharacter.h"
#include "PSLStatsComponent.h"
#include "PSLSurvivalComponent.h"

void APSLHUD::DrawHUD()
{
    Super::DrawHUD();

    const APSLCharacter* Character = Cast<APSLCharacter>(GetOwningPawn());
    if (!Character || !Canvas || !GEngine)
    {
        return;
    }

    const UPSLStatsComponent* Stats = Character->GetStatsComponent();
    const UPSLSurvivalComponent* Survival = Character->GetSurvivalComponent();
    if (!Stats || !Survival)
    {
        return;
    }

    const FString StatusText = FString::Printf(
        TEXT("Health: %.0f / %.0f\nStamina: %.0f / %.0f\nSprinting: %s%s"),
        Stats->GetHealth(),
        Stats->GetMaxHealth(),
        Survival->GetStamina(),
        Survival->GetMaxStamina(),
        Survival->IsSprinting() ? TEXT("Yes") : TEXT("No"),
        Stats->IsDead() ? TEXT("\nDEAD") : TEXT(""));

    const bool bExhausted = Survival->IsSprintLocked();
    const bool bDead = Stats->IsDead();

    constexpr float BarX = 50.0f;
    constexpr float BarWidth = 200.0f;
    constexpr float BarHeight = 14.0f;
    constexpr float HealthBarY = 110.0f;
    constexpr float StaminaBarY = 130.0f;
    const FLinearColor BackColor(0.05f, 0.05f, 0.05f, 0.7f);
    const FLinearColor HealthColor = bDead ? FLinearColor(0.4f, 0.4f, 0.4f, 1.0f) : FLinearColor(0.8f, 0.1f, 0.1f, 1.0f);
    const FLinearColor StaminaColor = bExhausted ? FLinearColor(0.9f, 0.5f, 0.0f, 1.0f) : FLinearColor(0.2f, 0.8f, 0.3f, 1.0f);

    DrawRect(BackColor, BarX, HealthBarY, BarWidth, BarHeight);
    DrawRect(HealthColor, BarX, HealthBarY, BarWidth * BarFraction(Stats->GetHealth(), Stats->GetMaxHealth()), BarHeight);
    DrawRect(BackColor, BarX, StaminaBarY, BarWidth, BarHeight);
    DrawRect(StaminaColor, BarX, StaminaBarY, BarWidth * BarFraction(Survival->GetStamina(), Survival->GetMaxStamina()), BarHeight);

    Canvas->DrawText(GEngine->GetSmallFont(), StatusText, 50.0f, 50.0f);
}

float APSLHUD::BarFraction(float Value, float Max)
{
    return Max > 0.0f ? FMath::Clamp(Value / Max, 0.0f, 1.0f) : 0.0f;
}
