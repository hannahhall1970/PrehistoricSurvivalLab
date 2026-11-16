#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"
#include "PSLDinoSpecies.h"
#include "PSLGameInstance.h"
#include "PSLSaveGame.h"
#include "PSLSaveSubsystem.h"
#include "PSLSurvivalComponent.h"
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
        UPSLSaveSubsystem& operator*() const { return *Subsystem.Get(); }
    };

    FSaveFixture MakeFixture(bool bWipeSlot = true, const TCHAR* BaseSlot = TEXT("PSL_Test"))
    {
        FSaveFixture Fixture;
        Fixture.Instance = TStrongObjectPtr<UPSLGameInstance>(NewObject<UPSLGameInstance>(GetTransientPackage()));
        Fixture.Subsystem = TStrongObjectPtr<UPSLSaveSubsystem>(NewObject<UPSLSaveSubsystem>(Fixture.Instance.Get()));
        Fixture.Subsystem->SlotName = BaseSlot;
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

namespace
{
    constexpr const TCHAR* PairSlot = TEXT("PSL_Test_Pair");

    // PreyKilled is the marker that tells the saves apart.
    FPSLSaveData MakeMarked(int32 Marker)
    {
        FPSLSaveData Data = MakeGoodData();
        Data.PreyKilled = Marker;
        return Data;
    }

    // Marker stored in one slot, or -1 when the slot is missing or does not validate.
    int32 MarkerIn(const UPSLSaveSubsystem& Saves, bool bAuto, int32 Index)
    {
        const UPSLSaveGame* Save = Cast<UPSLSaveGame>(UGameplayStatics::LoadGameFromSlot(Saves.GetSlotName(bAuto, Index), 0));
        if (!Save)
        {
            return -1;
        }
        FPSLSaveData Data = Save->Data;
        FText Error;
        return UPSLSaveSubsystem::Validate(Data, Error) ? Data.PreyKilled : -1;
    }

    int32 NewestMarker(const UPSLSaveSubsystem& Saves)
    {
        FPSLSaveData Data;
        FText Error;
        return Saves.LoadAndValidate(Data, Error) ? Data.PreyKilled : -1;
    }

    // Bit rot: a field changes on disk but the stored checksum does not, so Validate must reject the member.
    // (Raw junk bytes are not used: the engine header reader then allocates and logs on garbage lengths.)
    bool CorruptSlot(const UPSLSaveSubsystem& Saves, bool bAuto, int32 Index)
    {
        const FString Slot = Saves.GetSlotName(bAuto, Index);
        UPSLSaveGame* Save = Cast<UPSLSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
        if (!Save)
        {
            return false;
        }
        Save->Data.Health -= 1.0f;
        return UGameplayStatics::SaveGameToSlot(Save, Slot, 0);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLSavePairsIndependentTest, "PSL.Save.PairsIndependent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLSavePairsIndependentTest::RunTest(const FString& Parameters)
{
    const FSaveFixture Saves = MakeFixture(true, PairSlot);
    ON_SCOPE_EXIT { Saves->DeleteSave(); };

    TestEqual(TEXT("Slot names: manual"), Saves->GetSlotName(false, 0), FString(TEXT("PSL_Test_Pair")));
    TestEqual(TEXT("Slot names: manual B"), Saves->GetSlotName(false, 1), FString(TEXT("PSL_Test_Pair_B")));
    TestEqual(TEXT("Slot names: auto 0"), Saves->GetSlotName(true, 0), FString(TEXT("PSL_Test_Pair_Auto0")));
    TestEqual(TEXT("Slot names: auto 1"), Saves->GetSlotName(true, 1), FString(TEXT("PSL_Test_Pair_Auto1")));

    TestTrue(TEXT("Autosave write"), Saves->WriteData(MakeMarked(10), EPSLSaveReason::Autosave));
    TestEqual(TEXT("Autosave is the only save"), NewestMarker(*Saves), 10);
    TestTrue(TEXT("Manual write"), Saves->WriteData(MakeMarked(20), EPSLSaveReason::Pause));

    TestEqual(TEXT("Load returns the newest (manual)"), NewestMarker(*Saves), 20);
    TestEqual(TEXT("Manual pair holds the manual save"), MarkerIn(*Saves, false, 0), 20);
    TestEqual(TEXT("Auto pair still holds the autosave"), MarkerIn(*Saves, true, 0), 10);
    TestEqual(TEXT("Autosave did not touch the manual B member"), MarkerIn(*Saves, false, 1), -1);
    TestEqual(TEXT("Manual did not touch the auto 1 member"), MarkerIn(*Saves, true, 1), -1);

    // A newer autosave becomes the Continue target without destroying the manual save.
    TestTrue(TEXT("Second autosave write"), Saves->WriteData(MakeMarked(30), EPSLSaveReason::Autosave));
    TestEqual(TEXT("Load returns the newest (autosave)"), NewestMarker(*Saves), 30);
    TestEqual(TEXT("Manual save survives the autosave"), MarkerIn(*Saves, false, 0), 20);

    // SaveAndExit uses the manual pair as well.
    TestTrue(TEXT("SaveAndExit write"), Saves->WriteData(MakeMarked(40), EPSLSaveReason::SaveAndExit));
    TestEqual(TEXT("SaveAndExit is newest"), NewestMarker(*Saves), 40);
    TestEqual(TEXT("SaveAndExit went to the other manual member"), MarkerIn(*Saves, false, 1), 40);
    TestEqual(TEXT("The previous manual save is kept"), MarkerIn(*Saves, false, 0), 20);

    Saves->DeleteSave();
    for (const bool bAuto : { false, true })
    {
        for (int32 Index = 0; Index < 2; ++Index)
        {
            TestFalse(*FString::Printf(TEXT("DeleteSave removed auto=%d member %d"), bAuto, Index), UGameplayStatics::DoesSaveGameExist(Saves->GetSlotName(bAuto, Index), 0));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLSavePingPongTest, "PSL.Save.PingPong", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLSavePingPongTest::RunTest(const FString& Parameters)
{
    const FSaveFixture Saves = MakeFixture(true, PairSlot);
    ON_SCOPE_EXIT { Saves->DeleteSave(); };

    TestTrue(TEXT("Autosave 1"), Saves->WriteData(MakeMarked(1), EPSLSaveReason::Autosave));
    TestTrue(TEXT("Autosave 2"), Saves->WriteData(MakeMarked(2), EPSLSaveReason::Autosave));
    TestEqual(TEXT("Two valid auto members: first"), MarkerIn(*Saves, true, 0), 1);
    TestEqual(TEXT("Two valid auto members: second"), MarkerIn(*Saves, true, 1), 2);

    TestTrue(TEXT("Autosave 3"), Saves->WriteData(MakeMarked(3), EPSLSaveReason::Autosave));
    TestEqual(TEXT("Third overwrote the oldest member"), MarkerIn(*Saves, true, 0), 3);
    TestEqual(TEXT("Newest previous member kept"), MarkerIn(*Saves, true, 1), 2);
    TestEqual(TEXT("Load returns the third"), NewestMarker(*Saves), 3);

    TestTrue(TEXT("Autosave 4"), Saves->WriteData(MakeMarked(4), EPSLSaveReason::Autosave));
    TestEqual(TEXT("Fourth overwrote the oldest member"), MarkerIn(*Saves, true, 1), 4);
    TestEqual(TEXT("Third kept"), MarkerIn(*Saves, true, 0), 3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLSaveCorruptFallbackTest, "PSL.Save.CorruptFallback", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLSaveCorruptFallbackTest::RunTest(const FString& Parameters)
{
    const FSaveFixture Saves = MakeFixture(true, PairSlot);
    ON_SCOPE_EXIT { Saves->DeleteSave(); };

    TestTrue(TEXT("Autosave 1"), Saves->WriteData(MakeMarked(1), EPSLSaveReason::Autosave));
    TestTrue(TEXT("Autosave 2"), Saves->WriteData(MakeMarked(2), EPSLSaveReason::Autosave));
    TestEqual(TEXT("Newest is the second before corruption"), NewestMarker(*Saves), 2);

    // Corrupt the newest member on disk.
    if (!TestTrue(TEXT("Newest member corrupted"), CorruptSlot(*Saves, true, 1)))
    {
        return false;
    }
    TestEqual(TEXT("Corrupt member no longer validates"), MarkerIn(*Saves, true, 1), -1);

    FPSLSaveData Loaded;
    FText Error;
    if (TestTrue(TEXT("Load falls back to the older valid member"), Saves->LoadAndValidate(Loaded, Error)))
    {
        TestEqual(TEXT("Fallback is the first autosave"), Loaded.PreyKilled, 1);
    }
    TestTrue(TEXT("HasValidSave still true"), Saves->HasValidSave());

    // The next autosave replaces the corrupt member, not the good one.
    TestTrue(TEXT("Autosave 3"), Saves->WriteData(MakeMarked(3), EPSLSaveReason::Autosave));
    TestEqual(TEXT("Corrupt member replaced"), MarkerIn(*Saves, true, 1), 3);
    TestEqual(TEXT("Good member untouched"), MarkerIn(*Saves, true, 0), 1);
    TestEqual(TEXT("Load returns the newest again"), NewestMarker(*Saves), 3);

    // Every member corrupt: Continue must refuse with a reason.
    for (int32 Index = 0; Index < 2; ++Index)
    {
        TestTrue(*FString::Printf(TEXT("Member %d corrupted"), Index), CorruptSlot(*Saves, true, Index));
    }
    TestFalse(TEXT("All corrupt: no valid save"), Saves->LoadAndValidate(Loaded, Error));
    TestFalse(TEXT("All corrupt: a reason is given"), Error.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLSaveSecondsToLiveTest, "PSL.Save.SecondsToLive", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLSaveSecondsToLiveTest::RunTest(const FString& Parameters)
{
    // Values come from the species and the component defaults, not literals.
    const UPSLDinoSpecies* TRex = UPSLDinoSpecies::MakeBuiltin(UPSLDinoSpecies::TRexId, GetTransientPackage());
    const float Damage = GetDefault<UPSLSurvivalComponent>()->StarveDamagePerSecond;
    const float Drain = TRex->HungerDrainPerSecond;
    const float Min = UPSLSaveSubsystem::MinAutosaveSecondsToLive;
    auto Seconds = [&](float Health, float Hunger) { return UPSLSaveSubsystem::SecondsToLive(Health, Hunger, Drain, Damage); };

    TestEqual(TEXT("Minimum is 60 s"), Min, 60.0f);
    TestTrue(TEXT("TRex full: Hunger/drain + Health/damage (~206.8 s)"),
        FMath::IsNearlyEqual(Seconds(TRex->MaxHealth, TRex->MaxHunger), TRex->MaxHunger / Drain + TRex->MaxHealth / Damage, 1.0e-3f));
    TestTrue(TEXT("TRex full is far above the gate"), Seconds(TRex->MaxHealth, TRex->MaxHunger) > Min);

    // Doomed: Hunger 0, Health 10 -> 10 / damage seconds, refused even though CanCheckpoint allows it (10% HP).
    TestTrue(TEXT("Doomed: Health / damage"), FMath::IsNearlyEqual(Seconds(10.0f, 0.0f), 10.0f / Damage, 1.0e-4f));
    TestTrue(TEXT("Doomed state refused"), Seconds(10.0f, 0.0f) < Min);
    TestTrue(TEXT("Doomed state passes the old CanCheckpoint rule"), UPSLSaveSubsystem::CanCheckpoint(false, 10.0f, TRex->MaxHealth, 0.0f));
    TestTrue(TEXT("Starving at full health is also refused (25 s)"), Seconds(TRex->MaxHealth, 0.0f) < Min);

    // Recoverable: low hunger but the clock still has room.
    TestTrue(TEXT("Hunger 40 at full health allowed"), Seconds(TRex->MaxHealth, 40.0f) >= Min);
    TestTrue(TEXT("Hunger 40 at full health value"), FMath::IsNearlyEqual(Seconds(TRex->MaxHealth, 40.0f), 40.0f / Drain + TRex->MaxHealth / Damage, 1.0e-3f));

    // Non-positive rates are infinite, a dead pawn has no time left.
    const float Infinity = TNumericLimits<float>::Max();
    TestEqual(TEXT("No drain is infinite"), UPSLSaveSubsystem::SecondsToLive(50.0f, 50.0f, 0.0f, Damage), Infinity);
    TestEqual(TEXT("No starve damage is infinite"), UPSLSaveSubsystem::SecondsToLive(50.0f, 50.0f, Drain, 0.0f), Infinity);
    TestEqual(TEXT("No starve damage at hunger 0 is infinite"), UPSLSaveSubsystem::SecondsToLive(50.0f, 0.0f, Drain, 0.0f), Infinity);
    TestEqual(TEXT("Zero health is zero seconds"), UPSLSaveSubsystem::SecondsToLive(0.0f, 0.0f, Drain, Damage), 0.0f);

    // SaveCheckpoint refuses before touching any slot when there is no live pawn (the gate sits after the pawn lookup).
    const FSaveFixture Saves = MakeFixture(true, PairSlot);
    ON_SCOPE_EXIT { Saves->DeleteSave(); };
    TestFalse(TEXT("No pawn: autosave refused"), Saves->SaveCheckpoint(EPSLSaveReason::Autosave));
    TestFalse(TEXT("Refused autosave wrote nothing"), Saves->HasValidSave());
    return true;
}

#endif
