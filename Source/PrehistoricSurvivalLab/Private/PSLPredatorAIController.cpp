#include "PSLPredatorAIController.h"

#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISense_Sight.h"
#include "PSLCharacter.h"
#include "PSLPredatorCharacter.h"
#include "PSLAudio.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

namespace
{
constexpr float PredatorThinkInterval = 0.2f;
constexpr float PredatorMoveAcceptanceRadius = 80.0f;
constexpr float AttackReachSlack = 50.0f;
// Slack after impact so Recover starts once the strike has resolved (Think runs every PredatorThinkInterval).
constexpr float AttackEndMargin = 0.1f;
// Seconds into the attack clip where the bite lands; the clip is started this long before the impact (clip length is logged once).
constexpr float AttackClipBiteSeconds = 0.6f;
constexpr float SearchRadius = 600.0f;

const TCHAR* PredatorStateName(EPSLPredatorState State)
{
    switch (State)
    {
    case EPSLPredatorState::Idle: return TEXT("Idle");
    case EPSLPredatorState::Patrol: return TEXT("Patrol");
    case EPSLPredatorState::Investigate: return TEXT("Investigate");
    case EPSLPredatorState::Chase: return TEXT("Chase");
    case EPSLPredatorState::Attack: return TEXT("Attack");
    case EPSLPredatorState::Recover: return TEXT("Recover");
    case EPSLPredatorState::Dead: return TEXT("Dead");
    }
    return TEXT("?");
}

const TCHAR* ReasonFor(EPSLPredatorState From, EPSLPredatorState To, bool bHeard)
{
    using E = EPSLPredatorState;
    switch (To)
    {
    case E::Dead: return TEXT("dead");
    case E::Attack: return TEXT("target_in_range");
    case E::Recover: return TEXT("attack_done");
    case E::Chase: return TEXT("target_seen");
    case E::Investigate: return bHeard ? TEXT("heard_noise") : TEXT("lost_sight");
    case E::Patrol: return TEXT("idle_done");
    case E::Idle:
        return From == E::Investigate ? TEXT("search_gave_up") : From == E::Patrol ? TEXT("patrol_done") : TEXT("target_gone");
    }
    return TEXT("?");
}
}

APSLPredatorAIController::APSLPredatorAIController()
{
    PredatorPerception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("PredatorPerception"));

    SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
    SightConfig->SightRadius = 1800.0f;
    SightConfig->LoseSightRadius = 2600.0f;
    SightConfig->PeripheralVisionAngleDegrees = 70.0f;
    SightConfig->DetectionByAffiliation.bDetectEnemies = true;
    SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
    SightConfig->DetectionByAffiliation.bDetectFriendlies = true;
    PredatorPerception->ConfigureSense(*SightConfig);

    HearingConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("HearingConfig"));
    HearingConfig->HearingRange = 2500.0f;
    HearingConfig->SetMaxAge(5.0f);
    HearingConfig->DetectionByAffiliation.bDetectEnemies = true;
    HearingConfig->DetectionByAffiliation.bDetectNeutrals = true;
    HearingConfig->DetectionByAffiliation.bDetectFriendlies = true;
    PredatorPerception->ConfigureSense(*HearingConfig);

    PredatorPerception->SetDominantSense(SightConfig->GetSenseImplementation());
    SetPerceptionComponent(*PredatorPerception);
    PrimaryActorTick.bCanEverTick = true;
}

void APSLPredatorAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);

    if (PredatorPerception && !PredatorPerception->OnTargetPerceptionUpdated.IsAlreadyBound(this, &APSLPredatorAIController::HandleTargetPerceptionUpdated))
    {
        PredatorPerception->OnTargetPerceptionUpdated.AddDynamic(this, &APSLPredatorAIController::HandleTargetPerceptionUpdated);
    }

    const double Now = GetWorld()->GetTimeSeconds();
    HomeLocation = InPawn ? InPawn->GetActorLocation() : FVector::ZeroVector;
    PredatorState = EPSLPredatorState::Idle;
    StateEnteredAt = Now;
    StateEndsAt = Now + IdleSeconds;
    UE_LOG(LogTemp, Display, TEXT("PSL predator %s possessed at %s state=Idle"), *GetNameSafe(InPawn), *HomeLocation.ToCompactString());
    GetWorldTimerManager().SetTimer(ThinkTimer, this, &APSLPredatorAIController::Think, PredatorThinkInterval, true);
}

void APSLPredatorAIController::OnUnPossess()
{
    GetWorldTimerManager().ClearTimer(ThinkTimer);
    ClearAttackTimers();
    Super::OnUnPossess();
}

