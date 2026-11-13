#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Editor/EditorEngine.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "GameFramework/InputDeviceLibrary.h"
#include "InputKeyEventArgs.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PSLCharacter.h"
#include "PSLGameMode.h"
#include "PSLHUD.h"
#include "PSLSurvivalComponent.h"
#include "PSLDinoAnimInstance.h"
#include "PSLPreyAIController.h"
#include "PSLPreyCharacter.h"
#include "PSLStatsComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "UnrealClient.h"
#include "PSLPieTestSupport.h"

namespace
{
using namespace PSLPie;

static void QueueMoveCase(const TSharedRef<FPSLPieCtx>& Context, const TCHAR* Label, const FKey& Key, UInputAction* Action, const FVector& Direction)
{
    const bool bForwardAxis = FMath::Abs(Direction.X) >= FMath::Abs(Direction.Y);
    const bool bPositive = (bForwardAxis ? Direction.X : Direction.Y) >= 0.0f;
    AddPieStep(Context, Label, 2.0f, [Label, Key, Action, bForwardAxis, bPositive, bStarted = false, bFallbackSent = false, Start = FVector::ZeroVector, StartTime = -1.0](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        const double Now = Ctx.World->GetTimeSeconds();
        UInputAction* MoveAction = Action ? Action : Ctx.Character->MoveAction.Get();
        if (!bStarted)
        {
            bStarted = true;
            Start = Ctx.Character->GetActorLocation();
            StartTime = Now;
            SendKey(Ctx, Key, IE_Pressed);
            return false;
        }
        if (Now - StartTime > 0.1 && !EnsureHeldAction(Ctx, Key, MoveAction, IE_Pressed, bFallbackSent))
        {
            return false;
        }
        if (Now - StartTime < 0.6)
        {
            return false;
        }

        const FVector Delta = Ctx.Character->GetActorLocation() - Start;
        const FVector HorizontalDelta(Delta.X, Delta.Y, 0.0f);
        const float Distance = HorizontalDelta.Size();
        const FRotator YawRotation(0.0f, Ctx.PC->GetControlRotation().Yaw, 0.0f);
        const FVector Forward = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
        const FVector Right = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
        const FVector ExpectedDirection = (bForwardAxis ? Forward : Right) * (bPositive ? 1.0f : -1.0f);
        const float Alignment = FVector::DotProduct(HorizontalDelta, ExpectedDirection);
        Ctx.Test->TestTrue(FString::Printf(TEXT("%s action value is non-zero"), Label), ActionMagnitude(Ctx, MoveAction) > KINDA_SMALL_NUMBER);
        Ctx.Test->TestTrue(FString::Printf(TEXT("%s moves more than 50 uu"), Label), Distance > 50.0f);
        Ctx.Test->TestTrue(FString::Printf(TEXT("%s follows expected direction"), Label), Alignment > 0.7f * Distance);
        Record(Ctx, Label);
        SendKey(Ctx, Key, IE_Released);
        return true;
    });

    AddPieStep(Context, TEXT("MoveReleaseSettle"), 1.2f, [Key, Action, StartTime = -1.0](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        const double Now = Ctx.World->GetTimeSeconds();
        UInputAction* MoveAction = Action ? Action : Ctx.Character->MoveAction.Get();
        if (StartTime < 0.0)
        {
            StartTime = Now;
        }
        if (Now - StartTime < 0.3)
        {
            if (ActionMagnitude(Ctx, MoveAction) > KINDA_SMALL_NUMBER)
            {
                SendKeyToPlayerController(Ctx, Key, IE_Released);
            }
            return false;
        }
        return true;
    });
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLPiePlayerSpawnTest, "PSL.PIE.PlayerSpawn", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPSLPiePlayerSpawnTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("PlayerSpawn"));
    if (!QueuePieStart(*this, Context))
    {
        return false;
    }
    AddPieStep(Context, TEXT("PlayerSpawnAssertions"), 2.0f, [](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        ULocalPlayer* LocalPlayer = Ctx.PC->GetLocalPlayer();
        UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LocalPlayer ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer) : nullptr;
        const bool bMappingContextPresent = Ctx.Character->DefaultMappingContext != nullptr
            && InputSubsystem != nullptr && InputSubsystem->HasMappingContext(Ctx.Character->DefaultMappingContext);
        Ctx.Test->TestTrue(TEXT("World is PIE"), Ctx.World->WorldType == EWorldType::PIE);
        Ctx.Test->TestTrue(TEXT("PlayerController possesses the discovered character"), Ctx.PC->GetPawn() == Ctx.Character);
        Ctx.Test->TestTrue(TEXT("Character is controlled by the discovered PlayerController"), Ctx.Character->GetController() == Ctx.PC);
        Ctx.Test->TestTrue(TEXT("Character is locally controlled"), Ctx.Character->IsLocallyControlled());
        Ctx.Test->TestTrue(TEXT("PIE GameMode is APSLGameMode"), Ctx.World->GetAuthGameMode<APSLGameMode>() != nullptr);
        Ctx.Test->TestTrue(TEXT("Default mapping context is active"), bMappingContextPresent);
        Ctx.Test->TestTrue(TEXT("PlayerInput is UEnhancedPlayerInput"), Cast<UEnhancedPlayerInput>(Ctx.PC->PlayerInput) != nullptr);
        Record(Ctx, TEXT("spawn"));
        RequestScreenshot(Ctx, TEXT("after_spawn"));
        return true;
    });
    QueuePieEnd(Context);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLPieMoveInputTest, "PSL.PIE.MoveInput", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPSLPieMoveInputTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("MoveInput"));
    if (!QueuePieStart(*this, Context))
    {
        return false;
    }
    AddPieStep(Context, TEXT("MoveReady"), 1.0f, [](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        Record(Ctx, TEXT("move_start"));
        return true;
    });

    const FRotator YawRotation = FRotator::ZeroRotator;
    const FVector Forward = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
    const FVector Right = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
    QueueMoveCase(Context, TEXT("W"), EKeys::W, nullptr, Forward);
    QueueMoveCase(Context, TEXT("S"), EKeys::S, nullptr, -Forward);
    QueueMoveCase(Context, TEXT("D"), EKeys::D, nullptr, Right);
    QueueMoveCase(Context, TEXT("A"), EKeys::A, nullptr, -Right);
    AddPieStep(Context, TEXT("MoveScreenshot"), 1.0f, [](FPSLPieCtx& Ctx)
    {
        if (Ctx.IsUsable())
        {
            RequestScreenshot(Ctx, TEXT("after_move"));
        }
        return true;
    });
    QueuePieEnd(Context);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLPieLookInputTest, "PSL.PIE.LookInput", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPSLPieLookInputTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("LookInput"));
    if (!QueuePieStart(*this, Context))
    {
        return false;
    }

    AddPieStep(Context, TEXT("MouseX"), 2.0f, [Frames = 0, bForcePlayerController = false, Start = FRotator::ZeroRotator, CameraStart = FRotator::ZeroRotator](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        const UCameraComponent* Camera = Ctx.Character->FindComponentByClass<UCameraComponent>();
        if (Frames == 0)
        {
            Start = Ctx.PC->GetControlRotation();
            CameraStart = Camera ? Camera->GetComponentRotation() : FRotator::ZeroRotator;
        }
        if (Frames < 10)
        {
            SendAxis(Ctx, EKeys::MouseX, 10.0f, bForcePlayerController);
            ++Frames;
            return false;
        }

        const float DeltaYaw = FMath::Abs(FMath::FindDeltaAngleDegrees(Start.Yaw, Ctx.PC->GetControlRotation().Yaw));
        if (DeltaYaw <= 1.0f && !bForcePlayerController)
        {
            bForcePlayerController = true;
            Frames = 0;
            Start = Ctx.PC->GetControlRotation();
            Ctx.Test->AddInfo(TEXT("MouseX viewport axis had no observed effect; falling back to PlayerController::InputKey"));
            return false;
        }
        Ctx.Test->TestTrue(TEXT("MouseX changes yaw by more than one degree"), DeltaYaw > 1.0f);
        Ctx.Test->TestTrue(TEXT("Follow camera yaw changes by more than one degree"),
            Camera != nullptr && FMath::Abs(FMath::FindDeltaAngleDegrees(CameraStart.Yaw, Camera->GetComponentRotation().Yaw)) > 1.0f);
        Ctx.Test->AddInfo(FString::Printf(TEXT("MouseX yaw delta %.3f degrees (sign %.0f)"), DeltaYaw, FMath::Sign(FMath::FindDeltaAngleDegrees(Start.Yaw, Ctx.PC->GetControlRotation().Yaw))));
        Record(Ctx, TEXT("look_yaw"));
        return true;
    });

    AddPieStep(Context, TEXT("MouseY"), 2.0f, [Frames = 0, bForcePlayerController = false, Start = FRotator::ZeroRotator](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        if (Frames == 0)
        {
            Start = Ctx.PC->GetControlRotation();
        }
        if (Frames < 10)
        {
            SendAxis(Ctx, EKeys::MouseY, 10.0f, bForcePlayerController);
            ++Frames;
            return false;
        }

        const float DeltaPitch = FMath::Abs(FMath::FindDeltaAngleDegrees(Start.Pitch, Ctx.PC->GetControlRotation().Pitch));
        if (DeltaPitch <= 1.0f && !bForcePlayerController)
        {
            bForcePlayerController = true;
            Frames = 0;
            Start = Ctx.PC->GetControlRotation();
            Ctx.Test->AddInfo(TEXT("MouseY viewport axis had no observed effect; falling back to PlayerController::InputKey"));
            return false;
        }
        Ctx.Test->TestTrue(TEXT("MouseY changes pitch by more than one degree"), DeltaPitch > 1.0f);
        Ctx.Test->AddInfo(FString::Printf(TEXT("MouseY pitch delta %.3f degrees (sign %.0f)"), DeltaPitch, FMath::Sign(FMath::FindDeltaAngleDegrees(Start.Pitch, Ctx.PC->GetControlRotation().Pitch))));
        Record(Ctx, TEXT("look_pitch"));
        RequestScreenshot(Ctx, TEXT("after_look"));
        return true;
    });
    QueuePieEnd(Context);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLPieJumpInputTest, "PSL.PIE.JumpInput", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPSLPieJumpInputTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("JumpInput"));
    if (!QueuePieStart(*this, Context))
    {
        return false;
    }

    AddPieStep(Context, TEXT("Jump"), 4.0f, [bStarted = false, bFallbackSent = false, bObserved = false, StartZ = 0.0f, StartTime = -1.0](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        UCharacterMovementComponent* Movement = Ctx.Character->GetCharacterMovement();
        if (!Movement)
        {
            Ctx.Test->AddError(TEXT("Character has no CharacterMovementComponent"));
            return true;
        }
        const double Now = Ctx.World->GetTimeSeconds();
        if (!bStarted)
        {
            if (!Movement->IsMovingOnGround())
            {
                return false;
            }
            bStarted = true;
            StartZ = Ctx.Character->GetActorLocation().Z;
            StartTime = Now;
            SendKey(Ctx, EKeys::SpaceBar, IE_Pressed);
            return false;
        }
        if (Now - StartTime > 0.05 && !EnsureHeldAction(Ctx, EKeys::SpaceBar, Ctx.Character->JumpAction, IE_Pressed, bFallbackSent))
        {
            return false;
        }
        bObserved = Movement->IsFalling() || Ctx.Character->GetActorLocation().Z - StartZ > 20.0f;
        if (Now - StartTime >= 0.2)
        {
            SendKey(Ctx, EKeys::SpaceBar, IE_Released);
        }
        if (bObserved)
        {
            Ctx.Test->TestTrue(TEXT("Jump action value is non-zero while held"), ActionMagnitude(Ctx, Ctx.Character->JumpAction) > KINDA_SMALL_NUMBER);
            Ctx.Test->TestTrue(TEXT("Jump starts falling or rises more than 20 uu"), bObserved);
            Record(Ctx, TEXT("jump_started"));
            return true;
        }
        if (Now - StartTime >= 0.5)
        {
            Ctx.Test->TestTrue(TEXT("Jump starts falling or rises more than 20 uu"), bObserved);
            Record(Ctx, TEXT("jump_failed_to_start"));
            return true;
        }
        return false;
    });

    AddPieStep(Context, TEXT("JumpLand"), 3.0f, [StartTime = -1.0](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        if (StartTime < 0.0)
        {
            StartTime = Ctx.World->GetTimeSeconds();
        }
        const bool bLanded = Ctx.Character->GetCharacterMovement() && Ctx.Character->GetCharacterMovement()->IsMovingOnGround();
        if (bLanded || Ctx.World->GetTimeSeconds() - StartTime >= 2.9)
        {
            Ctx.Test->TestTrue(TEXT("Character lands after jump"), bLanded);
            Record(Ctx, TEXT("jump_landed"));
            return true;
        }
        return false;
    });
    QueuePieEnd(Context);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLPieSprintInputTest, "PSL.PIE.SprintInput", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPSLPieSprintInputTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("SprintInput"));
    if (!QueuePieStart(*this, Context))
    {
        return false;
    }

    AddPieStep(Context, TEXT("Walk"), 3.0f, [bStarted = false, bFallbackSent = false, StartTime = -1.0, MaxObservedSpeed = 0.0f](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        const double Now = Ctx.World->GetTimeSeconds();
        if (!bStarted)
        {
            bStarted = true;
            StartTime = Now;
            SendKey(Ctx, EKeys::W, IE_Pressed);
            return false;
        }
        if (Now - StartTime > 0.1 && !EnsureHeldAction(Ctx, EKeys::W, Ctx.Character->MoveAction, IE_Pressed, bFallbackSent))
        {
            return false;
        }
        MaxObservedSpeed = FMath::Max(MaxObservedSpeed, Ctx.Character->GetVelocity().Size2D());
        if (Now - StartTime < 1.0)
        {
            return false;
        }
        const UCharacterMovementComponent* Movement = Ctx.Character->GetCharacterMovement();
        Ctx.Test->TestTrue(TEXT("Walk action value is non-zero"), ActionMagnitude(Ctx, Ctx.Character->MoveAction) > KINDA_SMALL_NUMBER);
        Ctx.Test->TestEqual(TEXT("Walking MaxWalkSpeed"), Movement ? Movement->MaxWalkSpeed : 0.0f, 300.0f, 0.1f);
        Ctx.Test->TestTrue(TEXT("Observed walking speed is at most 320"), MaxObservedSpeed <= 320.0f);
        Record(Ctx, TEXT("walk"));
        return true;
    });

    AddPieStep(Context, TEXT("SprintStart"), 2.0f, [bStarted = false, bFallbackSent = false, StartTime = -1.0, MaxObservedSpeed = 0.0f](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        const double Now = Ctx.World->GetTimeSeconds();
        if (!bStarted)
        {
            bStarted = true;
            StartTime = Now;
            SendKey(Ctx, EKeys::LeftShift, IE_Pressed);
            return false;
        }
        if (Now - StartTime > 0.05 && !EnsureHeldAction(Ctx, EKeys::LeftShift, Ctx.Character->SprintAction, IE_Pressed, bFallbackSent))
        {
            return false;
        }
        MaxObservedSpeed = FMath::Max(MaxObservedSpeed, Ctx.Character->GetVelocity().Size2D());
        const UPSLSurvivalComponent* Survival = Ctx.Character->GetSurvivalComponent();
        const UCharacterMovementComponent* Movement = Ctx.Character->GetCharacterMovement();
        if (Survival && Survival->IsSprinting() && Movement && Movement->MaxWalkSpeed == 600.0f && MaxObservedSpeed >= 590.0f)
        {
            Ctx.Test->TestTrue(TEXT("Sprint action value is non-zero"), ActionMagnitude(Ctx, Ctx.Character->SprintAction) > KINDA_SMALL_NUMBER);
            Record(Ctx, TEXT("sprint_started"));
            return true;
        }
        if (Now - StartTime >= 0.9)
        {
            Ctx.Test->TestTrue(TEXT("Sprint reaches 590+ uu/s within 0.9 seconds"), false);
            Record(Ctx, TEXT("sprint_start_timeout"));
            return true;
        }
        return false;
    });

    AddPieStep(Context, TEXT("SprintStop"), 2.0f, [bStarted = false, StartTime = -1.0](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        const double Now = Ctx.World->GetTimeSeconds();
        if (!bStarted)
        {
            bStarted = true;
            StartTime = Now;
            SendKey(Ctx, EKeys::LeftShift, IE_Released);
            return false;
        }
        const UPSLSurvivalComponent* Survival = Ctx.Character->GetSurvivalComponent();
        const UCharacterMovementComponent* Movement = Ctx.Character->GetCharacterMovement();
        if ((Survival == nullptr || !Survival->IsSprinting()) && Movement && FMath::IsNearlyEqual(Movement->MaxWalkSpeed, 300.0f, 0.1f))
        {
            Record(Ctx, TEXT("sprint_stopped"));
            return true;
        }
        if (Now - StartTime >= 0.9)
        {
            Ctx.Test->TestTrue(TEXT("Sprint stops within 0.9 seconds of Shift release"), false);
            Record(Ctx, TEXT("sprint_stop_timeout"));
            return true;
        }
        return false;
    });

    AddPieStep(Context, TEXT("ReleaseWalk"), 0.8f, [bStarted = false, StartTime = -1.0](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        if (!bStarted)
        {
            bStarted = true;
            StartTime = Ctx.World->GetTimeSeconds();
            SendKey(Ctx, EKeys::W, IE_Released);
        }
        return Ctx.World->GetTimeSeconds() - StartTime >= 0.3;
    });
    QueuePieEnd(Context);
    return true;
}

