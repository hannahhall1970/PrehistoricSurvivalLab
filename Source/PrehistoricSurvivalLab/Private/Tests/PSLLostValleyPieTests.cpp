#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "PSLPieTestSupport.h"
#include "Landscape.h"
#include "LandscapeProxy.h"
#include "LandscapeComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "GameFramework/PlayerStart.h"
#include "Scalability.h"
#include "Misc/App.h"

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
                Params.AddIgnoredActor(Ctx.Character);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLLostValleyLoadsTest, "PSL.PIE.LostValley.Loads", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPSLLostValleyLoadsTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("LostValley_Loads"));
    if (!QueuePieStart(*this, Context, LostValleyMap))
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

        int32 LandscapeCount = 0;
        int32 ComponentCount = 0;
        for (TActorIterator<ALandscape> It(World); It; ++It)
        {
            ++LandscapeCount;
            TArray<ULandscapeComponent*> Components;
            It->GetComponents(Components);
            ComponentCount += Components.Num();
        }
        Ctx.Test->TestEqual(TEXT("Landscape count"), LandscapeCount, 1);
        Ctx.Test->TestEqual(TEXT("Landscape component count"), ComponentCount, 16);

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
    QueuePieEnd(Context);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLLostValleySpawnTest, "PSL.PIE.LostValley.Spawn", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPSLLostValleySpawnTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("LostValley_Spawn"));
    if (!QueuePieStart(*this, Context, LostValleyMap))
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
    QueuePieEnd(Context);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLLostValleyExploreTest, "PSL.PIE.LostValley.Explore", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPSLLostValleyExploreTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("LostValley_Explore"));
    if (!QueuePieStart(*this, Context, LostValleyMap))
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
    QueuePieEnd(Context);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLLostValleyHuntTest, "PSL.PIE.LostValley.Hunt", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPSLLostValleyHuntTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("LostValley_Hunt"));
    if (!QueuePieStart(*this, Context, LostValleyMap))
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
    QueuePieEnd(Context);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLLostValleyMoveAndBiteTest, "PSL.PIE.LostValley.MoveAndBite", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPSLLostValleyMoveAndBiteTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("LostValley_MoveAndBite"));
    if (!QueuePieStart(*this, Context, LostValleyMap))
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
    AddPieStep(Context, TEXT("BitePreyA"), 12.0f, [Phase = 0, StartTime = -1.0, NextBite = 0.0, Bites = 0, bReleasePending = false](FPSLPieCtx& Ctx) mutable
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
        HoldPreyStill(Ctx, PreyA);
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
            Record(Ctx, TEXT("prey_a_dead"));
            RequestScreenshot(Ctx, TEXT("after_bite"));
            return true;
        }
        return false;
    });
    QueueAwaitScreenshot(Context, TEXT("after_bite"));
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
    QueuePieEnd(Context);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLLostValleyPerfTest, "PSL.PIE.LostValley.Perf", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPSLLostValleyPerfTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("LostValley_Perf"));
    if (!QueuePieStart(*this, Context, LostValleyMap))
    {
        return false;
    }
    AddPieStep(Context, TEXT("PerfSample"), 15.0f, [Phase = 0, PhaseStart = 0.0, Frames = 0](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        const double Now = FPlatformTime::Seconds();
        if (Phase == 0)
        {
            Phase = 1;
            PhaseStart = Now;
            return false;
        }
        if (Phase == 1)
        {
            if (Now - PhaseStart < 2.0)
            {
                return false;
            }
            Phase = 2;
            PhaseStart = Now;
            Frames = 0;
            return false;
        }
        ++Frames;
        const double Seconds = Now - PhaseStart;
        if (Seconds < 3.0)
        {
            return false;
        }

        const double AvgFps = Seconds > 0.0 ? Frames / Seconds : 0.0;
        const double AvgMs = Frames > 0 ? Seconds * 1000.0 / Frames : 0.0;
        const FIntPoint Size = Ctx.GameViewport->Viewport ? Ctx.GameViewport->Viewport->GetSizeXY() : FIntPoint::ZeroValue;
        const Scalability::FQualityLevels Q = Scalability::GetQualityLevels();
        const FString Json = FString::Printf(
            TEXT("{\"map\":\"%s\",\"rhi\":\"%s\",\"canRender\":%s,\"viewport\":[%d,%d],\"frames\":%d,\"seconds\":%.4f,\"avgFps\":%.3f,\"avgMs\":%.3f,\"GAverageFPS\":%.3f,\"GAverageMS\":%.3f,")
            TEXT("\"quality\":{\"ResolutionQuality\":%.1f,\"ViewDistanceQuality\":%d,\"AntiAliasingQuality\":%d,\"ShadowQuality\":%d,\"GlobalIlluminationQuality\":%d,\"ReflectionQuality\":%d,\"PostProcessQuality\":%d,\"TextureQuality\":%d,\"EffectsQuality\":%d,\"FoliageQuality\":%d,\"ShadingQuality\":%d,\"LandscapeQuality\":%d}}"),
            *JsonEscape(Ctx.World->GetMapName()), *JsonEscape(FApp::GetGraphicsRHI()), JsonBool(FApp::CanEverRender()),
            Size.X, Size.Y, Frames, Seconds, AvgFps, AvgMs, static_cast<double>(GAverageFPS), static_cast<double>(GAverageMS),
            static_cast<double>(Q.ResolutionQuality), Q.ViewDistanceQuality, Q.AntiAliasingQuality, Q.ShadowQuality, Q.GlobalIlluminationQuality,
            Q.ReflectionQuality, Q.PostProcessQuality, Q.TextureQuality, Q.EffectsQuality, Q.FoliageQuality, Q.ShadingQuality, Q.LandscapeQuality);

        const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("PIE"));
        IFileManager::Get().MakeDirectory(*Directory, true);
        const FString Filename = FPaths::Combine(Directory, TEXT("lostvalley-perf.json"));
        const bool bSaved = FFileHelper::SaveStringToFile(Json, *Filename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        Ctx.Test->TestTrue(FString::Printf(TEXT("Perf file saved: %s"), *Filename), bSaved);
        Ctx.Test->TestTrue(TEXT("Perf frames > 0"), Frames > 0);
        Ctx.Test->TestTrue(TEXT("Perf avgFps finite and > 0"), FMath::IsFinite(AvgFps) && AvgFps > 0.0);
        Ctx.Test->TestTrue(TEXT("Perf avgMs finite"), FMath::IsFinite(AvgMs));
        Ctx.Test->TestTrue(TEXT("Perf GAverageFPS finite"), FMath::IsFinite(GAverageFPS));
        Ctx.Test->TestTrue(TEXT("Perf GAverageMS finite"), FMath::IsFinite(GAverageMS));
        if (FApp::CanEverRender())
        {
            Ctx.Test->TestTrue(FString::Printf(TEXT("Rendered avgFps >= 20 (was %.1f)"), AvgFps), AvgFps >= 20.0);
        }
        Ctx.Test->AddInfo(Json);
        return true;
    });
    QueuePieEnd(Context);
    return true;
}

} // namespace

#endif