void APSLPredatorAIController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    GetWorldTimerManager().ClearTimer(ThinkTimer);
    ClearAttackTimers();
    Super::EndPlay(EndPlayReason);
}

void APSLPredatorAIController::StopAI()
{
    if (PredatorState != EPSLPredatorState::Dead)
    {
        EnterState(EPSLPredatorState::Dead, TEXT("dead"), 0.0f, false);
    }
    Target.Reset();
    GetWorldTimerManager().ClearTimer(ThinkTimer);
    ClearAttackTimers();
    StopMovement();
    if (PredatorPerception)
    {
        PredatorPerception->Deactivate();
    }
}

// The only way the predator learns where the player is: a sight or hearing stimulus.
void APSLPredatorAIController::HandleTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
    if (PredatorState == EPSLPredatorState::Dead || !Cast<APSLCharacter>(Actor))
    {
        return;
    }

    // Only a successful stimulus is knowledge. Think() refreshes the position every tick while the target is seen,
    // so a sight-lost update adds nothing; an expired noise carries nothing new either.
    if (!Stimulus.WasSuccessfullySensed())
    {
        return;
    }
    const bool bHeard = Stimulus.Type == UAISense::GetSenseID<UAISense_Hearing>();
    Target = Actor;
    LastKnownPosition = Stimulus.StimulusLocation;
    bHasLastKnown = true;
    bNoisePending |= bHeard;
}

void APSLPredatorAIController::Think()
{
    APSLPredatorCharacter* Predator = Cast<APSLPredatorCharacter>(GetPawn());
    UWorld* World = GetWorld();
    if (!Predator || !World || PredatorState == EPSLPredatorState::Dead)
    {
        return;
    }

    // OnTargetPerceptionUpdated does not re-fire while a target stays visible: read the live sight set.
    TArray<AActor*> Seen;
    if (PredatorPerception)
    {
        PredatorPerception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Seen);
    }
    AActor* Visible = Target.IsValid() && Seen.Contains(Target.Get()) ? Target.Get() : nullptr;
    for (int32 Index = 0; !Visible && Index < Seen.Num(); ++Index)
    {
        if (Cast<APSLCharacter>(Seen[Index]))
        {
            Visible = Seen[Index];
        }
    }
    const APSLCharacter* VisiblePlayer = Cast<APSLCharacter>(Visible);
    if (VisiblePlayer && VisiblePlayer->GetStatsComponent() && VisiblePlayer->GetStatsComponent()->IsDead())
    {
        Visible = nullptr; // a dead player is no longer prey
    }
    // Position comes from the sight stimulus, never from the actor itself: a target that just vanished can stay in the
    // perceived set until the next sight query, and its live location must not leak.
    FVector VisibleAt = FVector::ZeroVector;
    if (Visible)
    {
        const FActorPerceptionInfo* Info = PredatorPerception->GetActorInfo(*Visible);
        const int32 SightIndex = UAISense::GetSenseID<UAISense_Sight>().Index;
        if (Info && Info->LastSensedStimuli.IsValidIndex(SightIndex))
        {
            VisibleAt = Info->LastSensedStimuli[SightIndex].StimulusLocation;
            Target = Visible;
            LastKnownPosition = VisibleAt;
            bHasLastKnown = true;
        }
        else
        {
            Visible = nullptr;
        }
    }

    const double Now = World->GetTimeSeconds();
    const float Distance = Visible ? FVector::Dist(Predator->GetActorLocation(), VisibleAt) : 0.0f;
    FPSLPredatorSenses Senses;
    Senses.bDead = Predator->IsDead();
    Senses.bTargetVisible = Visible != nullptr;
    Senses.TargetDistance = Distance;
    Senses.bHasLastKnownPosition = bHasLastKnown;
    Senses.bAttackReady = Now >= NextAttackAt;
    Senses.bStateTimerExpired = Now >= StateEndsAt;
    Senses.bHeardNoise = bNoisePending;
    bNoisePending = false;

    FPSLPredatorTuning Tuning;
    Tuning.DetectRadius = SightConfig->SightRadius;
    Tuning.LoseRadius = SightConfig->LoseSightRadius;
    Tuning.AttackRange = Predator->AttackRange;

    const EPSLPredatorState Next = DecidePredatorState(Senses, Tuning, PredatorState);
    // A fresh noise while already investigating restarts the search at the new position.
    if (Next != PredatorState || (Senses.bHeardNoise && Next == EPSLPredatorState::Investigate))
    {
        EnterState(Next, ReasonFor(PredatorState, Next, Senses.bHeardNoise), Distance, Senses.bTargetVisible);
        return;
    }

    const bool bMoveIdle = GetMoveStatus() == EPathFollowingStatus::Idle;
    switch (PredatorState)
    {
    case EPSLPredatorState::Chase:
        MoveToLocation(LastKnownPosition, PredatorMoveAcceptanceRadius);
        break;
    case EPSLPredatorState::Patrol:
        if (bMoveIdle)
        {
            StartPatrolMove();
        }
        break;
    case EPSLPredatorState::Investigate:
        if (bMoveIdle)
        {
            // Reached the last known position: sniff around it until the timer gives up.
            if (UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(World))
            {
                FNavLocation Point;
                if (Nav->GetRandomReachablePointInRadius(LastKnownPosition, SearchRadius, Point))
                {
                    MoveToLocation(Point.Location, PredatorMoveAcceptanceRadius);
                }
            }
        }
        break;
    default:
        break;
    }
}

