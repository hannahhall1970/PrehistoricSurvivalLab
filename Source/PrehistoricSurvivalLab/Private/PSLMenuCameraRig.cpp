#include "PSLMenuCameraRig.h"

APSLMenuCameraRig::APSLMenuCameraRig()
{
    PrimaryActorTick.bCanEverTick = true;
}

void APSLMenuCameraRig::BeginPlay()
{
    Super::BeginPlay();
    StartLocation = GetActorLocation();
}

void APSLMenuCameraRig::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    Elapsed += DeltaSeconds;
    // Cosine ease: starts at StartLocation, reaches DollyDistance at half the period, returns.
    const float Alpha = 0.5f - 0.5f * FMath::Cos(2.0f * PI * Elapsed / FMath::Max(DollyPeriod, 1.0f));
    SetActorLocation(StartLocation + GetActorForwardVector() * DollyDistance * Alpha);
}
