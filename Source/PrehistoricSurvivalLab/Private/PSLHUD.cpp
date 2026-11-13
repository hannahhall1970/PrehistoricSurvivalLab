#include "PSLHUD.h"

#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"
#include "PSLHudWidget.h"

void APSLHUD::BeginPlay()
{
    Super::BeginPlay();

    APlayerController* PC = GetOwningPlayerController();
    if (!PC || !PC->IsLocalController())
    {
        return;
    }

    HudWidget = CreateWidget<UPSLHudWidget>(PC, UPSLHudWidget::StaticClass());
    if (!HudWidget)
    {
        UE_LOG(LogTemp, Error, TEXT("%s: failed to create UPSLHudWidget"), *GetName());
        return;
    }
    HudWidget->AddToViewport();
}

float APSLHUD::BarFraction(float Value, float Max)
{
    return Max > 0.0f ? FMath::Clamp(Value / Max, 0.0f, 1.0f) : 0.0f;
}
