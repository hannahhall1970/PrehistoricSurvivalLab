#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "PSLHUD.generated.h"

UCLASS()
class PREHISTORICSURVIVALLAB_API APSLHUD : public AHUD
{
    GENERATED_BODY()

public:
    virtual void DrawHUD() override;

    // Value/Max clamped to [0,1]; 0 when Max <= 0.
    static float BarFraction(float Value, float Max);
};
