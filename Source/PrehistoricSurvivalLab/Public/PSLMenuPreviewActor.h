#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PSLMenuPreviewActor.generated.h"

class USkeletalMeshComponent;

/** Main-menu turntable: shows the selected species' mesh, plays its Idle clip through UPSLDinoAnimInstance. */
UCLASS()
class PREHISTORICSURVIVALLAB_API APSLMenuPreviewActor : public AActor
{
    GENERATED_BODY()

public:
    static constexpr float TurntableDegreesPerSecond = 12.0f;
    static constexpr float FitHeight = 300.0f;

    APSLMenuPreviewActor();

    // Swaps mesh and idle clip to the built-in species, and rescales the mesh to FitHeight uu tall.
    void SetSpecies(FName SpeciesId);

    USkeletalMeshComponent* GetMeshComponent() const { return MeshComponent; }
    FName GetSpeciesId() const { return CurrentSpecies; }

    virtual void Tick(float DeltaSeconds) override;

protected:
    virtual void BeginPlay() override;

private:
    UPROPERTY(VisibleAnywhere, Category="Preview")
    TObjectPtr<USkeletalMeshComponent> MeshComponent;

    FName CurrentSpecies;
};
