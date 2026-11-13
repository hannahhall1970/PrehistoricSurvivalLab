#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "PSLDinoSpecies.h"
#include "PSLGameInstance.h"
#include "PSLSaveGame.h"
#include "PSLSaveSubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

namespace
{
    // The subsystem's Outer is a real GameInstance (as in game), so GetGameInstance() is valid and no ensure fires.
    // Both objects are strongly held for the test's duration. Slot is injected so a developer's save is never touched.
    struct FSaveFixture
    {
        TStrongObjectPtr<UPSLGameInstance> Instance;
        TStrongObjectPtr<UPSLSaveSubsystem> Subsystem;
        UPSLSaveSubsystem* operator->() const { return Subsystem.Get(); }
    };

    FSaveFixture MakeFixture(bool bWipeSlot = true)
    {
        FSaveFixture Fixture;
        Fixture.Instance = TStrongObjectPtr<UPSLGameInstance>(NewObject<UPSLGameInstance>(GetTransientPackage()));
        Fixture.Subsystem = TStrongObjectPtr<UPSLSaveSubsystem>(NewObject<UPSLSaveSubsystem>(Fixture.Instance.Get()));
        Fixture.Subsystem->SlotName = TEXT("PSL_Test");
        if (bWipeSlot)
        {
            Fixture.Subsystem->DeleteSave();
        }
        return Fixture;
    }

    FPSLSaveData MakeGoodData()
    {
        FPSLSaveData Data;
        Data.SpeciesId = UPSLDinoSpecies::ParaId;
        Data.MapName = FName(UPSLGameInstance::ValleyMap);
        Data.PlayerTransform = FTransform(FRotator(0.0f, 45.0f, 0.0f), FVector(1200.0, -800.0, 350.0));
        Data.ControlRotation = FRotator(-10.0f, 45.0f, 0.0f);
        Data.Health = 55.0f;
        Data.Stamina = 80.0f;
        Data.Hunger = 42.0f;
        Data.PreyKilled = 3;
        Data.FoodEaten = 150.0f;
        Data.SurvivalSeconds = 321.0f;
        return Data;
    }

