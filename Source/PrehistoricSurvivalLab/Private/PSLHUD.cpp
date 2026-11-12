#include "PSLHUD.h"

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

    Canvas->DrawText(GEngine->GetSmallFont(), StatusText, 50.0f, 50.0f);
}
