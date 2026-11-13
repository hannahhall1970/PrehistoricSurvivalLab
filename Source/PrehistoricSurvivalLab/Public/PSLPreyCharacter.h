#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Engine/TimerHandle.h"
#include "PSLPreyCharacter.generated.h"

class UPSLStatsComponent;
class UStaticMeshComponent;
class USkeletalMesh;
class UAnimSequence;
class UTextRenderComponent;
class UMaterialInstanceDynamic;
class APSLPreyCharacter;

// Fired once when any prey dies; Killer is the actor that dealt the killing damage (may be null).
DECLARE_MULTICAST_DELEGATE_TwoParams(FPSLPreyKilledSignature, APSLPreyCharacter*, AActor*);

// Fired when a dead prey is removed from the world (eaten up); parameterless on purpose, the actor is going away.
DECLARE_MULTICAST_DELEGATE(FPSLCarcassDestroyedSignature);

/** Small prey animal: wanders, flees from the player dino, dies through UPSLStatsComponent. */
UCLASS()
class PREHISTORICSURVIVALLAB_API APSLPreyCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    APSLPreyCharacter();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Prey")
    float WanderSpeed;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Prey")
    float FleeSpeed;

    /** Seconds the prey is stunned after a hit. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Prey")
    float StaggerDuration;

    UPROPERTY(BlueprintReadOnly, Category="Prey")
    int32 HitCount;

    UFUNCTION(BlueprintPure, Category="Prey")
    UPSLStatsComponent* GetStatsComponent() const { return StatsComponent; }

    UFUNCTION(BlueprintPure, Category="Prey")
    bool IsStaggered() const { return bStaggered; }

    // Meat left on the carcass; only a dead prey can be eaten.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Prey|Food", meta=(ClampMin="0.0"))
    float MeatRemaining = 60.0f;

    // Seconds a fully eaten carcass lingers before it is destroyed.
    static constexpr float CarcassDestroyDelay = 2.0f;

    // Removes up to Amount meat from a dead prey and returns what was taken (0 while alive).
    // Destroys the carcass CarcassDestroyDelay seconds after the last meat is gone.
    float EatMeat(float Amount);

    bool IsDead() const { return bDeathHandled; }

    static FPSLPreyKilledSignature& OnAnyPreyKilled();

    // Fired when a dead prey (a carcass) is destroyed; the GameMode uses it to schedule a replacement.
    static FPSLCarcassDestroyedSignature& OnAnyCarcassDestroyed();

    virtual float TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaSeconds) override;

public:
    // Soft references only: a missing import keeps the cube placeholder (L_TestArena tests rely on that).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Prey|Visual")
    TSoftObjectPtr<USkeletalMesh> PreyMesh;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Prey|Visual")
    TSoftObjectPtr<UAnimSequence> PreyIdleAnim;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Prey|Visual")
    TSoftObjectPtr<UAnimSequence> PreyWalkAnim;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Prey|Visual")
    TSoftObjectPtr<UAnimSequence> PreyRunAnim;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Prey|Visual")
    TSoftObjectPtr<UAnimSequence> PreyAttackAnim;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Prey|Visual")
    TSoftObjectPtr<UAnimSequence> PreyDeathAnim;

private:
    UFUNCTION()
    void HandleDeath();

    void EndStagger();

    UPROPERTY(VisibleAnywhere, Category="Components")
    TObjectPtr<UPSLStatsComponent> StatsComponent;

    UPROPERTY(VisibleAnywhere, Category="Visual")
    TObjectPtr<UStaticMeshComponent> BodyMesh;

    UPROPERTY(VisibleAnywhere, Category="Visual")
    TObjectPtr<UStaticMeshComponent> HeadMesh;

    // Applies the soft-referenced skeletal mesh + clips; keeps the cube placeholder if any asset is missing.
    void ApplyPreyVisuals();

    bool bUsingSkeletalVisual;

    FTimerHandle StaggerTimer;
    bool bStaggered;
    bool bDeathHandled;

    // Visual only: "!" above the head while fleeing, HitFlash/Dead scalars on the per-slot dynamic materials.
    void SetMaterialScalar(FName Param, float Value);

    UPROPERTY(VisibleAnywhere, Category="Visual")
    TObjectPtr<UTextRenderComponent> AlertText;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UMaterialInstanceDynamic>> PreyMaterials;

    TWeakObjectPtr<AActor> LastDamager;
    float HitFlash;
};
