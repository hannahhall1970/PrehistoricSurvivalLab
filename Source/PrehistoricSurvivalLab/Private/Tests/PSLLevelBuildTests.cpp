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

#endif
