#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "PSLPieTestSupport.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "PSLPredatorAIController.h"
#include "PSLPredatorCharacter.h"

namespace
{
using namespace PSLPie;

// L_TestArena floor is |x|,|y| < 2200 with cover cubes at (+-1200, +-1200).
constexpr double FloorHalf = 2000.0;

void Teleport(FPSLPieCtx& Ctx, const FVector& Location)
{
    Ctx.Character->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
    Ctx.Character->GetCharacterMovement()->StopMovementImmediately();
}

bool OnFloor(const FVector& P)
{
    return FMath::Abs(P.X) < FloorHalf && FMath::Abs(P.Y) < FloorHalf;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLPredatorControllerTest, "PSL.AI.PredatorController", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPSLPredatorControllerTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("PredatorController"));
    const TSharedRef<FPredatorRun> Run = MakeShared<FPredatorRun>();
    if (!QueuePieStart(*this, Context))
    {
        return false;
    }

    // a) Spawn far from the player; the predator must start Idle and begin patrolling on its own.
    AddPieStep(Context, TEXT("SpawnPredator"), 5.0f, [Run](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        int32 Existing = 0;
        for (TActorIterator<APSLPredatorCharacter> It(Ctx.World.Get()); It; ++It)
        {
            ++Existing;
        }
        Ctx.Test->TestEqual(TEXT("L_TestArena spawns no predator by itself (GameMode spawns only in L_LostValley)"), Existing, 0);
        const double Z = Ctx.Character->GetActorLocation().Z;
        Teleport(Ctx, FVector(1800.0, 1800.0, Z));
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
        APSLPredatorCharacter* Predator = Ctx.World->SpawnActor<APSLPredatorCharacter>(APSLPredatorCharacter::StaticClass(),
            FTransform(FRotator::ZeroRotator, FVector(-1500.0, 0.0, Z + 80.0)), Params);
        Run->Predator = Predator;
        APSLPredatorAIController* AI = AIOf(*Run);
        if (!Ctx.Test->TestNotNull(TEXT("Predator spawned and possessed by APSLPredatorAIController"), AI))
        {
            Ctx.MarkInvalid(TEXT("Predator controller missing"));
            return true;
        }
        Ctx.Test->TestEqual(TEXT("Predator starts Idle"), AI->GetPredatorState(), EPSLPredatorState::Idle);
        Ctx.Test->TestFalse(TEXT("Predator starts without a last known position"), AI->HasLastKnownPosition());
        AI->IdleSeconds = 0.5f;
        AI->PatrolSeconds = 30.0f;
        AI->InvestigateSeconds = 6.0f;
        AI->RecoverSeconds = 0.5f;
        AI->PatrolRadius = 800.0f;
        return true;
    });
    AddPieStep(Context, TEXT("Patrol"), 8.0f, [Run](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable() || !AIOf(*Run))
        {
            return true;
        }
        if (!ConsumeUntil(*Run, EPSLPredatorState::Patrol, TEXT("idle_done")))
        {
            return false;
        }
        Ctx.Test->TestFalse(TEXT("Patrol began without knowing where the player is"), AIOf(*Run)->HasLastKnownPosition());
        return true;
    });
    AddPieStep(Context, TEXT("PatrolMoves"), 8.0f, [Run, Start = FVector::ZeroVector, bInit = false](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable() || !Run->Predator.IsValid())
        {
            return true;
        }
        if (!bInit)
        {
            bInit = true;
            Start = Run->Predator->GetActorLocation();
        }
        return FVector::Dist2D(Start, Run->Predator->GetActorLocation()) > 150.0;
    });

    // b) Out of sight (behind the predator, beyond sight radius) but within hearing: a bite is heard and investigated.
    AddPieStep(Context, TEXT("NoiseBehind"), 3.0f, [Run](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable() || !Run->Predator.IsValid())
        {
            return true;
        }
        const FVector P = Run->Predator->GetActorLocation();
        const FVector Forward = Run->Predator->GetActorForwardVector().GetSafeNormal2D();
        // Behind the predator (outside its 70 deg cone, so unseen at any range) and inside hearing range 2500.
        for (const double Distance : {2100.0, 1600.0, 1200.0})
        {
            for (int32 Step = 0; Step < 16; ++Step)
            {
                const FVector Dir = FVector(1.0, 0.0, 0.0).RotateAngleAxis(Step * 22.5, FVector::UpVector);
                const FVector Candidate(P.X + Dir.X * Distance, P.Y + Dir.Y * Distance, Ctx.Character->GetActorLocation().Z);
                if (FVector::DotProduct(Dir, Forward) < -0.5 && OnFloor(Candidate))
                {
                    Teleport(Ctx, Candidate);
                    Run->Seen = AIOf(*Run)->GetTransitions().Num();
                    Ctx.Test->TestTrue(TEXT("Bite accepted (noise source)"), Ctx.Character->TryBite());
                    return true;
                }
            }
        }
        Ctx.MarkInvalid(FString::Printf(TEXT("No floor spot behind the predator at %s"), *P.ToCompactString()));
        return true;
    });
    AddPieStep(Context, TEXT("InvestigateNoise"), 3.0f, [Run](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable() || !AIOf(*Run))
        {
            return true;
        }
        if (!ConsumeUntil(*Run, EPSLPredatorState::Investigate, TEXT("heard_noise")))
        {
            return false;
        }
        const APSLPredatorAIController* AI = AIOf(*Run);
        Ctx.Test->TestTrue(TEXT("Heard noise gives a last known position"), AI->HasLastKnownPosition());
        Ctx.Test->TestTrue(TEXT("Last known position is the noise source"),
            FVector::Dist2D(AI->GetLastKnownPosition(), Ctx.Character->GetActorLocation()) < 200.0);
        return true;
    });

    // c) Walking to the noise, it sees the (still) player and chases on the navmesh.
    AddPieStep(Context, TEXT("Chase"), 15.0f, [Run](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable() || !AIOf(*Run))
        {
            return true;
        }
        Ctx.Character->GetCharacterMovement()->StopMovementImmediately();
        if (!ConsumeUntil(*Run, EPSLPredatorState::Chase, TEXT("target_seen")))
        {
            return false;
        }
        Run->HealthBefore = Ctx.Character->GetStatsComponent()->GetHealth();
        return true;
    });

    // d) It closes in, attacks and deals damage.
    AddPieStep(Context, TEXT("Attack"), 15.0f, [Run](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable() || !AIOf(*Run))
        {
            return true;
        }
        Ctx.Character->GetCharacterMovement()->StopMovementImmediately();
        return ConsumeUntil(*Run, EPSLPredatorState::Attack, TEXT("target_in_range"));
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
        return true;
    });
    AddPieStep(Context, TEXT("Recover"), 3.0f, [Run](FPSLPieCtx& Ctx)
    {
        return !Ctx.IsUsable() || ConsumeUntil(*Run, EPSLPredatorState::Recover, TEXT("attack_done"));
    });

    // e) Back in Chase, the player vanishes beyond sight: it searches the last seen spot (not the new one), then gives up.
    AddPieStep(Context, TEXT("ChaseAgainThenHide"), 6.0f, [Run](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable() || !AIOf(*Run))
        {
            return true;
        }
        Ctx.Character->GetCharacterMovement()->StopMovementImmediately();
        if (AIOf(*Run)->GetPredatorState() != EPSLPredatorState::Chase)
        {
            return false;
        }
        const FVector P = Run->Predator->GetActorLocation();
        FVector Far = FVector::ZeroVector;
        for (const double X : {-FloorHalf, FloorHalf})
        {
            for (const double Y : {-FloorHalf, FloorHalf})
            {
                if (FVector::Dist2D(P, FVector(X, Y, 0.0)) > FVector::Dist2D(P, Far))
                {
                    Far = FVector(X, Y, 0.0);
                }
            }
        }
        Far.Z = Ctx.Character->GetActorLocation().Z;
        Ctx.Test->TestTrue(FString::Printf(TEXT("Hide spot is beyond lose radius (%.0f)"), FVector::Dist2D(P, Far)), FVector::Dist2D(P, Far) > 2600.0);
        Run->Seen = AIOf(*Run)->GetTransitions().Num();
        Teleport(Ctx, Far);
        return true;
    });
    AddPieStep(Context, TEXT("LoseSight"), 3.0f, [Run](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable() || !AIOf(*Run))
        {
            return true;
        }
        if (!ConsumeUntil(*Run, EPSLPredatorState::Investigate, TEXT("lost_sight")))
        {
            return false;
        }
        const APSLPredatorAIController* AI = AIOf(*Run);
        Ctx.Test->TestTrue(TEXT("Investigating with a last known position"), AI->HasLastKnownPosition());
        Ctx.Test->TestTrue(TEXT("Last known position is not the hidden player's position (no omniscience)"),
            FVector::Dist2D(AI->GetLastKnownPosition(), Ctx.Character->GetActorLocation()) > 1000.0);
        return true;
    });
    AddPieStep(Context, TEXT("GiveUp"), 8.0f, [Run](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable() || !AIOf(*Run))
        {
            return true;
        }
        if (!ConsumeUntil(*Run, EPSLPredatorState::Idle, TEXT("search_gave_up")))
        {
            return false;
        }
        Ctx.Test->TestFalse(TEXT("Giving up forgets the last known position"), AIOf(*Run)->HasLastKnownPosition());
        return true;
    });
    AddPieStep(Context, TEXT("BackToPatrol"), 4.0f, [Run](FPSLPieCtx& Ctx)
    {
        return !Ctx.IsUsable() || ConsumeUntil(*Run, EPSLPredatorState::Patrol, TEXT("idle_done"));
    });

    // f) Lethal damage: Dead is terminal and the brain stops.
    AddPieStep(Context, TEXT("Death"), 3.0f, [Run](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable() || !AIOf(*Run))
        {
            return true;
        }
        UGameplayStatics::ApplyDamage(Run->Predator.Get(), 1000.0f, Ctx.PC.Get(), Ctx.Character.Get(), UDamageType::StaticClass());
        Ctx.Test->TestTrue(TEXT("Predator is dead"), Run->Predator->IsDead());
        Ctx.Test->TestEqual(TEXT("Controller state Dead"), AIOf(*Run)->GetPredatorState(), EPSLPredatorState::Dead);
        Ctx.Test->AddInfo(TEXT("Transitions: ") + DumpTransitions(*Run));
        return true;
    });
    QueuePieEnd(Context);
    return true;
}

#endif
