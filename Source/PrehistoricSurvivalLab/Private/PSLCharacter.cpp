#include "PSLCharacter.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "PSLAudio.h"
#include "PSLInputDefaults.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
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
#include "Camera/PlayerCameraManager.h"
#include "TimerManager.h"
#include "PSLDinoSpecies.h"
#include "PSLFoodPlant.h"
#include "PSLPreyCharacter.h"
#include "PSLGameMode.h"
#include "Sound/SoundBase.h"
#include "PSLGameUserSettings.h"

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
    FoodPerBite = 1.0f;
    EatSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Audio/SFX_Eat.SFX_Eat")));

    StatsComponent = CreateDefaultSubobject<UPSLStatsComponent>(TEXT("StatsComponent"));
    SurvivalComponent = CreateDefaultSubobject<UPSLSurvivalComponent>(TEXT("SurvivalComponent"));

    USpringArmComponent* SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    SpringArm->SetupAttachment(RootComponent);
    SpringArm->TargetArmLength = 650.0f;
    SpringArm->SocketOffset = FVector(0.0f, 70.0f, 110.0f);
    SpringArm->TargetOffset = FVector(0.0f, 0.0f, 40.0f);
    SpringArm->bUsePawnControlRotation = true;
    SpringArm->bDoCollisionTest = true;
    SpringArm->ProbeSize = 12.0f;
    SpringArm->bEnableCameraLag = true;
    SpringArm->CameraLagSpeed = 10.0f;
    // No rotation lag: PSL.PIE.LookInput reads the camera rotation right after input.
    SpringArm->bEnableCameraRotationLag = false;

    UCameraComponent* Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
    Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
    Camera->bUsePawnControlRotation = false;
    Camera->SetFieldOfView(75.0f);

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

    // Species mesh scale: 0 fits the mesh to 90% of the capsule height with the feet on the capsule bottom.
    if (ActiveSpecies)
    {
        const float HalfHeight = GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
        float Scale = ActiveSpecies->MeshScale;
        float FootZ = 0.0f;
        if (Scale <= 0.0f)
        {
            const FBoxSphereBounds Bounds = Mesh->GetBounds();
            Scale = Bounds.BoxExtent.Z > KINDA_SMALL_NUMBER ? (HalfHeight * 2.0f * 0.9f) / (Bounds.BoxExtent.Z * 2.0f) : 1.0f;
            FootZ = (Bounds.Origin.Z - Bounds.BoxExtent.Z) * Scale;
        }
        GetMesh()->SetRelativeScale3D(FVector(Scale));
        GetMesh()->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, -HalfHeight - FootZ), FRotator(0.0f, ActiveSpecies->MeshYaw, 0.0f));
    }
}

void APSLCharacter::ApplySpecies(const UPSLDinoSpecies* Species)
{
    if (!Species)
    {
        return;
    }

    ActiveSpecies = Species;
    WalkSpeed = Species->WalkSpeed;
    SprintSpeed = Species->SprintSpeed;
    BiteDamage = Species->BiteDamage;
    BiteRadius = Species->BiteRadius;
    BiteCooldown = Species->BiteCooldown;
    FoodPerBite = Species->FoodPerBite;

    DinoMesh = Species->Mesh;
    DinoIdleAnim = Species->IdleAnim;
    DinoWalkAnim = Species->WalkAnim;
    DinoRunAnim = Species->RunAnim;
    DinoAttackAnim = Species->AttackAnim;
    DinoDeathAnim = Species->DeathAnim;

    GetCapsuleComponent()->SetCapsuleSize(Species->CapsuleRadius, Species->CapsuleHalfHeight);
    GetMesh()->SetRelativeLocationAndRotation(
        FVector(0.0f, 0.0f, -Species->CapsuleHalfHeight), FRotator(0.0f, Species->MeshYaw, 0.0f));
    GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;

    if (StatsComponent)
    {
        StatsComponent->MaxHealth = Species->MaxHealth;
        StatsComponent->Health = Species->MaxHealth;
    }
    if (SurvivalComponent)
    {
        SurvivalComponent->MaxStamina = Species->MaxStamina;
        SurvivalComponent->Stamina = Species->MaxStamina;
        SurvivalComponent->MaxHunger = Species->MaxHunger;
        SurvivalComponent->Hunger = Species->MaxHunger;
        SurvivalComponent->HungerDrainPerSecond = Species->HungerDrainPerSecond;
    }

    // Before BeginPlay the visuals are applied there; afterwards they must be refreshed here.
    if (HasActorBegunPlay())
    {
        ApplyDinoVisuals();
    }
}

