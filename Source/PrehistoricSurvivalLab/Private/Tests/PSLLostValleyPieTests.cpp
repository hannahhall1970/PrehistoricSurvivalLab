#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "PSLPieTestSupport.h"
#include "Landscape.h"
#include "LandscapeProxy.h"
#include "LandscapeComponent.h"
#include "Materials/MaterialInstance.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "GameFramework/PlayerStart.h"
#include "Scalability.h"
#include "Framework/Docking/TabManager.h"
#include "Misc/App.h"
#include "PSLPredatorAIController.h"
#include "HAL/IConsoleManager.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "PSLPredatorCharacter.h"

extern ENGINE_API float GAverageFPS;
extern ENGINE_API float GAverageMS;

namespace
{
using namespace PSLPie;

constexpr const TCHAR* LostValleyMap = TEXT("/Game/Maps/L_LostValley");

bool GroundAt(UWorld* World, double X, double Y, FVector& Out)
{
    TArray<FHitResult> Hits;
    if (World && World->LineTraceMultiByChannel(Hits, FVector(X, Y, 8000.0), FVector(X, Y, -8000.0), ECC_Visibility))
    {
        for (const FHitResult& Hit : Hits)
        {
            if (Cast<ALandscapeProxy>(Hit.GetActor()))
            {
                Out = Hit.ImpactPoint + FVector(0.0, 0.0, 120.0);
                return true;
            }
        }
    }
    return false;
}

void TeleportTo(FPSLPieCtx& Ctx, const FVector& Location)
{
    Ctx.Character->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
    Ctx.Character->GetCharacterMovement()->StopMovementImmediately();
}

void FaceYawToward(FPSLPieCtx& Ctx, const FVector& Target)
{
    const FRotator Yaw(0.0, (Target - Ctx.Character->GetActorLocation()).GetSafeNormal2D().Rotation().Yaw, 0.0);
    Ctx.PC->SetControlRotation(Yaw);
    Ctx.Character->SetActorRotation(Yaw);
}

// Places the player Distance (2D) from Center on the first of 8 directions whose ground Z is within 300 of the target Z
// and, when SightFrom is set, that has a clear visibility line to SightFrom.
bool PlaceNear(FPSLPieCtx& Ctx, const FVector& Center, double Distance, const AActor* SightFrom = nullptr)
{
    for (int32 Index = 0; Index < 8; ++Index)
    {
        const double Angle = FMath::DegreesToRadians(45.0 * Index);
        FVector Ground;
        if (GroundAt(Ctx.World, Center.X + Distance * FMath::Cos(Angle), Center.Y + Distance * FMath::Sin(Angle), Ground)
            && FMath::Abs(Ground.Z - Center.Z) <= 300.0)
        {
            if (SightFrom)
            {
                FCollisionQueryParams Params;
                Params.AddIgnoredActor(SightFrom);
                Params.AddIgnoredActor(Ctx.Character.Get());
                FHitResult Hit;
                if (Ctx.World->LineTraceSingleByChannel(Hit, SightFrom->GetActorLocation() + FVector(0.0, 0.0, 60.0), Ground, ECC_Visibility, Params))
                {
                    continue;
                }
            }
            TeleportTo(Ctx, Ground);
            FaceYawToward(Ctx, Center);
            return true;
        }
    }
    return false;
}

TArray<APSLPreyCharacter*> GetPrey(UWorld* World)
{
    TArray<APSLPreyCharacter*> Prey;
    for (TActorIterator<APSLPreyCharacter> It(World); It; ++It)
    {
        Prey.Add(*It);
    }
    return Prey;
}

AActor* FindByLabel(UWorld* World, const TCHAR* Label)
{
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (It->GetActorLabel() == Label)
        {
            return *It;
        }
    }
    return nullptr;
}

double Dist2D(const AActor& A, const AActor& B)
{
    return FVector::Dist2D(A.GetActorLocation(), B.GetActorLocation());
}

// True when the cvar now holds Value (a console/ini set outranks ECVF_SetByCode and would be kept).
bool SetPredatorSpawnCvar(int32 Value)
{
    IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(TEXT("psl.Predator.Spawn"));
    if (V)
    {
        V->Set(Value, ECVF_SetByCode);
    }
    return V && V->GetInt() == Value;
}

// Projects both points (extent 300,300,1000) and runs FindPathSync; true only for a successful, valid, non-partial path.
bool CompletePathBetween(UWorld* World, const FVector& From, const FVector& To, FVector* OutFromNav = nullptr, FVector* OutToNav = nullptr, float* OutLength = nullptr)
{
    UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(World);
    const ANavigationData* NavData = Nav ? Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) : nullptr;
    FNavLocation FromNav;
    FNavLocation ToNav;
    if (!NavData || !Nav->ProjectPointToNavigation(From, FromNav, FVector(300.0, 300.0, 1000.0))
        || !Nav->ProjectPointToNavigation(To, ToNav, FVector(300.0, 300.0, 1000.0)))
    {
        return false;
    }
    const FPathFindingResult Result = Nav->FindPathSync(FPathFindingQuery(nullptr, *NavData, FromNav.Location, ToNav.Location));
    if (!Result.IsSuccessful() || !Result.Path.IsValid() || Result.Path->IsPartial())
    {
        return false;
    }
    if (OutFromNav)
    {
        *OutFromNav = FromNav.Location;
    }
    if (OutToNav)
    {
        *OutToNav = ToNav.Location;
    }
    if (OutLength)
    {
        *OutLength = static_cast<float>(Result.Path->GetLength());
    }
    return true;
}

// Physical mouse input reaches the GUI PIE window and rotates the camera mid-capture; ignore look input for the whole test.
// Steps that call SetControlRotation are unaffected. The lock is taken once the pawn is usable and always released at the end.
// bWithPredator: psl.Predator.Spawn=2 before BeginPlay lets the GameMode spawn the predator under automation.
bool QueueLostValleyStart(FAutomationTestBase& Test, const TSharedRef<FPSLPieCtx>& Context, bool bWithPredator = false)
{
    // Every valley start writes the cvar, so a value of 2 left by an aborted Predator test cannot leak into the next one.
    if (!SetPredatorSpawnCvar(bWithPredator ? 2 : 1) && bWithPredator)
    {
        Test.AddError(TEXT("psl.Predator.Spawn could not be set to 2 (missing, or overridden by console/ini)"));
        return false;
    }
    if (!QueuePieStart(Test, Context, LostValleyMap))
    {
        SetPredatorSpawnCvar(1); // QueueLostValleyEnd is never queued on this path
        return false;
    }
    AddPieStep(Context, TEXT("IgnoreLookInput"), 2.0f, [](FPSLPieCtx& Ctx)
    {
        if (Ctx.IsUsable())
        {
            Ctx.PC->SetIgnoreLookInput(true);
        }
        return true;
    });
    return true;
}

