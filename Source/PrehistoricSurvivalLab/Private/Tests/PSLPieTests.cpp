#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Camera/CameraComponent.h"
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
#include "InputKeyEventArgs.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PSLCharacter.h"
#include "PSLGameMode.h"
#include "PSLHUD.h"
#include "PSLSurvivalComponent.h"
#include "UnrealClient.h"

namespace
{
constexpr const TCHAR* MapPath = TEXT("/Game/Maps/L_TestArena");
constexpr float FrameDelta = 1.0f / 60.0f;

struct FPSLPieCtx
{
    explicit FPSLPieCtx(FAutomationTestBase* InTest, const TCHAR* InName)
        : Test(InTest)
        , TestName(InName)
    {
    }

    bool HasObjects() const
    {
        return World != nullptr && GameViewport != nullptr && PC != nullptr && Character != nullptr
            && IsValid(World) && IsValid(GameViewport) && IsValid(PC) && IsValid(Character);
    }

    bool IsUsable() const
    {
        return !bInvalid && HasObjects() && World->WorldType == EWorldType::PIE;
    }

    void MarkInvalid(const FString& Message)
    {
        if (!bInvalid)
        {
            bInvalid = true;
            if (Test)
            {
                Test->AddError(Message);
            }
        }
    }

    FAutomationTestBase* Test = nullptr;
    FString TestName;
    UWorld* World = nullptr;
    UGameViewportClient* GameViewport = nullptr;
    APlayerController* PC = nullptr;
    APSLCharacter* Character = nullptr;
    bool bReady = false;
    double ReadyWorldTime = 0.0;
    bool bInvalid = false;
    TArray<FString> Measurements;
    TArray<FString> InputResults;
    FString LastInputPath = TEXT("none");
    bool bLastInputKeyReturn = false;
    bool bLastInputAxisReturn = false;
};

class FPSLPieStep final : public IAutomationLatentCommand
{
public:
    FPSLPieStep(TSharedRef<FPSLPieCtx> InContext, const TCHAR* InName, float InTimeout, TFunction<bool(FPSLPieCtx&)> InFunction)
        : Context(MoveTemp(InContext))
        , Name(InName)
        , Timeout(InTimeout)
        , Function(MoveTemp(InFunction))
    {
    }

