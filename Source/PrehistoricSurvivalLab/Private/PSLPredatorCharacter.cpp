#include "PSLPredatorCharacter.h"

#include "Animation/AnimSequence.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PSLDinoAnimInstance.h"
#include "PSLPredatorAIController.h"
#include "PSLStatsComponent.h"
#include "UObject/ConstructorHelpers.h"

APSLPredatorCharacter::APSLPredatorCharacter()
{
    PrimaryActorTick.bCanEverTick = true;

    GetCapsuleComponent()->InitCapsuleSize(70.0f, 140.0f);
    // The player's spring arm probes ECC_Camera; a creature next to the player must not collapse it (D-CAM-01).
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    GetMesh()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

    StatsComponent = CreateDefaultSubobject<UPSLStatsComponent>(TEXT("StatsComponent"));
    StatsComponent->MaxHealth = 250.0f;
    StatsComponent->Health = 250.0f;

    AIControllerClass = APSLPredatorAIController::StaticClass();
    AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

    bUseControllerRotationYaw = false;
    UCharacterMovementComponent* Movement = GetCharacterMovement();
    Movement->bOrientRotationToMovement = true;
    Movement->RotationRate = FRotator(0.0f, 240.0f, 0.0f);
    Movement->MaxWalkSpeed = PatrolSpeed;

    // Placeholder look built from engine cubes: no binary assets needed.
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));

    BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
    BodyMesh->SetupAttachment(GetCapsuleComponent());
    BodyMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -40.0f));
    BodyMesh->SetRelativeScale3D(FVector(2.4f, 1.0f, 1.4f));
    BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    HeadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HeadMesh"));
    HeadMesh->SetupAttachment(BodyMesh);
    HeadMesh->SetRelativeLocation(FVector(0.6f, 0.0f, 0.5f));
    HeadMesh->SetRelativeScale3D(FVector(0.4f, 0.7f, 0.5f));
    HeadMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    // Intent indicator 60uu above the capsule top (half height 140).
    AlertText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("AlertText"));
    AlertText->SetupAttachment(GetCapsuleComponent());
    AlertText->SetRelativeLocation(FVector(0.0f, 0.0f, 200.0f));
    AlertText->SetText(FText::AsCultureInvariant(TEXT("!"))); // symbol, not language
    AlertText->SetWorldSize(80.0f);
    AlertText->SetHorizontalAlignment(EHTA_Center);
    AlertText->SetVerticalAlignment(EVRTA_TextCenter);
    AlertText->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    AlertText->SetCastShadow(false);
    AlertText->SetVisibility(false);

    if (CubeFinder.Succeeded())
    {
        BodyMesh->SetStaticMesh(CubeFinder.Object);
        HeadMesh->SetStaticMesh(CubeFinder.Object);
    }

    PredatorMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Characters/Dino/SK_Trex.SK_Trex")));
    PredatorIdleAnim = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Characters/Dino/A_Trex_Idle.A_Trex_Idle")));
    PredatorWalkAnim = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Characters/Dino/A_Trex_Walk.A_Trex_Walk")));
    PredatorRunAnim = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Characters/Dino/A_Trex_Run.A_Trex_Run")));
    PredatorAttackAnim = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Characters/Dino/TrexArmature_TRex_Attack.TrexArmature_TRex_Attack")));
    PredatorDeathAnim = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Characters/Dino/TrexArmature_TRex_Death.TrexArmature_TRex_Death")));
}

void APSLPredatorCharacter::BeginPlay()
{
    Super::BeginPlay();
    ApplyPredatorVisuals();
    if (StatsComponent)
    {
        StatsComponent->OnDeath.AddDynamic(this, &APSLPredatorCharacter::HandleDeath);
    }
}

void APSLPredatorCharacter::ApplyPredatorVisuals()
{
    USkeletalMesh* Mesh = PredatorMesh.LoadSynchronous();
    UAnimSequence* Idle = PredatorIdleAnim.LoadSynchronous();
    UAnimSequence* Walk = PredatorWalkAnim.LoadSynchronous();
    UAnimSequence* Run = PredatorRunAnim.LoadSynchronous();
    if (!Mesh || !Idle || !Walk || !Run)
    {
        UE_LOG(LogTemp, Warning, TEXT("%s: predator mesh/Idle/Walk/Run missing (%s); keeping cube placeholder."), *GetName(), *PredatorMesh.ToString());
        return;
    }

    USkeletalMeshComponent* Skel = GetMesh();
    Skel->SetSkeletalMeshAsset(Mesh);
    Skel->SetAnimationMode(EAnimationMode::AnimationBlueprint);
    Skel->SetAnimInstanceClass(UPSLDinoAnimInstance::StaticClass());
    UPSLDinoAnimInstance* Anim = Cast<UPSLDinoAnimInstance>(Skel->GetAnimInstance());
    if (!Anim)
    {
        UE_LOG(LogTemp, Warning, TEXT("%s: predator anim instance not created; keeping cube placeholder."), *GetName());
        Skel->SetSkeletalMeshAsset(nullptr);
        return;
    }
    Anim->IdleSequence = Idle;
    Anim->WalkSequence = Walk;
    Anim->RunSequence = Run;
    Anim->AttackSequence = PredatorAttackAnim.LoadSynchronous();
    Anim->DeathSequence = PredatorDeathAnim.LoadSynchronous();
    if (!Anim->AttackSequence || !Anim->DeathSequence)
    {
        UE_LOG(LogTemp, Warning, TEXT("%s: predator Attack/Death clips missing; those actions are skipped."), *GetName());
    }

    // Fit into the capsule: feet on the capsule bottom, height 90% of the capsule. Imported mesh faces +Y (yaw -90).
    const float HalfHeight = GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
    const FBoxSphereBounds Bounds = Mesh->GetBounds();
    const float Scale = Bounds.BoxExtent.Z > KINDA_SMALL_NUMBER ? (HalfHeight * 2.0f * 0.9f) / (Bounds.BoxExtent.Z * 2.0f) : 1.0f;
    const float FootZ = (Bounds.Origin.Z - Bounds.BoxExtent.Z) * Scale;
    Skel->SetRelativeScale3D(FVector(Scale));
    Skel->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, -HalfHeight - FootZ), FRotator(0.0f, -90.0f, 0.0f));

    PredatorMaterials.Reset();
    for (int32 Slot = 0; Slot < Skel->GetNumMaterials(); ++Slot)
    {
        if (UMaterialInstanceDynamic* Mid = Skel->CreateDynamicMaterialInstance(Slot, Skel->GetMaterial(Slot)))
        {
            PredatorMaterials.Add(Mid);
        }
    }

    BodyMesh->SetVisibility(false);
    HeadMesh->SetVisibility(false);
    bUsingSkeletalVisual = true;
}

