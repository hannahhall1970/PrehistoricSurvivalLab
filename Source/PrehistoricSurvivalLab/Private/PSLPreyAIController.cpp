#include "PSLPreyAIController.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISense_Sight.h"
#include "PSLCharacter.h"
#include "PSLPreyCharacter.h"
#include "TimerManager.h"
#include "Engine/World.h"

namespace
{
    constexpr float ThinkInterval = 0.2f;
    constexpr float MoveAcceptanceRadius = 50.0f;
}

APSLPreyAIController::APSLPreyAIController()
{
    FleeRadius = 900.0f;
    SafeRadius = 1600.0f;
    WanderRadius = 800.0f;
    FleeDistance = 1000.0f;
    PreyState = EPSLPreyState::Idle;
    IdleUntil = 0.0;

    PreyPerception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("PreyPerception"));
    SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
    SightConfig->SightRadius = 1500.0f;
    SightConfig->LoseSightRadius = 2000.0f;
    SightConfig->PeripheralVisionAngleDegrees = 75.0f;
    SightConfig->DetectionByAffiliation.bDetectEnemies = true;
    SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
    SightConfig->DetectionByAffiliation.bDetectFriendlies = true;
    PreyPerception->ConfigureSense(*SightConfig);
    PreyPerception->SetDominantSense(SightConfig->GetSenseImplementation());
    SetPerceptionComponent(*PreyPerception);
}

EPSLPreyState APSLPreyAIController::DecidePreyState(bool bDead, bool bHasThreat, float ThreatDistance, float InFleeRadius, float InSafeRadius, EPSLPreyState Current, bool bIdleExpired)
{
    if (bDead || Current == EPSLPreyState::Dead)
    {
        return EPSLPreyState::Dead;
    }

    if (bHasThreat)
    {
        // Hysteresis: start fleeing inside FleeRadius, keep fleeing until beyond SafeRadius.
        const float Limit = (Current == EPSLPreyState::Flee) ? InSafeRadius : InFleeRadius;
        if (ThreatDistance < Limit)
        {
            return EPSLPreyState::Flee;
        }
    }

    switch (Current)
    {
    case EPSLPreyState::Flee:
        return EPSLPreyState::Idle;
    case EPSLPreyState::Wander:
        return EPSLPreyState::Wander;
    default:
        return bIdleExpired ? EPSLPreyState::Wander : EPSLPreyState::Idle;
    }
}

void APSLPreyAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);

    if (PreyPerception && !PreyPerception->OnTargetPerceptionUpdated.IsAlreadyBound(this, &APSLPreyAIController::HandleTargetPerceptionUpdated))
    {
        PreyPerception->OnTargetPerceptionUpdated.AddDynamic(this, &APSLPreyAIController::HandleTargetPerceptionUpdated);
    }

    PreyState = EPSLPreyState::Idle;
    IdleUntil = GetWorld()->GetTimeSeconds() + 1.0;
    GetWorldTimerManager().SetTimer(ThinkTimer, this, &APSLPreyAIController::Think, ThinkInterval, true);
}

void APSLPreyAIController::OnUnPossess()
{
    GetWorldTimerManager().ClearTimer(ThinkTimer);
    Super::OnUnPossess();
}

void APSLPreyAIController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    GetWorldTimerManager().ClearTimer(ThinkTimer);
    Super::EndPlay(EndPlayReason);
}

void APSLPreyAIController::HandleTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
    if (PreyState == EPSLPreyState::Dead || !Cast<APSLCharacter>(Actor))
    {
        return;
    }

    // Losing sight does not end the threat: the prey turns away while fleeing and sight drops at once.
    // Think() clears Threat once the player is beyond SafeRadius.
    if (Stimulus.WasSuccessfullySensed())
    {
        Threat = Actor;
    }
}

void APSLPreyAIController::NotifyDamaged(AActor* Attacker)
{
    if (PreyState != EPSLPreyState::Dead && Attacker)
    {
        Threat = Attacker;
    }
}

void APSLPreyAIController::StopAI()
{
    PreyState = EPSLPreyState::Dead;
    Threat.Reset();
    GetWorldTimerManager().ClearTimer(ThinkTimer);
    StopMovement();
    if (PreyPerception)
    {
        PreyPerception->Deactivate();
    }
}