static void QueueDrainToZero(const TSharedRef<FPSLPieCtx>& Context, const TCHAR* TestName)
{
    AddPieStep(Context, TestName, 12.0f, [TestName, bStarted = false, bFallbackW = false, bFallbackShift = false, LastStamina = -1.0f, NextSample = -1.0](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        const double Now = Ctx.World->GetTimeSeconds();
        const UPSLSurvivalComponent* Survival = Ctx.Character->GetSurvivalComponent();
        if (!Survival)
        {
            Ctx.Test->AddError(TEXT("Character has no survival component"));
            return true;
        }
        if (!bStarted)
        {
            bStarted = true;
            // Fixture, not input: the floor spans +/-2500 uu (create_test_arena.py scale 50) while a full drain
            // plus the follow-up steps cover ~4500 uu. Start at the west edge facing the diagonal; (-2300,-2300)
            // is blocked by cover cube PSL_Cover_03 at (-1200,-1200) (setup_hunt_arena.py).
            const FVector Start = Ctx.Character->GetActorLocation();
            Ctx.Character->SetActorLocation(FVector(-2300.0, -1800.0, Start.Z), false, nullptr, ETeleportType::TeleportPhysics);
            Ctx.PC->SetControlRotation(FRotator(0.0, 45.0, 0.0));
            Record(Ctx, TEXT("drain_fixture"));
            NextSample = Now;
            SendKey(Ctx, EKeys::W, IE_Pressed);
            SendKey(Ctx, EKeys::LeftShift, IE_Pressed);
            return false;
        }
        if (Now - (NextSample - 0.25) > 0.1)
        {
            EnsureHeldAction(Ctx, EKeys::W, Ctx.Character->MoveAction, IE_Pressed, bFallbackW);
            EnsureHeldAction(Ctx, EKeys::LeftShift, Ctx.Character->SprintAction, IE_Pressed, bFallbackShift);
        }
        if (Now >= NextSample)
        {
            const float Stamina = Survival->GetStamina();
            if (LastStamina >= 0.0f && Stamina > 0.0f)
            {
                Ctx.Test->TestTrue(TEXT("Stamina strictly decreases while sprinting"), Stamina < LastStamina);
            }
            LastStamina = Stamina;
            NextSample += 0.25;
            Record(Ctx, TEXT("stamina_sample"));
        }
        if (Survival->GetStamina() <= 0.0f)
        {
            const UCharacterMovementComponent* Movement = Ctx.Character->GetCharacterMovement();
            Ctx.Test->TestTrue(TEXT("Stamina reaches zero"), Survival->GetStamina() <= 0.0f);
            Ctx.Test->TestTrue(TEXT("Sprint locks at zero stamina"), Survival->IsSprintLocked());
            Ctx.Test->TestFalse(TEXT("Sprint stops at zero stamina"), Survival->IsSprinting());
            Ctx.Test->TestEqual(TEXT("Exhausted MaxWalkSpeed"), Movement ? Movement->MaxWalkSpeed : 0.0f, 300.0f, 0.1f);
            Record(Ctx, TEXT("exhausted"));
            if (FCString::Stricmp(TestName, TEXT("StaminaExhaustion")) == 0)
            {
                RequestScreenshot(Ctx, TEXT("after_exhaustion"));
            }
            return true;
        }
        return false;
    });
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLPieStaminaExhaustionTest, "PSL.PIE.StaminaExhaustion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPSLPieStaminaExhaustionTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("StaminaExhaustion"));
    if (!QueuePieStart(*this, Context))
    {
        return false;
    }
    QueueDrainToZero(Context, TEXT("StaminaExhaustion"));
    QueueAwaitScreenshot(Context, TEXT("exhausted_captured"));
    AddPieStep(Context, TEXT("ReleaseExhaustionKeys"), 1.0f, [bStarted = false, StartTime = -1.0](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        if (!bStarted)
        {
            bStarted = true;
            StartTime = Ctx.World->GetTimeSeconds();
            SendKey(Ctx, EKeys::LeftShift, IE_Released);
            SendKey(Ctx, EKeys::W, IE_Released);
        }
        return Ctx.World->GetTimeSeconds() - StartTime >= 0.2;
    });
    QueuePieEnd(Context);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLPieSprintReleaseTest, "PSL.PIE.SprintRelease", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPSLPieSprintReleaseTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("SprintRelease"));
    if (!QueuePieStart(*this, Context))
    {
        return false;
    }
    QueueDrainToZero(Context, TEXT("SprintReleaseDrain"));

    AddPieStep(Context, TEXT("LockedHold"), 4.0f, [bStarted = false, StartTime = -1.0](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        const UPSLSurvivalComponent* Survival = Ctx.Character->GetSurvivalComponent();
        const UCharacterMovementComponent* Movement = Ctx.Character->GetCharacterMovement();
        if (!bStarted)
        {
            bStarted = true;
            StartTime = Ctx.World->GetTimeSeconds();
        }
        Ctx.Test->TestFalse(TEXT("Held sprint does not restart while locked"), Survival && Survival->IsSprinting());
        Ctx.Test->TestTrue(TEXT("Sprint remains locked while Shift is held"), Survival && Survival->IsSprintLocked());
        Ctx.Test->TestEqual(TEXT("Locked sprint stays at walk speed"), Movement ? Movement->MaxWalkSpeed : 0.0f, 300.0f, 0.1f);
        if (Ctx.World->GetTimeSeconds() - StartTime >= 1.5)
        {
            Record(Ctx, TEXT("locked_hold"));
            return true;
        }
        return false;
    });

    AddPieStep(Context, TEXT("ReleaseShiftRegenerate"), 4.0f, [bStarted = false, bFallbackSent = false, StartTime = -1.0, ReleaseStamina = -1.0f](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        const double Now = Ctx.World->GetTimeSeconds();
        const UPSLSurvivalComponent* Survival = Ctx.Character->GetSurvivalComponent();
        if (!bStarted)
        {
            bStarted = true;
            StartTime = Now;
            ReleaseStamina = Survival ? Survival->GetStamina() : -1.0f;
            SendKey(Ctx, EKeys::LeftShift, IE_Released);
            return false;
        }
        if (Now - StartTime > 0.1 && ActionMagnitude(Ctx, Ctx.Character->SprintAction) > KINDA_SMALL_NUMBER && !bFallbackSent)
        {
            SendKeyToPlayerController(Ctx, EKeys::LeftShift, IE_Released);
            bFallbackSent = true;
        }
        if (Now - StartTime >= 1.5)
        {
            Ctx.Test->TestTrue(TEXT("Stamina rises after Shift release"), Survival && ReleaseStamina >= 0.0f && Survival->GetStamina() > ReleaseStamina + 1.0f);
            Ctx.Test->TestFalse(TEXT("Sprint lock clears when Shift is released"), Survival && Survival->IsSprintLocked());
            Record(Ctx, TEXT("released_regeneration"));
            RequestScreenshot(Ctx, TEXT("after_recovery"));
            return true;
        }
        return false;
    });
    QueueAwaitScreenshot(Context, TEXT("recovery_captured"));

    AddPieStep(Context, TEXT("SprintAgain"), 2.0f, [bStarted = false, bFallbackSent = false, StartTime = -1.0](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        const double Now = Ctx.World->GetTimeSeconds();
        const UPSLSurvivalComponent* Survival = Ctx.Character->GetSurvivalComponent();
        const UCharacterMovementComponent* Movement = Ctx.Character->GetCharacterMovement();
        if (!bStarted)
        {
            bStarted = true;
            StartTime = Now;
            SendKey(Ctx, EKeys::LeftShift, IE_Pressed);
            return false;
        }
        if (Now - StartTime > 0.05 && !EnsureHeldAction(Ctx, EKeys::LeftShift, Ctx.Character->SprintAction, IE_Pressed, bFallbackSent))
        {
            return false;
        }
        if (Survival && Survival->IsSprinting() && Movement && FMath::IsNearlyEqual(Movement->MaxWalkSpeed, 600.0f, 0.1f))
        {
            Ctx.Test->TestTrue(TEXT("Sprint restarts after release and regeneration"), Survival->IsSprinting());
            Ctx.Test->TestEqual(TEXT("Restarted sprint MaxWalkSpeed"), Movement->MaxWalkSpeed, 600.0f, 0.1f);
            Record(Ctx, TEXT("sprint_restarted"));
            RequestScreenshot(Ctx, TEXT("after_sprint_unlock"));
            return true;
        }
        return false;
    });
    QueueAwaitScreenshot(Context, TEXT("sprint_unlock_captured"));
    QueueReleaseSprint(Context, TEXT("Sprint still active after unlock screenshot"));

    AddPieStep(Context, TEXT("RegenerationMonotonic"), 5.0f, [bStarted = false, StartTime = -1.0, LastStamina = -1.0f, NextSample = -1.0](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        const double Now = Ctx.World->GetTimeSeconds();
        const UPSLSurvivalComponent* Survival = Ctx.Character->GetSurvivalComponent();
        if (!Survival)
        {
            Ctx.Test->AddError(TEXT("Character has no survival component"));
            return true;
        }
        if (!bStarted)
        {
            bStarted = true;
            StartTime = Now;
            NextSample = Now;
        }
        if (Now >= NextSample)
        {
            const float Stamina = Survival->GetStamina();
            if (LastStamina >= 0.0f)
            {
                Ctx.Test->TestTrue(TEXT("Stamina strictly increases while regenerating"), Stamina > LastStamina || Stamina >= Survival->GetMaxStamina());
            }
            LastStamina = Stamina;
            NextSample += 0.25;
            Record(Ctx, TEXT("regen_sample"));
        }
        return Now - StartTime >= 2.0;
    });
    QueuePieEnd(Context);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLPieHudTest, "PSL.PIE.HUD", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPSLPieHudTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("HUD"));
    if (!QueuePieStart(*this, Context))
    {
        return false;
    }

    AddPieStep(Context, TEXT("HUD"), 3.0f, [bStarted = false, bFallbackW = false, bFallbackShift = false, StartTime = -1.0, InitialFraction = 0.0f](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        const double Now = Ctx.World->GetTimeSeconds();
        APSLHUD* HUD = Ctx.PC->GetHUD<APSLHUD>();
        UPSLSurvivalComponent* Survival = Ctx.Character->GetSurvivalComponent();
        if (!HUD || !Survival)
        {
            Ctx.Test->TestNotNull(TEXT("PlayerController HUD is APSLHUD"), HUD);
            Ctx.Test->TestNotNull(TEXT("Character has survival component"), Survival);
            return true;
        }
        if (!bStarted)
        {
            bStarted = true;
            StartTime = Now;
            // APSLHUD::DrawHUD only runs when FApp::CanEverRender() (HUD.cpp PostRender); under -nullrhi the
            // drawn bar is not observable, so this checks the HUD instance plus the value it would draw.
            Ctx.Test->TestTrue(TEXT("APSLHUD bShowHUD is enabled"), HUD->bShowHUD != 0);
            InitialFraction = APSLHUD::BarFraction(Survival->GetStamina(), Survival->GetMaxStamina());
            Ctx.Test->TestEqual(TEXT("Stamina bar input fraction matches stamina"), InitialFraction, Survival->GetStamina() / Survival->GetMaxStamina(), 1.0e-3f);
            SendKey(Ctx, EKeys::W, IE_Pressed);
            SendKey(Ctx, EKeys::LeftShift, IE_Pressed);
            return false;
        }
        if (Now - StartTime > 0.1)
        {
            EnsureHeldAction(Ctx, EKeys::W, Ctx.Character->MoveAction, IE_Pressed, bFallbackW);
            EnsureHeldAction(Ctx, EKeys::LeftShift, Ctx.Character->SprintAction, IE_Pressed, bFallbackShift);
        }
        if (Now - StartTime < 1.0)
        {
            return false;
        }
        const float CurrentFraction = APSLHUD::BarFraction(Survival->GetStamina(), Survival->GetMaxStamina());
        Ctx.Test->TestEqual(TEXT("Stamina bar input fraction matches post-sprint stamina"), CurrentFraction, Survival->GetStamina() / Survival->GetMaxStamina(), 1.0e-3f);
        Ctx.Test->TestTrue(TEXT("Stamina bar input fraction changes while sprinting"), !FMath::IsNearlyEqual(CurrentFraction, InitialFraction, 1.0e-3f));
        Record(Ctx, TEXT("hud_after_sprint"));
        RequestScreenshot(Ctx, TEXT("after_hud"));
        return true;
    });
    QueueAwaitScreenshot(Context, TEXT("hud_captured"));
    QueueReleaseSprint(Context, TEXT("Sprint still active after HUD screenshot"));
    QueuePieEnd(Context);
    return true;
}

