#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Landscape.h"
#include "LandscapeEdit.h"
#include "LandscapeInfo.h"
#include "LandscapeComponent.h"
#include "LandscapeDataAccess.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Materials/MaterialInstance.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Level.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/SkyLight.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionPixelDepth.h"
#include "PSLFoodPlant.h"
#include "PSLLevelBuildLibrary.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLLandscapeBuildTest, "PSL.Editor.LandscapeBuild", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLLandscapeBuildTest::RunTest(const FString& Parameters)
{
    constexpr int32 SizeVerts = 127; // 2 x 2 components of 63 quads
    constexpr float RampPerVertZ = 1.0f; // local z rises by 1 uu per X vertex

    UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, TEXT("PSLLandscapeBuildWorld"), GetTransientPackage());
    if (!TestNotNull(TEXT("Transient world"), World))
    {
        return false;
    }
    FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Editor);
    WorldContext.SetCurrentWorld(World);

    TArray<int32> Heights;
    Heights.SetNum(SizeVerts * SizeVerts);
    for (int32 Y = 0; Y < SizeVerts; ++Y)
    {
        for (int32 X = 0; X < SizeVerts; ++X)
        {
            Heights[Y * SizeVerts + X] = LandscapeDataAccess::GetTexHeight(X * RampPerVertZ);
        }
    }

    // A null load fails the instance check below (and is reported), without leaking the world context.
    UMaterialInterface* Terrain = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Environment/Materials/M_PSL_Terrain.M_PSL_Terrain"));
    TestNotNull(TEXT("M_PSL_Terrain loads"), Terrain);
    ALandscape* Landscape = UPSLLevelBuildLibrary::CreateLandscape(World, FVector(0.0f, 0.0f, 500.0f), FVector(100.0f, 100.0f, 1.0f), SizeVerts, Heights, Terrain);
    if (TestNotNull(TEXT("Landscape created"), Landscape))
    {
        TArray<ULandscapeComponent*> Components;
        Landscape->GetComponents(Components);
        TestEqual(TEXT("Landscape component count"), Components.Num(), 4);

        // The saved component instances are what the cooked game draws; they must be built from the passed material,
        // not from the default WorldGridMaterial (the editor's load-time fixup that hid this does not run in the cook).
        for (const ULandscapeComponent* Component : Components)
        {
            TestTrue(TEXT("Component has material instances"), Component->GetMaterialInstanceCount(false) > 0);
            for (int32 Index = 0; Index < Component->GetMaterialInstanceCount(false); ++Index)
            {
                const UMaterialInstance* Instance = Component->GetMaterialInstance(Index, false);
                TestTrue(TEXT("Component material instance uses M_PSL_Terrain"), Terrain && Instance && Instance->GetMaterial() == Terrain->GetMaterial());
            }
        }

        // GetHeightAtLocation samples the collision heightfield, so verify the imported data directly.
        // Vertex X=60: local z = 60, scale z = 1 -> stored texel must be GetTexHeight(60).
        TestEqual(TEXT("Landscape world Z"), Landscape->GetActorLocation().Z, 500.0);
        TestEqual(TEXT("Landscape scale Z"), Landscape->GetActorScale3D().Z, 1.0);
        TestEqual(TEXT("Landscape scale X"), Landscape->GetActorScale3D().X, 100.0);

        ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
        if (TestNotNull(TEXT("Landscape info"), Info))
        {
            TArray<uint16> Data;
            Data.SetNumZeroed(SizeVerts * SizeVerts);
            FLandscapeEditDataInterface EditInterface(Info);
            EditInterface.GetHeightDataFast(0, 0, SizeVerts - 1, SizeVerts - 1, Data.GetData(), 0);
            TestEqual(TEXT("Stored height at X=60,Y=30"), (int32)Data[30 * SizeVerts + 60], (int32)LandscapeDataAccess::GetTexHeight(60.0f * RampPerVertZ));
            TestEqual(TEXT("Stored height at X=0,Y=0"), (int32)Data[0], (int32)LandscapeDataAccess::GetTexHeight(0.0f));
        }
    }

    // Invalid inputs fail loudly (nullptr), they do not create a landscape.
    AddExpectedError(TEXT("PSL CreateLandscape"), EAutomationExpectedErrorFlags::Contains, 2);
    TestNull(TEXT("Bad SizeVerts rejected"), UPSLLevelBuildLibrary::CreateLandscape(World, FVector::ZeroVector, FVector::OneVector, 100, Heights, nullptr));
    TestNull(TEXT("Bad height count rejected"), UPSLLevelBuildLibrary::CreateLandscape(World, FVector::ZeroVector, FVector::OneVector, SizeVerts, TArray<int32>(), nullptr));

    // HISM.
    AActor* Holder = World->SpawnActor<AActor>();
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (TestNotNull(TEXT("Holder actor"), Holder) && TestNotNull(TEXT("Cube mesh"), Cube))
    {
        USceneComponent* Root = NewObject<USceneComponent>(Holder, TEXT("Root"));
        Holder->SetRootComponent(Root);
        Root->RegisterComponent();

        TArray<FTransform> Transforms;
        for (int32 I = 0; I < 5; ++I)
        {
            Transforms.Add(FTransform(FVector(I * 200.0f, 0.0f, 0.0f)));
        }
        UHierarchicalInstancedStaticMeshComponent* Hism = UPSLLevelBuildLibrary::AddMeshInstances(Holder, Cube, Transforms, false, 5000.0f);
        if (TestNotNull(TEXT("HISM created"), Hism))
        {
            TestEqual(TEXT("HISM instance count"), Hism->GetInstanceCount(), 5);
        }

        AddExpectedError(TEXT("PSL AddMeshInstances"), EAutomationExpectedErrorFlags::Contains, 1);
        TestNull(TEXT("Empty transforms rejected"), UPSLLevelBuildLibrary::AddMeshInstances(Holder, Cube, TArray<FTransform>(), false, 0.0f));
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLFoliageMaterialUsageTest, "PSL.Editor.FoliageMaterialUsage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// HISM foliage (build_lost_valley.py / create_main_menu.py) uses these materials. Without the saved
// InstancedStaticMeshes usage the cooked game logs "missing usage flag" and draws the default material.
bool FPSLFoliageMaterialUsageTest::RunTest(const FString& Parameters)
{
    static const TCHAR* Names[] = {TEXT("M_PSL_Flat"), TEXT("M_PSL_Leaf"), TEXT("MI_PSL_Rock"), TEXT("MI_PSL_Grass"),
        TEXT("MI_PSL_Bark"), TEXT("MI_PSL_Flower"), TEXT("MI_PSL_Leaf_Leaf_Pine_C")};
    for (const TCHAR* Name : Names)
    {
        const FString Path = FString::Printf(TEXT("/Game/Environment/Materials/%s.%s"), Name, Name);
        const UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *Path);
        if (!TestNotNull(Path + TEXT(" loads"), Material))
        {
            continue;
        }
        TestTrue(Path + TEXT(" is used with instanced static meshes"), Material->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes));
        // An editor-side auto-fix sets the flag in memory and dirties the package; only a saved flag reaches the cook.
        TestFalse(Path + TEXT(" package is not dirty (flag comes from disk)"), Material->GetPackage()->IsDirty());
    }
    return true;
}