void APSLPredatorCharacter::PlayAttackAnimation()
{
    if (!bUsingSkeletalVisual)
    {
        return;
    }
    if (UPSLDinoAnimInstance* Anim = Cast<UPSLDinoAnimInstance>(GetMesh()->GetAnimInstance()))
    {
        Anim->PlayAction(EPSLDinoAction::Attack);
    }
}

float APSLPredatorCharacter::GetAttackClipLength() const
{
    const UAnimSequence* Clip = PredatorAttackAnim.Get();
    return Clip ? Clip->GetPlayLength() : 0.0f;
}

void APSLPredatorCharacter::SetTelegraphFlash(float Value)
{
    if (Value == LastFlash)
    {
        return;
    }
    LastFlash = Value;
    for (UMaterialInstanceDynamic* Mid : PredatorMaterials)
    {
        if (Mid)
        {
            Mid->SetScalarParameterValue(TEXT("HitFlash"), Value);
        }
    }
}

void APSLPredatorCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (bDeathHandled || !AlertText)
    {
        if (bTelegraphFlashing)
        {
            bTelegraphFlashing = false;
            SetTelegraphFlash(0.0f);
        }
        return;
    }

    const APSLPredatorAIController* AI = Cast<APSLPredatorAIController>(GetController());
    const EPSLPredatorState State = AI ? AI->GetPredatorState() : EPSLPredatorState::Idle;
    const bool bWindingUp = AI && AI->IsAttackWindingUp();

    // Wind-up telegraph: pulse the HitFlash scalar (0..1, ~3 Hz); reset once when it ends.
    if (bWindingUp)
    {
        bTelegraphFlashing = true;
        SetTelegraphFlash(0.5f + 0.5f * FMath::Sin(GetWorld()->GetTimeSeconds() * 2.0 * UE_DOUBLE_PI * 3.0));
    }
    else if (bTelegraphFlashing)
    {
        bTelegraphFlashing = false;
        SetTelegraphFlash(0.0f);
    }

    const bool bInvestigate = State == EPSLPredatorState::Investigate;
    const bool bAlert = State == EPSLPredatorState::Chase || State == EPSLPredatorState::Attack;
    AlertText->SetVisibility(bInvestigate || bAlert);
    if (!bInvestigate && !bAlert)
    {
        return;
    }

    // Style = (bAlert, bWindingUp) packed +1 so 0 means "nothing pushed yet"; the text component only changes when it flips.
    const int32 Style = 1 + (bAlert ? 1 : 0) + (bWindingUp ? 2 : 0);
    if (Style != AlertStyle)
    {
        AlertStyle = Style;
        AlertText->SetText(FText::AsCultureInvariant(bAlert ? TEXT("!") : TEXT("?"))); // symbols, not language
        // Wind-up '!' is larger and hot yellow-white so it reads differently from Chase's red '!'.
        AlertText->SetTextRenderColor(bWindingUp ? FColor(255, 240, 150) : bAlert ? FColor::Red : FColor(255, 170, 0));
        AlertText->SetWorldSize(bWindingUp ? 130.0f : 80.0f);
    }
    if (const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0))
    {
        const FVector ToCamera = Camera->GetCameraLocation() - AlertText->GetComponentLocation();
        AlertText->SetWorldRotation(FRotator(0.0f, ToCamera.Rotation().Yaw, 0.0f));
    }
}

float APSLPredatorCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
    const float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
    return StatsComponent ? StatsComponent->ApplyDamage(ActualDamage) : 0.0f;
}

void APSLPredatorCharacter::HandleDeath()
{
    if (bDeathHandled)
    {
        return;
    }

    bDeathHandled = true;
    if (AlertText)
    {
        AlertText->SetVisibility(false);
    }
    if (APSLPredatorAIController* AI = Cast<APSLPredatorAIController>(GetController()))
    {
        AI->StopAI();
    }
    GetCharacterMovement()->StopMovementImmediately();
    GetCharacterMovement()->DisableMovement();

    // The corpse must not block pawns.
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
