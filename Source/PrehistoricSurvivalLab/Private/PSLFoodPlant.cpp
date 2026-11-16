#include "PSLFoodPlant.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

APSLFoodPlant::APSLFoodPlant()
{
    PrimaryActorTick.bCanEverTick = false;
    FoodAmount = 30.0f;
    RegrowSeconds = 60.0f;
    bAvailable = true;

    BiteVolume = CreateDefaultSubobject<USphereComponent>(TEXT("BiteVolume"));
    BiteVolume->InitSphereRadius(80.0f);
    SetRootComponent(BiteVolume);
    // Bite sphere queries by object type, so the plant is a WorldDynamic that overlaps only; it never blocks pawns.
    BiteVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    BiteVolume->SetCollisionObjectType(ECC_WorldDynamic);
    BiteVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
    BiteVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

    PlantMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlantMesh"));
    PlantMesh->SetupAttachment(BiteVolume);
    PlantMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

float APSLFoodPlant::Consume()
{
    if (!bAvailable)
    {
        return 0.0f;
    }

    bAvailable = false;
    SetActorHiddenInGame(true);
    BiteVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GetWorldTimerManager().SetTimer(RegrowTimer, this, &APSLFoodPlant::Regrow, FMath::Max(RegrowSeconds, KINDA_SMALL_NUMBER), false);
    return FoodAmount;
}

void APSLFoodPlant::Regrow()
{
    bAvailable = true;
    SetActorHiddenInGame(false);
    BiteVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
}

void APSLFoodPlant::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    GetWorldTimerManager().ClearTimer(RegrowTimer);
    Super::EndPlay(EndPlayReason);
}