void APSLPredatorAIController::EnterState(EPSLPredatorState NewState, const TCHAR* Reason, float Distance, bool bVisible)
{
    UWorld* World = GetWorld();
    const double Now = World ? World->GetTimeSeconds() : 0.0;
    const EPSLPredatorState From = PredatorState;
    PredatorState = NewState;
    StateEnteredAt = Now;
    StateEndsAt = Now + StateDuration(NewState);

    if (Transitions.Num() >= MaxTransitions)
    {
        Transitions.RemoveAt(0);
    }
    Transitions.Add({Now, From, NewState, FString(Reason)});
    UE_LOG(LogTemp, Display, TEXT("PSL predator %s %s -> %s reason=%s dist=%.0f visible=%d lkp=%d"),
        *GetNameSafe(GetPawn()), PredatorStateName(From), PredatorStateName(NewState), Reason, Distance, bVisible ? 1 : 0, bHasLastKnown ? 1 : 0);

    APSLPredatorCharacter* Predator = Cast<APSLPredatorCharacter>(GetPawn());
    if (!Predator || NewState == EPSLPredatorState::Dead)
    {
        return;
    }
    UCharacterMovementComponent* Movement = Predator->GetCharacterMovement();
    switch (NewState)
    {
    case EPSLPredatorState::Idle:
        // Back to normal: forget the encounter.
        StopMovement();
        bHasLastKnown = false;
        Target.Reset();
        Movement->MaxWalkSpeed = Predator->PatrolSpeed;
        break;
    case EPSLPredatorState::Patrol:
        Movement->MaxWalkSpeed = Predator->PatrolSpeed;
        StartPatrolMove();
        break;
    case EPSLPredatorState::Investigate:
        Movement->MaxWalkSpeed = (Predator->PatrolSpeed + Predator->ChaseSpeed) * 0.5f;
        MoveToLocation(LastKnownPosition, PredatorMoveAcceptanceRadius);
        break;
    case EPSLPredatorState::Chase:
        Movement->MaxWalkSpeed = Predator->ChaseSpeed;
        MoveToLocation(LastKnownPosition, PredatorMoveAcceptanceRadius);
        break;
    case EPSLPredatorState::Attack:
    {
        StopMovement();
        ClearAttackTimers();
        // Cooldown counts from the end of the strike; otherwise the long attack outlasts it and Recover loops straight back into Attack.
        NextAttackAt = Now + StateDuration(EPSLPredatorState::Attack) + Predator->AttackCooldown;
        bAttackWindingUp = true;
        WindupEndsAt = Now + AttackWindupSeconds;
        if (!bClipLengthLogged)
        {
            bClipLengthLogged = true;
            UE_LOG(LogTemp, Display, TEXT("PSL predator %s attack clip length=%.2fs (bite moment constant %.2fs, impact %.2fs after Attack starts)"),
                *Predator->GetName(), Predator->GetAttackClipLength(), AttackClipBiteSeconds, AttackWindupSeconds + AttackStrikeSeconds);
        }
        // Start the clip so its bite moment lands on the impact; a window shorter than the bite moment starts it now.
        const float ClipDelay = AttackWindupSeconds + AttackStrikeSeconds - AttackClipBiteSeconds;
        if (ClipDelay <= KINDA_SMALL_NUMBER)
        {
            Predator->PlayAttackAnimation();
        }
        else
        {
            FTimerDelegate Play = FTimerDelegate::CreateWeakLambda(Predator, [Predator]() { Predator->PlayAttackAnimation(); });
            GetWorldTimerManager().SetTimer(AttackAnimTimer, Play, ClipDelay, false);
        }
        break;
    }
    case EPSLPredatorState::Recover:
        StopMovement();
        ClearAttackTimers();
        break;
    default:
        break;
    }
}