void QueueLostValleyEnd(const TSharedRef<FPSLPieCtx>& Context)
{
    // HasObjects, not IsUsable: also release after a failed or timed-out step.
    AddPieStep(Context, TEXT("RestoreLookInput"), 1.0f, [](FPSLPieCtx& Ctx)
    {
        SetPredatorSpawnCvar(1); // idempotent; runs even after failed steps
        if (Ctx.HasObjects())
        {
            Ctx.PC->ResetIgnoreLookInput();
        }
        return true;
    });
    QueuePieEnd(Context);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLLostValleyLoadsTest, "PSL.PIE.LostValley.Loads", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPSLLostValleyLoadsTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("LostValley_Loads"));
    if (!QueueLostValleyStart(*this, Context))
    {
        return false;
    }
    AddPieStep(Context, TEXT("LoadsAssertions"), 5.0f, [](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        UWorld* World = Ctx.World;
        Ctx.Test->TestTrue(TEXT("Map is L_LostValley"), World->GetMapName().Contains(TEXT("L_LostValley")));

        // The cooked game uses the component material instances saved in the map as-is, so they must derive from
        // M_PSL_Terrain (built with a null material they derive from WorldGridMaterial: a checkerboard ground).
        const UMaterialInterface* Terrain = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Environment/Materials/M_PSL_Terrain.M_PSL_Terrain"));
        Ctx.Test->TestNotNull(TEXT("M_PSL_Terrain loads"), Terrain);
        int32 LandscapeCount = 0;
        int32 ComponentCount = 0;
        int32 TerrainComponents = 0;
        for (TActorIterator<ALandscape> It(World); It; ++It)
        {
            ++LandscapeCount;
            TArray<ULandscapeComponent*> Components;
            It->GetComponents(Components);
            ComponentCount += Components.Num();
            for (const ULandscapeComponent* Component : Components)
            {
                const UMaterialInstance* Instance = Component->GetMaterialInstanceCount(false) > 0 ? Component->GetMaterialInstance(0, false) : nullptr;
                TerrainComponents += Terrain && Instance && Instance->GetMaterial() == Terrain->GetMaterial() ? 1 : 0;
            }
        }
        Ctx.Test->TestEqual(TEXT("Landscape count"), LandscapeCount, 1);
        Ctx.Test->TestEqual(TEXT("Landscape component count"), ComponentCount, 16);
        Ctx.Test->TestEqual(TEXT("Landscape components whose saved material instance derives from M_PSL_Terrain"), TerrainComponents, ComponentCount);

        int32 HismWithInstances = 0;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            TArray<UHierarchicalInstancedStaticMeshComponent*> Hism;
            It->GetComponents(Hism);
            for (const UHierarchicalInstancedStaticMeshComponent* Component : Hism)
            {
                HismWithInstances += Component->GetInstanceCount() > 0 ? 1 : 0;
            }
        }
        Ctx.Test->TestTrue(TEXT("At least 5 HISM components with instances"), HismWithInstances >= 5);

        const TArray<APSLPreyCharacter*> Prey = GetPrey(World);
        Ctx.Test->TestEqual(TEXT("Prey count"), Prey.Num(), 3);
        for (const APSLPreyCharacter* P : Prey)
        {
            Ctx.Test->TestTrue(TEXT("Prey has APSLPreyAIController"), Cast<APSLPreyAIController>(P->GetController()) != nullptr);
        }

        UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(World);
        Ctx.Test->TestTrue(TEXT("Nav data present"), Nav && Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) != nullptr);

        int32 StartCount = 0;
        for (TActorIterator<APlayerStart> It(World); It; ++It)
        {
            ++StartCount;
        }
        Ctx.Test->TestTrue(TEXT("At least one PlayerStart"), StartCount >= 1);

        AActor* Sun = FindByLabel(World, TEXT("PSL_Sun"));
        AActor* Sky = FindByLabel(World, TEXT("PSL_SkyLight"));
        AActor* Fog = FindByLabel(World, TEXT("PSL_Fog"));
        AActor* Post = FindByLabel(World, TEXT("PSL_PostProcess"));
        AActor* Atmosphere = FindByLabel(World, TEXT("PSL_SkyAtmosphere"));
        Ctx.Test->TestTrue(TEXT("PSL_Sun is ADirectionalLight"), Cast<ADirectionalLight>(Sun) != nullptr);
        Ctx.Test->TestTrue(TEXT("PSL_SkyLight is ASkyLight"), Cast<ASkyLight>(Sky) != nullptr);
        Ctx.Test->TestTrue(TEXT("PSL_Fog is AExponentialHeightFog"), Cast<AExponentialHeightFog>(Fog) != nullptr);
        Ctx.Test->TestTrue(TEXT("PSL_PostProcess is APostProcessVolume"), Cast<APostProcessVolume>(Post) != nullptr);
        Ctx.Test->TestTrue(TEXT("PSL_SkyAtmosphere has USkyAtmosphereComponent"),
            Atmosphere && Atmosphere->FindComponentByClass<USkyAtmosphereComponent>() != nullptr);
        Record(Ctx, TEXT("loads"));
        return true;
    });
    QueueLostValleyEnd(Context);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLLostValleySpawnTest, "PSL.PIE.LostValley.Spawn", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPSLLostValleySpawnTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("LostValley_Spawn"));
    if (!QueueLostValleyStart(*this, Context))
    {
        return false;
    }
    AddPieStep(Context, TEXT("SpawnOnGround"), 3.0f, [bChecked = false](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        if (!bChecked)
        {
            bChecked = true;
            AActor* Start = FindByLabel(Ctx.World, TEXT("PSL_PlayerStart"));
            if (!Start)
            {
                Ctx.MarkInvalid(TEXT("PSL_PlayerStart not found"));
                return true;
            }
            Ctx.Test->TestTrue(TEXT("Player within 600 uu (2D) of PSL_PlayerStart"), Dist2D(*Ctx.Character, *Start) <= 600.0);
        }
        return Ctx.Character->GetMovementComponent()->IsMovingOnGround();
    });
    AddPieStep(Context, TEXT("SpawnScreenshot"), 2.0f, [](FPSLPieCtx& Ctx)
    {
        if (Ctx.IsUsable())
        {
            Record(Ctx, TEXT("spawn"));
            RequestScreenshot(Ctx, TEXT("after_spawn"));
        }
        return true;
    });
    QueueAwaitScreenshot(Context, TEXT("after_spawn"));
    // 1920x1080 path: only when the PIE viewport has its own window; resize the client area iteratively until the viewport
    // is exactly 1920x1080, shoot hud_1080, then always restore the stored client size.
    const TSharedRef<FVector2D> OriginalClient = MakeShared<FVector2D>(FVector2D::ZeroVector);
    AddPieStep(Context, TEXT("Resize1080"), 8.0f, [OriginalClient, Pass = 0, StepTime = -1.0](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable() || !FApp::CanEverRender())
        {
            return true;
        }
        const TSharedPtr<SWindow> Window = Ctx.GameViewport->GetWindow();
        if (!Window.IsValid() || !Ctx.GameViewport->Viewport || Window == FGlobalTabmanager::Get()->GetRootWindow())
        {
            Ctx.Test->AddInfo(TEXT("hud_1080 skipped: PIE not in new window"));
            return true;
        }
        const double Now = Ctx.World->GetTimeSeconds();
        if (StepTime >= 0.0 && Now - StepTime < 0.5)
        {
            return false;
        }
        if (StepTime < 0.0)
        {
            *OriginalClient = FVector2D(Window->GetClientSizeInScreen());
        }
        const FIntPoint Size = Ctx.GameViewport->Viewport->GetSizeXY();
        const bool bExact = Size.X == 1920 && Size.Y == 1080;
        if (!bExact && Pass < 3)
        {
            ++Pass;
            StepTime = Now;
            Window->Resize(FVector2D(Window->GetClientSizeInScreen()) + FVector2D(1920 - Size.X, 1080 - Size.Y));
            return false;
        }
        Ctx.Test->TestTrue(FString::Printf(TEXT("Viewport is 1920x1080 after %d resize passes (was %dx%d)"), Pass, Size.X, Size.Y), bExact);
        if (bExact)
        {
            RequestScreenshot(Ctx, TEXT("hud_1080"));
        }
        return true;
    });
    QueueAwaitScreenshot(Context, TEXT("hud_1080"));
    AddPieStep(Context, TEXT("Restore1080"), 2.0f, [OriginalClient](FPSLPieCtx& Ctx)
    {
        // HasObjects, not IsUsable: restore also after a failed or timed-out resize.
        if (OriginalClient->X > 0.0 && Ctx.HasObjects() && FApp::CanEverRender())
        {
            if (const TSharedPtr<SWindow> Window = Ctx.GameViewport->GetWindow())
            {
                Window->Resize(*OriginalClient);
            }
        }
        return true;
    });
    QueueLostValleyEnd(Context);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLLostValleyExploreTest, "PSL.PIE.LostValley.Explore", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPSLLostValleyExploreTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("LostValley_Explore"));
    if (!QueueLostValleyStart(*this, Context))
    {
        return false;
    }
    AddPieStep(Context, TEXT("ExploreTeleport"), 2.0f, [](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        FVector Ground;
        if (!GroundAt(Ctx.World, -6142.0, -4328.0, Ground))
        {
            Ctx.MarkInvalid(TEXT("No ground at (-6142,-4328)"));
            return true;
        }
        TeleportTo(Ctx, Ground);
        FaceYawToward(Ctx, FVector(0.0, 0.0, Ground.Z));
        return true;
    });
    AddPieStep(Context, TEXT("ExploreSettle"), 3.0f, [StartTime = -1.0](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        const double Now = Ctx.World->GetTimeSeconds();
        if (StartTime < 0.0)
        {
            StartTime = Now;
        }
        if (Now - StartTime < 1.0)
        {
            return false;
        }
        Ctx.Test->TestTrue(TEXT("Player is on the ground after exploring"), Ctx.Character->GetMovementComponent()->IsMovingOnGround());
        Record(Ctx, TEXT("explore"));
        RequestScreenshot(Ctx, TEXT("after_explore"));
        return true;
    });
    QueueAwaitScreenshot(Context, TEXT("after_explore"));
    // Camera sweep at the explore spot: arm 450/650/800, 1 s settle each, then restore the default arm length.
    const TSharedRef<float> DefaultArm = MakeShared<float>(-1.0f);
    AddPieStep(Context, TEXT("CameraSweep"), 15.0f, [DefaultArm, Index = 0, StartTime = -1.0, bShot = false](FPSLPieCtx& Ctx) mutable
    {
        static constexpr float Arms[3] = {450.0f, 650.0f, 800.0f};
        static const TCHAR* const Names[3] = {TEXT("cam_450"), TEXT("cam_650"), TEXT("cam_800")};
        if (!Ctx.IsUsable() || !FApp::CanEverRender())
        {
            return true;
        }
        USpringArmComponent* Arm = Ctx.Character->FindComponentByClass<USpringArmComponent>();
        if (!Arm)
        {
            Ctx.MarkInvalid(TEXT("Player has no USpringArmComponent"));
            return true;
        }
        const double Now = Ctx.World->GetTimeSeconds();
        if (*DefaultArm < 0.0f)
        {
            *DefaultArm = Arm->TargetArmLength;
        }
        if (StartTime < 0.0)
        {
            StartTime = Now;
            Arm->TargetArmLength = Arms[Index];
            return false;
        }
        if (!bShot)
        {
            if (Now - StartTime < 1.0)
            {
                return false;
            }
            RequestScreenshot(Ctx, Names[Index]);
            bShot = true;
            return false;
        }
        if (FScreenshotRequest::IsScreenshotRequested())
        {
            return false;
        }
        if (++Index < 3)
        {
            StartTime = -1.0;
            bShot = false;
            return false;
        }
        Arm->TargetArmLength = *DefaultArm;
        return true;
    });
    // Also restore on timeout/invalidation paths so later steps see the default camera.
    AddPieStep(Context, TEXT("CameraRestore"), 1.0f, [DefaultArm](FPSLPieCtx& Ctx)
    {
        if (Ctx.HasObjects() && *DefaultArm >= 0.0f)
        {
            if (USpringArmComponent* Arm = Ctx.Character->FindComponentByClass<USpringArmComponent>())
            {
                Arm->TargetArmLength = *DefaultArm;
            }
        }
        return true;
    });
    QueueLostValleyEnd(Context);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLLostValleyHuntTest, "PSL.PIE.LostValley.Hunt", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPSLLostValleyHuntTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("LostValley_Hunt"));
    if (!QueueLostValleyStart(*this, Context))
    {
        return false;
    }
    AddPieStep(Context, TEXT("HuntPlace"), 3.0f, [](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        const TArray<APSLPreyCharacter*> Prey = GetPrey(Ctx.World);
        if (Prey.Num() == 0 || !Prey[0])
        {
            Ctx.MarkInvalid(TEXT("No prey in L_LostValley"));
            return true;
        }
        if (!PlaceNear(Ctx, Prey[0]->GetActorLocation(), 600.0, Prey[0]))
        {
            Ctx.MarkInvalid(TEXT("No ground placement 600 uu from prey"));
            return true;
        }
        HoldPreyStill(Ctx, *Prey[0]);
        return true;
    });
    AddPieStep(Context, TEXT("HuntAssert"), 4.0f, [StartTime = -1.0](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        const TArray<APSLPreyCharacter*> Prey = GetPrey(Ctx.World);
        if (Prey.Num() == 0 || !Prey[0])
        {
            Ctx.MarkInvalid(TEXT("Prey missing"));
            return true;
        }
        HoldPreyStill(Ctx, *Prey[0]);
        const double Now = Ctx.World->GetTimeSeconds();
        if (StartTime < 0.0)
        {
            StartTime = Now;
        }
        if (Now - StartTime < 1.0)
        {
            return false;
        }
        const double Distance = Dist2D(*Ctx.Character, *Prey[0]);
        Ctx.Test->TestTrue(FString::Printf(TEXT("Player-prey 2D distance in 450..750 (was %.0f)"), Distance), Distance >= 450.0 && Distance <= 750.0);
        Record(Ctx, TEXT("hunt"));
        RequestScreenshot(Ctx, TEXT("after_hunt"));
        return true;
    });
    QueueAwaitScreenshot(Context, TEXT("after_hunt"));
    QueueLostValleyEnd(Context);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLLostValleyMoveAndBiteTest, "PSL.PIE.LostValley.MoveAndBite", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPSLLostValleyMoveAndBiteTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("LostValley_MoveAndBite"));
    const TSharedRef<FPSLPreyDeathPose> Death = MakeShared<FPSLPreyDeathPose>();
    if (!QueueLostValleyStart(*this, Context))
    {
        return false;
    }
    // (a) walk with W for 2 s.
    AddPieStep(Context, TEXT("WalkW"), 5.0f, [Phase = 0, StartTime = -1.0, Start = FVector::ZeroVector](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        const double Now = Ctx.World->GetTimeSeconds();
        if (Phase == 0)
        {
            Phase = 1;
            StartTime = Now;
            Start = Ctx.Character->GetActorLocation();
            SendKey(Ctx, EKeys::W, IE_Pressed);
            return false;
        }
        if (Now - StartTime < 2.0)
        {
            return false;
        }
        SendKey(Ctx, EKeys::W, IE_Released);
        const double Moved = FVector::Dist2D(Start, Ctx.Character->GetActorLocation());
        Ctx.Test->TestTrue(FString::Printf(TEXT("W moved > 300 uu in 2 s (was %.0f)"), Moved), Moved > 300.0);
        Record(Ctx, TEXT("walked"));
        return true;
    });
    // (b) three bites on prey A.
    AddPieStep(Context, TEXT("BitePreyA"), 12.0f, [Death, Phase = 0, StartTime = -1.0, NextBite = 0.0, Bites = 0, bReleasePending = false](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        const TArray<APSLPreyCharacter*> Prey = GetPrey(Ctx.World);
        if (Prey.Num() < 2 || !Prey[0] || !Prey[1])
        {
            Ctx.MarkInvalid(TEXT("Need at least 2 prey"));
            return true;
        }
        APSLPreyCharacter& PreyA = *Prey[0];
        const double Now = Ctx.World->GetTimeSeconds();
        if (Phase == 0)
        {
            if (!PlaceNear(Ctx, PreyA.GetActorLocation(), 180.0))
            {
                Ctx.MarkInvalid(TEXT("No ground placement 180 uu from prey A"));
                return true;
            }
            Phase = 1;
            StartTime = Now;
            NextBite = Now;
        }
        TrackPreyBones(PreyA);
        HoldPreyStill(Ctx, PreyA);
        if (Death->AliveHeight <= 0.0f && Bites >= 1 && Now - StartTime >= 0.3 && !PreyA.GetStatsComponent()->IsDead())
        {
            Death->AliveHeight = PreyBodyHeight(PreyA);
        }
        if (bReleasePending)
        {
            SendKey(Ctx, EKeys::LeftMouseButton, IE_Released);
            bReleasePending = false;
        }
        else if (Bites < 3 && Now >= NextBite)
        {
            SendKey(Ctx, EKeys::LeftMouseButton, IE_Pressed);
            bReleasePending = true;
            ++Bites;
            NextBite = Now + 1.2;
        }
        UPSLStatsComponent* Stats = PreyA.GetStatsComponent();
        if (!Stats)
        {
            Ctx.MarkInvalid(TEXT("Prey A has no stats"));
            return true;
        }
        if (Now - StartTime >= 6.0 || Stats->IsDead())
        {
            if (bReleasePending)
            {
                SendKey(Ctx, EKeys::LeftMouseButton, IE_Released);
            }
            Ctx.Test->TestTrue(TEXT("Prey A dead within 6 s of biting"), Stats->IsDead());
            Death->DeathTime = Now;
            Record(Ctx, TEXT("prey_a_dead"));
            RequestScreenshot(Ctx, TEXT("after_bite"));
            return true;
        }
        return false;
    });
    QueueAwaitScreenshot(Context, TEXT("after_bite"));
    // The corpse must lie down before death_side: >= 1.5 s after death, body height <= 0.6 x the standing height.
    // Not gated on rendering, so the -nullrhi gate runs it too (the bones are refreshed by TrackPreyBones).
    AddPieStep(Context, TEXT("DeathPose"), 6.0f, [Death](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        const TArray<APSLPreyCharacter*> Prey = GetPrey(Ctx.World);
        if (Prey.Num() == 0 || !Prey[0])
        {
            Ctx.MarkInvalid(TEXT("Prey A missing for death pose"));
            return true;
        }
        if (Ctx.World->GetTimeSeconds() - Death->DeathTime < 1.5)
        {
            return false;
        }
        Ctx.Test->TestTrue(TEXT("Alive prey A body height measured"), Death->AliveHeight > 0.0f);
        AssertPreyCollapsed(Ctx, *Prey[0], Death->AliveHeight, TEXT("1.5 s after death"));
        return true;
    });
    // death_side: stand 350 uu from the corpse on the far side from the player, control yaw corpse+20 deg and pitch -30 so the
    // pawn is off to one side and the camera looks down past it. Asserts the corpse is within 30 deg of camera forward and
    // not occluded (camera -> corpse bounds centre, ECC_Visibility). Pawn occlusion is a line trace against the real bodies
    // (mesh physics-asset bodies and the capsule, not an AABB); a hit gets one retry at yaw -20 deg.
    AddPieStep(Context, TEXT("DeathSide"), 8.0f, [Phase = 0, StartTime = -1.0, CorpseYaw = 0.0, YawOffset = 20.0, bRetried = false](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable() || !FApp::CanEverRender())
        {
            return true;
        }
        const TArray<APSLPreyCharacter*> Prey = GetPrey(Ctx.World);
        if (Prey.Num() == 0 || !Prey[0])
        {
            Ctx.MarkInvalid(TEXT("Prey A missing for death_side"));
            return true;
        }
        const FVector Corpse = Prey[0]->GetActorLocation();
        const double Now = Ctx.World->GetTimeSeconds();
        if (Phase == 0)
        {
            const FVector Player = Ctx.Character->GetActorLocation();
            const FVector Away = (Corpse - Player).GetSafeNormal2D();
            // Yaw is set after the teleport, from the real standing point.
            FVector Ground;
            if (!GroundAt(Ctx.World, Corpse.X + Away.X * 350.0, Corpse.Y + Away.Y * 350.0, Ground)
                && !GroundAt(Ctx.World, Corpse.X - Away.X * 350.0, Corpse.Y - Away.Y * 350.0, Ground))
            {
                Ctx.MarkInvalid(TEXT("No ground 350 uu from the corpse for death_side"));
                return true;
            }
            TeleportTo(Ctx, Ground);
            // Both ground branches (far or near side) then face the corpse.
            CorpseYaw = (Corpse - Ground).GetSafeNormal2D().Rotation().Yaw;
            Phase = 1;
            StartTime = Now;
        }
        const double Yaw = FRotator::NormalizeAxis(CorpseYaw + YawOffset);
        Ctx.PC->SetControlRotation(FRotator(-30.0, Yaw, 0.0));
        Ctx.Character->SetActorRotation(FRotator(0.0, Yaw, 0.0));
        if (Now - StartTime < 1.0)
        {
            return false;
        }
        const APlayerCameraManager* Camera = Ctx.PC->PlayerCameraManager;
        if (!Camera)
        {
            Ctx.MarkInvalid(TEXT("No camera manager for death_side"));
            return true;
        }
        const FVector CameraLocation = Camera->GetCameraLocation();
        const FVector Center = Prey[0]->GetComponentsBoundingBox().GetCenter();
        // The player pawn ignores ECC_Visibility under the default Pawn profile, so the world trace ignores it and pawn
        // occlusion is tested separately against the real bodies (component line traces do not use collision channels).
        FCollisionQueryParams Params(SCENE_QUERY_STAT(PSLDeathSide), false, Ctx.Character.Get());
        FHitResult Hit;
        const bool bHit = Ctx.World->LineTraceSingleByChannel(Hit, CameraLocation, Center, ECC_Visibility, Params);
        const FCollisionQueryParams PawnParams(SCENE_QUERY_STAT(PSLDeathSidePawn));
        FHitResult PawnHit;
        const bool bPawnInTheWay = Ctx.Character->GetMesh()->LineTraceComponent(PawnHit, CameraLocation, Center, PawnParams)
            || Ctx.Character->GetCapsuleComponent()->LineTraceComponent(PawnHit, CameraLocation, Center, PawnParams);
        if (bPawnInTheWay && !bRetried)
        {
            bRetried = true;
            YawOffset = -20.0;
            StartTime = Now;
            return false;
        }
        const double AngleDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
            FVector::DotProduct(Camera->GetCameraRotation().Vector(), (Center - CameraLocation).GetSafeNormal()), -1.0, 1.0)));
        Ctx.Test->TestTrue(FString::Printf(TEXT("Corpse within 30 deg of camera forward (was %.1f)"), AngleDeg), AngleDeg <= 30.0);
        Ctx.Test->TestTrue(TEXT("Player pawn does not cover the corpse"), !bPawnInTheWay);
        Ctx.Test->TestTrue(FString::Printf(TEXT("Corpse not occluded from the camera (hit %s)"), bHit ? *GetNameSafe(Hit.GetActor()) : TEXT("nothing")),
            !bHit || Hit.GetActor() == Prey[0]);
        Record(Ctx, TEXT("death_side"));
        RequestScreenshot(Ctx, TEXT("death_side"));
        return true;
    });
    QueueAwaitScreenshot(Context, TEXT("death_side"));
    // The capture is served on a later draw, so the pitch goes back to 0 only after it.
    AddPieStep(Context, TEXT("DeathSideRestore"), 2.0f, [](FPSLPieCtx& Ctx)
    {
        if (Ctx.HasObjects())
        {
            FRotator Rotation = Ctx.PC->GetControlRotation();
            Rotation.Pitch = 0.0;
            Ctx.PC->SetControlRotation(Rotation);
        }
        return true;
    });
    // (c) prey B flees when the player stands 500 uu away.
    AddPieStep(Context, TEXT("PreyBFlee"), 8.0f, [Phase = 0, StartTime = -1.0](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        const TArray<APSLPreyCharacter*> Prey = GetPrey(Ctx.World);
        if (Prey.Num() < 2 || !Prey[1])
        {
            Ctx.MarkInvalid(TEXT("Prey B missing"));
            return true;
        }
        APSLPreyCharacter& PreyB = *Prey[1];
        const double Now = Ctx.World->GetTimeSeconds();
        if (Phase == 0)
        {
            if (!PlaceNear(Ctx, PreyB.GetActorLocation(), 500.0, &PreyB))
            {
                Ctx.MarkInvalid(TEXT("No ground placement 500 uu from prey B"));
                return true;
            }
            Phase = 1;
            StartTime = Now;
            return false;
        }
        if (PreyStateOf(PreyB) == EPSLPreyState::Flee)
        {
            Record(Ctx, TEXT("prey_b_flee"));
            return true;
        }
        if (AController* AI = PreyB.GetController())
        {
            AI->StopMovement();
        }
        PreyB.GetCharacterMovement()->StopMovementImmediately();
        PreyB.SetActorRotation(FRotator(0.0, (Ctx.Character->GetActorLocation() - PreyB.GetActorLocation()).GetSafeNormal2D().Rotation().Yaw, 0.0));
        if (Now - StartTime >= 4.0)
        {
            Ctx.Test->AddError(TEXT("Prey B did not enter Flee within 4 s"));
            return true;
        }
        return false;
    });
    QueueLostValleyEnd(Context);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLLostValleyPerfTest, "PSL.PIE.LostValley.Perf", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPSLLostValleyPerfTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("LostValley_Perf"));
    if (!QueueLostValleyStart(*this, Context))
    {
        return false;
    }
    // Phases: 0 spawn (PlayerStart, no move), 1 explore (-6142,-4328), 2 hunt (600 uu from prey[0], sight line).
    // Each: 1 s settle, then 3 s of per-frame FApp::GetDeltaTime() samples.
    AddPieStep(Context, TEXT("PerfSample"), 30.0f, [Phase = 0, Stage = 0, StageStart = 0.0, Samples = TArray<TArray<double>>()](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        if (Samples.IsEmpty())
        {
            Samples.SetNum(3);
        }
        const double Now = FPlatformTime::Seconds();
        if (Stage == 0)
        {
            if (Phase == 1)
            {
                FVector Ground;
                if (!GroundAt(Ctx.World, -6142.0, -4328.0, Ground))
                {
                    Ctx.MarkInvalid(TEXT("No ground at (-6142,-4328)"));
                    return true;
                }
                TeleportTo(Ctx, Ground);
                FaceYawToward(Ctx, FVector(0.0, 0.0, Ground.Z));
            }
            else if (Phase == 2)
            {
                const TArray<APSLPreyCharacter*> Prey = GetPrey(Ctx.World);
                if (Prey.Num() == 0 || !Prey[0] || !PlaceNear(Ctx, Prey[0]->GetActorLocation(), 600.0, Prey[0]))
                {
                    Ctx.MarkInvalid(TEXT("No hunt placement 600 uu from prey[0]"));
                    return true;
                }
            }
            Stage = 1;
            StageStart = Now;
            return false;
        }
        if (Phase == 2)
        {
            const TArray<APSLPreyCharacter*> Prey = GetPrey(Ctx.World);
            if (Prey.Num() > 0 && Prey[0])
            {
                HoldPreyStill(Ctx, *Prey[0]);
            }
        }
        if (Stage == 1)
        {
            if (Now - StageStart >= 1.0)
            {
                Stage = 2;
                StageStart = Now;
            }
            return false;
        }
        Samples[Phase].Add(static_cast<double>(FApp::GetDeltaTime()) * 1000.0);
        if (Now - StageStart < 3.0)
        {
            return false;
        }
        if (++Phase < 3)
        {
            Stage = 0;
            return false;
        }

        static const TCHAR* const PhaseNames[3] = {TEXT("spawn"), TEXT("explore"), TEXT("hunt")};
        TArray<double> All;
        FString PhasesJson;
        for (int32 Index = 0; Index < 3; ++Index)
        {
            All.Append(Samples[Index]);
            PhasesJson += FString::Printf(TEXT("%s\"%s\":{%s}"), Index > 0 ? TEXT(",") : TEXT(""), PhaseNames[Index], *FrameStatsJsonFields(ComputeFrameStats(Samples[Index])));
        }
        const FPSLFrameStats Overall = ComputeFrameStats(All);
        const FIntPoint Size = Ctx.GameViewport->Viewport ? Ctx.GameViewport->Viewport->GetSizeXY() : FIntPoint::ZeroValue;
        const IConsoleVariable* VSync = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"));
        const Scalability::FQualityLevels Q = Scalability::GetQualityLevels();
        const FString Json = FString::Printf(
            TEXT("{\"map\":\"%s\",\"rhi\":\"%s\",\"canRender\":%s,\"viewport\":[%d,%d],\"vsync\":%d,%s,\"GAverageFPS\":%.3f,\"GAverageMS\":%.3f,\"phases\":{%s},")
            TEXT("\"quality\":{\"ResolutionQuality\":%.1f,\"ViewDistanceQuality\":%d,\"AntiAliasingQuality\":%d,\"ShadowQuality\":%d,\"GlobalIlluminationQuality\":%d,\"ReflectionQuality\":%d,\"PostProcessQuality\":%d,\"TextureQuality\":%d,\"EffectsQuality\":%d,\"FoliageQuality\":%d,\"ShadingQuality\":%d,\"LandscapeQuality\":%d}}"),
            *JsonEscape(Ctx.World->GetMapName()), *JsonEscape(FApp::GetGraphicsRHI()), JsonBool(FApp::CanEverRender()),
            Size.X, Size.Y, VSync ? VSync->GetInt() : -1, *FrameStatsJsonFields(Overall),
            static_cast<double>(GAverageFPS), static_cast<double>(GAverageMS), *PhasesJson,
            static_cast<double>(Q.ResolutionQuality), Q.ViewDistanceQuality, Q.AntiAliasingQuality, Q.ShadowQuality, Q.GlobalIlluminationQuality,
            Q.ReflectionQuality, Q.PostProcessQuality, Q.TextureQuality, Q.EffectsQuality, Q.FoliageQuality, Q.ShadingQuality, Q.LandscapeQuality);

        const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("PIE"));
        IFileManager::Get().MakeDirectory(*Directory, true);
        const FString Filename = FPaths::Combine(Directory, TEXT("lostvalley-perf.json"));
        const bool bSaved = FFileHelper::SaveStringToFile(Json, *Filename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        Ctx.Test->TestTrue(FString::Printf(TEXT("Perf file saved: %s"), *Filename), bSaved);
        Ctx.Test->TestTrue(TEXT("Perf frames > 0"), Overall.Frames > 0);
        Ctx.Test->TestTrue(TEXT("Perf avgFps finite and > 0"), FMath::IsFinite(Overall.AvgFps) && Overall.AvgFps > 0.0);
        Ctx.Test->TestTrue(TEXT("Perf avgMs finite"), FMath::IsFinite(Overall.AvgMs));
        Ctx.Test->TestTrue(TEXT("Perf GAverageFPS finite"), FMath::IsFinite(GAverageFPS));
        Ctx.Test->TestTrue(TEXT("Perf GAverageMS finite"), FMath::IsFinite(GAverageMS));
        if (FApp::CanEverRender())
        {
            Ctx.Test->TestTrue(FString::Printf(TEXT("Rendered avgFps >= 20 (was %.1f)"), Overall.AvgFps), Overall.AvgFps >= 20.0);
            Ctx.Test->TestTrue(FString::Printf(TEXT("Rendered p95Ms <= 50 (was %.1f)"), Overall.P95Ms), Overall.P95Ms <= 50.0);
        }
        Ctx.Test->AddInfo(Json);
        return true;
    });
    QueueLostValleyEnd(Context);
    return true;
}