    virtual bool Update() override
    {
        if (Context->bReady && !Context->HasObjects() && !Context->bInvalid)
        {
            Context->MarkInvalid(FString::Printf(TEXT("PIE context was lost during step %s"), *Name));
            return true;
        }

        if (Function(*Context))
        {
            return true;
        }

        if (GetCurrentRunTime() >= Timeout)
        {
            Context->MarkInvalid(FString::Printf(TEXT("PIE step %s timed out after %.1f seconds"), *Name, Timeout));
            return true;
        }
        return false;
    }

private:
    TSharedRef<FPSLPieCtx> Context;
    FString Name;
    float Timeout;
    TFunction<bool(FPSLPieCtx&)> Function;
};

static void AddPieStep(const TSharedRef<FPSLPieCtx>& Context, const TCHAR* Name, float Timeout, TFunction<bool(FPSLPieCtx&)> Function)
{
    ADD_LATENT_AUTOMATION_COMMAND(FPSLPieStep(Context, Name, Timeout, MoveTemp(Function)));
}

static FString JsonEscape(const FString& Value)
{
    FString Result = Value;
    Result.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
    Result.ReplaceInline(TEXT("\""), TEXT("\\\""));
    return Result;
}

static const TCHAR* JsonBool(const bool Value)
{
    return Value ? TEXT("true") : TEXT("false");
}

static const TCHAR* InputEventName(const EInputEvent Event)
{
    return Event == IE_Pressed ? TEXT("pressed") : TEXT("released");
}

static float ActionMagnitude(const FPSLPieCtx& Context, const UInputAction* Action)
{
    if (!Action || !Context.PC)
    {
        return 0.0f;
    }

    if (const UEnhancedPlayerInput* EnhancedInput = Cast<UEnhancedPlayerInput>(Context.PC->PlayerInput))
    {
        return EnhancedInput->GetActionValue(Action).GetMagnitude();
    }
    return 0.0f;
}

static void LogKeyInput(FPSLPieCtx& Context, const FKey& Key, EInputEvent Event, const FString& Path, bool bViewportReturn, bool bInputKeyReturn)
{
    Context.LastInputPath = Path;
    Context.bLastInputKeyReturn = bInputKeyReturn;
    Context.bLastInputAxisReturn = false;
    Context.InputResults.Add(FString::Printf(
        TEXT("{\"kind\":\"key\",\"key\":\"%s\",\"event\":\"%s\",\"path\":\"%s\",\"viewport_return\":%s,\"input_key_return\":%s}"),
        *JsonEscape(Key.ToString()), InputEventName(Event), *JsonEscape(Path), JsonBool(bViewportReturn), JsonBool(bInputKeyReturn)));
}

static void LogAxisInput(FPSLPieCtx& Context, const FKey& Key, const FString& Path, bool bAxisReturn, bool bInputKeyReturn)
{
    Context.LastInputPath = Path;
    Context.bLastInputAxisReturn = bAxisReturn;
    Context.bLastInputKeyReturn = bInputKeyReturn;
    Context.InputResults.Add(FString::Printf(
        TEXT("{\"kind\":\"axis\",\"key\":\"%s\",\"path\":\"%s\",\"input_axis_return\":%s,\"input_key_return\":%s}"),
        *JsonEscape(Key.ToString()), *JsonEscape(Path), JsonBool(bAxisReturn), JsonBool(bInputKeyReturn)));
}

static bool SendKeyToPlayerController(FPSLPieCtx& Context, const FKey& Key, EInputEvent Event)
{
    if (!Context.IsUsable())
    {
        return false;
    }

    const FInputKeyEventArgs Args = FInputKeyEventArgs::CreateSimulated(
        Key, Event, 1.0f, -1, INPUTDEVICEID_NONE, false, Context.GameViewport->Viewport);
    const bool bReturn = Context.PC->InputKey(Args);
    LogKeyInput(Context, Key, Event, TEXT("PlayerController.InputKey(fallback)"), false, bReturn);
    return bReturn;
}

static bool SendKey(FPSLPieCtx& Context, const FKey& Key, EInputEvent Event)
{
    if (!Context.IsUsable())
    {
        return false;
    }

    const FInputKeyEventArgs Args = FInputKeyEventArgs::CreateSimulated(
        Key, Event, 1.0f, -1, INPUTDEVICEID_NONE, false, Context.GameViewport->Viewport);
    const bool bViewportReturn = Context.GameViewport->InputKey(Args);
    if (bViewportReturn)
    {
        LogKeyInput(Context, Key, Event, TEXT("GameViewport.InputKey"), true, bViewportReturn);
        return bViewportReturn;
    }

    const bool bInputKeyReturn = Context.PC->InputKey(Args);
    LogKeyInput(Context, Key, Event, TEXT("PlayerController.InputKey(fallback)"), false, bInputKeyReturn);
    return bInputKeyReturn;
}

static bool SendAxis(FPSLPieCtx& Context, const FKey& Key, float Delta, bool bForcePlayerController)
{
    if (!Context.IsUsable())
    {
        return false;
    }

    const FInputKeyEventArgs Args(
        Context.GameViewport->Viewport, INPUTDEVICEID_NONE, Key, Delta, FrameDelta, 1, FPlatformTime::Cycles64());
    if (!bForcePlayerController)
    {
        const bool bAxisReturn = Context.GameViewport->InputAxis(Args);
        if (bAxisReturn)
        {
            LogAxisInput(Context, Key, TEXT("GameViewport.InputAxis"), bAxisReturn, false);
            return bAxisReturn;
        }
    }

    const bool bInputKeyReturn = Context.PC->InputKey(Args);
    LogAxisInput(Context, Key, TEXT("PlayerController.InputKey(axis fallback)"), false, bInputKeyReturn);
    return bInputKeyReturn;
}

static void Record(FPSLPieCtx& Context, const TCHAR* Step)
{
    if (!Context.HasObjects())
    {
        return;
    }

    const UCharacterMovementComponent* Movement = Context.Character->GetCharacterMovement();
    const UPSLSurvivalComponent* Survival = Context.Character->GetSurvivalComponent();
    const FVector Location = Context.Character->GetActorLocation();
    const FVector Velocity = Context.Character->GetVelocity();
    const FRotator Rotation = Context.PC->GetControlRotation();
    const FString Inputs = Context.InputResults.Num() > 0 ? FString::Join(Context.InputResults, TEXT(",")) : FString();
    const FString Line = FString::Printf(
        TEXT("{\"step\":\"%s\",\"world_time\":%.6f,\"location\":[%.3f,%.3f,%.3f],\"velocity\":[%.3f,%.3f,%.3f],\"max_walk_speed\":%.3f,\"stamina\":%.3f,\"is_sprinting\":%s,\"is_sprint_locked\":%s,\"control_rotation\":[%.3f,%.3f,%.3f],\"injection_path\":\"%s\",\"input_key_return\":%s,\"input_axis_return\":%s,\"input_returns\":[%s]}"),
        Step,
        static_cast<double>(Context.World->GetTimeSeconds()),
        static_cast<double>(Location.X), static_cast<double>(Location.Y), static_cast<double>(Location.Z),
        static_cast<double>(Velocity.X), static_cast<double>(Velocity.Y), static_cast<double>(Velocity.Z),
        Movement ? static_cast<double>(Movement->MaxWalkSpeed) : 0.0,
        Survival ? static_cast<double>(Survival->GetStamina()) : 0.0,
        JsonBool(Survival && Survival->IsSprinting()), JsonBool(Survival && Survival->IsSprintLocked()),
        static_cast<double>(Rotation.Pitch), static_cast<double>(Rotation.Yaw), static_cast<double>(Rotation.Roll),
        *JsonEscape(Context.LastInputPath), JsonBool(Context.bLastInputKeyReturn), JsonBool(Context.bLastInputAxisReturn), *Inputs);
    Context.Measurements.Add(Line);
    if (Context.Test)
    {
        Context.Test->AddInfo(Line);
    }
}

static void RequestScreenshot(FPSLPieCtx& Context, const TCHAR* Step)
{
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("PIE"));
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Filename = FPaths::Combine(Directory, Context.TestName + TEXT("_") + Step + TEXT(".png"));
    if (!FApp::CanEverRender())
    {
        if (Context.Test)
        {
            Context.Test->AddInfo(FString::Printf(TEXT("Screenshot skipped for %s: FApp::CanEverRender() is false"), *Filename));
        }
        return;
    }

