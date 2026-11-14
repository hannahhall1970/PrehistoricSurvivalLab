#include "PSLMenuPreviewActor.h"

#include "Animation/AnimSequence.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "PSLDinoAnimInstance.h"
#include "PSLDinoSpecies.h"

APSLMenuPreviewActor::APSLMenuPreviewActor()
{
    PrimaryActorTick.bCanEverTick = true;
    USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(Root);
    MeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
    MeshComponent->SetupAttachment(Root);
    MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void APSLMenuPreviewActor::BeginPlay()
{
    Super::BeginPlay();
    if (CurrentSpecies.IsNone())
    {
        SetSpecies(UPSLDinoSpecies::TRexId);
    }
}

void APSLMenuPreviewActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    AddActorLocalRotation(FRotator(0.0f, TurntableDegreesPerSecond * DeltaSeconds, 0.0f));
}

void APSLMenuPreviewActor::SetSpecies(FName SpeciesId)
{
    const UPSLDinoSpecies* Species = UPSLDinoSpecies::MakeBuiltin(SpeciesId, this);
    if (!Species)
    {
        return;
    }
    CurrentSpecies = SpeciesId;

    USkeletalMesh* Mesh = Species->Mesh.LoadSynchronous();
    if (!Mesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("%s: species mesh %s missing; preview left empty."), *GetName(), *Species->Mesh.ToString());
        MeshComponent->SetSkeletalMeshAsset(nullptr);
        return;
    }

    MeshComponent->SetSkeletalMeshAsset(Mesh);
    MeshComponent->SetAnimationMode(EAnimationMode::AnimationBlueprint);
    MeshComponent->SetAnimInstanceClass(UPSLDinoAnimInstance::StaticClass());
    if (UPSLDinoAnimInstance* Anim = Cast<UPSLDinoAnimInstance>(MeshComponent->GetAnimInstance()))
    {
        // Only Idle: the preview never moves, so locomotion weights stay on Idle.
        Anim->IdleSequence = Species->IdleAnim.LoadSynchronous();
        Anim->WalkSequence = Species->WalkAnim.LoadSynchronous();
        Anim->RunSequence = Species->RunAnim.LoadSynchronous();
    }

    // Fit to FitHeight uu with the feet on the actor origin.
    const FBoxSphereBounds Bounds = Mesh->GetBounds();
    const float Scale = Bounds.BoxExtent.Z > KINDA_SMALL_NUMBER ? FitHeight / (Bounds.BoxExtent.Z * 2.0f) : 1.0f;
    const float FootZ = (Bounds.Origin.Z - Bounds.BoxExtent.Z) * Scale;
    MeshComponent->SetRelativeScale3D(FVector(Scale));
    MeshComponent->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, -FootZ), FRotator(0.0f, Species->MeshYaw, 0.0f));
}