void APSLPredatorAIController::StartPatrolMove()
{
    UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(GetWorld());
    FNavLocation Point;
    if (!Nav || !Nav->GetRandomReachablePointInRadius(HomeLocation, PatrolRadius, Point)
        || MoveToLocation(Point.Location, PredatorMoveAcceptanceRadius) == EPathFollowingRequestResult::Failed)
    {
        UE_LOG(LogTemp, Warning, TEXT("PSL predator %s: no reachable patrol point near %s"), *GetNameSafe(GetPawn()), *HomeLocation.ToCompactString());
    }
}

void APSLPredatorAIController::CommitStrike()
{
    bAttackWindingUp = false;
    if (PredatorState != EPSLPredatorState::Attack)
    {
        return;
    }
    const APSLPredatorCharacter* Predator = Cast<APSLPredatorCharacter>(GetPawn());
    if (!Predator)
    {
        return;
    }
    FrozenStrikeDir = Predator->GetActorForwardVector().GetSafeNormal2D();
    bStrikeCommitted = true;
    GetWorldTimerManager().SetTimer(AttackHitTimer, this, &APSLPredatorAIController::ResolveAttackHit, FMath::Max(AttackStrikeSeconds, KINDA_SMALL_NUMBER), false);
}

void APSLPredatorAIController::ResolveAttackHit()
{
    APSLPredatorCharacter* Predator = Cast<APSLPredatorCharacter>(GetPawn());
    AActor* Victim = Target.Get();
    if (!Predator || Predator->IsDead() || !Victim || !bStrikeCommitted)
    {
        return;
    }
    const FVector ToVictim = Victim->GetActorLocation() - Predator->GetActorLocation();
    const float Distance = ToVictim.Size();
    const float AngleDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(FrozenStrikeDir, ToVictim.GetSafeNormal2D()), -1.0f, 1.0f)));
    const TCHAR* MissReason = Distance > Predator->AttackRange + AttackReachSlack ? TEXT("out_of_reach")
        : AngleDeg > AttackConeDegrees ? TEXT("out_of_cone") : nullptr;

    // Impact sound plays on hit and miss alike (positional, at the predator).
    if (USoundBase* Bite = PSLAudio::Load(PSLAudio::Bite))
    {
        UGameplayStatics::PlaySoundAtLocation(Predator, Bite, Predator->GetActorLocation());
    }
    if (MissReason)
    {
        ++MissCount;
        UE_LOG(LogTemp, Display, TEXT("PSL predator %s attack missed %s dist=%.0f angle=%.0f reason=%s"),
            *Predator->GetName(), *Victim->GetName(), Distance, AngleDeg, MissReason);
        return;
    }
    ++HitCount;
    const float Dealt = UGameplayStatics::ApplyDamage(Victim, Predator->AttackDamage, this, Predator, UDamageType::StaticClass());
    UE_LOG(LogTemp, Display, TEXT("PSL predator %s hit %s for %.0f dist=%.0f angle=%.0f"), *Predator->GetName(), *Victim->GetName(), Dealt, Distance, AngleDeg);
}

void APSLPredatorAIController::ClearAttackTimers()
{
    GetWorldTimerManager().ClearTimer(AttackHitTimer);
    GetWorldTimerManager().ClearTimer(AttackAnimTimer);
    bAttackWindingUp = false;
    bStrikeCommitted = false;
}

void APSLPredatorAIController::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bAttackWindingUp)
    {
        return;
    }
    APSLPredatorCharacter* Predator = Cast<APSLPredatorCharacter>(GetPawn());
    UWorld* World = GetWorld();
    if (!Predator || !World || PredatorState != EPSLPredatorState::Attack)
    {
        bAttackWindingUp = false;
        return;
    }
    // Rate-limited turn toward the last known target position (no snap).
    const FRotator Current = Predator->GetActorRotation();
    const FVector ToTarget = (LastKnownPosition - Predator->GetActorLocation()).GetSafeNormal2D();
    if (!ToTarget.IsNearlyZero())
    {
        const FRotator Desired(0.0, ToTarget.Rotation().Yaw, 0.0);
        Predator->SetActorRotation(FMath::RInterpConstantTo(Current, Desired, DeltaSeconds, AttackTurnRateDegPerSec));
    }
    if (World->GetTimeSeconds() >= WindupEndsAt)
    {
        CommitStrike();
    }
}

float APSLPredatorAIController::StateDuration(EPSLPredatorState State) const
{
    switch (State)
    {
    case EPSLPredatorState::Idle: return IdleSeconds;
    case EPSLPredatorState::Patrol: return PatrolSeconds;
    case EPSLPredatorState::Investigate: return InvestigateSeconds;
    case EPSLPredatorState::Recover: return RecoverSeconds;
    case EPSLPredatorState::Attack: return AttackWindupSeconds + AttackStrikeSeconds + AttackEndMargin;
    default: return 1.0e9f; // Chase/Dead end by senses, not by time
    }
}