    // Seals Data the way WriteData does, so Validate sees a consistent checksum.
    void Seal(FPSLSaveData& Data)
    {
        Data.SaveVersion = UPSLSaveGame::CurrentVersion;
        Data.SavedAtUtc = FDateTime(2026, 1, 1);
        Data.Checksum = UPSLSaveSubsystem::ComputeChecksum(Data);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLSaveRoundTripTest, "PSL.Save.RoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLSaveRoundTripTest::RunTest(const FString& Parameters)
{
    const FSaveFixture Saves = MakeFixture();
    TestFalse(TEXT("No save yet"), Saves->HasValidSave());

    FText Error;
    FPSLSaveData Loaded;
    TestFalse(TEXT("Loading a missing slot fails"), Saves->LoadAndValidate(Loaded, Error));
    TestFalse(TEXT("A reason is given"), Error.IsEmpty());

    const FPSLSaveData Original = MakeGoodData();
    TestTrue(TEXT("Write succeeds"), Saves->WriteData(Original));
    TestTrue(TEXT("Valid save exists"), Saves->HasValidSave());

    if (TestTrue(TEXT("Load succeeds"), Saves->LoadAndValidate(Loaded, Error)))
    {
        TestTrue(TEXT("Version"), Loaded.SaveVersion == UPSLSaveGame::CurrentVersion);
        TestTrue(TEXT("Species"), Original.SpeciesId == Loaded.SpeciesId);
        TestTrue(TEXT("Map"), Original.MapName == Loaded.MapName);
        TestTrue(TEXT("Location"), Loaded.PlayerTransform.GetLocation().Equals(Original.PlayerTransform.GetLocation(), 0.01));
        TestTrue(TEXT("Control rotation"), Loaded.ControlRotation.Equals(Original.ControlRotation, 0.01f));
        TestEqual(TEXT("Health"), Loaded.Health, Original.Health);
        TestEqual(TEXT("Stamina"), Loaded.Stamina, Original.Stamina);
        TestEqual(TEXT("Hunger"), Loaded.Hunger, Original.Hunger);
        TestEqual(TEXT("Prey killed"), Loaded.PreyKilled, Original.PreyKilled);
        TestEqual(TEXT("Food eaten"), Loaded.FoodEaten, Original.FoodEaten);
        TestEqual(TEXT("Survival seconds"), Loaded.SurvivalSeconds, Original.SurvivalSeconds);
        TestTrue(TEXT("Timestamp stamped"), Loaded.SavedAtUtc.GetTicks() > 0);
    }

    Saves->DeleteSave();
    TestFalse(TEXT("Deleted"), Saves->HasValidSave());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLSaveValidationTest, "PSL.Save.Validation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLSaveValidationTest::RunTest(const FString& Parameters)
{
    FText Error;

    FPSLSaveData Good = MakeGoodData();
    Seal(Good);
    FPSLSaveData Copy = Good;
    TestTrue(TEXT("A sealed good save validates"), UPSLSaveSubsystem::Validate(Copy, Error));

    // Each case mutates one field, re-seals (so only that field is wrong) and must be rejected.
    auto ExpectRejected = [&](const TCHAR* Label, TFunctionRef<void(FPSLSaveData&)> Mutate)
    {
        FPSLSaveData Data = MakeGoodData();
        Mutate(Data);
        Seal(Data);
        FText Reason;
        TestFalse(Label, UPSLSaveSubsystem::Validate(Data, Reason));
        TestFalse(*FString::Printf(TEXT("%s: reason given"), Label), Reason.IsEmpty());
    };

    ExpectRejected(TEXT("Unknown species"), [](FPSLSaveData& D) { D.SpeciesId = FName(TEXT("Raptor")); });
    ExpectRejected(TEXT("Wrong map"), [](FPSLSaveData& D) { D.MapName = FName(TEXT("L_TestArena")); });
    ExpectRejected(TEXT("NaN location"), [](FPSLSaveData& D) { D.PlayerTransform.SetLocation(FVector(NAN, 0.0, 0.0)); });
    ExpectRejected(TEXT("Location out of bounds"), [](FPSLSaveData& D) { D.PlayerTransform.SetLocation(FVector(5.0e6, 0.0, 0.0)); });
    ExpectRejected(TEXT("Zero health"), [](FPSLSaveData& D) { D.Health = 0.0f; });
    ExpectRejected(TEXT("Negative health"), [](FPSLSaveData& D) { D.Health = -5.0f; });
    ExpectRejected(TEXT("Health above species max"), [](FPSLSaveData& D) { D.Health = 9999.0f; });
    ExpectRejected(TEXT("NaN stamina"), [](FPSLSaveData& D) { D.Stamina = NAN; });

    // Versions are set after sealing so the checksum still matches and only the version is at fault.
    for (const int32 BadVersion : { 0, UPSLSaveGame::CurrentVersion + 1 })
    {
        FPSLSaveData Data = MakeGoodData();
        Data.SaveVersion = BadVersion;
        Data.SavedAtUtc = FDateTime(2026, 1, 1);
        Data.Checksum = UPSLSaveSubsystem::ComputeChecksum(Data);
        FText Reason;
        TestFalse(*FString::Printf(TEXT("Version %d rejected"), BadVersion), UPSLSaveSubsystem::Validate(Data, Reason));
        TestFalse(TEXT("Version reason given"), Reason.IsEmpty());
    }

    // Tampering after sealing breaks the checksum.
    FPSLSaveData Tampered = Good;
    Tampered.Health += 1.0f;
    TestFalse(TEXT("Tampered health fails the checksum"), UPSLSaveSubsystem::Validate(Tampered, Error));
    FPSLSaveData BadChecksum = Good;
    BadChecksum.Checksum ^= 1u;
    TestFalse(TEXT("Flipped checksum rejected"), UPSLSaveSubsystem::Validate(BadChecksum, Error));

    // Out-of-range stats are clamped, not rejected.
    FPSLSaveData Overfull = MakeGoodData();
    Overfull.Stamina = 5000.0f;
    Overfull.Hunger = -20.0f;
    Seal(Overfull);
    if (TestTrue(TEXT("Out-of-range stats still load"), UPSLSaveSubsystem::Validate(Overfull, Error)))
    {
        const UPSLDinoSpecies* Para = UPSLDinoSpecies::MakeBuiltin(UPSLDinoSpecies::ParaId, GetTransientPackage());
        TestEqual(TEXT("Stamina clamped to max"), Overfull.Stamina, Para->MaxStamina);
        TestEqual(TEXT("Hunger clamped to zero"), Overfull.Hunger, 0.0f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLSaveNoDeadOverwriteTest, "PSL.Save.NoDeadOverwrite", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLSaveNoDeadOverwriteTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("Healthy and fed may save"), UPSLSaveSubsystem::CanCheckpoint(false, 80.0f, 100.0f, 50.0f));
    TestTrue(TEXT("Starving at healthy HP may save"), UPSLSaveSubsystem::CanCheckpoint(false, 50.0f, 100.0f, 0.0f));
    TestTrue(TEXT("Low HP but fed may save"), UPSLSaveSubsystem::CanCheckpoint(false, 5.0f, 100.0f, 30.0f));
    TestFalse(TEXT("Dead never saves"), UPSLSaveSubsystem::CanCheckpoint(true, 80.0f, 100.0f, 50.0f));
    TestFalse(TEXT("Zero health never saves"), UPSLSaveSubsystem::CanCheckpoint(false, 0.0f, 100.0f, 50.0f));
    TestFalse(TEXT("Starving below 10% HP never saves"), UPSLSaveSubsystem::CanCheckpoint(false, 9.0f, 100.0f, 0.0f));

    // SaveCheckpoint with no live player must leave an existing good slot untouched.
    const FSaveFixture Saves = MakeFixture();
    const FPSLSaveData Original = MakeGoodData();
    if (!TestTrue(TEXT("Seed write"), Saves->WriteData(Original)))
    {
        return false;
    }
    FPSLSaveData Before;
    FText Error;
    if (!TestTrue(TEXT("Seed loads"), Saves->LoadAndValidate(Before, Error)))
    {
        return false;
    }

    TestFalse(TEXT("No player: checkpoint refused"), Saves->SaveCheckpoint(EPSLSaveReason::Autosave));

    FPSLSaveData After;
    if (TestTrue(TEXT("Slot still loads"), Saves->LoadAndValidate(After, Error)))
    {
        TestTrue(TEXT("Checksum unchanged"), After.Checksum == Before.Checksum);
        TestEqual(TEXT("Timestamp unchanged"), After.SavedAtUtc.GetTicks(), Before.SavedAtUtc.GetTicks());
        TestEqual(TEXT("Health unchanged"), After.Health, Original.Health);
    }
    Saves->DeleteSave();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLSaveDiskRoundTripTest, "PSL.Save.DiskRoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLSaveDiskRoundTripTest::RunTest(const FString& Parameters)
{
    const FPSLSaveData Original = MakeGoodData();
    {
        // Writer instance goes away; the reader below only sees what reached the disk.
        const FSaveFixture Writer = MakeFixture();
        if (!TestTrue(TEXT("Write succeeds"), Writer->WriteData(Original)))
        {
            return false;
        }
    }

    const FSaveFixture Reader = MakeFixture(false);
    FPSLSaveData Loaded;
    FText Error;
    if (TestTrue(TEXT("A fresh GameInstance loads the slot from disk"), Reader->LoadAndValidate(Loaded, Error)))
    {
        TestTrue(TEXT("Version"), Loaded.SaveVersion == UPSLSaveGame::CurrentVersion);
        TestTrue(TEXT("Species"), Original.SpeciesId == Loaded.SpeciesId);
        TestTrue(TEXT("Map"), Original.MapName == Loaded.MapName);
        TestTrue(TEXT("Transform"), Loaded.PlayerTransform.Equals(Original.PlayerTransform, 0.01));
        TestTrue(TEXT("Control rotation"), Loaded.ControlRotation.Equals(Original.ControlRotation, 0.01f));
        TestEqual(TEXT("Health"), Loaded.Health, Original.Health);
        TestEqual(TEXT("Stamina"), Loaded.Stamina, Original.Stamina);
        TestEqual(TEXT("Hunger"), Loaded.Hunger, Original.Hunger);
        TestEqual(TEXT("Prey killed"), Loaded.PreyKilled, Original.PreyKilled);
        TestEqual(TEXT("Food eaten"), Loaded.FoodEaten, Original.FoodEaten);
        TestEqual(TEXT("Survival seconds"), Loaded.SurvivalSeconds, Original.SurvivalSeconds);
        TestTrue(TEXT("Timestamp stamped"), Loaded.SavedAtUtc.GetTicks() > 0);
    }

    Reader->DeleteSave();
    TestFalse(TEXT("Deleted"), Reader->HasValidSave());
    return true;
}

#endif
