#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "PSLHUD.generated.h"

class UPSLHudWidget;

UCLASS()
class PREHISTORICSURVIVALLAB_API APSLHUD : public AHUD
{
    GENERATED_BODY()

public:
    // Value/Max clamped to [0,1]; 0 when Max <= 0.
    static float BarFraction(float Value, float Max);

    UPSLHudWidget* GetHudWidget() const { return HudWidget; }

protected:
    // Creates the UMG widget for the owning local player.
    virtual void BeginPlay() override;

private:
    UPROPERTY(Transient)
    TObjectPtr<UPSLHudWidget> HudWidget;
};
