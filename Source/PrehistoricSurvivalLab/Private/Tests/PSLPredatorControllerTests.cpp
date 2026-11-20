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
        for (const double Distance : {2100.0, 1600.0, 1200.0, 900.0, 700.0})
        {
            for (int32 Step = 0; Step < 36; ++Step)
            {
                const FVector Dir = FVector(1.0, 0.0, 0.0).RotateAngleAxis(Step * 10.0, FVector::UpVector);
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
        Ctx.MarkInvalid(FString::Printf(TEXT("No floor spot behind the predator at %s facing %s"), *P.ToCompactString(), *Forward.ToCompactString()));
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
    AddPieStep(Context, TEXT("AttackDamage"), 4.0f, [Run](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable() || !Run->Predator.IsValid())
        {
            return true;
        }
        const float Expected = Run->HealthBefore - Run->Predator->AttackDamage;
        if (Ctx.Character->GetStatsComponent()->GetHealth() > Expected + 1.0f)
        {
            return false; // the hit lands AttackWindupSeconds + AttackStrikeSeconds after entering Attack
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

namespace
{
struct FAttackFeel
{
    double AttackStart = 0.0;
    float HealthBefore = 0.0f;
    float LastYaw = 0.0f;
    bool bHaveYaw = false;
    float TotalTurn = 0.0f;
};

// Per-tick yaw change must respect the wind-up turn rate (no snap).
void SampleYaw(FPSLPieCtx& Ctx, FAttackFeel& Feel, const APSLPredatorAIController& AI, const APSLPredatorCharacter& Predator)
{
    const float Yaw = Predator.GetActorRotation().Yaw;
    if (Feel.bHaveYaw)
    {
        const float Delta = FMath::Abs(FRotator::NormalizeAxis(Yaw - Feel.LastYaw));
        Feel.TotalTurn += Delta;
        const float Limit = AI.AttackTurnRateDegPerSec * Ctx.World->GetDeltaSeconds() + 2.0f;
        Ctx.Test->TestTrue(FString::Printf(TEXT("Yaw change %.1f deg this tick stays within %.1f deg (no snap)"), Delta, Limit), Delta <= Limit);
    }
    Feel.LastYaw = Yaw;
    Feel.bHaveYaw = true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLPredatorAttackFeelTest, "PSL.AI.PredatorAttackFeel", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPSLPredatorAttackFeelTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("PredatorAttackFeel"));
    const TSharedRef<FPredatorRun> Run = MakeShared<FPredatorRun>();
    const TSharedRef<FAttackFeel> Feel = MakeShared<FAttackFeel>();
    if (!QueuePieStart(*this, Context))
    {
        return false;
    }

    // Player stands still 170 uu in front of the predator, which sees it and attacks.
    AddPieStep(Context, TEXT("Spawn"), 5.0f, [Run](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        const double Z = Ctx.Character->GetActorLocation().Z;
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
        Run->Predator = Ctx.World->SpawnActor<APSLPredatorCharacter>(APSLPredatorCharacter::StaticClass(),
            FTransform(FRotator::ZeroRotator, FVector(-1500.0, 0.0, Z + 80.0)), Params);
        if (!Ctx.Test->TestNotNull(TEXT("Predator spawned and possessed"), AIOf(*Run)))
        {
            Ctx.MarkInvalid(TEXT("Predator controller missing"));
            return true;
        }
        Teleport(Ctx, FVector(-1330.0, 0.0, Z));
        return true;
    });
    AddPieStep(Context, TEXT("AttackBegins"), 8.0f, [Run, Feel](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable() || !AIOf(*Run))
        {
            return true;
        }
        Ctx.Character->GetCharacterMovement()->StopMovementImmediately();
        if (!ConsumeUntil(*Run, EPSLPredatorState::Attack, TEXT("target_in_range")))
        {
            return false;
        }
        Feel->AttackStart = AIOf(*Run)->GetTransitions().Last().WorldTime;
        Feel->HealthBefore = Ctx.Character->GetStatsComponent()->GetHealth();
        Feel->bHaveYaw = false;
        Ctx.Test->TestTrue(TEXT("Attack starts in the wind-up"), AIOf(*Run)->IsAttackWindingUp());
        return true;
    });
    // a) No damage until the strike is committed; the wind-up lasts the tunable.
    AddPieStep(Context, TEXT("Windup"), 3.0f, [Run, Feel](FPSLPieCtx& Ctx)
    {
        APSLPredatorAIController* AI = AIOf(*Run);
        if (!Ctx.IsUsable() || !AI)
        {
            return true;
        }
        SampleYaw(Ctx, *Feel, *AI, *Run->Predator);
        if (!AI->IsStrikeCommitted())
        {
            Ctx.Test->TestEqual(TEXT("No damage during the wind-up"), Ctx.Character->GetStatsComponent()->GetHealth(), Feel->HealthBefore);
            return false;
        }
        const double Windup = Ctx.World->GetTimeSeconds() - Feel->AttackStart;
        Ctx.Test->TestTrue(FString::Printf(TEXT("Wind-up lasted %.2fs (>= %.2fs - 0.1)"), Windup, AI->AttackWindupSeconds), Windup >= AI->AttackWindupSeconds - 0.1);
        Ctx.Test->TestEqual(TEXT("Still no damage at commit"), Ctx.Character->GetStatsComponent()->GetHealth(), Feel->HealthBefore);
        return true;
    });
    // b) Standing still in front: hit exactly once.
    AddPieStep(Context, TEXT("StandingHit"), 3.0f, [Run, Feel](FPSLPieCtx& Ctx)
    {
        const APSLPredatorAIController* AI = AIOf(*Run);
        if (!Ctx.IsUsable() || !AI)
        {
            return true;
        }
        if (AI->GetHitCount() < 1)
        {
            return false;
        }
        Ctx.Test->TestEqual(TEXT("Exactly one hit"), AI->GetHitCount(), 1);
        Ctx.Test->TestEqual(TEXT("No miss"), AI->GetMissCount(), 0);
        Ctx.Test->TestTrue(TEXT("Player lost AttackDamage"),
            FMath::IsNearlyEqual(Ctx.Character->GetStatsComponent()->GetHealth(), Feel->HealthBefore - Run->Predator->AttackDamage, 1.0f));
        return true;
    });
    // Second attack: the player stands 50 deg off-axis (the predator must turn, rate-limited), then sidesteps after commit.
    AddPieStep(Context, TEXT("SecondAttackBegins"), 12.0f, [Run, Feel](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable() || !AIOf(*Run))
        {
            return true;
        }
        if (!ConsumeUntil(*Run, EPSLPredatorState::Attack, TEXT("target_in_range")))
        {
            return false;
        }
        const FVector P = Run->Predator->GetActorLocation();
        const FVector Side = Run->Predator->GetActorForwardVector().GetSafeNormal2D().RotateAngleAxis(50.0, FVector::UpVector);
        Teleport(Ctx, FVector(P.X + Side.X * 170.0, P.Y + Side.Y * 170.0, Ctx.Character->GetActorLocation().Z));
        Feel->AttackStart = AIOf(*Run)->GetTransitions().Last().WorldTime;
        Feel->HealthBefore = Ctx.Character->GetStatsComponent()->GetHealth();
        Feel->bHaveYaw = false;
        Feel->TotalTurn = 0.0f;
        return true;
    });
    // c) Sidestep >= 300 uu perpendicular to the frozen direction right after commit: no damage, miss observed.
    AddPieStep(Context, TEXT("Sidestep"), 3.0f, [Run, Feel](FPSLPieCtx& Ctx)
    {
        APSLPredatorAIController* AI = AIOf(*Run);
        if (!Ctx.IsUsable() || !AI)
        {
            return true;
        }
        SampleYaw(Ctx, *Feel, *AI, *Run->Predator);
        if (!AI->IsStrikeCommitted())
        {
            return false;
        }
        const FVector P = Run->Predator->GetActorLocation();
        const FVector Right = FVector::CrossProduct(FVector::UpVector, AI->GetFrozenStrikeDirection());
        FVector Dest = P + Right * 350.0;
        if (!OnFloor(Dest))
        {
            Dest = P - Right * 350.0;
        }
        Dest.Z = Ctx.Character->GetActorLocation().Z;
        Ctx.Test->TestTrue(TEXT("Sidestep destination is on the arena floor"), OnFloor(Dest));
        Ctx.Test->AddInfo(FString::Printf(TEXT("Predator turned %.0f deg during the wind-up"), Feel->TotalTurn));
        Teleport(Ctx, Dest);
        return true;
    });
    AddPieStep(Context, TEXT("Missed"), 3.0f, [Run, Feel](FPSLPieCtx& Ctx)
    {
        const APSLPredatorAIController* AI = AIOf(*Run);
        if (!Ctx.IsUsable() || !AI)
        {
            return true;
        }
        if (AI->GetMissCount() < 1)
        {
            return false;
        }
        Ctx.Test->TestEqual(TEXT("Still exactly one hit"), AI->GetHitCount(), 1);
        Ctx.Test->TestEqual(TEXT("Sidestepped player is not damaged"), Ctx.Character->GetStatsComponent()->GetHealth(), Feel->HealthBefore);
        return true;
    });
    // Third attack: in reach (200 uu, inside AttackRange + slack) but ~90 deg off the frozen direction -> out_of_cone miss.
    AddPieStep(Context, TEXT("ThirdAttackBegins"), 15.0f, [Run, bStaged = false](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable() || !AIOf(*Run))
        {
            return true;
        }
        if (!bStaged)
        {
            // After the sidestep the player sits 90 deg off the predator; stage it in front once (as Spawn does) so re-acquisition does not depend on the random patrol point.
            bStaged = true;
            const FVector P = Run->Predator->GetActorLocation();
            const FVector Forward = Run->Predator->GetActorForwardVector().GetSafeNormal2D();
            Teleport(Ctx, FVector(P.X + Forward.X * 170.0, P.Y + Forward.Y * 170.0, Ctx.Character->GetActorLocation().Z));
        }
        Ctx.Character->GetCharacterMovement()->StopMovementImmediately();
        return ConsumeUntil(*Run, EPSLPredatorState::Attack, TEXT("target_in_range"));
    });
    AddPieStep(Context, TEXT("OffConeStep"), 3.0f, [Run, Feel](FPSLPieCtx& Ctx)
    {
        const APSLPredatorAIController* AI = AIOf(*Run);
        if (!Ctx.IsUsable() || !AI)
        {
            return true;
        }
        Ctx.Character->GetCharacterMovement()->StopMovementImmediately();
        if (!AI->IsStrikeCommitted())
        {
            return false;
        }
        const FVector P = Run->Predator->GetActorLocation();
        const FVector Right = FVector::CrossProduct(FVector::UpVector, AI->GetFrozenStrikeDirection());
        FVector Dest = P + Right * 200.0;
        if (!OnFloor(Dest))
        {
            Dest = P - Right * 200.0;
        }
        Dest.Z = Ctx.Character->GetActorLocation().Z;
        Ctx.Test->TestTrue(TEXT("Off-cone destination is on the arena floor"), OnFloor(Dest));
        Feel->HealthBefore = Ctx.Character->GetStatsComponent()->GetHealth();
        Teleport(Ctx, Dest);
        return true;
    });
    AddPieStep(Context, TEXT("OutOfCone"), 3.0f, [Run, Feel](FPSLPieCtx& Ctx)
    {
        const APSLPredatorAIController* AI = AIOf(*Run);
        if (!Ctx.IsUsable() || !AI)
        {
            return true;
        }
        if (AI->GetMissCount() < 2)
        {
            return false;
        }
        Ctx.Test->TestEqual(TEXT("Exactly two misses"), AI->GetMissCount(), 2);
        Ctx.Test->TestEqual(TEXT("Hit count unchanged"), AI->GetHitCount(), 1);
        Ctx.Test->TestEqual(TEXT("Off-cone player is not damaged"), Ctx.Character->GetStatsComponent()->GetHealth(), Feel->HealthBefore);
        Ctx.Test->TestEqual(TEXT("Miss reason is out_of_cone"), FString(AI->GetLastMissReason() ? AI->GetLastMissReason() : TEXT("")), FString(TEXT("out_of_cone")));
        return true;
    });
    QueuePieEnd(Context);
    return true;
}

#endif
