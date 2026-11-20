#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "PSLPredatorCharacter.generated.h"

class UPSLStatsComponent;
class UStaticMeshComponent;
class USkeletalMesh;
class UAnimSequence;
class UTextRenderComponent;
class UMaterialInstanceDynamic;

/** T-Rex predator: perception-driven brain lives in APSLPredatorAIController; this pawn owns stats, visuals and tuning. */
UCLASS()
class PREHISTORICSURVIVALLAB_API APSLPredatorCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    APSLPredatorCharacter();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Predator")
    float PatrolSpeed = 220.0f;

    // Must stay below the T-Rex sprint (600) and Parasaurolophus sprint (760) so the player can outrun it.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Predator")
    float ChaseSpeed = 520.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Predator")
    float AttackDamage = 20.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Predator")
    float AttackCooldown = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Predator")
    float AttackRange = 260.0f;

    UFUNCTION(BlueprintPure, Category="Predator")
    UPSLStatsComponent* GetStatsComponent() const { return StatsComponent; }

    bool IsDead() const { return bDeathHandled; }

    /** Plays the Attack action on the skeletal visual; no-op on the cube placeholder. */
    void PlayAttackAnimation();

    /** Play length of the attack clip in seconds once loaded; 0 when the clip is missing (cube placeholder). */
    float GetAttackClipLength() const;

    virtual float TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

    // Soft references only: a missing import keeps the cube placeholder (L_TestArena tests rely on that).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Predator|Visual")
    TSoftObjectPtr<USkeletalMesh> PredatorMesh;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Predator|Visual")
    TSoftObjectPtr<UAnimSequence> PredatorIdleAnim;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Predator|Visual")
    TSoftObjectPtr<UAnimSequence> PredatorWalkAnim;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Predator|Visual")
    TSoftObjectPtr<UAnimSequence> PredatorRunAnim;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Predator|Visual")
    TSoftObjectPtr<UAnimSequence> PredatorAttackAnim;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Predator|Visual")
    TSoftObjectPtr<UAnimSequence> PredatorDeathAnim;

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

private:
    UFUNCTION()
    void HandleDeath();

    // Applies the soft-referenced skeletal mesh + clips fitted to the capsule; keeps the cube placeholder if any asset is missing.
    void ApplyPredatorVisuals();

    // Writes the HitFlash scalar on every predator material slot (no-op on the cube placeholder).
    void SetTelegraphFlash(float Value);

    UPROPERTY(VisibleAnywhere, Category="Components")
    TObjectPtr<UPSLStatsComponent> StatsComponent;

    UPROPERTY(VisibleAnywhere, Category="Visual")
    TObjectPtr<UStaticMeshComponent> BodyMesh;

    UPROPERTY(VisibleAnywhere, Category="Visual")
    TObjectPtr<UStaticMeshComponent> HeadMesh;

    // '?' amber while Investigate, red '!' while Chase/Attack, larger hot yellow-white '!' during the attack wind-up; hidden otherwise.
    UPROPERTY(VisibleAnywhere, Category="Visual")
    TObjectPtr<UTextRenderComponent> AlertText;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UMaterialInstanceDynamic>> PredatorMaterials;

    bool bUsingSkeletalVisual = false;
    bool bDeathHandled = false;
    bool bTelegraphFlashing = false;
};