    FScreenshotRequest::RequestScreenshot(Filename, false, false);
    if (Context.Test)
    {
        Context.Test->AddInfo(FString::Printf(TEXT("Screenshot requested: %s"), *Filename));
    }
}

static void SaveMeasurements(FPSLPieCtx& Context)
{
    if (Context.IsUsable())
    {
        Record(Context, TEXT("test_end"));
    }

    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("PIE"));
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Filename = FPaths::Combine(Directory, Context.TestName + TEXT(".json"));
    FString Json = TEXT("[");
    for (int32 Index = 0; Index < Context.Measurements.Num(); ++Index)
    {
        if (Index > 0)
        {
            Json += TEXT(",");
        }
        Json += Context.Measurements[Index];
    }
    Json += TEXT("]");

    const bool bSaved = FFileHelper::SaveStringToFile(Json, *Filename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    if (Context.Test)
    {
        Context.Test->TestTrue(FString::Printf(TEXT("Measurement log saved: %s"), *Filename), bSaved);
    }
}

static bool WaitForPlayer(FPSLPieCtx& Context)
{
    if (Context.bReady)
    {
        return Context.IsUsable() && Context.World->GetTimeSeconds() - Context.ReadyWorldTime >= 0.5;
    }

    if (!GEditor)
    {
        return false;
    }

    for (const FWorldContext& WorldContext : GEditor->GetWorldContexts())
    {
        if (WorldContext.WorldType != EWorldType::PIE || !WorldContext.GameViewport)
        {
            continue;
        }

        UWorld* World = WorldContext.World();
        UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
        APlayerController* PC = GameInstance ? GameInstance->GetFirstLocalPlayerController(World) : nullptr;
        APSLCharacter* Character = PC ? Cast<APSLCharacter>(PC->GetPawn()) : nullptr;
        if (World && PC && Character)
        {
            Context.World = World;
            Context.GameViewport = WorldContext.GameViewport;
            Context.PC = PC;
            Context.Character = Character;
            Context.bReady = true;
            Context.ReadyWorldTime = World->GetTimeSeconds();
            return false;
        }
    }
    return false;
}

