#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Engine/TimerHandle.h"
#include "PSLCharacter.generated.h"

class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class USpringArmComponent;
class UPSLStatsComponent;
class UPSLSurvivalComponent;
struct FInputActionValue;
class UAnimInstance;
class UAnimSequence;
class USkeletalMesh;
class UPSLDinoSpecies;
class USoundBase;

// Fired when a resolved bite damaged at least one pawn. Visual feedback only (HUD hit marker).
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPSLBiteHitSignature);

// Fired when a bite ate Amount hunger worth of food (carcass meat or plant).
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSLAteFoodSignature, float, Amount);

// What hurt the player. Starvation drains through UPSLStatsComponent::ApplyDamage and never raises OnDamageTaken; the value is reserved for future injury/bleeding tints.
UENUM(BlueprintType)
enum class EPSLDamageKind : uint8
{
    Bite,
    Starvation,
    Unknown
};

// Fired once per APSLCharacter::TakeDamage that applied damage to a living player. NewHealth is the health after the hit.
// CauserLocation is only meaningful when bHasCauser (damage came with a live causer actor). Hit feedback (HUD) listens here, never to OnHealthChanged.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(FPSLDamageTakenSignature, float, Amount, float, NewHealth, EPSLDamageKind, Kind, FVector, CauserLocation, bool, bHasCauser);

UCLASS()
class PREHISTORICSURVIVALLAB_API APSLCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    APSLCharacter();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
    TObjectPtr<UPSLStatsComponent> StatsComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
    TObjectPtr<UPSLSurvivalComponent> SurvivalComponent;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Input")
    TObjectPtr<UInputMappingContext> DefaultMappingContext;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Input")
    TObjectPtr<UInputAction> MoveAction;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Input")
    TObjectPtr<UInputAction> LookAction;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Input")
    TObjectPtr<UInputAction> JumpAction;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Input")
    TObjectPtr<UInputAction> SprintAction;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Input")
    TObjectPtr<UInputAction> BiteAction;

    // Escape/P; fires while paused so the same key resumes.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Input")
    TObjectPtr<UInputAction> PauseAction;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement", meta=(ClampMin="0.0", UIMin="0.0"))
    float WalkSpeed;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement", meta=(ClampMin="0.0", UIMin="0.0"))
    float SprintSpeed;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat", meta=(ClampMin="0.0", UIMin="0.0"))
    float BiteCooldown;

    // Delay between pressing bite and the damage sphere, so it lines up with the Attack clip.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat", meta=(ClampMin="0.0", UIMin="0.0"))
    float BiteHitDelay;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat", meta=(ClampMin="0.0", UIMin="0.0"))
    float BiteReach;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat", meta=(ClampMin="0.0", UIMin="0.0"))
    float BiteRadius;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat", meta=(ClampMin="0.0", UIMin="0.0"))
    float BiteDamage;

    // Accepted bites so far.
    UPROPERTY(BlueprintReadOnly, Category="Combat")
    int32 BiteCount;

    UPROPERTY(BlueprintAssignable, Category="Combat")
    FPSLBiteHitSignature OnBiteHit;

    UPROPERTY(BlueprintAssignable, Category="Combat")
    FPSLDamageTakenSignature OnDamageTaken;

    // Pawns overlapped by the last resolved bite; -1 while a bite is pending (stays -1 if the player dies before it resolves).
    UPROPERTY(BlueprintReadOnly, Category="Combat")
    int32 LastBiteHitCount;

    // Biped capsule + mesh placement for the Quaternius T-Rex (mesh faces +Y in the FBX, so yaw -90).
    static constexpr float DinoCapsuleRadius = 60.0f;
    static constexpr float DinoCapsuleHalfHeight = 120.0f;
    static constexpr float DinoMeshYaw = -90.0f;

    // Soft references only: resolved in BeginPlay so a missing import leaves the capsule playable.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Dino")
    TSoftObjectPtr<USkeletalMesh> DinoMesh;

    // Native PSLDinoAnimInstance by default; an assigned AnimBP class wins.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Dino")
    TSoftClassPtr<UAnimInstance> DinoAnimClass;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Dino")
    TSoftObjectPtr<UAnimSequence> DinoIdleAnim;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Dino")
    TSoftObjectPtr<UAnimSequence> DinoWalkAnim;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Dino")
    TSoftObjectPtr<UAnimSequence> DinoRunAnim;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Dino")
    TSoftObjectPtr<UAnimSequence> DinoAttackAnim;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Dino")
    TSoftObjectPtr<UAnimSequence> DinoDeathAnim;

    UPROPERTY(BlueprintAssignable, Category="Hunger")
    FPSLAteFoodSignature OnAteFood;

    // Played on every bite that ate food; soft so a missing asset is skipped.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hunger")
    TSoftObjectPtr<USoundBase> EatSound;

    // Multiplier on the food a bite yields; set from the species.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hunger")
    float FoodPerBite;

    // Hunger given by one full bite before the FoodPerBite multiplier.
    static constexpr float BaseFoodPerBite = 25.0f;

    // Species applied by ApplySpecies; null while the constructor's T-Rex defaults are in effect.
    UFUNCTION(BlueprintPure, Category="Dino")
    const UPSLDinoSpecies* GetSpecies() const { return ActiveSpecies; }

    // Applies mesh, anims, capsule, speeds, bite and survival stats from Species. Null is ignored.
    UFUNCTION(BlueprintCallable, Category="Dino")
    void ApplySpecies(const UPSLDinoSpecies* Species);

    // Transient message from the last bite (food this dino cannot eat) if any, else the proximity "Bite to eat" prompt. The HUD shows it. Empty otherwise.
    const FText& GetFoodHint() const { return FoodHint.IsEmpty() ? ProximityHint : FoodHint; }

    UFUNCTION(BlueprintPure, Category="Components")
    UPSLStatsComponent* GetStatsComponent() const { return StatsComponent; }

    UFUNCTION(BlueprintPure, Category="Components")
    UPSLSurvivalComponent* GetSurvivalComponent() const { return SurvivalComponent; }

    // Pure gate for TryBite: never while dead, otherwise once Cooldown seconds have passed since LastBite.
    static bool CanBite(double Now, double LastBite, double Cooldown, bool bDead);

    // Returns true when the bite was accepted (animation started, damage scheduled after BiteHitDelay).
    UFUNCTION(BlueprintCallable, Category="Combat")
    bool TryBite();

    // Pure look-input transform used by Look(): scaled by Sensitivity, Y negated when bInvertY.
    static FVector2D ScaleLookInput(FVector2D Raw, float Sensitivity, bool bInvertY);

    // Development diagnostics only: log the jump apex height and landing airtime ("PSL jump ..." lines).
    virtual void NotifyJumpApex() override;
    virtual void Landed(const FHitResult& Hit) override;

