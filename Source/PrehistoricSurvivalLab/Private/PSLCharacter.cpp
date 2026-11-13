#include "PSLCharacter.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "PSLInputDefaults.h"
#include "InputActionValue.h"
#include "PSLStatsComponent.h"
#include "PSLSurvivalComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "PSLDinoAnimInstance.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

APSLCharacter::APSLCharacter()
{
    PrimaryActorTick.bCanEverTick = false;
    WalkSpeed = 300.0f;
    SprintSpeed = 600.0f;
    bDeathHandled = false;
    BiteCooldown = 1.0f;
    BiteHitDelay = 0.35f;
    BiteReach = 220.0f;
    BiteRadius = 120.0f;
    BiteDamage = 25.0f;
    BiteCount = 0;
    LastBiteHitCount = 0;
    LastBiteTime = TNumericLimits<double>::Lowest() / 2.0;

    StatsComponent = CreateDefaultSubobject<UPSLStatsComponent>(TEXT("StatsComponent"));
    SurvivalComponent = CreateDefaultSubobject<UPSLSurvivalComponent>(TEXT("SurvivalComponent"));

    USpringArmComponent* SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    SpringArm->SetupAttachment(RootComponent);
    SpringArm->TargetArmLength = 300.0f;
    SpringArm->bUsePawnControlRotation = true;

    UCameraComponent* Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
    Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
    Camera->bUsePawnControlRotation = false;

    bUseControllerRotationPitch = false;
    bUseControllerRotationYaw = false;
    bUseControllerRotationRoll = false;
    GetCharacterMovement()->bOrientRotationToMovement = true;
    GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;

    GetCapsuleComponent()->InitCapsuleSize(DinoCapsuleRadius, DinoCapsuleHalfHeight);
    GetMesh()->SetRelativeLocationAndRotation(
        FVector(0.0f, 0.0f, -DinoCapsuleHalfHeight), FRotator(0.0f, DinoMeshYaw, 0.0f));

    DinoMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Characters/Dino/SK_Trex.SK_Trex")));
    DinoAnimClass = TSoftClassPtr<UAnimInstance>(FSoftObjectPath(TEXT("/Script/PrehistoricSurvivalLab.PSLDinoAnimInstance")));
    DinoIdleAnim = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Characters/Dino/A_Trex_Idle.A_Trex_Idle")));
    DinoWalkAnim = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Characters/Dino/A_Trex_Walk.A_Trex_Walk")));
    DinoRunAnim = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Characters/Dino/A_Trex_Run.A_Trex_Run")));
    DinoAttackAnim = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Characters/Dino/TrexArmature_TRex_Attack.TrexArmature_TRex_Attack")));
    DinoDeathAnim = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Characters/Dino/TrexArmature_TRex_Death.TrexArmature_TRex_Death")));
}


void APSLCharacter::ApplyDinoVisuals()
{
    USkeletalMesh* Mesh = DinoMesh.LoadSynchronous();
    UClass* AnimClass = DinoAnimClass.LoadSynchronous();
    if (!Mesh || !AnimClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("%s: Dino mesh/anim class missing (%s, %s); keeping capsule placeholder. Run scripts/import_dino.py."),
            *GetName(), *DinoMesh.ToString(), *DinoAnimClass.ToString());
        return;
    }

    GetMesh()->SetSkeletalMeshAsset(Mesh);
    GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
    GetMesh()->SetAnimInstanceClass(AnimClass);

    if (UPSLDinoAnimInstance* DinoAnim = Cast<UPSLDinoAnimInstance>(GetMesh()->GetAnimInstance()))
    {
        DinoAnim->IdleSequence = DinoIdleAnim.LoadSynchronous();
        DinoAnim->WalkSequence = DinoWalkAnim.LoadSynchronous();
        DinoAnim->RunSequence = DinoRunAnim.LoadSynchronous();
        if (!DinoAnim->IdleSequence || !DinoAnim->WalkSequence || !DinoAnim->RunSequence)
        {
            UE_LOG(LogTemp, Warning, TEXT("%s: Dino Idle/Walk/Run sequences incomplete; pose will fall back to reference pose."), *GetName());
        }

        DinoAnim->AttackSequence = DinoAttackAnim.LoadSynchronous();
        DinoAnim->DeathSequence = DinoDeathAnim.LoadSynchronous();
        if (!DinoAnim->AttackSequence || !DinoAnim->DeathSequence)
        {
            UE_LOG(LogTemp, Warning, TEXT("%s: Dino Attack/Death clips missing; those actions will be skipped, locomotion unaffected."), *GetName());
        }
    }
}

void APSLCharacter::ApplyInputDefaults()
{
    PSLInputDefaults::Resolve(this, DefaultMappingContext, MoveAction, LookAction, JumpAction, SprintAction, BiteAction);
}

