#include "PSLPreyCharacter.h"

#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "PSLDinoAnimInstance.h"
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
    bUsingSkeletalVisual = false;

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

    PreyMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Characters/Prey/SK_Parasaurolophus.SK_Parasaurolophus")));
    PreyIdleAnim = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Characters/Prey/A_Para_Idle.A_Para_Idle")));
    PreyWalkAnim = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Characters/Prey/A_Para_Walk.A_Para_Walk")));
    PreyRunAnim = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Characters/Prey/A_Para_Run.A_Para_Run")));
    PreyAttackAnim = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Characters/Prey/A_Para_Attack.A_Para_Attack")));
    PreyDeathAnim = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Characters/Prey/A_Para_Death.A_Para_Death")));
}

void APSLPreyCharacter::BeginPlay()
{
    Super::BeginPlay();
    ApplyPreyVisuals();
    if (StatsComponent)
    {
        StatsComponent->OnDeath.AddDynamic(this, &APSLPreyCharacter::HandleDeath);
    }
}

void APSLPreyCharacter::ApplyPreyVisuals()
{
    USkeletalMesh* Mesh = PreyMesh.LoadSynchronous();
    UAnimSequence* Idle = PreyIdleAnim.LoadSynchronous();
    UAnimSequence* Walk = PreyWalkAnim.LoadSynchronous();
    UAnimSequence* Run = PreyRunAnim.LoadSynchronous();
    if (!Mesh || !Idle || !Walk || !Run)
    {
        UE_LOG(LogTemp, Warning, TEXT("%s: prey mesh/Idle/Walk/Run missing (%s); keeping cube placeholder. Run scripts/build_lost_valley.py."),
            *GetName(), *PreyMesh.ToString());
        return;
    }

    USkeletalMeshComponent* Skel = GetMesh();
    Skel->SetSkeletalMeshAsset(Mesh);
    Skel->SetAnimationMode(EAnimationMode::AnimationBlueprint);
    Skel->SetAnimInstanceClass(UPSLDinoAnimInstance::StaticClass());
    UPSLDinoAnimInstance* Anim = Cast<UPSLDinoAnimInstance>(Skel->GetAnimInstance());
    if (!Anim)
    {
        UE_LOG(LogTemp, Warning, TEXT("%s: prey anim instance not created; keeping cube placeholder."), *GetName());
        Skel->SetSkeletalMeshAsset(nullptr);
        return;
    }
    Anim->IdleSequence = Idle;
    Anim->WalkSequence = Walk;
    Anim->RunSequence = Run;
    Anim->AttackSequence = PreyAttackAnim.LoadSynchronous();
    Anim->DeathSequence = PreyDeathAnim.LoadSynchronous();
    if (!Anim->AttackSequence || !Anim->DeathSequence)
    {
        UE_LOG(LogTemp, Warning, TEXT("%s: prey Attack/Death clips missing; those actions are skipped."), *GetName());
    }

    // Fit the mesh into the capsule: feet on the capsule bottom, height 90% of the capsule. Imported mesh faces +Y (yaw -90, like the T-Rex).
    const UCapsuleComponent* Capsule = GetCapsuleComponent();
    const float HalfHeight = Capsule->GetUnscaledCapsuleHalfHeight();
    const FBoxSphereBounds Bounds = Mesh->GetBounds();
    const float Scale = Bounds.BoxExtent.Z > KINDA_SMALL_NUMBER ? (HalfHeight * 2.0f * 0.9f) / (Bounds.BoxExtent.Z * 2.0f) : 1.0f;
    const float FootZ = (Bounds.Origin.Z - Bounds.BoxExtent.Z) * Scale;
    Skel->SetRelativeScale3D(FVector(Scale));
    Skel->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, -HalfHeight - FootZ), FRotator(0.0f, -90.0f, 0.0f));

    BodyMesh->SetVisibility(false);
    HeadMesh->SetVisibility(false);
    bUsingSkeletalVisual = true;
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
    if (bUsingSkeletalVisual)
    {
        if (UPSLDinoAnimInstance* Anim = Cast<UPSLDinoAnimInstance>(GetMesh()->GetAnimInstance()))
        {
            Anim->PlayAction(EPSLDinoAction::Death);
        }
    }
    else
    {
        BodyMesh->AddRelativeRotation(FRotator(0.0f, 0.0f, 90.0f));
    }
    UE_LOG(LogTemp, Log, TEXT("%s died"), *GetName());
}
