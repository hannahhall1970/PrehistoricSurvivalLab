#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "PSLPieTestSupport.h"
#include "PSLDinoSpecies.h"
#include "DynamicRHI.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Framework/Docking/TabManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Scalability.h"

// PSL.Dev.HeroTrex.Perf: old SK_Trex vs hero T-Rex as the PLAYER in L_LostValley at 1920x1080, follow camera, vsync and
// frame caps off. ABBA segments (old, hero, hero, old), each 300 discarded + 1500 sampled frames. Writes
// Saved/Automation/PIE/HeroPerfStats.json (HeroPerf.json is the FPSLPieCtx step log, written at test end). Only validity is
// asserted (sample counts, mesh paths, 1080p); numbers are evidence.
namespace
{
using namespace PSLPie;

// Unity builds merge anonymous namespaces: names here must not collide with the other PSLHero*PieTests.cpp files.
constexpr const TCHAR* PerfHeroMeshPackage = TEXT("/Game/Dev/ArtDir/Hero/SK_HeroTrex");
constexpr const TCHAR* PerfHeroMeshPath = TEXT("/Game/Dev/ArtDir/Hero/SK_HeroTrex.SK_HeroTrex");
constexpr const TCHAR* PerfOldMeshPath = TEXT("/Game/Characters/Dino/SK_Trex.SK_Trex");
constexpr const TCHAR* PerfMapPath = TEXT("/Game/Maps/L_LostValley");
constexpr int32 PerfDiscardFrames = 300;
constexpr int32 PerfSampleFrames = 1500;
constexpr int32 PerfSkelIterations = 300;
constexpr bool PerfSegmentHero[4] = {false, true, true, false}; // ABBA: A = old, B = hero

struct FPerfSegment
{
    TArray<double> FrameMs;
    TArray<double> GpuMs;
    FString MeshPath;
    FIntPoint Viewport = FIntPoint::ZeroValue;
};

struct FPerfState
{
    FPerfSegment Segments[4];
    FString SpeciesJson[2]; // [0] old, [1] hero: mesh, skeletal-cost and texture-memory fields (no braces)
    FVector Spawn = FVector::ZeroVector;
    FRotator SpawnYaw = FRotator::ZeroRotator;
    FVector2D OriginalClient = FVector2D::ZeroVector;
    int32 Segment = 0;
    int32 Frame = 0;      // frames since the segment's species was applied
    int32 Unfocused = 0;  // sampled frames without app focus (FApp::HasFocus); logged as evidence
    // Engine state changed by the test, restored in Cleanup.
    bool bSaved = false;
    int32 PrevVSync = 0;
    int32 PrevVSyncEditor = 0;
    float PrevMaxFps = 0.0f;
    int32 PrevDontLimitOnBattery = 0;
    bool bPrevSmooth = false;
};

IConsoleVariable* PerfCvar(const TCHAR* Name)
{
    return IConsoleManager::Get().FindConsoleVariable(Name);
}

int32 PerfCvarInt(const TCHAR* Name)
{
    const IConsoleVariable* Var = PerfCvar(Name);
    return Var ? Var->GetInt() : -1;
}

// Uncapped frames: vsync off (the editor presents with r.VSyncEditor, SlateRHIRenderer.cpp IsVSyncRequired), t.MaxFPS 0,
// no frame smoothing, no 60 Hz battery cap (UEditorEngine::GetMaxTickRate). PerfReport asserts the read-back values.
void PerfUncap(FPerfState& S)
{
    IConsoleVariable* VSync = PerfCvar(TEXT("r.VSync"));
    IConsoleVariable* VSyncEditor = PerfCvar(TEXT("r.VSyncEditor"));
    IConsoleVariable* MaxFps = PerfCvar(TEXT("t.MaxFPS"));
    IConsoleVariable* Battery = PerfCvar(TEXT("r.DontLimitOnBattery"));
    S.PrevVSync = VSync ? VSync->GetInt() : 0;
    S.PrevVSyncEditor = VSyncEditor ? VSyncEditor->GetInt() : 0;
    S.PrevMaxFps = MaxFps ? MaxFps->GetFloat() : 0.0f;
    S.PrevDontLimitOnBattery = Battery ? Battery->GetInt() : 0;
    S.bPrevSmooth = GEngine->bSmoothFrameRate;
    S.bSaved = true;
    if (VSync) { VSync->Set(0, ECVF_SetByCode); }
    if (VSyncEditor) { VSyncEditor->Set(0, ECVF_SetByCode); }
    if (MaxFps) { MaxFps->Set(0.0f, ECVF_SetByCode); }
    if (Battery) { Battery->Set(1, ECVF_SetByCode); }
    GEngine->bSmoothFrameRate = false;
}

void PerfRestoreCaps(FPerfState& S)
{
    if (!S.bSaved)
    {
        return;
    }
    if (IConsoleVariable* VSync = PerfCvar(TEXT("r.VSync"))) { VSync->Set(S.PrevVSync, ECVF_SetByCode); }
    if (IConsoleVariable* VSyncEditor = PerfCvar(TEXT("r.VSyncEditor"))) { VSyncEditor->Set(S.PrevVSyncEditor, ECVF_SetByCode); }
    if (IConsoleVariable* MaxFps = PerfCvar(TEXT("t.MaxFPS"))) { MaxFps->Set(S.PrevMaxFps, ECVF_SetByCode); }
    if (IConsoleVariable* Battery = PerfCvar(TEXT("r.DontLimitOnBattery"))) { Battery->Set(S.PrevDontLimitOnBattery, ECVF_SetByCode); }
    GEngine->bSmoothFrameRate = S.bPrevSmooth;
    S.bSaved = false;
}

double PerfMean(const TArray<double>& Values)
{
    double Sum = 0.0;
    for (const double V : Values)
    {
        Sum += V;
    }
    return Values.IsEmpty() ? 0.0 : Sum / Values.Num();
}

// Applies the built-in T-Rex to the player with the hero cvar at Hero (MakeBuiltin reads the cvar), then puts the player
// back at the recorded spawn with the spawn yaw and zero velocity, so every segment sees the same view.
void PerfApplySpecies(FPSLPieCtx& Ctx, const FPerfState& S, bool bHero)
{
    SetIntCvar(TEXT("psl.Dev.HeroTrex"), bHero ? 1 : 0);
    Ctx.Character->ApplySpecies(UPSLDinoSpecies::MakeBuiltin(UPSLDinoSpecies::TRexId, Ctx.Character.Get()));
    SetIntCvar(TEXT("psl.Dev.HeroTrex"), 1);
    TeleportTo(Ctx, S.Spawn);
    Ctx.Character->SetActorRotation(S.SpawnYaw);
    Ctx.PC->SetControlRotation(S.SpawnYaw);
}

// Every texture the player mesh's materials use (compiled shader maps), plus referenced textures and instance overrides.
TArray<UTexture*> PerfUsedTextures(const USkeletalMeshComponent* Mesh)
{
    TArray<UTexture*> Textures;
    for (int32 Index = 0; Index < Mesh->GetNumMaterials(); ++Index)
    {
        const UMaterialInterface* Mat = Mesh->GetMaterial(Index);
        if (!Mat)
        {
            continue;
        }
        TArray<UTexture*> Used;
        Mat->GetUsedTextures(Used);
        for (UTexture* Tex : Used)
        {
            if (Tex)
            {
                Textures.AddUnique(Tex);
            }
        }
        for (UObject* Ref : Mat->GetReferencedTextures())
        {
            if (UTexture* Tex = Cast<UTexture>(Ref))
            {
                Textures.AddUnique(Tex);
            }
        }
    }
    return Textures;
}

// Mesh complexity, mean game-thread cost of one animation tick + bone refresh, and texture memory, as JSON fields.
FString PerfSpeciesFields(FPSLPieCtx& Ctx)
{
    USkeletalMeshComponent* Mesh = Ctx.Character->GetMesh();
    const USkeletalMesh* Asset = Mesh->GetSkeletalMeshAsset();
    const FSkeletalMeshRenderData* Render = Asset ? Asset->GetResourceForRendering() : nullptr;
    const bool bLod0 = Render && Render->LODRenderData.Num() > 0;
    const uint32 Verts = bLod0 ? Render->LODRenderData[0].GetNumVertices() : 0;
    const int32 Tris = bLod0 ? Render->LODRenderData[0].GetTotalFaces() : 0;

    // ponytail: synchronous TickAnimation + RefreshBoneTransforms(nullptr) on the game thread, outside the world tick; it
    // measures evaluation cost without parallel-task overlap, so it is an upper bound per frame, not the frame's share.
    const double T0 = FPlatformTime::Seconds();
    for (int32 I = 0; I < PerfSkelIterations; ++I)
    {
        Mesh->TickAnimation(1.0f / 60.0f, false);
        Mesh->RefreshBoneTransforms();
    }
    const double SkelMs = (FPlatformTime::Seconds() - T0) * 1000.0 / PerfSkelIterations;

    const TArray<UTexture*> Textures = PerfUsedTextures(Mesh);
    uint64 Resident = 0, AllMips = 0;
    FString Names;
    for (const UTexture* Tex : Textures)
    {
        Resident += Tex->CalcTextureMemorySizeEnum(TMC_ResidentMips);
        AllMips += Tex->CalcTextureMemorySizeEnum(TMC_AllMips);
        Names += FString::Printf(TEXT("%s\"%s\""), Names.IsEmpty() ? TEXT("") : TEXT(","), *JsonEscape(Tex->GetName()));
    }
    constexpr double MiB = 1024.0 * 1024.0;
    return FString::Printf(
        TEXT("\"mesh\":\"%s\",\"bones\":%d,\"lods\":%d,\"lod0_vertices\":%u,\"lod0_triangles\":%d,\"materials\":%d,")
        TEXT("\"skel_tick_refresh_ms\":%.4f,\"skel_iterations\":%d,\"textures\":%d,\"texture_resident_mib\":%.2f,\"texture_all_mips_mib\":%.2f,\"texture_names\":[%s]"),
        *JsonEscape(GetPathNameSafe(Asset)), Asset ? Asset->GetRefSkeleton().GetNum() : 0, Asset ? Asset->GetLODNum() : 0, Verts, Tris,
        Asset ? Asset->GetMaterials().Num() : 0, SkelMs, PerfSkelIterations, Textures.Num(), Resident / MiB, AllMips / MiB, *Names);
}

// One frame of the ABBA schedule. Returns true when all four segments are sampled.
bool PerfTick(FPSLPieCtx& Ctx, FPerfState& S)
{
    const bool bHero = PerfSegmentHero[S.Segment];
    FPerfSegment& Seg = S.Segments[S.Segment];
    if (S.Frame == 0)
    {
        PerfApplySpecies(Ctx, S, bHero);
        Seg.MeshPath = GetPathNameSafe(Ctx.Character->GetMesh()->GetSkeletalMeshAsset());
    }
    else if (S.Frame > PerfDiscardFrames)
    {
        // FApp::GetDeltaTime is the previous frame's game-thread delta; GPU cycles are the last completed GPU frame.
        Seg.FrameMs.Add(FApp::GetDeltaTime() * 1000.0);
        Seg.GpuMs.Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0)));
        S.Unfocused += FApp::HasFocus() ? 0 : 1;
    }
    if (++S.Frame <= PerfDiscardFrames + PerfSampleFrames)
    {
        return false;
    }
    Seg.Viewport = Ctx.GameViewport->Viewport ? Ctx.GameViewport->Viewport->GetSizeXY() : FIntPoint::ZeroValue;
    const int32 Species = bHero ? 1 : 0;
    if (S.SpeciesJson[Species].IsEmpty())
    {
        S.SpeciesJson[Species] = PerfSpeciesFields(Ctx);
    }
    S.Frame = 0;
    return ++S.Segment >= 4;
}

