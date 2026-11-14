#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraActor.h"
#include "PSLMenuCameraRig.generated.h"

/** Main-menu camera with a slow back-and-forth dolly along its own forward axis. The menu game mode makes it the view target. */
UCLASS()
class PREHISTORICSURVIVALLAB_API APSLMenuCameraRig : public ACameraActor
{
    GENERATED_BODY()

public:
    APSLMenuCameraRig();

    virtual void Tick(float DeltaSeconds) override;

    // Total travel (uu) and one full back-and-forth period (s).
    UPROPERTY(EditAnywhere, Category="Dolly")
    float DollyDistance = 120.0f;

    UPROPERTY(EditAnywhere, Category="Dolly", meta=(ClampMin="1.0"))
    float DollyPeriod = 24.0f;

protected:
    virtual void BeginPlay() override;

private:
    FVector StartLocation = FVector::ZeroVector;
    float Elapsed = 0.0f;
};
