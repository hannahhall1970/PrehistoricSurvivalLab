#include "PSLDamageFeedback.h"

namespace PSLDamageFeedback
{
    float Severity(float Amount, float MaxHealth)
    {
        return (Amount > 0.0f && MaxHealth > 0.0f) ? FMath::Clamp(Amount / MaxHealth, 0.0f, 1.0f) : 0.0f;
    }

    float BearingDegrees(const FVector& PawnLocation, float PawnYawDegrees, const FVector& CauserLocation)
    {
        const FVector2D Delta(CauserLocation.X - PawnLocation.X, CauserLocation.Y - PawnLocation.Y);
        if (Delta.IsNearlyZero())
        {
            return 0.0f;
        }
        // Unreal yaw is measured from +X towards +Y (right), so a positive result is to the right of the pawn.
        const float WorldYaw = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X)));
        return FMath::UnwindDegrees(WorldYaw - PawnYawDegrees);
    }

    bool ShouldFlash(double Now, double LastFlashTime)
    {
        return Now - LastFlashTime >= MinFlashIntervalSeconds;
    }

    void GhostOnHit(FGhostBar& Ghost, float NewFraction)
    {
        if (NewFraction >= Ghost.Fraction)
        {
            Ghost = FGhostBar{NewFraction, 0.0f, 0.0f};
            return;
        }
        // Restart the hold from the current ghost value and re-time the drain so it still takes GhostDrainSeconds.
        Ghost.HoldRemaining = GhostHoldSeconds;
        Ghost.DrainPerSecond = (Ghost.Fraction - NewFraction) / GhostDrainSeconds;
    }

    void GhostStep(FGhostBar& Ghost, float Target, float DeltaSeconds)
    {
        // Not a hit (heal, starvation, save restore) or nothing left to trail: follow the value with no trail.
        if (Target >= Ghost.Fraction || Ghost.DrainPerSecond <= 0.0f)
        {
            Ghost = FGhostBar{Target, 0.0f, 0.0f};
            return;
        }
        if (Ghost.HoldRemaining > 0.0f)
        {
            const float Held = FMath::Min(Ghost.HoldRemaining, DeltaSeconds);
            Ghost.HoldRemaining -= Held;
            DeltaSeconds -= Held;
        }
        Ghost.Fraction = FMath::Max(Target, Ghost.Fraction - Ghost.DrainPerSecond * DeltaSeconds);
        if (Ghost.Fraction <= Target)
        {
            Ghost = FGhostBar{Target, 0.0f, 0.0f};
        }
    }

    bool NextLowHealth(bool bWasLow, float Health, float MaxHealth, bool bDead)
    {
        if (bDead || MaxHealth <= 0.0f)
        {
            return false;
        }
        const float Fraction = Health / MaxHealth;
        return Fraction <= (bWasLow ? LowHealthExitFraction : LowHealthEnterFraction);
    }

    bool ShouldHitStop(float SeverityValue, double Now, double LastHitStopTime)
    {
        return SeverityValue >= HitStopMinSeverity && Now - LastHitStopTime >= HitStopMinIntervalSeconds;
    }
}
