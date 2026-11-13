#include "PSLFoodPlant.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

APSLFoodPlant::APSLFoodPlant()
{
    PrimaryActorTick.bCanEverTick = false;
    FoodAmount = 30.0f;
    RegrowSeconds = 60.0f;
    bAvailable = true;

    PlantMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlantMesh"));
    SetRootComponent(PlantMesh);
    // Bite sphere queries by object type, so the plant is a WorldDynamic that overlaps only; it never blocks pawns.
    PlantMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    PlantMesh->SetCollisionObjectType(ECC_WorldDynamic);
    PlantMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
    PlantMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
}

float APSLFoodPlant::Consume()
{
    if (!bAvailable)
    {
        return 0.0f;
    }

    bAvailable = false;
    SetActorHiddenInGame(true);
    PlantMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GetWorldTimerManager().SetTimer(RegrowTimer, this, &APSLFoodPlant::Regrow, FMath::Max(RegrowSeconds, KINDA_SMALL_NUMBER), false);
    return FoodAmount;
}

void APSLFoodPlant::Regrow()
{
    bAvailable = true;
    SetActorHiddenInGame(false);
    PlantMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
}

void APSLFoodPlant::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    GetWorldTimerManager().ClearTimer(RegrowTimer);
    Super::EndPlay(EndPlayReason);
}
