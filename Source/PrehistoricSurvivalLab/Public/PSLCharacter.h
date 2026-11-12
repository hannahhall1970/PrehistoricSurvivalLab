#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "PSLCharacter.generated.h"

class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class USpringArmComponent;
class UPSLStatsComponent;
class UPSLSurvivalComponent;
struct FInputActionValue;

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

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement", meta=(ClampMin="0.0", UIMin="0.0"))
    float WalkSpeed;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement", meta=(ClampMin="0.0", UIMin="0.0"))
    float SprintSpeed;

    UFUNCTION(BlueprintPure, Category="Components")
    UPSLStatsComponent* GetStatsComponent() const { return StatsComponent; }

    UFUNCTION(BlueprintPure, Category="Components")
    UPSLSurvivalComponent* GetSurvivalComponent() const { return SurvivalComponent; }

protected:
    virtual void BeginPlay() override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
    virtual float TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

private:
    void Move(const FInputActionValue& Value);
    void Look(const FInputActionValue& Value);
    void StartJump(const FInputActionValue& Value);
    void StopJump(const FInputActionValue& Value);
    void StartSprint(const FInputActionValue& Value);
    void StopSprint(const FInputActionValue& Value);

    // Fills only the null input properties with code-built defaults; assigned assets win.
    void ApplyInputDefaults();

    UFUNCTION()
    void HandleSprintStateChanged(bool bIsSprinting);

    UFUNCTION()
    void HandleDeath();

    bool bDeathHandled;
};
