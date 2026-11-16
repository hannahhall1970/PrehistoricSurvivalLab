#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PSLMenuPreviewActor.generated.h"

class USkeletalMeshComponent;

/** Main-menu hero preview: shows the selected species' mesh, plays its Idle clip through UPSLDinoAnimInstance, and sways
 *  gently around the yaw it was placed with, so the framed 3/4 view never turns its back to the camera. */
UCLASS()
class PREHISTORICSURVIVALLAB_API APSLMenuPreviewActor : public AActor
{
    GENERATED_BODY()

public:
    static constexpr float SwayAmplitudeDegrees = 20.0f;
    static constexpr float SwayPeriodSeconds = 16.0f;
    static constexpr float FitHeight = 300.0f;

    APSLMenuPreviewActor();

    // Swaps mesh and idle clip to the built-in species, and rescales the mesh to FitHeight uu tall.
    void SetSpecies(FName SpeciesId);

    // Yaw offset (degrees) from the placed yaw after Seconds of play: Amplitude * sin(2*pi*Seconds/Period), within +-Amplitude.
    static float SwayYawOffset(float Seconds);

    USkeletalMeshComponent* GetMeshComponent() const { return MeshComponent; }
    FName GetSpeciesId() const { return CurrentSpecies; }

    virtual void Tick(float DeltaSeconds) override;

protected:
    virtual void BeginPlay() override;

private:
    UPROPERTY(VisibleAnywhere, Category="Preview")
    TObjectPtr<USkeletalMeshComponent> MeshComponent;

    FName CurrentSpecies;
    float BaseYaw = 0.0f;
    float Elapsed = 0.0f;
};
