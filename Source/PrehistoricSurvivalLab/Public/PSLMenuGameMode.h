#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "PSLMenuGameMode.generated.h"

class UAudioComponent;
class UPSLMainMenuWidget;

/** Game mode of L_MainMenu: no pawn, main menu widget, UI-only input with the cursor shown, camera rig as view target. */
UCLASS()
class PREHISTORICSURVIVALLAB_API APSLMenuGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    APSLMenuGameMode();

    UPSLMainMenuWidget* GetMenuWidget() const { return MenuWidget; }

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UPROPERTY(Transient)
    TObjectPtr<UPSLMainMenuWidget> MenuWidget;

    UPROPERTY(Transient)
    TObjectPtr<UAudioComponent> MenuMusic;
};