void APSLCharacter::ApplyInputDefaults()
{
    PSLInputDefaults::Resolve(this, DefaultMappingContext, MoveAction, LookAction, JumpAction, SprintAction, BiteAction, PauseAction);
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
        if (PlayerController->PlayerCameraManager)
        {
            PlayerController->PlayerCameraManager->ViewPitchMin = -60.0f;
            PlayerController->PlayerCameraManager->ViewPitchMax = 20.0f;
        }

        ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
        UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LocalPlayer ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer) : nullptr;
        if (InputSubsystem && DefaultMappingContext)
        {
            InputSubsystem->AddMappingContext(DefaultMappingContext, 0);
            UE_LOG(LogTemp, Display, TEXT("PSL input context added: %s"), *GetNameSafe(DefaultMappingContext));
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("PSL input context NOT added (local player %d, subsystem %d, context %d)"),
                LocalPlayer != nullptr, InputSubsystem != nullptr, DefaultMappingContext != nullptr);
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("PSL input context NOT added: no player controller at BeginPlay"));
    }

    // Cheap 5 Hz poll; the HUD reads GetFoodHint() every frame.
    GetWorldTimerManager().SetTimer(ProximityHintTimer, this, &APSLCharacter::UpdateProximityHint, 0.2f, true);
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
        if (PauseAction)
        {
            EnhancedInputComponent->BindAction(PauseAction, ETriggerEvent::Started, this, &APSLCharacter::StartPause);
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

FVector2D APSLCharacter::ScaleLookInput(FVector2D Raw, float Sensitivity, bool bInvertY)
{
    Raw *= Sensitivity;
    if (bInvertY)
    {
        Raw.Y = -Raw.Y;
    }
    return Raw;
}

void APSLCharacter::Look(const FInputActionValue& Value)
{
    const UPSLGameUserSettings* Settings = UPSLGameUserSettings::Get();
    const FVector2D LookAxisVector = Settings
        ? ScaleLookInput(Value.Get<FVector2D>(), Settings->MouseSensitivity, Settings->bInvertY)
        : Value.Get<FVector2D>();
    AddControllerYawInput(LookAxisVector.X);
    AddControllerPitchInput(LookAxisVector.Y);
}

void APSLCharacter::StartJump(const FInputActionValue& Value)
{
    UCharacterMovementComponent* Movement = GetCharacterMovement();
    UE_LOG(LogTemp, Display, TEXT("PSL input: Jump Started (canjump=%d grounded=%d z=%.1f)"), CanJump(),
        Movement && Movement->IsMovingOnGround(), GetActorLocation().Z);
    if (CanJump() && Movement)
    {
        JumpStartZ = GetActorLocation().Z;
        JumpStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
        Movement->bNotifyApex = true; // one-shot: the movement component clears it when NotifyJumpApex fires
    }
    Jump();
}

void APSLCharacter::NotifyJumpApex()
{
    Super::NotifyJumpApex();
    UE_LOG(LogTemp, Display, TEXT("PSL jump apex: +%.1f uu"), GetActorLocation().Z - JumpStartZ);
}

void APSLCharacter::Landed(const FHitResult& Hit)
{
    Super::Landed(Hit);
    if (JumpStartTime >= 0.0 && GetWorld())
    {
        UE_LOG(LogTemp, Display, TEXT("PSL jump landed: z=%.1f airtime=%.2f s"), GetActorLocation().Z,
            GetWorld()->GetTimeSeconds() - JumpStartTime);
        JumpStartTime = -1.0;
    }
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
    GetWorldTimerManager().ClearTimer(ProximityHintTimer);
    ProximityHint = FText::GetEmpty();
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
    GetWorldTimerManager().ClearTimer(ProximityHintTimer);
    Super::EndPlay(EndPlayReason);
}

bool APSLCharacter::CanBite(double Now, double LastBite, double Cooldown, bool bDead)
{
    return !bDead && (Now - LastBite) >= Cooldown;
}

void APSLCharacter::StartBite(const FInputActionValue& Value)
{
    UE_LOG(LogTemp, Display, TEXT("PSL input: Bite Started"));
    TryBite();
}

void APSLCharacter::StartPause(const FInputActionValue& Value)
{
    if (APSLGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<APSLGameMode>() : nullptr)
    {
        GameMode->TogglePause();
    }
}

bool APSLCharacter::TryBite()
{
    UWorld* World = GetWorld();
    const bool bDead = !StatsComponent || StatsComponent->IsDead();
    if (!World || !CanBite(World->GetTimeSeconds(), LastBiteTime, BiteCooldown, bDead))
    {
        UE_LOG(LogTemp, Display, TEXT("PSL bite rejected: dead=%d cooldown_left=%.2f"), bDead,
            World ? FMath::Max(0.0, BiteCooldown - (World->GetTimeSeconds() - LastBiteTime)) : 0.0);
        return false;
    }

    LastBiteTime = World->GetTimeSeconds();
    ++BiteCount;
    LastBiteHitCount = -1;
    UE_LOG(LogTemp, Display, TEXT("PSL bite accepted #%d"), BiteCount);
    if (UPSLDinoAnimInstance* DinoAnim = Cast<UPSLDinoAnimInstance>(GetMesh()->GetAnimInstance()))
    {
        DinoAnim->PlayAction(EPSLDinoAction::Attack);
    }
    World->GetTimerManager().SetTimer(BiteHitTimer, this, &APSLCharacter::ResolveBiteHit, FMath::Max(BiteHitDelay, KINDA_SMALL_NUMBER), false);
    if (USoundBase* S = PSLAudio::Load(PSLAudio::Bite))
    {
        UGameplayStatics::PlaySoundAtLocation(this, S, GetActorLocation());
    }
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
    if (Victims.Num() > 0)
    {
        OnBiteHit.Broadcast();
    }

    const float Ate = TryEat(Center);
    UE_LOG(LogTemp, Display, TEXT("PSL bite hit: pawns=%d damage=%.1f food=%.1f hunger=%.1f"), Victims.Num(), BiteDamage, Ate,
        SurvivalComponent ? SurvivalComponent->GetHunger() : -1.0f);
    if (Ate > 0.0f)
    {
        OnAteFood.Broadcast(Ate);
        if (USoundBase* Sound = EatSound.LoadSynchronous())
        {
            UGameplayStatics::PlaySoundAtLocation(this, Sound, GetActorLocation());
        }
    }
}

float APSLCharacter::TryEat(const FVector& Center)
{
    UWorld* World = GetWorld();
    if (!World || !SurvivalComponent)
    {
        return 0.0f;
    }

    // Corpses and plants are WorldDynamic (the dead prey capsule ignores Pawn), so the pawn overlap in ResolveBiteHit never sees them.
    FCollisionQueryParams Params(SCENE_QUERY_STAT(PSLEat), false, this);
    TArray<FOverlapResult> Overlaps;
    World->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldDynamic), FCollisionShape::MakeSphere(BiteRadius), Params);

    // No species applied = default T-Rex = carnivore.
    const bool bCarnivore = !ActiveSpecies || ActiveSpecies->Diet == EPSLDiet::Carnivore;
    float Gained = 0.0f;
    bool bAte = false;
    bool bWrongFood = false;
    for (const FOverlapResult& Overlap : Overlaps)
    {
        AActor* Actor = Overlap.GetActor();
        if (APSLPreyCharacter* Carcass = Cast<APSLPreyCharacter>(Actor))
        {
            if (!Carcass->IsDead() || Carcass->MeatRemaining <= 0.0f)
            {
                continue;
            }
            if (!bCarnivore)
            {
                bWrongFood = true;
            }
            else if (!bAte)
            {
                // One carcass per bite.
                Gained = Carcass->EatMeat(BaseFoodPerBite * FoodPerBite);
                bAte = Gained > 0.0f;
            }
        }
        else if (APSLFoodPlant* Plant = Cast<APSLFoodPlant>(Actor))
        {
            if (!Plant->IsAvailable())
            {
                continue;
            }
            if (bCarnivore)
            {
                bWrongFood = true;
            }
            else if (!bAte)
            {
                Gained = Plant->Consume();
                bAte = Gained > 0.0f;
            }
        }
    }

    if (bAte)
    {
        FoodHint = FText::GetEmpty();
        SurvivalComponent->AddFood(Gained);
    }
    else if (bWrongFood)
    {
        FoodHint = bCarnivore
            ? NSLOCTEXT("PSL", "HintCannotEatPlants", "Carnivores can't eat plants. Hunt prey instead.")
            : NSLOCTEXT("PSL", "HintCannotEatMeat", "Herbivores can't eat meat. Find plants instead.");
    }
    else
    {
        // A bite that touched no food must not leave a stale hint on the HUD.
        FoodHint = FText::GetEmpty();
    }
    return Gained;
}

