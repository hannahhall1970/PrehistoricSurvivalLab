#pragma once

#include "CoreMinimal.h"

/** Pure hit-feedback math (spec 005): no UObject, no world, no clock. Timings and thresholds are balance hypotheses. */
namespace PSLDamageFeedback
{
    // Health-bar flash (FR-002): on for FlashSeconds, at most one flash per MinFlashIntervalSeconds (<= 3 Hz).
    constexpr float FlashSeconds = 0.2f;
    constexpr float MinFlashIntervalSeconds = 1.0f / 3.0f;

    // Ghost bar (FR-003): holds the old value, then drains to the new one.
    constexpr float GhostHoldSeconds = 0.4f;
    constexpr float GhostDrainSeconds = 0.5f;

    // Hit-stop gate (FR-008).
    constexpr float HitStopMinSeverity = 0.15f;
    constexpr float HitStopMinIntervalSeconds = 0.5f;

    // Low-health hysteresis (FR-009), fractions of max health.
    constexpr float LowHealthEnterFraction = 0.25f;
    constexpr float LowHealthExitFraction = 0.35f;

    /** Ghost segment: Fraction is what the bar shows trailing the real value. Hold/Rate are internal. */
    struct FGhostBar
    {
        float Fraction = 1.0f;
        float HoldRemaining = 0.0f;
        float DrainPerSecond = 0.0f;
    };

    /** Amount / MaxHealth clamped to [0,1]; 0 when Amount <= 0 or MaxHealth <= 0. */
    PREHISTORICSURVIVALLAB_API float Severity(float Amount, float MaxHealth);

    /**
     * Bearing of Causer seen from Pawn, degrees in (-180, 180]: 0 = straight ahead of PawnYawDegrees, +90 = right, 180 = behind.
     * 0 when the two points coincide in the XY plane.
     */
    PREHISTORICSURVIVALLAB_API float BearingDegrees(const FVector& PawnLocation, float PawnYawDegrees, const FVector& CauserLocation);

    /** A hit moved the real value to NewFraction: the ghost keeps its (higher) value, restarts the hold and re-times the drain. A rise snaps the ghost. */
    PREHISTORICSURVIVALLAB_API void GhostOnHit(FGhostBar& Ghost, float NewFraction);

    /** Advances the ghost by DeltaSeconds toward Target: hold first, then a linear drain that never undershoots Target. A Target above the ghost snaps it. */
    PREHISTORICSURVIVALLAB_API void GhostStep(FGhostBar& Ghost, float Target, float DeltaSeconds);

    /** Next low-health state: false when dead; enters at <= 25 %, leaves above 35 %, otherwise keeps bWasLow. */
    PREHISTORICSURVIVALLAB_API bool NextLowHealth(bool bWasLow, float Health, float MaxHealth, bool bDead);

    /** Hit-stop only for severe hits (>= 0.15), and at most once per 0.5 s (LastHitStopTime is -inf-like before the first one). */
    PREHISTORICSURVIVALLAB_API bool ShouldHitStop(float SeverityValue, double Now, double LastHitStopTime);

    /** True when at least MinFlashIntervalSeconds passed since LastFlashTime (photosensitivity cap, 3 Hz). */
    PREHISTORICSURVIVALLAB_API bool ShouldFlash(double Now, double LastFlashTime);
}
