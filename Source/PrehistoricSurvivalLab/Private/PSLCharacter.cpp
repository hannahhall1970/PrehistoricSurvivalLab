#include "PSLCharacter.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "PSLStatsComponent.h"
#include "PSLSurvivalComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"

APSLCharacter::APSLCharacter()
{
    PrimaryActorTick.bCanEverTick = false;
    WalkSpeed = 300.0f;
    SprintSpeed = 600.0f;
    bDeathHandled = false;

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
}

void APSLCharacter::BeginPlay()
{
    Super::BeginPlay();

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
    UE_LOG(LogTemp, Log, TEXT("%s died"), *GetName());
}
