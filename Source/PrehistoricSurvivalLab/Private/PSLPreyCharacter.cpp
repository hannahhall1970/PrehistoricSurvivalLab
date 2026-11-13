#include "PSLPreyCharacter.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PSLPreyAIController.h"
#include "PSLStatsComponent.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

APSLPreyCharacter::APSLPreyCharacter()
{
    PrimaryActorTick.bCanEverTick = false;
    WanderSpeed = 200.0f;
    FleeSpeed = 450.0f;
    StaggerDuration = 0.25f;
    HitCount = 0;
    bStaggered = false;
    bDeathHandled = false;

    GetCapsuleComponent()->InitCapsuleSize(40.0f, 60.0f);

    StatsComponent = CreateDefaultSubobject<UPSLStatsComponent>(TEXT("StatsComponent"));
    StatsComponent->MaxHealth = 60.0f;
    StatsComponent->Health = 60.0f;

    AIControllerClass = APSLPreyAIController::StaticClass();
    AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

    bUseControllerRotationYaw = false;
    UCharacterMovementComponent* Movement = GetCharacterMovement();
    Movement->bOrientRotationToMovement = true;
    Movement->RotationRate = FRotator(0.0f, 360.0f, 0.0f);
    Movement->MaxWalkSpeed = WanderSpeed;

    // Placeholder look built from engine cubes: no binary assets needed.
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));

    BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
    BodyMesh->SetupAttachment(GetCapsuleComponent());
    BodyMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -30.0f));
    BodyMesh->SetRelativeScale3D(FVector(1.2f, 0.5f, 0.6f));
    BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    HeadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HeadMesh"));
    HeadMesh->SetupAttachment(BodyMesh);
    // Body-local units: scale 1.2/0.5/0.6 makes this a 40uu cube 84uu ahead, 24uu up.
    HeadMesh->SetRelativeLocation(FVector(0.7f, 0.0f, 0.4f));
    HeadMesh->SetRelativeScale3D(FVector(0.333f, 0.8f, 0.667f));
    HeadMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    if (CubeFinder.Succeeded())
    {
        BodyMesh->SetStaticMesh(CubeFinder.Object);
        HeadMesh->SetStaticMesh(CubeFinder.Object);
    }
}

void APSLPreyCharacter::BeginPlay()
{
    Super::BeginPlay();
    if (StatsComponent)
    {
        StatsComponent->OnDeath.AddDynamic(this, &APSLPreyCharacter::HandleDeath);
    }
}

void APSLPreyCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    GetWorldTimerManager().ClearTimer(StaggerTimer);
    Super::EndPlay(EndPlayReason);
}

float APSLPreyCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
    // Super applies engine damage modifiers and broadcasts OnTakeAnyDamage for Blueprints.
    const float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
    const float Applied = StatsComponent ? StatsComponent->ApplyDamage(ActualDamage) : 0.0f;
    if (Applied <= 0.0f)
    {
        return 0.0f;
    }

    ++HitCount;
    if (APSLPreyAIController* PreyAI = Cast<APSLPreyAIController>(GetController()))
    {
        PreyAI->NotifyDamaged(DamageCauser);
    }

    // OnDeath fires inside ApplyDamage, so a dead prey is never staggered back to life.
    if (!bDeathHandled && StaggerDuration > 0.0f)
    {
        bStaggered = true;
        GetCharacterMovement()->StopMovementImmediately();
        GetCharacterMovement()->DisableMovement();
        GetWorldTimerManager().SetTimer(StaggerTimer, this, &APSLPreyCharacter::EndStagger, StaggerDuration, false);
    }
    return Applied;
}

void APSLPreyCharacter::EndStagger()
{
    bStaggered = false;
    if (!bDeathHandled)
    {
        GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    }
}

void APSLPreyCharacter::HandleDeath()
{
    if (bDeathHandled)
    {
        return;
    }

    bDeathHandled = true;
    bStaggered = false;
    GetWorldTimerManager().ClearTimer(StaggerTimer);

    if (APSLPreyAIController* PreyAI = Cast<APSLPreyAIController>(GetController()))
    {
        PreyAI->StopAI();
    }
    GetCharacterMovement()->StopMovementImmediately();
    GetCharacterMovement()->DisableMovement();

    // The corpse must not be a bite target or block pawns.
    GetCapsuleComponent()->SetCollisionObjectType(ECC_WorldDynamic);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
    BodyMesh->AddRelativeRotation(FRotator(0.0f, 0.0f, 90.0f));
    UE_LOG(LogTemp, Log, TEXT("%s died"), *GetName());
}
