#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimSequence.h"
#include "Engine/AssetManager.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"
#include "PSLDinoSpecies.h"

namespace
{
    // The species texts must reference the shared PSLSpecies string table (never a culture-invariant or package-rekeyed copy).
    void PSLCheckSpeciesText(FAutomationTestBase& Test, const FString& What, const FText& Asset, const FText& Builtin)
    {
        FName AssetTable, BuiltinTable;
        FString AssetKey, BuiltinKey;
        Test.TestTrue(What + TEXT(" is a string-table text"), FTextInspector::GetTableIdAndKey(Asset, AssetTable, AssetKey));
        FTextInspector::GetTableIdAndKey(Builtin, BuiltinTable, BuiltinKey);
        Test.TestEqual(What + TEXT(" table"), AssetTable.ToString(), UPSLDinoSpecies::TextTableId.ToString());
        Test.TestEqual(What + TEXT(" table = builtin"), AssetTable.ToString(), BuiltinTable.ToString());
        Test.TestEqual(What + TEXT(" key = builtin"), AssetKey, BuiltinKey);
        Test.TestFalse(What + TEXT(" is not culture-invariant"), Asset.IsCultureInvariant());
        Test.TestEqual(What + TEXT(" text = builtin"), Asset.ToString(), Builtin.ToString());
    }

    const UPSLDinoSpecies* PSLLoadSpeciesAsset(FAutomationTestBase& Test, FName Id)
    {
        const FString Name = TEXT("DA_Species_") + Id.ToString();
        const FString Path = FString::Printf(TEXT("/Game/Data/Species/%s.%s"), *Name, *Name);
        const UPSLDinoSpecies* Asset = LoadObject<UPSLDinoSpecies>(nullptr, *Path);
        if (!Test.TestNotNull(FString::Printf(TEXT("%s loads"), *Path), Asset))
        {
            return nullptr;
        }
        Test.TestEqual(Name + TEXT(" primary asset id"), Asset->GetPrimaryAssetId().ToString(), FPrimaryAssetId(UPSLDinoSpecies::AssetType, Id).ToString());
        if (Test.TestTrue(TEXT("Asset manager initialized"), UAssetManager::IsInitialized()))
        {
            Test.TestEqual(Name + TEXT(" is registered with the asset manager"),
                UAssetManager::Get().GetPrimaryAssetPath(Asset->GetPrimaryAssetId()).ToString(), Path);
        }
        return Asset;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLSpeciesAssetsTest, "PSL.Species.Assets", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLSpeciesAssetsTest::RunTest(const FString& Parameters)
{
    using FFloatField = float UPSLDinoSpecies::*;
    using FAnimField = TSoftObjectPtr<UAnimSequence> UPSLDinoSpecies::*;
    static const TPair<const TCHAR*, FFloatField> Floats[] = {
        {TEXT("CapsuleRadius"), &UPSLDinoSpecies::CapsuleRadius}, {TEXT("CapsuleHalfHeight"), &UPSLDinoSpecies::CapsuleHalfHeight},
        {TEXT("MeshYaw"), &UPSLDinoSpecies::MeshYaw}, {TEXT("MeshScale"), &UPSLDinoSpecies::MeshScale},
        {TEXT("MaxHealth"), &UPSLDinoSpecies::MaxHealth}, {TEXT("MaxStamina"), &UPSLDinoSpecies::MaxStamina},
        {TEXT("WalkSpeed"), &UPSLDinoSpecies::WalkSpeed}, {TEXT("SprintSpeed"), &UPSLDinoSpecies::SprintSpeed},
        {TEXT("BiteDamage"), &UPSLDinoSpecies::BiteDamage}, {TEXT("BiteRadius"), &UPSLDinoSpecies::BiteRadius},
        {TEXT("BiteCooldown"), &UPSLDinoSpecies::BiteCooldown}, {TEXT("MaxHunger"), &UPSLDinoSpecies::MaxHunger},
        {TEXT("HungerDrainPerSecond"), &UPSLDinoSpecies::HungerDrainPerSecond}, {TEXT("FoodPerBite"), &UPSLDinoSpecies::FoodPerBite}};
    static const TPair<const TCHAR*, FAnimField> Anims[] = {
        {TEXT("IdleAnim"), &UPSLDinoSpecies::IdleAnim}, {TEXT("WalkAnim"), &UPSLDinoSpecies::WalkAnim}, {TEXT("RunAnim"), &UPSLDinoSpecies::RunAnim},
        {TEXT("AttackAnim"), &UPSLDinoSpecies::AttackAnim}, {TEXT("DeathAnim"), &UPSLDinoSpecies::DeathAnim}};

    for (const FName Id : {UPSLDinoSpecies::TRexId, UPSLDinoSpecies::ParaId})
    {
        const UPSLDinoSpecies* Asset = PSLLoadSpeciesAsset(*this, Id);
        const UPSLDinoSpecies* Builtin = UPSLDinoSpecies::MakeBuiltin(Id, GetTransientPackage());
        if (!Asset || !TestNotNull(TEXT("Builtin species"), Builtin))
        {
            return false;
        }

        const FString N = Id.ToString() + TEXT(" ");
        TestEqual(N + TEXT("SpeciesId"), Asset->SpeciesId.ToString(), Id.ToString());
        TestTrue(N + TEXT("Diet"), Asset->Diet == Builtin->Diet);
        PSLCheckSpeciesText(*this, N + TEXT("DisplayName"), Asset->DisplayName, Builtin->DisplayName);
        PSLCheckSpeciesText(*this, N + TEXT("Description"), Asset->Description, Builtin->Description);
        for (const TPair<const TCHAR*, FFloatField>& F : Floats)
        {
            TestEqual(N + F.Key, Asset->*F.Value, Builtin->*F.Value);
        }
        TestEqual(N + TEXT("Mesh path"), Asset->Mesh.ToSoftObjectPath().ToString(), Builtin->Mesh.ToSoftObjectPath().ToString());
        TestNotNull(N + TEXT("Mesh loads"), Asset->Mesh.LoadSynchronous());
        for (const TPair<const TCHAR*, FAnimField>& A : Anims)
        {
            TestEqual(N + A.Key + TEXT(" path"), (Asset->*A.Value).ToSoftObjectPath().ToString(), (Builtin->*A.Value).ToSoftObjectPath().ToString());
            TestNotNull(N + A.Key + TEXT(" loads"), (Asset->*A.Value).LoadSynchronous());
        }
    }

    return true;
}

#endif