// Returns the placed prey after checking it is AI-driven and a nav mesh exists; otherwise AddError + MarkInvalid.
static APSLPreyCharacter* RequirePrey(FPSLPieCtx& Ctx)
{
    TActorIterator<APSLPreyCharacter> It(Ctx.World);
    APSLPreyCharacter* Prey = It ? *It : nullptr;
    if (!Prey)
    {
        Ctx.MarkInvalid(TEXT("No APSLPreyCharacter in L_TestArena (run scripts/setup_hunt_arena.py)"));
        return nullptr;
    }
    if (!Cast<APSLPreyAIController>(Prey->GetController()))
    {
        Ctx.MarkInvalid(TEXT("Prey is not possessed by APSLPreyAIController"));
        return nullptr;
    }
    UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(Ctx.World);
    if (!NavSys || !NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate))
    {
        Ctx.MarkInvalid(TEXT("UNavigationSystemV1 has no nav data (missing NavMeshBoundsVolume or unbuilt navmesh)"));
        return nullptr;
    }
    return Prey;
}

// Fixture: puts the player Distance uu from the prey on a side clear of the cover cubes (+-1200,+-1200) and
// the floor edge, both actors facing each other (the prey's sight cone is 75 deg half-angle).
static bool PlacePlayerNearPrey(FPSLPieCtx& Ctx, const APSLPreyCharacter& Prey, float Distance)
{
    const FVector PreyLoc = Prey.GetActorLocation();
    for (int32 Step = 0; Step < 8; ++Step)
    {
        const FVector Dir = FVector(1.0, 0.0, 0.0).RotateAngleAxis(Step * 45.0, FVector::UpVector);
        FVector Candidate = PreyLoc + Dir * Distance;
        bool bClear = FMath::Abs(Candidate.X) < 2200.0 && FMath::Abs(Candidate.Y) < 2200.0;
        for (const double CX : {-1200.0, 1200.0})
        {
            for (const double CY : {-1200.0, 1200.0})
            {
                bClear = bClear && FVector2D::Distance(FVector2D(Candidate.X, Candidate.Y), FVector2D(CX, CY)) > 400.0;
                // The prey-to-player segment must not cross a cover cube either (sight line).
                bClear = bClear && FMath::PointDistToSegment(FVector(CX, CY, PreyLoc.Z), PreyLoc, FVector(Candidate.X, Candidate.Y, PreyLoc.Z)) > 400.0;
            }
        }
        if (!bClear)
        {
            continue;
        }
        Candidate.Z = Ctx.Character->GetActorLocation().Z;
        Ctx.Character->SetActorLocation(Candidate, false, nullptr, ETeleportType::TeleportPhysics);
        const FRotator ToPrey = (-Dir).Rotation();
        Ctx.PC->SetControlRotation(FRotator(0.0, ToPrey.Yaw, 0.0));
        Ctx.Character->SetActorRotation(FRotator(0.0, ToPrey.Yaw, 0.0));
        const_cast<APSLPreyCharacter&>(Prey).SetActorRotation(FRotator(0.0, Dir.Rotation().Yaw, 0.0));
        return true;
    }
    Ctx.MarkInvalid(TEXT("No cover-free spot found to place the player near the prey"));
    return false;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLPiePreyWanderTest, "PSL.PIE.PreyWander", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPSLPiePreyWanderTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("PreyWander"));
    if (!QueuePieStart(*this, Context))
    {
        return false;
    }
    AddPieStep(Context, TEXT("PreyWander"), 16.0f, [bStarted = false, bSawWander = false, StartTime = -1.0, Start = FVector::ZeroVector](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        const APSLPreyCharacter* Prey = RequirePrey(Ctx);
        if (!Prey)
        {
            return true;
        }
        const double Now = Ctx.World->GetTimeSeconds();
        if (!bStarted)
        {
            bStarted = true;
            StartTime = Now;
            Start = Prey->GetActorLocation();
            Record(Ctx, TEXT("wander_start"));
            return false;
        }
        bSawWander = bSawWander || PreyStateOf(*Prey) == EPSLPreyState::Wander;
        const float Moved = FVector::Dist2D(Start, Prey->GetActorLocation());
        if ((bSawWander && Moved > 150.0f) || Now - StartTime >= 12.0)
        {
            Ctx.Test->TestTrue(TEXT("Prey entered Wander within 12 s"), bSawWander);
            Ctx.Test->TestTrue(TEXT("Prey moved more than 150 uu within 12 s"), Moved > 150.0f);
            Record(Ctx, TEXT("wander_end"));
            return true;
        }
        return false;
    });
    QueuePieEnd(Context);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLPiePreyFleeTest, "PSL.PIE.PreyFlee", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