static bool QueuePieStart(FAutomationTestBase& Test, const TSharedRef<FPSLPieCtx>& Context)
{
    // In the editor, AutomationOpenMap already queues FWaitForEditorNavMeshBuildCommand and
    // FStartPIEForAutomationCommand (UEditorEngine::AutomationLoadMap). Queuing a second start restarts PIE.
    if (!Test.TestTrue(TEXT("AutomationOpenMap returned true"), AutomationOpenMap(MapPath)))
    {
        return false;
    }

    AddPieStep(Context, TEXT("WaitForPIEPlayer"), 30.0f, [](FPSLPieCtx& Ctx)
    {
        return WaitForPlayer(Ctx);
    });
    return true;
}

static void QueuePieEnd(const TSharedRef<FPSLPieCtx>& Context)
{
    // A screenshot requested in the previous step is written during a later viewport draw; let PIE render first.
    AddPieStep(Context, TEXT("ScreenshotSettle"), 2.0f, [Frames = 0](FPSLPieCtx& Ctx) mutable
    {
        return !Ctx.IsUsable() || ++Frames >= 10;
    });
    AddPieStep(Context, TEXT("SaveMeasurements"), 2.0f, [](FPSLPieCtx& Ctx)
    {
        SaveMeasurements(Ctx);
        return true;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
}

// RequestScreenshot is served on a later viewport draw (UGameViewportClient::ProcessScreenShots); keep the
// current input held until that draw has happened, then record the state the capture shows.
static void QueueAwaitScreenshot(const TSharedRef<FPSLPieCtx>& Context, const TCHAR* Label)
{
    AddPieStep(Context, TEXT("AwaitScreenshot"), 2.0f, [Label](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable() || !FApp::CanEverRender())
        {
            return true;
        }
        if (FScreenshotRequest::IsScreenshotRequested())
        {
            return false;
        }
        Record(Ctx, Label);
        return true;
    });
}

static void QueueReleaseSprint(const TSharedRef<FPSLPieCtx>& Context, const TCHAR* StillSprintingWhat)
{
    AddPieStep(Context, TEXT("ReleaseSprint"), 1.0f, [StillSprintingWhat](FPSLPieCtx& Ctx)
    {
        if (Ctx.IsUsable())
        {
            const UPSLSurvivalComponent* Survival = Ctx.Character->GetSurvivalComponent();
            Ctx.Test->TestTrue(StillSprintingWhat, Survival && Survival->IsSprinting());
            SendKey(Ctx, EKeys::LeftShift, IE_Released);
            SendKey(Ctx, EKeys::W, IE_Released);
        }
        return true;
    });
}

static bool EnsureHeldAction(FPSLPieCtx& Context, const FKey& Key, UInputAction* Action, EInputEvent Event, bool& bFallbackSent)
{
    if (ActionMagnitude(Context, Action) > KINDA_SMALL_NUMBER)
    {
        return true;
    }
    if (!bFallbackSent)
    {
        SendKeyToPlayerController(Context, Key, Event);
        bFallbackSent = true;
    }
    return ActionMagnitude(Context, Action) > KINDA_SMALL_NUMBER;
}

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
            // plus the follow-up steps cover ~4500 uu. Start at a corner facing the diagonal (~6600 uu of floor).
            const FVector Start = Ctx.Character->GetActorLocation();
            Ctx.Character->SetActorLocation(FVector(-2300.0, -2300.0, Start.Z), false, nullptr, ETeleportType::TeleportPhysics);
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

} // namespace

#endif