namespace
{
constexpr const TCHAR* ValleyMatDir = TEXT("/Game/Environment/Materials/");

template <typename T>
T* LoadValleyAsset(const TCHAR* Name)
{
    return LoadObject<T>(nullptr, *FString::Printf(TEXT("%s%s.%s"), ValleyMatDir, Name, Name));
}

bool HasScalarParam(const UMaterialInterface& Material, const TCHAR* Name, float* OutDefault = nullptr)
{
    float Value = 0.0f;
    const bool bFound = Material.GetScalarParameterDefaultValue(FHashedMaterialParameterInfo(FName(Name)), Value);
    if (OutDefault)
    {
        *OutDefault = Value;
    }
    return bFound;
}

// Depth-first walk over every input of the expression graph feeding `Expression`.
bool GraphContainsPixelDepth(UMaterialExpression* Expression, TSet<UMaterialExpression*>& Visited)
{
    if (!Expression || Visited.Contains(Expression))
    {
        return false;
    }
    Visited.Add(Expression);
    if (Expression->IsA<UMaterialExpressionPixelDepth>())
    {
        return true;
    }
    for (FExpressionInputIterator It{Expression}; It; ++It)
    {
        if (GraphContainsPixelDepth(It->Expression, Visited))
        {
            return true;
        }
    }
    return false;
}

UWorld* LoadValleyWorld()
{
    UPackage* Package = LoadPackage(nullptr, TEXT("/Game/Maps/L_LostValley"), LOAD_None);
    return Package ? UWorld::FindWorldInPackage(Package) : nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLLostValleyFoliageReadabilityTest, "PSL.Editor.LostValleyFoliageReadability", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// Camera-fading foliage materials, plain bark/rock, capped decor bushes, and the edible-plant cue (build/polish_lost_valley.py).
bool FPSLLostValleyFoliageReadabilityTest::RunTest(const FString& Parameters)
{
    for (const TCHAR* Name : {TEXT("M_PSL_Leaf"), TEXT("M_PSL_FoliageFlat")})
    {
        const UMaterial* Material = LoadValleyAsset<UMaterial>(Name);
        if (!TestNotNull(FString(Name) + TEXT(" loads"), Material))
        {
            continue;
        }
        TestTrue(FString(Name) + TEXT(" blend mode is Masked"), Material->GetBlendMode() == BLEND_Masked);
        TestTrue(FString(Name) + TEXT(" dither opacity mask"), Material->DitherOpacityMask != 0);
        TestTrue(FString(Name) + TEXT(" two sided"), Material->IsTwoSided());
        TestTrue(FString(Name) + TEXT(" has CameraFadeStart"), HasScalarParam(*Material, TEXT("CameraFadeStart")));
        TestTrue(FString(Name) + TEXT(" has CameraFadeRange"), HasScalarParam(*Material, TEXT("CameraFadeRange")));
        const UMaterialEditorOnlyData* Data = Material->GetEditorOnlyData();
        TSet<UMaterialExpression*> Visited;
        TestTrue(FString(Name) + TEXT(" OpacityMask depends on PixelDepth"), Data && GraphContainsPixelDepth(Data->OpacityMask.Expression, Visited));
    }

    const UMaterial* FoliageFlat = LoadValleyAsset<UMaterial>(TEXT("M_PSL_FoliageFlat"));
    for (const TCHAR* Name : {TEXT("MI_PSL_Grass"), TEXT("MI_PSL_Flower")})
    {
        const UMaterialInstance* Instance = LoadValleyAsset<UMaterialInstance>(Name);
        if (TestNotNull(FString(Name) + TEXT(" loads"), Instance))
        {
            TestTrue(FString(Name) + TEXT(" parent is M_PSL_FoliageFlat"), FoliageFlat && Instance->Parent == FoliageFlat);
        }
    }
    // Solid scenery must not dither away near the camera.
    for (const TCHAR* Name : {TEXT("MI_PSL_Bark"), TEXT("MI_PSL_Rock")})
    {
        const UMaterialInstance* Instance = LoadValleyAsset<UMaterialInstance>(Name);
        if (TestNotNull(FString(Name) + TEXT(" loads"), Instance))
        {
            TestFalse(FString(Name) + TEXT(" has no CameraFadeStart"), HasScalarParam(*Instance, TEXT("CameraFadeStart")));
            TestTrue(FString(Name) + TEXT(" is not masked"), Instance->GetBlendMode() != BLEND_Masked);
        }
    }
    TestNotNull(TEXT("MI_PSL_FoodFlower loads"), LoadValleyAsset<UMaterialInstance>(TEXT("MI_PSL_FoodFlower")));

    UWorld* World = LoadValleyWorld();
    if (!TestNotNull(TEXT("L_LostValley world loads"), World))
    {
        return false;
    }
    int32 FoodPlants = 0;
    for (AActor* Actor : World->PersistentLevel->Actors)
    {
        if (!Actor)
        {
            continue;
        }
        const FString Label = Actor->GetActorLabel();
        if (Label == TEXT("PSL_Foliage_Bushes") || Label == TEXT("PSL_Foliage_Grass"))
        {
            TArray<UHierarchicalInstancedStaticMeshComponent*> Holders;
            Actor->GetComponents(Holders);
            TestTrue(Label + TEXT(" has HISM components"), Holders.Num() > 0);
            for (const UHierarchicalInstancedStaticMeshComponent* Holder : Holders)
            {
                TestTrue(Label + TEXT(" ignores the camera trace"), Holder->GetCollisionResponseToChannel(ECC_Camera) == ECR_Ignore);
                const UStaticMesh* Mesh = Holder->GetStaticMesh();
                if (Label != TEXT("PSL_Foliage_Bushes") || !TestNotNull(TEXT("Bush HISM mesh"), Mesh))
                {
                    continue;
                }
                TestNotEqual(TEXT("Decor bushes never use the food-plant mesh"), Mesh->GetName(), FString(TEXT("SM_Bush_Common_Flowers")));
                for (int32 I = 0; I < Holder->GetInstanceCount(); ++I)
                {
                    FTransform Transform;
                    Holder->GetInstanceTransform(I, Transform, false);
                    const FVector Scale = Transform.GetScale3D();
                    if (Scale.GetMax() > 1.05 + 1.0e-3)
                    {
                        AddError(FString::Printf(TEXT("Decor bush instance %d scale %.3f > 1.05"), I, Scale.GetMax()));
                        break;
                    }
                }
            }
        }
        else if (const APSLFoodPlant* Plant = Cast<APSLFoodPlant>(Actor))
        {
            ++FoodPlants;
            const UStaticMeshComponent* Mesh = Plant->PlantMesh;
            if (!TestNotNull(Label + TEXT(" plant mesh component"), Mesh) || !TestNotNull(Label + TEXT(" plant static mesh"), Mesh->GetStaticMesh().Get()))
            {
                continue;
            }
            TestEqual(Label + TEXT(" plant mesh asset"), Mesh->GetStaticMesh()->GetName(), FString(TEXT("SM_Bush_Common_Flowers")));
            bool bFlowerSlot = false;
            for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
            {
                const UMaterialInterface* Material = Mesh->GetMaterial(Slot);
                bFlowerSlot |= Material && Material->GetName() == TEXT("MI_PSL_FoodFlower");
            }
            TestTrue(Label + TEXT(" has MI_PSL_FoodFlower on a slot"), bFlowerSlot);
        }
    }
    TestTrue(FString::Printf(TEXT("Valley has food plants (found %d)"), FoodPlants), FoodPlants >= 6);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLLostValleyAtmosphereTest, "PSL.Editor.LostValleyAtmosphere", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// One volumetric cloud layer, a sky light that bounces ground colour, and a terrain with macro variation.
bool FPSLLostValleyAtmosphereTest::RunTest(const FString& Parameters)
{
    UWorld* World = LoadValleyWorld();
    if (!TestNotNull(TEXT("L_LostValley world loads"), World))
    {
        return false;
    }
    int32 Clouds = 0;
    int32 SkyLights = 0;
    for (AActor* Actor : World->PersistentLevel->Actors)
    {
        if (const AVolumetricCloud* Cloud = Cast<AVolumetricCloud>(Actor))
        {
            ++Clouds;
            const UVolumetricCloudComponent* Component = Cloud->FindComponentByClass<UVolumetricCloudComponent>();
            if (TestNotNull(TEXT("Cloud component"), Component))
            {
                TestFalse(TEXT("Cloud material is set"), Component->Material.IsNull());
            }
        }
        else if (const ASkyLight* Sky = Cast<ASkyLight>(Actor))
        {
            ++SkyLights;
            const USkyLightComponent* Light = Sky->GetLightComponent();
            if (TestNotNull(TEXT("SkyLight component"), Light))
            {
                TestFalse(TEXT("Lower hemisphere is not solid black"), Light->bLowerHemisphereIsBlack);
            }
        }
    }
    TestEqual(TEXT("Exactly one AVolumetricCloud"), Clouds, 1);
    TestEqual(TEXT("Exactly one ASkyLight"), SkyLights, 1);

    const UMaterial* Terrain = LoadValleyAsset<UMaterial>(TEXT("M_PSL_Terrain"));
    if (TestNotNull(TEXT("M_PSL_Terrain loads"), Terrain))
    {
        float Macro = 0.0f;
        if (TestTrue(TEXT("M_PSL_Terrain has MacroVariation"), HasScalarParam(*Terrain, TEXT("MacroVariation"), &Macro)))
        {
            TestTrue(FString::Printf(TEXT("MacroVariation default > 0 (%.3f)"), Macro), Macro > 0.0f);
        }
    }
    return true;
}

#endif