static void RecordPrey(FPSLPieCtx& Ctx, const APSLPreyCharacter& Prey, const TCHAR* Step)
{
    const FVector Location = Prey.GetActorLocation();
    const UEnum* StateEnum = StaticEnum<EPSLPreyState>();
    const FString Line = FString::Printf(
        TEXT("{\"step\":\"%s\",\"world_time\":%.6f,\"prey_location\":[%.3f,%.3f,%.3f],\"prey_state\":\"%s\"}"),
        Step, static_cast<double>(Ctx.World->GetTimeSeconds()),
        static_cast<double>(Location.X), static_cast<double>(Location.Y), static_cast<double>(Location.Z),
        *StateEnum->GetNameStringByValue(static_cast<int64>(PreyStateOf(Prey))));
    Ctx.Measurements.Add(Line);
    Ctx.Test->AddInfo(Line);
}

bool FPSLPiePreyFleeTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("PreyFlee"));
    if (!QueuePieStart(*this, Context))
    {
        return false;
    }
    AddPieStep(Context, TEXT("PreyFlee"), 10.0f, [bStarted = false, StartTime = -1.0](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        const APSLPreyCharacter* Prey = RequirePrey(Ctx);
        if (!Prey)
        {
            return true;
        }
        const double Now = Ctx.World->GetTimeSeconds();
        if (!bStarted)
        {
            bStarted = true;
            StartTime = Now;
            if (!PlacePlayerNearPrey(Ctx, *Prey, 500.0f))
            {
                return true;
            }
            RecordPrey(Ctx, *Prey, TEXT("flee_start"));
            return false;
        }
        const float Distance = FVector::Dist2D(Ctx.Character->GetActorLocation(), Prey->GetActorLocation());
        RecordPrey(Ctx, *Prey, TEXT("flee_sample"));
        const bool bFleeing = PreyStateOf(*Prey) == EPSLPreyState::Flee;
        if (Distance > 900.0f || Now - StartTime >= 6.0)
        {
            Ctx.Test->TestTrue(TEXT("Prey is still in Flee once it is beyond FleeRadius (900 uu) of the player"), bFleeing);
            Ctx.Test->TestTrue(TEXT("Prey got beyond 900 uu of the player within 6 s"), Distance > 900.0f);
            Record(Ctx, TEXT("flee_end"));
            RequestScreenshot(Ctx, TEXT("after_flee"));
            return true;
        }
        return false;
    });
    QueueAwaitScreenshot(Context, TEXT("flee_captured"));
    QueuePieEnd(Context);
    return true;
}

