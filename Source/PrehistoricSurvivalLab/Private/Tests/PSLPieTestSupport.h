#pragma once

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
#include "GameFramework/SpringArmComponent.h"
#include "HAL/IConsoleManager.h"
#include "Widgets/SWindow.h"

// Shared PIE test helpers (moved verbatim from PSLPieTests.cpp; static -> inline).
namespace PSLPie
{
constexpr const TCHAR* ArenaMapPath = TEXT("/Game/Maps/L_TestArena");
constexpr float FrameDelta = 1.0f / 60.0f;

// Weak object reference that keeps the raw-pointer call syntax (`Ctx.World->X`, `*Ctx.Character`, implicit T*).
// A PIE world torn down mid-test makes Get() null (HasObjects() false) instead of leaving a dangling pointer.
template <typename T>
struct TPSLWeakRef
{
    T* Get() const { return Ptr.Get(); }
    bool IsValid() const { return Ptr.IsValid(); }
    operator T*() const { return Ptr.Get(); }
    T* operator->() const { return Ptr.Get(); }
    T& operator*() const { return *Ptr.Get(); }
    TPSLWeakRef& operator=(T* InPtr) { Ptr = InPtr; return *this; }

private:
    TWeakObjectPtr<T> Ptr;
};

struct FPSLPieCtx
{
    explicit FPSLPieCtx(FAutomationTestBase* InTest, const TCHAR* InName)
        : Test(InTest)
        , TestName(InName)
    {
    }