// Validity asserts, then Saved/Automation/PIE/HeroPerfStats.json.
void PerfReport(FPSLPieCtx& Ctx, const FPerfState& S)
{
    FAutomationTestBase& T = *Ctx.Test;
    FString SegJson;
    TArray<double> Frames[2], Gpu[2];
    for (int32 I = 0; I < 4; ++I)
    {
        const FPerfSegment& Seg = S.Segments[I];
        const bool bHero = PerfSegmentHero[I];
        const TCHAR* Expected = bHero ? PerfHeroMeshPath : PerfOldMeshPath;
        T.TestEqual(FString::Printf(TEXT("Segment %d mesh"), I), Seg.MeshPath, FString(Expected));
        T.TestTrue(FString::Printf(TEXT("Segment %d has >= %d frame samples (%d)"), I, PerfSampleFrames, Seg.FrameMs.Num()), Seg.FrameMs.Num() >= PerfSampleFrames);
        T.TestTrue(FString::Printf(TEXT("Segment %d viewport is 1920x1080 (%dx%d)"), I, Seg.Viewport.X, Seg.Viewport.Y), Seg.Viewport == FIntPoint(1920, 1080));
        Frames[bHero ? 1 : 0].Append(Seg.FrameMs);
        Gpu[bHero ? 1 : 0].Append(Seg.GpuMs);
        const FPSLFrameStats GpuStats = ComputeFrameStats(Seg.GpuMs);
        SegJson += FString::Printf(TEXT("%s{\"index\":%d,\"species\":\"%s\",\"mesh\":\"%s\",\"viewport\":[%d,%d],%s,\"gpu_p50Ms\":%.3f,\"gpu_p95Ms\":%.3f,\"gpu_meanMs\":%.3f}"),
            I > 0 ? TEXT(",") : TEXT(""), I, bHero ? TEXT("hero") : TEXT("old"), *JsonEscape(Seg.MeshPath), Seg.Viewport.X, Seg.Viewport.Y,
            *FrameStatsJsonFields(ComputeFrameStats(Seg.FrameMs)), GpuStats.P50Ms, GpuStats.P95Ms, PerfMean(Seg.GpuMs));
    }
    // Frame caps must really be off (a console/ini value outranks ECVF_SetByCode).
    T.TestEqual(TEXT("r.VSync is 0 during sampling"), PerfCvarInt(TEXT("r.VSync")), 0);
    T.TestEqual(TEXT("r.VSyncEditor is 0 during sampling"), PerfCvarInt(TEXT("r.VSyncEditor")), 0);
    T.TestEqual(TEXT("r.DontLimitOnBattery is 1 during sampling"), PerfCvarInt(TEXT("r.DontLimitOnBattery")), 1);
    T.TestEqual(TEXT("t.MaxFPS is 0 during sampling"), PerfCvar(TEXT("t.MaxFPS")) ? PerfCvar(TEXT("t.MaxFPS"))->GetFloat() : -1.0f, 0.0f);
    T.TestTrue(TEXT("GPU frame timing is non-zero (RHIGetGPUFrameCycles)"), PerfMean(Gpu[0]) > 0.0 && PerfMean(Gpu[1]) > 0.0);

    FString SpeciesJson;
    for (int32 Sp = 0; Sp < 2; ++Sp)
    {
        const FPSLFrameStats F = ComputeFrameStats(Frames[Sp]);
        const FPSLFrameStats G = ComputeFrameStats(Gpu[Sp]);
        SpeciesJson += FString::Printf(TEXT("%s\"%s\":{%s,\"gpu_p50Ms\":%.3f,\"gpu_p95Ms\":%.3f,\"gpu_p99Ms\":%.3f,%s}"),
            Sp > 0 ? TEXT(",") : TEXT(""), Sp ? TEXT("hero") : TEXT("old"), *FrameStatsJsonFields(F), G.P50Ms, G.P95Ms, G.P99Ms,
            S.SpeciesJson[Sp].IsEmpty() ? TEXT("\"mesh\":null") : *S.SpeciesJson[Sp]);
    }
    const IConsoleVariable* MaxFps = PerfCvar(TEXT("t.MaxFPS"));
    const Scalability::FQualityLevels Q = Scalability::GetQualityLevels();
    const FString Json = FString::Printf(
        TEXT("{\"test\":\"PSL.Dev.HeroTrex.Perf\",\"map\":\"%s\",\"rhi\":\"%s\",\"vsync\":%d,\"vsync_editor\":%d,\"dont_limit_on_battery\":%d,\"t_maxfps\":%.1f,\"smooth_frame_rate\":%s,\"on_battery\":%s,")
        TEXT("\"discard_frames\":%d,\"sample_frames\":%d,\"order\":\"ABBA old,hero,hero,old\",\"unfocused_frames\":%d,")
        TEXT("\"quality\":{\"ResolutionQuality\":%.1f,\"ViewDistanceQuality\":%d,\"AntiAliasingQuality\":%d,\"ShadowQuality\":%d,\"GlobalIlluminationQuality\":%d,\"ReflectionQuality\":%d,\"PostProcessQuality\":%d,\"TextureQuality\":%d,\"EffectsQuality\":%d,\"FoliageQuality\":%d,\"ShadingQuality\":%d},")
        TEXT("\"species\":{%s},\"segments\":[%s]}"),
        *JsonEscape(Ctx.World->GetMapName()), *JsonEscape(FApp::GetGraphicsRHI()), PerfCvarInt(TEXT("r.VSync")), PerfCvarInt(TEXT("r.VSyncEditor")),
        PerfCvarInt(TEXT("r.DontLimitOnBattery")), MaxFps ? MaxFps->GetFloat() : -1.0f,
        JsonBool(GEngine->bSmoothFrameRate), JsonBool(FPlatformMisc::IsRunningOnBattery()), PerfDiscardFrames, PerfSampleFrames, S.Unfocused,
        static_cast<double>(Q.ResolutionQuality), Q.ViewDistanceQuality, Q.AntiAliasingQuality, Q.ShadowQuality, Q.GlobalIlluminationQuality,
        Q.ReflectionQuality, Q.PostProcessQuality, Q.TextureQuality, Q.EffectsQuality, Q.FoliageQuality, Q.ShadingQuality, *SpeciesJson, *SegJson);
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("PIE"));
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Filename = FPaths::Combine(Directory, TEXT("HeroPerfStats.json"));
    T.TestTrue(FString::Printf(TEXT("Perf file saved: %s"), *Filename), FFileHelper::SaveStringToFile(Json, *Filename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    T.AddInfo(Json);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLHeroPerfTest, "PSL.Dev.HeroTrex.Perf", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPSLHeroPerfTest::RunTest(const FString& Parameters)
{
    if (!FApp::CanEverRender())
    {
        AddWarning(TEXT("perf needs a real RHI: skipped under -nullrhi"));
        return true;
    }
    if (!FPackageName::DoesPackageExist(PerfHeroMeshPackage))
    {
        AddWarning(TEXT("hero assets absent: perf skipped"));
        return true;
    }
    const int32 PrevHero = GetIntCvar(TEXT("psl.Dev.HeroTrex"));
    if (!SetIntCvar(TEXT("psl.Dev.HeroTrex"), 1))
    {
        AddWarning(TEXT("hero assets absent: perf skipped (psl.Dev.HeroTrex could not be set)"));
        SetIntCvar(TEXT("psl.Dev.HeroTrex"), PrevHero);
        return true;
    }
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("HeroPerf"));
    if (!SetPredatorSpawnCvar(1) || !QueuePieStart(*this, Context, PerfMapPath))
    {
        SetIntCvar(TEXT("psl.Dev.HeroTrex"), PrevHero);
        return false;
    }
    const TSharedRef<FPerfState> S = MakeShared<FPerfState>();
    AddPieStep(Context, TEXT("PerfSpawn"), 5.0f, [S](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        if (!Ctx.Character->GetMovementComponent()->IsMovingOnGround())
        {
            return false;
        }
        Ctx.PC->SetIgnoreLookInput(true);
        Ctx.PC->SetIgnoreMoveInput(true);
        S->Spawn = Ctx.Character->GetActorLocation();
        S->SpawnYaw = FRotator(0.0, Ctx.Character->GetActorRotation().Yaw, 0.0);
        PerfUncap(*S);
        return true;
    });
    // Same 1920x1080 path as PSL.PIE.LostValley.Explore: the PIE viewport needs its own window; restored in Cleanup.
    AddPieStep(Context, TEXT("PerfResize1080"), 8.0f, [S, Pass = 0, StepTime = -1.0](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        const TSharedPtr<SWindow> Window = Ctx.GameViewport->GetWindow();
        if (!Ctx.Test->TestTrue(TEXT("PIE runs in its own window (needed for 1920x1080)"),
                Window.IsValid() && Ctx.GameViewport->Viewport && Window != FGlobalTabmanager::Get()->GetRootWindow()))
        {
            Ctx.MarkInvalid(TEXT("PIE not in a new window: no 1080p perf"));
            return true;
        }
        const double Now = Ctx.World->GetTimeSeconds();
        if (StepTime >= 0.0 && Now - StepTime < 0.5)
        {
            return false;
        }
        if (StepTime < 0.0)
        {
            S->OriginalClient = FVector2D(Window->GetClientSizeInScreen());
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
        if (!Ctx.Test->TestTrue(FString::Printf(TEXT("Viewport is 1920x1080 after %d resize passes (was %dx%d)"), Pass, Size.X, Size.Y), bExact))
        {
            Ctx.MarkInvalid(TEXT("Viewport not 1920x1080"));
        }
        return true;
    });
    // 4 x (300 + 1500) frames; at a 20 fps floor this is 360 s.
    AddPieStep(Context, TEXT("PerfSample"), 400.0f, [S](FPSLPieCtx& Ctx)
    {
        return !Ctx.IsUsable() || PerfTick(Ctx, *S);
    });
    AddPieStep(Context, TEXT("PerfReport"), 5.0f, [S](FPSLPieCtx& Ctx)
    {
        if (Ctx.IsUsable())
        {
            PerfReport(Ctx, *S);
        }
        return true;
    });
    // HasObjects, not IsUsable: also restore after a failed or timed-out step.
    AddPieStep(Context, TEXT("Cleanup"), 2.0f, [S, PrevHero](FPSLPieCtx& Ctx)
    {
        PerfRestoreCaps(*S);
        SetIntCvar(TEXT("psl.Dev.HeroTrex"), PrevHero);
        SetPredatorSpawnCvar(1);
        if (Ctx.HasObjects())
        {
            Ctx.PC->ResetIgnoreLookInput();
            Ctx.PC->ResetIgnoreMoveInput();
            if (S->OriginalClient.X > 0.0)
            {
                if (const TSharedPtr<SWindow> Window = Ctx.GameViewport->GetWindow())
                {
                    Window->Resize(S->OriginalClient);
                }
            }
        }
        return true;
    });
    QueuePieEnd(Context);
    return true;
}

} // namespace

#endif