// GameMode spawn plus encounter: spawn -> pursuit -> attack/damage -> lose target -> disengage.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLLostValleyPredatorTest, "PSL.PIE.LostValley.Predator", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPSLLostValleyPredatorTest::RunTest(const FString& Parameters)
{
    struct FPredatorExtra
    {
        FVector PlayerStart = FVector::ZeroVector;
        FVector ChaseStart = FVector::ZeroVector;
    };
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("LostValley_Predator"));
    const TSharedRef<FPredatorRun> Run = MakeShared<FPredatorRun>();
    const TSharedRef<FPredatorExtra> Extra = MakeShared<FPredatorExtra>();
    if (!QueueLostValleyStart(*this, Context, true))
    {
        return false;
    }

    // The GameMode rebuilds the empty navmesh at runtime (up to 60 s of retries) before it spawns.
    AddPieStep(Context, TEXT("PredatorSpawned"), 75.0f, [Run, Extra](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        SetPredatorSpawnCvar(1); // BeginPlay already read it
        TArray<APSLPredatorCharacter*> Predators;
        for (TActorIterator<APSLPredatorCharacter> It(Ctx.World.Get()); It; ++It)
        {
            Predators.Add(*It);
        }
        if (Predators.Num() == 0)
        {
            return false;
        }
        if (!Ctx.Test->TestEqual(TEXT("Exactly one predator in L_LostValley"), Predators.Num(), 1))
        {
            Ctx.MarkInvalid(TEXT("Predator count is not 1"));
            return true;
        }
        APSLPredatorCharacter* Predator = Predators[0];
        APSLPredatorAIController* AI = Cast<APSLPredatorAIController>(Predator->GetController());
        if (!AI)
        {
            Ctx.MarkInvalid(TEXT("Predator is not possessed by APSLPredatorAIController"));
            return true;
        }
        const double Dist = Dist2D(*Predator, *Ctx.Character);
        Ctx.Test->TestTrue(FString::Printf(TEXT("Predator spawned 4000-6000 uu from the player (%.0f)"), Dist), Dist >= 4000.0 && Dist <= 6000.0);

        FVector PredNav;
        FVector PlayerNav;
        float PathLength = 0.0f;
        if (!CompletePathBetween(Ctx.World.Get(), Predator->GetActorLocation(), Ctx.Character->GetActorLocation(), &PredNav, &PlayerNav, &PathLength))
        {
            Ctx.MarkInvalid(TEXT("No complete navmesh path from the predator to the player"));
            return true;
        }
        Ctx.Test->AddInfo(FString::Printf(TEXT("Predator->player path length %.0f"), PathLength));

        const FVector PlayerDir = (Predator->GetActorLocation() - Ctx.Character->GetActorLocation()).GetSafeNormal2D();
        const double ViewDot = FVector::DotProduct(FRotator(0.0, Ctx.PC->GetControlRotation().Yaw, 0.0).Vector(), PlayerDir);
        Ctx.Test->TestTrue(FString::Printf(TEXT("Predator spawned out of view (dot %.2f)"), ViewDot), ViewDot < 0.5);
        Ctx.Test->TestTrue(TEXT("Predator starts calm (Idle/Patrol)"),
            AI->GetPredatorState() == EPSLPredatorState::Idle || AI->GetPredatorState() == EPSLPredatorState::Patrol);

        AI->IdleSeconds = 0.5f;
        AI->InvestigateSeconds = 6.0f;
        AI->RecoverSeconds = 0.5f;
        Run->Predator = Predator;
        Extra->PlayerStart = Ctx.Character->GetActorLocation();
        return true;
    });

    // A real bite noise on the predator's side of the player, 1200-2000 uu away on reachable navmesh. The spot must be in
    // clear sight of the predator's eyes (ECC_Visibility, the AI sight default) with a path < 2000 uu, so the predator
    // closes inside SightRadius 1800 before InvestigateSeconds 6 at ~370 uu/s runs out.
    AddPieStep(Context, TEXT("Approach"), 5.0f, [Run, Extra](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        APSLPredatorAIController* AI = AIOf(*Run);
        if (!AI)
        {
            Ctx.MarkInvalid(TEXT("Predator or its controller is gone"));
            return true;
        }
        const FVector P = Run->Predator->GetActorLocation();
        const FVector D = (Extra->PlayerStart - P).GetSafeNormal2D();
        for (const double Dist : {2000.0, 1600.0, 1200.0})
        {
            FVector Ground;
            float PathLength = 0.0f;
            FCollisionQueryParams Params(SCENE_QUERY_STAT(PSLPredatorApproachSight), false, Run->Predator.Get());
            Params.AddIgnoredActor(Ctx.Character.Get());
            if (GroundAt(Ctx.World.Get(), P.X + D.X * Dist, P.Y + D.Y * Dist, Ground)
                && CompletePathBetween(Ctx.World.Get(), P, Ground, nullptr, nullptr, &PathLength) && PathLength < 2000.0f
                && !Ctx.World->LineTraceTestByChannel(Run->Predator->GetPawnViewLocation(), Ground, ECC_Visibility, Params))
            {
                Run->Seen = AI->GetTransitions().Num();
                TeleportTo(Ctx, Ground);
                FaceYawToward(Ctx, P);
                Ctx.Test->TestTrue(TEXT("Bite accepted (noise source)"), Ctx.Character->TryBite());
                return true;
            }
        }
        Ctx.MarkInvalid(FString::Printf(TEXT("No visible ground 1200-2000 uu from the predator at %s with a complete path < 2000 uu"), *P.ToCompactString()));
        return true;
    });
    AddPieStep(Context, TEXT("Chase"), 25.0f, [Run, Extra](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        if (!AIOf(*Run))
        {
            Ctx.MarkInvalid(TEXT("Predator or its controller is gone"));
            return true;
        }
        Ctx.Character->GetCharacterMovement()->StopMovementImmediately();
        if (!ConsumeUntil(*Run, EPSLPredatorState::Chase, TEXT("target_seen")))
        {
            return false; // a heard_noise Investigate may precede it
        }
        Extra->ChaseStart = Run->Predator->GetActorLocation();
        Run->HealthBefore = Ctx.Character->GetStatsComponent()->GetHealth();
        return true;
    });
    AddPieStep(Context, TEXT("Attack"), 20.0f, [Run, Extra](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        if (!AIOf(*Run))
        {
            Ctx.MarkInvalid(TEXT("Predator or its controller is gone"));
            return true;
        }
        Ctx.Character->GetCharacterMovement()->StopMovementImmediately();
        if (!ConsumeUntil(*Run, EPSLPredatorState::Attack, TEXT("target_in_range")))
        {
            return false;
        }
        const double Moved = FVector::Dist2D(Extra->ChaseStart, Run->Predator->GetActorLocation());
        Ctx.Test->TestTrue(FString::Printf(TEXT("Predator pursued over the navmesh (moved %.0f uu)"), Moved), Moved > 300.0);
        Ctx.Test->AddInfo(FString::Printf(TEXT("Pursuit distance %.0f uu"), Moved));
        return true;
    });
    AddPieStep(Context, TEXT("AttackDamage"), 3.0f, [Run](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable() || !Run->Predator.IsValid())
        {
            return true;
        }
        const float Expected = Run->HealthBefore - Run->Predator->AttackDamage;
        if (Ctx.Character->GetStatsComponent()->GetHealth() > Expected + 1.0f)
        {
            return false; // the hit lands AttackHitDelay after entering Attack
        }
        Ctx.Test->AddInfo(FString::Printf(TEXT("Player health %.1f -> %.1f"), Run->HealthBefore, Ctx.Character->GetStatsComponent()->GetHealth()));
        RequestScreenshot(Ctx, TEXT("predator_bite")); // flash 0.2 s / ghost bar 0.4 s: capture in the detection step
        return true;
    });
    QueueAwaitScreenshot(Context, TEXT("predator_bite"));

    // Back in Chase, the player vanishes beyond LoseSightRadius 2600: on landscape ground 3200-4000 uu further along
    // the predator->player direction (away from the predator).
    AddPieStep(Context, TEXT("ChaseAgainThenHide"), 8.0f, [Run](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        APSLPredatorAIController* AI = AIOf(*Run);
        if (!AI)
        {
            Ctx.MarkInvalid(TEXT("Predator or its controller is gone"));
            return true;
        }
        Ctx.Character->GetCharacterMovement()->StopMovementImmediately();
        if (AI->GetPredatorState() != EPSLPredatorState::Chase)
        {
            return false;
        }
        const FVector P = Run->Predator->GetActorLocation();
        const FVector Away = (Ctx.Character->GetActorLocation() - P).GetSafeNormal2D();
        for (const double Dist : {3500.0, 4000.0, 3200.0})
        {
            FVector Ground;
            if (GroundAt(Ctx.World.Get(), P.X + Away.X * Dist, P.Y + Away.Y * Dist, Ground) && FVector::Dist2D(P, Ground) > 2600.0)
            {
                Ctx.Test->AddInfo(FString::Printf(TEXT("Hide spot %.0f uu from the predator"), FVector::Dist2D(P, Ground)));
                Run->Seen = AI->GetTransitions().Num();
                TeleportTo(Ctx, Ground);
                return true;
            }
        }
        Ctx.MarkInvalid(FString::Printf(TEXT("No landscape ground 3200-4000 uu away from the predator at %s"), *P.ToCompactString()));
        return true;
    });
    AddPieStep(Context, TEXT("LoseSight"), 5.0f, [Run](FPSLPieCtx& Ctx)
    {
        return !Ctx.IsUsable() || ConsumeUntil(*Run, EPSLPredatorState::Investigate, TEXT("lost_sight"));
    });
    AddPieStep(Context, TEXT("GiveUp"), 15.0f, [Run](FPSLPieCtx& Ctx)
    {
        return !Ctx.IsUsable() || ConsumeUntil(*Run, EPSLPredatorState::Idle, TEXT("search_gave_up"));
    });
    AddPieStep(Context, TEXT("BackToPatrol"), 5.0f, [Run](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        if (!ConsumeUntil(*Run, EPSLPredatorState::Patrol, TEXT("idle_done")))
        {
            return false;
        }
        Ctx.Test->AddInfo(TEXT("Transitions: ") + DumpTransitions(*Run));
        return true;
    });
    QueueLostValleyEnd(Context);
    return true;
}

} // namespace

#endif