void APSLCharacter::UpdateProximityHint()
{
    ProximityHint = FText::GetEmpty();
    UWorld* World = GetWorld();
    // Herbivores only (mirrors TryEat); carnivores get no eat prompt.
    if (!World || bDeathHandled || !ActiveSpecies || ActiveSpecies->Diet == EPSLDiet::Carnivore)
    {
        return;
    }

    // Same bite point as ResolveBiteHit. The +120 margin is a tuning knob: it lets the prompt show slightly before a bite would land.
    const FVector Center = GetActorLocation() + GetActorForwardVector() * BiteReach;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(PSLHint), false, this);
    TArray<FOverlapResult> Overlaps;
    World->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldDynamic), FCollisionShape::MakeSphere(BiteRadius + 120.0f), Params);
    for (const FOverlapResult& Overlap : Overlaps)
    {
        const APSLFoodPlant* Plant = Cast<APSLFoodPlant>(Overlap.GetActor());
        if (Plant && Plant->IsAvailable())
        {
            // A stale misbite message ("cannot eat meat") must not mask the prompt once the dino faces an edible plant.
            FoodHint = FText::GetEmpty();
            ProximityHint = NSLOCTEXT("PSL", "HintBiteToEat", "Bite to eat");
            return;
        }
    }
}