void APSLCharacter::BeginPlay()
{
    Super::BeginPlay();
    ApplyDinoVisuals();
    ApplyInputDefaults();

    if (StatsComponent)
    {
        StatsComponent->OnDeath.AddDynamic(this, &APSLCharacter::HandleDeath);
    }

    if (SurvivalComponent)
    {
        SurvivalComponent->OnSprintStateChanged.AddDynamic(this, &APSLCharacter::HandleSprintStateChanged);
    }

    if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
    {
        if (ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
        {
            if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
            {
                if (DefaultMappingContext)
                {
                    InputSubsystem->AddMappingContext(DefaultMappingContext, 0);
                }
            }
        }
    }
}

void APSLCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
    ApplyInputDefaults();

    if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
    {
        if (MoveAction)
        {
            EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &APSLCharacter::Move);
        }
        if (LookAction)
        {
            EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &APSLCharacter::Look);
        }
        if (JumpAction)
        {
            EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &APSLCharacter::StartJump);
            EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &APSLCharacter::StopJump);
        }
        if (SprintAction)
        {
            EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Started, this, &APSLCharacter::StartSprint);
            EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Completed, this, &APSLCharacter::StopSprint);
        }
        if (BiteAction)
        {
            EnhancedInputComponent->BindAction(BiteAction, ETriggerEvent::Started, this, &APSLCharacter::StartBite);
        }
    }
}

float APSLCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
    // Super applies engine damage modifiers and broadcasts OnTakeAnyDamage for Blueprints.
    const float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
    return StatsComponent ? StatsComponent->ApplyDamage(ActualDamage) : 0.0f;
}

void APSLCharacter::Move(const FInputActionValue& Value)
{
    const FVector2D MovementVector = Value.Get<FVector2D>();
    const FRotator ControlRotation = Controller ? Controller->GetControlRotation() : FRotator::ZeroRotator;
    const FRotator YawRotation(0.0f, ControlRotation.Yaw, 0.0f);

    AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X), MovementVector.Y);
    AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y), MovementVector.X);
}

void APSLCharacter::Look(const FInputActionValue& Value)
{
    const FVector2D LookAxisVector = Value.Get<FVector2D>();
    AddControllerYawInput(LookAxisVector.X);
    AddControllerPitchInput(LookAxisVector.Y);
}

void APSLCharacter::StartJump(const FInputActionValue& Value)
{
    Jump();
}

void APSLCharacter::StopJump(const FInputActionValue& Value)
{
    StopJumping();
}

void APSLCharacter::StartSprint(const FInputActionValue& Value)
{
    if (SurvivalComponent)
    {
        SurvivalComponent->SetSprintRequested(true);
    }
}

void APSLCharacter::StopSprint(const FInputActionValue& Value)
{
    if (SurvivalComponent)
    {
        SurvivalComponent->SetSprintRequested(false);
    }
}

void APSLCharacter::HandleSprintStateChanged(bool bIsSprinting)
{
    if (GetCharacterMovement())
    {
        GetCharacterMovement()->MaxWalkSpeed = bIsSprinting ? SprintSpeed : WalkSpeed;
    }
}

void APSLCharacter::HandleDeath()
{
    if (bDeathHandled)
    {
        return;
    }

    bDeathHandled = true;
    if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
    {
        DisableInput(PlayerController);
    }
    if (GetCharacterMovement())
    {
        GetCharacterMovement()->DisableMovement();
    }
    GetWorldTimerManager().ClearTimer(BiteHitTimer);
    if (const USkeletalMeshComponent* SkelMesh = GetMesh())
    {
        if (UPSLDinoAnimInstance* DinoAnim = Cast<UPSLDinoAnimInstance>(SkelMesh->GetAnimInstance()))
        {
            DinoAnim->PlayAction(EPSLDinoAction::Death);
        }
    }
    UE_LOG(LogTemp, Log, TEXT("%s died"), *GetName());
}

void APSLCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    GetWorldTimerManager().ClearTimer(BiteHitTimer);
    Super::EndPlay(EndPlayReason);
}

bool APSLCharacter::CanBite(double Now, double LastBite, double Cooldown, bool bDead)
{
    return !bDead && (Now - LastBite) >= Cooldown;
}

void APSLCharacter::StartBite(const FInputActionValue& Value)
{
    TryBite();
}

bool APSLCharacter::TryBite()
{
    UWorld* World = GetWorld();
    const bool bDead = !StatsComponent || StatsComponent->IsDead();
    if (!World || !CanBite(World->GetTimeSeconds(), LastBiteTime, BiteCooldown, bDead))
    {
        return false;
    }

    LastBiteTime = World->GetTimeSeconds();
    ++BiteCount;
    if (UPSLDinoAnimInstance* DinoAnim = Cast<UPSLDinoAnimInstance>(GetMesh()->GetAnimInstance()))
    {
        DinoAnim->PlayAction(EPSLDinoAction::Attack);
    }
    World->GetTimerManager().SetTimer(BiteHitTimer, this, &APSLCharacter::ResolveBiteHit, FMath::Max(BiteHitDelay, KINDA_SMALL_NUMBER), false);
    return true;
}

void APSLCharacter::ResolveBiteHit()
{
    UWorld* World = GetWorld();
    if (!World || !StatsComponent || StatsComponent->IsDead())
    {
        return;
    }

    const FVector Center = GetActorLocation() + GetActorForwardVector() * BiteReach;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(PSLBite), false, this);
    TArray<FOverlapResult> Overlaps;
    World->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(BiteRadius), Params);

    TSet<AActor*> Victims;
    for (const FOverlapResult& Overlap : Overlaps)
    {
        AActor* Victim = Overlap.GetActor();
        if (Victim && Victim != this && Victim->IsA<APawn>())
        {
            Victims.Add(Victim);
        }
    }

    for (AActor* Victim : Victims)
    {
        UGameplayStatics::ApplyDamage(Victim, BiteDamage, GetController(), this, UDamageType::StaticClass());
    }
    LastBiteHitCount = Victims.Num();
}