struct FPSLBiteSeq
{
    double LastPress = -1000.0;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLPieBiteCombatTest, "PSL.PIE.BiteCombat", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPSLPieBiteCombatTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("BiteCombat"));
    const TSharedRef<FPSLBiteSeq> Seq = MakeShared<FPSLBiteSeq>();
    if (!QueuePieStart(*this, Context))
    {
        return false;
    }

    // First bite, then an immediate second press that the cooldown must reject.
    AddPieStep(Context, TEXT("FirstBite"), 4.0f, [Seq, Phase = 0, T0 = 0.0, bSawAttack = false](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        APSLPreyCharacter* Prey = RequirePrey(Ctx);
        if (!Prey)
        {
            return true;
        }
        const double Now = Ctx.World->GetTimeSeconds();
        if (Phase == 0)
        {
            if (!PlacePlayerNearPrey(Ctx, *Prey, 180.0f))
            {
                return true;
            }
            Ctx.Test->TestEqual(TEXT("Prey starts at 60 health"), Prey->GetStatsComponent()->GetHealth(), 60.0f, 0.01f);
            Ctx.Test->TestEqual(TEXT("Bite count starts at 0"), Ctx.Character->BiteCount, 0);
            HoldPreyStill(Ctx, *Prey);
            T0 = Now;
            Seq->LastPress = Now;
            SendKey(Ctx, EKeys::LeftMouseButton, IE_Pressed);
            Phase = 1;
            return false;
        }
        HoldPreyStill(Ctx, *Prey);
        bSawAttack = bSawAttack || PlayerActiveAction(Ctx) == EPSLDinoAction::Attack;
        if (Phase == 1 && Now - T0 >= 0.1)
        {
            SendKey(Ctx, EKeys::LeftMouseButton, IE_Released);
            Phase = 2;
        }
        else if (Phase == 2 && Now - T0 >= 0.15)
        {
            SendKey(Ctx, EKeys::LeftMouseButton, IE_Pressed);
            Phase = 3;
        }
        else if (Phase == 3 && Now - T0 >= 0.25)
        {
            SendKey(Ctx, EKeys::LeftMouseButton, IE_Released);
            Phase = 4;
        }
        else if (Phase == 4 && Now - T0 >= 0.5)
        {
            Ctx.Test->TestEqual(TEXT("Prey health 60 -> 35 after one bite"), Prey->GetStatsComponent()->GetHealth(), 35.0f, 0.01f);
            Ctx.Test->TestEqual(TEXT("Bite count is 1 (immediate second press ignored by cooldown)"), Ctx.Character->BiteCount, 1);
            Ctx.Test->TestEqual(TEXT("Bite hit exactly one pawn"), Ctx.Character->LastBiteHitCount, 1);
            Ctx.Test->TestTrue(TEXT("Dino anim ActiveAction was Attack after the bite"), bSawAttack);
            Record(Ctx, TEXT("bite_1"));
            RequestScreenshot(Ctx, TEXT("after_bite"));
            return true;
        }
        return false;
    });
    QueueAwaitScreenshot(Context, TEXT("bite_captured"));

    // Two more bites after the cooldown kill the prey (60 - 3 x 25 < 0).
    AddPieStep(Context, TEXT("KillBites"), 10.0f, [Seq, bDown = false, PressTime = 0.0](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        APSLPreyCharacter* Prey = RequirePrey(Ctx);
        if (!Prey)
        {
            return true;
        }
        const double Now = Ctx.World->GetTimeSeconds();
        HoldPreyStill(Ctx, *Prey);
        if (bDown)
        {
            if (Now - PressTime >= 0.1)
            {
                SendKey(Ctx, EKeys::LeftMouseButton, IE_Released);
                bDown = false;
            }
            return false;
        }
        if (Ctx.Character->BiteCount < 3)
        {
            if (Now - Seq->LastPress >= Ctx.Character->BiteCooldown + 0.1)
            {
                SendKey(Ctx, EKeys::LeftMouseButton, IE_Pressed);
                bDown = true;
                PressTime = Now;
                Seq->LastPress = Now;
            }
            return false;
        }
        if (Now - Seq->LastPress < 0.8)
        {
            return false;
        }
        const APSLPreyAIController* AI = Cast<APSLPreyAIController>(Prey->GetController());
        Ctx.Test->TestEqual(TEXT("Bite count is 3"), Ctx.Character->BiteCount, 3);
        Ctx.Test->TestTrue(TEXT("Prey IsDead after three bites"), Prey->GetStatsComponent()->IsDead());
        Ctx.Test->TestTrue(TEXT("Prey AI state is Dead"), AI && AI->GetPreyState() == EPSLPreyState::Dead);
        Ctx.Test->TestTrue(TEXT("Dead prey movement mode is MOVE_None"), Prey->GetCharacterMovement()->MovementMode == MOVE_None);
        Ctx.Test->TestTrue(TEXT("Dead prey capsule object type is ECC_WorldDynamic"), Prey->GetCapsuleComponent()->GetCollisionObjectType() == ECC_WorldDynamic);
        Record(Ctx, TEXT("prey_dead"));
        RequestScreenshot(Ctx, TEXT("prey_dead"));
        return true;
    });
    QueueAwaitScreenshot(Context, TEXT("dead_captured"));
    QueuePieEnd(Context);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLPieBiteMissTest, "PSL.PIE.BiteMiss", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPSLPieBiteMissTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("BiteMiss"));
    if (!QueuePieStart(*this, Context))
    {
        return false;
    }
    AddPieStep(Context, TEXT("BiteMiss"), 4.0f, [Phase = 0, T0 = 0.0](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        const APSLPreyCharacter* Prey = RequirePrey(Ctx);
        if (!Prey)
        {
            return true;
        }
        const double Now = Ctx.World->GetTimeSeconds();
        if (Phase == 0)
        {
            // Fixture: far west of the prey (it wanders within 800 uu of (1800,600)), facing empty floor.
            Ctx.Character->SetActorLocation(FVector(-1800.0, 0.0, Ctx.Character->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
            Ctx.PC->SetControlRotation(FRotator::ZeroRotator);
            Ctx.Character->SetActorRotation(FRotator::ZeroRotator);
            T0 = Now;
            SendKey(Ctx, EKeys::LeftMouseButton, IE_Pressed);
            Phase = 1;
            return false;
        }
        if (Phase == 1 && Now - T0 >= 0.1)
        {
            SendKey(Ctx, EKeys::LeftMouseButton, IE_Released);
            Phase = 2;
        }
        else if (Phase == 2 && Now - T0 >= 0.6)
        {
            Ctx.Test->TestEqual(TEXT("Bite count is 1 (a miss still counts)"), Ctx.Character->BiteCount, 1);
            Ctx.Test->TestEqual(TEXT("Bite hit resolution ran and hit no pawn (-1 would mean still pending)"), Ctx.Character->LastBiteHitCount, 0);
            Ctx.Test->TestEqual(TEXT("Prey took no damage"), Prey->GetStatsComponent()->GetHealth(), 60.0f, 0.01f);
            Record(Ctx, TEXT("bite_miss"));
            return true;
        }
        return false;
    });
    QueuePieEnd(Context);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLPiePreyReacquireTest, "PSL.PIE.PreyReacquire", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Regression: the player stays sensed at 1750 uu (> SafeRadius 1600, < LoseSightRadius 2000) without a new perception
// event, so the old Think() cleared Threat; the prey must still flee once the player returns to 600 uu.
bool FPSLPiePreyReacquireTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("PreyReacquire"));
    if (!QueuePieStart(*this, Context))
    {
        return false;
    }
    AddPieStep(Context, TEXT("PreyReacquire"), 20.0f, [Phase = 0, PhaseStart = 0.0, Dir = FVector::ZeroVector](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        APSLPreyCharacter* Prey = RequirePrey(Ctx);
        if (!Prey)
        {
            return true;
        }
        const double Now = Ctx.World->GetTimeSeconds();
        const auto PlaceAt = [&](float Distance)
        {
            FVector Location = Prey->GetActorLocation() + Dir * Distance;
            Location.Z = Ctx.Character->GetActorLocation().Z;
            Ctx.Character->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
        };
        const auto HoldAndFace = [&]()
        {
            HoldPreyStill(Ctx, *Prey);
            const FRotator ToPlayer = (Ctx.Character->GetActorLocation() - Prey->GetActorLocation()).GetSafeNormal2D().Rotation();
            Prey->SetActorRotation(FRotator(0.0, ToPlayer.Yaw, 0.0));
        };

        if (Phase == 0)
        {
            // Same clear-side rule as PlacePlayerNearPrey (floor edge, cover cubes at +-1200,+-1200), checked for the farthest phase.
            const FVector PreyLoc = Prey->GetActorLocation();
            bool bFound = false;
            for (int32 Step = 0; Step < 8 && !bFound; ++Step)
            {
                const FVector Candidate = FVector(1.0, 0.0, 0.0).RotateAngleAxis(Step * 45.0, FVector::UpVector);
                const FVector Far = PreyLoc + Candidate * 1750.0;
                bool bClear = FMath::Abs(Far.X) < 2200.0 && FMath::Abs(Far.Y) < 2200.0;
                for (const double CX : {-1200.0, 1200.0})
                {
                    for (const double CY : {-1200.0, 1200.0})
                    {
                        bClear = bClear && FVector2D::Distance(FVector2D(Far.X, Far.Y), FVector2D(CX, CY)) > 400.0;
                        bClear = bClear && FMath::PointDistToSegment(FVector(CX, CY, PreyLoc.Z), PreyLoc, FVector(Far.X, Far.Y, PreyLoc.Z)) > 400.0;
                    }
                }
                if (bClear)
                {
                    Dir = Candidate;
                    bFound = true;
                }
            }
            if (!bFound)
            {
                Ctx.MarkInvalid(TEXT("No cover-free direction found for the 1750 uu phase"));
                return true;
            }
            PlaceAt(1400.0f);
            HoldAndFace();
            PhaseStart = Now;
            Phase = 1;
            return false;
        }
        if (Phase == 1 || Phase == 2)
        {
            HoldAndFace();
            if (Now - PhaseStart >= 1.0)
            {
                Ctx.Test->TestTrue(TEXT("Prey is not fleeing while the player is outside FleeRadius"), PreyStateOf(*Prey) != EPSLPreyState::Flee);
                if (Phase == 1)
                {
                    Record(Ctx, TEXT("reacquire_sensed_1400"));
                    PlaceAt(1750.0f);
                    Phase = 2;
                }
                else
                {
                    Record(Ctx, TEXT("reacquire_still_visible_1750"));
                    // Last phase: release the prey (no more HoldPreyStill) and close in.
                    PlaceAt(600.0f);
                    Phase = 3;
                }
                PhaseStart = Now;
            }
            return false;
        }
        const bool bFleeing = PreyStateOf(*Prey) == EPSLPreyState::Flee;
        if (bFleeing || Now - PhaseStart >= 3.0)
        {
            Ctx.Test->TestTrue(TEXT("Prey flees within 3 s of the player closing to 600 uu after it stayed visible at 1750 uu"), bFleeing);
            Record(Ctx, TEXT("reacquire_end"));
            return true;
        }
        return false;
    });
    QueuePieEnd(Context);
    return true;
}

} // namespace

#endif