protected:
    virtual void BeginPlay() override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual float TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

private:
    void Move(const FInputActionValue& Value);
    void Look(const FInputActionValue& Value);
    void StartJump(const FInputActionValue& Value);
    void StopJump(const FInputActionValue& Value);
    void StartSprint(const FInputActionValue& Value);
    void StopSprint(const FInputActionValue& Value);
    void StartBite(const FInputActionValue& Value);
    void StartPause(const FInputActionValue& Value);

    // Sphere overlap in front of the dino; damages each other pawn once.
    void ResolveBiteHit();

    // Loads the soft dino assets; warns and keeps the capsule if any are missing.
    void ApplyDinoVisuals();

    // Fills only the null input properties with code-built defaults; assigned assets win.
    void ApplyInputDefaults();

    UFUNCTION()
    void HandleSprintStateChanged(bool bIsSprinting);

    UFUNCTION()
    void HandleDeath();

    bool bDeathHandled;
    double LastBiteTime;
    FTimerHandle BiteHitTimer;
    double JumpStartZ = 0.0;
    double JumpStartTime = -1.0; // < 0: no diagnosed jump in flight

    // Carnivore bites eat one dead prey in the sphere; herbivore bites eat plants. Returns hunger gained.
    float TryEat(const FVector& Center);

    UPROPERTY(Transient)
    TObjectPtr<const UPSLDinoSpecies> ActiveSpecies;

    FText FoodHint;

    // Herbivore-only "Bite to eat" prompt, refreshed by ProximityHintTimer; transient FoodHint messages take priority in GetFoodHint.
    FText ProximityHint;
    FTimerHandle ProximityHintTimer;
    void UpdateProximityHint();
};
