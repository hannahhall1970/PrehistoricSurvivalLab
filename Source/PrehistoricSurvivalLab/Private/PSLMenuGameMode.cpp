#include "PSLMenuGameMode.h"

#include "Blueprint/UserWidget.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "PSLMainMenuWidget.h"
#include "PSLMenuCameraRig.h"

APSLMenuGameMode::APSLMenuGameMode()
{
    DefaultPawnClass = nullptr;
}

void APSLMenuGameMode::BeginPlay()
{
    Super::BeginPlay();

    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    if (!PC)
    {
        return;
    }

    TActorIterator<APSLMenuCameraRig> Rig(GetWorld());
    if (Rig)
    {
        PC->SetViewTarget(*Rig);
    }

    MenuWidget = CreateWidget<UPSLMainMenuWidget>(PC, UPSLMainMenuWidget::StaticClass());
    if (!MenuWidget)
    {
        UE_LOG(LogTemp, Error, TEXT("%s: failed to create UPSLMainMenuWidget"), *GetName());
        return;
    }
    MenuWidget->AddToViewport();

    FInputModeUIOnly InputMode;
    InputMode.SetWidgetToFocus(MenuWidget->TakeWidget());
    PC->SetInputMode(InputMode);
    PC->bShowMouseCursor = true;
}