    bool HasObjects() const
    {
        return World.IsValid() && GameViewport.IsValid() && PC.IsValid() && Character.IsValid();
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
    TPSLWeakRef<UWorld> World;
    TPSLWeakRef<UGameViewportClient> GameViewport;
    TPSLWeakRef<APlayerController> PC;
    TPSLWeakRef<APSLCharacter> Character;
    // When set, WaitForPlayer only accepts a PIE world whose map name contains this (used after OpenLevel).
    FString RequiredMap;
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

inline void AddPieStep(const TSharedRef<FPSLPieCtx>& Context, const TCHAR* Name, float Timeout, TFunction<bool(FPSLPieCtx&)> Function)
{
    ADD_LATENT_AUTOMATION_COMMAND(FPSLPieStep(Context, Name, Timeout, MoveTemp(Function)));
}

inline FString JsonEscape(const FString& Value)
{
    FString Result = Value;
    Result.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
    Result.ReplaceInline(TEXT("\""), TEXT("\\\""));
    return Result;
}

inline const TCHAR* JsonBool(const bool Value)
{
    return Value ? TEXT("true") : TEXT("false");
}

inline const TCHAR* InputEventName(const EInputEvent Event)
{
    return Event == IE_Pressed ? TEXT("pressed") : TEXT("released");
}

inline float ActionMagnitude(const FPSLPieCtx& Context, const UInputAction* Action)
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

inline void LogKeyInput(FPSLPieCtx& Context, const FKey& Key, EInputEvent Event, const FString& Path, bool bViewportReturn, bool bInputKeyReturn)
{
    Context.LastInputPath = Path;
    Context.bLastInputKeyReturn = bInputKeyReturn;
    Context.bLastInputAxisReturn = false;
    Context.InputResults.Add(FString::Printf(
        TEXT("{\"kind\":\"key\",\"key\":\"%s\",\"event\":\"%s\",\"path\":\"%s\",\"viewport_return\":%s,\"input_key_return\":%s}"),
        *JsonEscape(Key.ToString()), InputEventName(Event), *JsonEscape(Path), JsonBool(bViewportReturn), JsonBool(bInputKeyReturn)));
}

inline void LogAxisInput(FPSLPieCtx& Context, const FKey& Key, const FString& Path, bool bAxisReturn, bool bInputKeyReturn)
{
    Context.LastInputPath = Path;
    Context.bLastInputAxisReturn = bAxisReturn;
    Context.bLastInputKeyReturn = bInputKeyReturn;
    Context.InputResults.Add(FString::Printf(
        TEXT("{\"kind\":\"axis\",\"key\":\"%s\",\"path\":\"%s\",\"input_axis_return\":%s,\"input_key_return\":%s}"),
        *JsonEscape(Key.ToString()), *JsonEscape(Path), JsonBool(bAxisReturn), JsonBool(bInputKeyReturn)));
}

// GameViewportClient::InputKey resolves the LocalPlayer from the device id, so INPUTDEVICEID_NONE never reaches the PC.
inline FInputDeviceId PrimaryInputDevice(const FPSLPieCtx& Context)
{
    return UInputDeviceLibrary::GetPrimaryInputDeviceForUser(Context.PC->GetPlatformUserId());
}

inline bool SendKeyToPlayerController(FPSLPieCtx& Context, const FKey& Key, EInputEvent Event)
{
    if (!Context.IsUsable())
    {
        return false;
    }

    const FInputKeyEventArgs Args = FInputKeyEventArgs::CreateSimulated(
        Key, Event, 1.0f, -1, PrimaryInputDevice(Context), false, Context.GameViewport->Viewport);
    const bool bReturn = Context.PC->InputKey(Args);
    LogKeyInput(Context, Key, Event, TEXT("PlayerController.InputKey(fallback)"), false, bReturn);
    return bReturn;
}

inline bool SendKey(FPSLPieCtx& Context, const FKey& Key, EInputEvent Event)
{
    if (!Context.IsUsable())
    {
        return false;
    }

    const FInputKeyEventArgs Args = FInputKeyEventArgs::CreateSimulated(
        Key, Event, 1.0f, -1, PrimaryInputDevice(Context), false, Context.GameViewport->Viewport);
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

inline bool SendAxis(FPSLPieCtx& Context, const FKey& Key, float Delta, bool bForcePlayerController)
{
    if (!Context.IsUsable())
    {
        return false;
    }

    const FInputKeyEventArgs Args(
        Context.GameViewport->Viewport, PrimaryInputDevice(Context), Key, Delta, FrameDelta, 1, FPlatformTime::Cycles64());
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

inline void Record(FPSLPieCtx& Context, const TCHAR* Step)
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

// Frame-time distribution of one perf phase (milliseconds per frame).
struct FPSLFrameStats
{
    int32 Frames = 0;
    double AvgFps = 0.0;
    double AvgMs = 0.0;
    double P50Ms = 0.0;
    double P95Ms = 0.0;
    double P99Ms = 0.0;
    double MaxMs = 0.0;
    int32 Over33 = 0;
    int32 Over50 = 0;
};

// Nearest-rank percentile: index = ceil(Percent * N / 100) - 1 clamped to [0, N-1]; integer math avoids 0.95 * 100 rounding.
inline double PercentileMs(const TArray<double>& Sorted, int32 Percent)
{
    if (Sorted.IsEmpty())
    {
        return 0.0;
    }
    const int32 Count = Sorted.Num();
    return Sorted[FMath::Clamp((Percent * Count + 99) / 100 - 1, 0, Count - 1)];
}

inline FPSLFrameStats ComputeFrameStats(const TArray<double>& FrameMs)
{
    FPSLFrameStats Stats;
    Stats.Frames = FrameMs.Num();
    if (FrameMs.IsEmpty())
    {
        return Stats;
    }
    TArray<double> Sorted = FrameMs;
    Sorted.Sort();
    double Sum = 0.0;
    for (const double Ms : Sorted)
    {
        Sum += Ms;
        Stats.Over33 += Ms > 33.0 ? 1 : 0;
        Stats.Over50 += Ms > 50.0 ? 1 : 0;
    }
    Stats.AvgMs = Sum / Sorted.Num();
    Stats.AvgFps = Stats.AvgMs > 0.0 ? 1000.0 / Stats.AvgMs : 0.0;
    Stats.P50Ms = PercentileMs(Sorted, 50);
    Stats.P95Ms = PercentileMs(Sorted, 95);
    Stats.P99Ms = PercentileMs(Sorted, 99);
    Stats.MaxMs = Sorted.Last();
    return Stats;
}

// JSON members (no braces) so callers can embed them in an object or at top level.
inline FString FrameStatsJsonFields(const FPSLFrameStats& Stats)
{
    return FString::Printf(
        TEXT("\"frames\":%d,\"avgFps\":%.3f,\"avgMs\":%.3f,\"p50Ms\":%.3f,\"p95Ms\":%.3f,\"p99Ms\":%.3f,\"maxMs\":%.3f,\"over33ms\":%d,\"over50ms\":%d"),
        Stats.Frames, Stats.AvgFps, Stats.AvgMs, Stats.P50Ms, Stats.P95Ms, Stats.P99Ms, Stats.MaxMs, Stats.Over33, Stats.Over50);
}

inline void RequestScreenshot(FPSLPieCtx& Context, const TCHAR* Step)
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

    FScreenshotRequest::RequestScreenshot(Filename, true, false, false, FIntRect(), true);
    if (Context.Test)
    {
        Context.Test->AddInfo(FString::Printf(TEXT("Screenshot requested: %s"), *Filename));
    }
}

inline void SaveMeasurements(FPSLPieCtx& Context)
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

inline bool WaitForPlayer(FPSLPieCtx& Context)
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
        if (World && !Context.RequiredMap.IsEmpty() && !World->GetMapName().Contains(Context.RequiredMap))
        {
            continue;
        }
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

inline bool QueuePieStart(FAutomationTestBase& Test, const TSharedRef<FPSLPieCtx>& Context, const TCHAR* Map = ArenaMapPath)
{
    // In the editor, AutomationOpenMap already queues FWaitForEditorNavMeshBuildCommand and
    // FStartPIEForAutomationCommand (UEditorEngine::AutomationLoadMap). Queuing a second start restarts PIE.
    if (!Test.TestTrue(TEXT("AutomationOpenMap returned true"), AutomationOpenMap(Map)))
    {
        return false;
    }

    AddPieStep(Context, TEXT("WaitForPIEPlayer"), 30.0f, [](FPSLPieCtx& Ctx)
    {
        return WaitForPlayer(Ctx);
    });
    return true;
}

inline void QueuePieEnd(const TSharedRef<FPSLPieCtx>& Context)
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
inline void QueueAwaitScreenshot(const TSharedRef<FPSLPieCtx>& Context, const TCHAR* Label)
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

inline void QueueReleaseSprint(const TSharedRef<FPSLPieCtx>& Context, const TCHAR* StillSprintingWhat)
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

inline bool EnsureHeldAction(FPSLPieCtx& Context, const FKey& Key, UInputAction* Action, EInputEvent Event, bool& bFallbackSent)
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

inline EPSLPreyState PreyStateOf(const APSLPreyCharacter& Prey)
{
    const APSLPreyAIController* AI = Cast<APSLPreyAIController>(Prey.GetController());
    return AI ? AI->GetPreyState() : EPSLPreyState::Idle;
}

// Keeps the prey in place for bite tests (the AI re-issues flee moves every 0.2 s) and re-aims the player at it.
inline void HoldPreyStill(FPSLPieCtx& Ctx, APSLPreyCharacter& Prey)
{
    if (AController* AI = Prey.GetController())
    {
        AI->StopMovement();
    }
    Prey.GetCharacterMovement()->StopMovementImmediately();
    const FRotator ToPrey = (Prey.GetActorLocation() - Ctx.Character->GetActorLocation()).GetSafeNormal2D().Rotation();
    Ctx.PC->SetControlRotation(FRotator(0.0, ToPrey.Yaw, 0.0));
    Ctx.Character->SetActorRotation(FRotator(0.0, ToPrey.Yaw, 0.0));
}

inline EPSLDinoAction PlayerActiveAction(const FPSLPieCtx& Ctx)
{
    const UPSLDinoAnimInstance* Anim = Cast<UPSLDinoAnimInstance>(Ctx.Character->GetMesh()->GetAnimInstance());
    return Anim ? Anim->GetActiveAction() : EPSLDinoAction::None;
}

// Shared between the steps of a prey-death test: standing height, when the prey died and where its capsule was.
struct FPSLPreyDeathPose
{
    float AliveHeight = 0.0f;
    double DeathTime = 0.0;
    FVector DeathLocation = FVector::ZeroVector;
};

// ACharacter meshes default to AlwaysTickPose, which does not refresh bones while unrendered (-nullrhi gate), so the
// height below would stay at the reference pose. Test-only: gameplay code is unchanged.
inline void TrackPreyBones(APSLPreyCharacter& Prey)
{
    Prey.GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
}

// Z of bone Hips above the capsule bottom: ~50 uu standing, ~20 uu lying (the mesh is scaled to the capsule, not the raw import size); -1 when the mesh has no Hips bone.
inline float PreyBodyHeight(const APSLPreyCharacter& Prey)
{
    const USkeletalMeshComponent* Mesh = Prey.GetMesh();
    const FName Hips(TEXT("Hips"));
    if (!Mesh || Mesh->GetBoneIndex(Hips) == INDEX_NONE)
    {
        return -1.0f;
    }
    const double CapsuleBottom = Prey.GetActorLocation().Z - Prey.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    return static_cast<float>(Mesh->GetBoneLocation(Hips, EBoneSpaces::WorldSpace).Z - CapsuleBottom);
}

inline EPSLDinoAction PreyActiveAction(const APSLPreyCharacter& Prey)
{
    const UPSLDinoAnimInstance* Anim = Cast<UPSLDinoAnimInstance>(Prey.GetMesh()->GetAnimInstance());
    return Anim ? Anim->GetActiveAction() : EPSLDinoAction::None;
}

// A dead prey must have collapsed: body height <= 0.6 x the standing height.
inline void AssertPreyCollapsed(FPSLPieCtx& Ctx, const APSLPreyCharacter& Prey, float AliveHeight, const TCHAR* When)
{
    const float Height = PreyBodyHeight(Prey);
    Ctx.Test->AddInfo(FString::Printf(TEXT("%s: prey body height %.1f, alive %.1f"), When, Height, AliveHeight));
    Ctx.Test->TestTrue(
        FString::Printf(TEXT("Dead prey collapsed %s (height %.1f <= 0.6 x alive %.1f)"), When, Height, AliveHeight),
        Height >= 0.0f && Height <= 0.6f * AliveHeight);
}

} // namespace PSLPie

#endif