void APSLPreyAIController::Think()
{
    const APawn* ControlledPawn = GetPawn();
    UWorld* World = GetWorld();
    if (!ControlledPawn || !World || PreyState == EPSLPreyState::Dead)
    {
        return;
    }

    // OnTargetPerceptionUpdated does not re-fire for a target that stays visible, so Threat is synced with the
    // live sight set here: cleared only when far away AND no longer sensed, re-acquired when empty.
    TArray<AActor*> Sensed;
    if (PreyPerception)
    {
        PreyPerception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Sensed);
    }
    AActor* ThreatActor = Threat.Get();
    if (ThreatActor && FVector::Dist(ControlledPawn->GetActorLocation(), ThreatActor->GetActorLocation()) >= SafeRadius && !Sensed.Contains(ThreatActor))
    {
        Threat.Reset();
        ThreatActor = nullptr;
    }
    if (!ThreatActor)
    {
        float Best = TNumericLimits<float>::Max();
        for (AActor* Candidate : Sensed)
        {
            const float D = FVector::Dist(ControlledPawn->GetActorLocation(), Candidate->GetActorLocation());
            if (Cast<APSLCharacter>(Candidate) && D < Best)
            {
                Best = D;
                ThreatActor = Candidate;
            }
        }
        if (ThreatActor)
        {
            Threat = ThreatActor;
        }
    }
    const float Distance = ThreatActor ? FVector::Dist(ControlledPawn->GetActorLocation(), ThreatActor->GetActorLocation()) : 0.0f;
    const bool bIdleExpired = World->GetTimeSeconds() >= IdleUntil;
    const EPSLPreyState Next = DecidePreyState(false, ThreatActor != nullptr, Distance, FleeRadius, SafeRadius, PreyState, bIdleExpired);

    if (Next != PreyState)
    {
        EnterState(Next);
    }
    else if (PreyState == EPSLPreyState::Flee && GetMoveStatus() == EPathFollowingStatus::Idle)
    {
        // Reached the flee point but the threat is still close: pick the next one.
        StartFlee();
    }
}

void APSLPreyAIController::EnterState(EPSLPreyState NewState)
{
    PreyState = NewState;
    switch (NewState)
    {
    case EPSLPreyState::Idle:
        StopMovement();
        IdleUntil = GetWorld()->GetTimeSeconds() + FMath::FRandRange(1.5f, 3.0f);
        break;
    case EPSLPreyState::Wander:
        StartWander();
        break;
    case EPSLPreyState::Flee:
        StartFlee();
        break;
    default:
        break;
    }
}

void APSLPreyAIController::StartWander()
{
    const APSLPreyCharacter* Prey = Cast<APSLPreyCharacter>(GetPawn());
    UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
    FNavLocation Destination;
    if (!Prey || !NavSys)
    {
        UE_LOG(LogTemp, Warning, TEXT("%s: no navigation system or prey pawn; staying idle."), *GetName());
        EnterState(EPSLPreyState::Idle);
        return;
    }

    if (!NavSys->GetRandomReachablePointInRadius(Prey->GetActorLocation(), WanderRadius, Destination))
    {
        EnterState(EPSLPreyState::Idle);
        return;
    }

    Prey->GetCharacterMovement()->MaxWalkSpeed = Prey->WanderSpeed;
    if (MoveToLocation(Destination.Location, MoveAcceptanceRadius) == EPathFollowingRequestResult::Failed)
    {
        EnterState(EPSLPreyState::Idle);
    }
}

void APSLPreyAIController::StartFlee()
{
    const APSLPreyCharacter* Prey = Cast<APSLPreyCharacter>(GetPawn());
    const AActor* ThreatActor = Threat.Get();
    UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
    if (!Prey || !ThreatActor || !NavSys)
    {
        if (!NavSys)
        {
            UE_LOG(LogTemp, Warning, TEXT("%s: no navigation system; cannot flee."), *GetName());
        }
        return;
    }

    const FVector Origin = Prey->GetActorLocation();
    FVector Away = (Origin - ThreatActor->GetActorLocation()).GetSafeNormal2D();
    if (Away.IsNearlyZero())
    {
        Away = Prey->GetActorForwardVector();
    }

    Prey->GetCharacterMovement()->MaxWalkSpeed = Prey->FleeSpeed;
    const float Angles[] = {0.0f, 45.0f, -45.0f, 90.0f, -90.0f};
    for (const float Angle : Angles)
    {
        const FVector Target = Origin + Away.RotateAngleAxis(Angle, FVector::UpVector) * FleeDistance;
        FNavLocation Projected;
        if (NavSys->ProjectPointToNavigation(Target, Projected, FVector(300.0f, 300.0f, 500.0f))
            && MoveToLocation(Projected.Location, MoveAcceptanceRadius) != EPathFollowingRequestResult::Failed)
        {
            return;
        }
    }
    // No reachable flee point this tick; Think() retries while the threat stays close.
}

void APSLPreyAIController::OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
    Super::OnMoveCompleted(RequestID, Result);
    if (PreyState == EPSLPreyState::Wander)
    {
        EnterState(EPSLPreyState::Idle);
    }
}
