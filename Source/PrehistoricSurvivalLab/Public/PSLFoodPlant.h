#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/TimerHandle.h"
#include "PSLFoodPlant.generated.h"

class UStaticMeshComponent;

/** Herbivore food: a bite overlapping it yields FoodAmount, then it hides until it regrows. The mesh is set by content. */
UCLASS()
class PREHISTORICSURVIVALLAB_API APSLFoodPlant : public AActor
{
    GENERATED_BODY()

public:
    APSLFoodPlant();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Food")
    TObjectPtr<UStaticMeshComponent> PlantMesh;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Food", meta=(ClampMin="0.0"))
    float FoodAmount;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Food", meta=(ClampMin="0.0"))
    float RegrowSeconds;

    UFUNCTION(BlueprintPure, Category="Food")
    bool IsAvailable() const { return bAvailable; }

    // Returns FoodAmount and hides the plant until regrow; 0 while it is still regrowing.
    UFUNCTION(BlueprintCallable, Category="Food")
    float Consume();

protected:
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    void Regrow();

    bool bAvailable;
    FTimerHandle RegrowTimer;
};
