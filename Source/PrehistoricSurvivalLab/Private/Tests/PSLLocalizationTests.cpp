#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Fonts/FontCache.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "PSLDinoSpecies.h"
#include "PSLGameOverWidget.h"
#include "PSLGameUserSettings.h"
#include "PSLHudWidget.h"
#include "PSLMainMenuWidget.h"
#include "PSLPauseWidget.h"
#include "PSLSettingsWidget.h"
#include "PSLSpeciesSelectWidget.h"
#include "PSLUiStyle.h"
#include "Rendering/SlateRenderer.h"

namespace
{
    struct FPoEntry
    {
        FString Id;
        FString Str;
    };

    // Minimal PO reader: msgctxt/msgid/msgstr with quoted continuation lines. Comments and the header entry (empty msgid, no ctxt) are skipped.
    bool LoadPo(const FString& Path, TMap<FString, FPoEntry>& Out, FString& Error)
    {
        TArray<FString> Lines;
        if (!FFileHelper::LoadFileToStringArray(Lines, *Path))
        {
            Error = FString::Printf(TEXT("cannot read %s"), *Path);
            return false;
        }
        auto Unquote = [](const FString& Quoted)
        {
            FString S = Quoted.TrimStartAndEnd();
            S.RemoveFromStart(TEXT("\""));
            S.RemoveFromEnd(TEXT("\""));
            return S.Replace(TEXT("\\n"), TEXT("\n")).Replace(TEXT("\\\""), TEXT("\"")).Replace(TEXT("\\\\"), TEXT("\\"));
        };
        FString Ctx, Id, Str;
        FString* Current = nullptr;
        bool bHave = false;
        auto Flush = [&]()
        {
            if (bHave && !Ctx.IsEmpty())
            {
                Out.Add(Ctx, FPoEntry{Id, Str});
            }
            Ctx.Reset(); Id.Reset(); Str.Reset(); Current = nullptr; bHave = false;
        };
        for (const FString& Raw : Lines)
        {
            const FString Line = Raw.TrimStartAndEnd();
            if (Line.IsEmpty()) { Flush(); }
            else if (Line.StartsWith(TEXT("#"))) { continue; }
            else if (Line.StartsWith(TEXT("msgctxt "))) { if (bHave) { Flush(); } bHave = true; Ctx = Unquote(Line.Mid(8)); Current = &Ctx; }
            else if (Line.StartsWith(TEXT("msgid "))) { bHave = true; Id = Unquote(Line.Mid(6)); Current = &Id; }
            else if (Line.StartsWith(TEXT("msgstr "))) { Str = Unquote(Line.Mid(7)); Current = &Str; }
            else if (Line.StartsWith(TEXT("\"")) && Current) { *Current += Unquote(Line); }
        }
        Flush();
        return true;
    }

    FString PoPath(const TCHAR* Culture)
    {
        return FPaths::ProjectContentDir() / TEXT("Localization/Game") / Culture / TEXT("Game.po");
    }

    UTextBlock* FindText(UUserWidget* Widget, const FString& Expected)
    {
        TArray<UWidget*> All;
        Widget->WidgetTree->GetAllWidgets(All);
        for (UWidget* Child : All)
        {
            UTextBlock* Text = Cast<UTextBlock>(Child);
            if (Text && Text->GetText().ToString() == Expected)
            {
                return Text;
            }
        }
        return nullptr;
    }

    // Sorted "{...}" format arguments, joined: vi msgstr must keep exactly the msgid's placeholders.
    FString Placeholders(const FString& S)
    {
        TArray<FString> Tokens;
        for (int32 Open = S.Find(TEXT("{")); Open != INDEX_NONE; Open = S.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Open + 1))
        {
            const int32 Close = S.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Open);
            if (Close == INDEX_NONE)
            {
                break;
            }
            Tokens.Add(S.Mid(Open, Close - Open + 1));
        }
        Tokens.Sort();
        return FString::Join(Tokens, TEXT(","));
    }

    // Initialize builds the tree; TakeWidget builds the Slate widget, whose OnWidgetRebuilt runs NativeConstruct (UserWidget.cpp:1219-1234),
    // where the widgets bind OnCultureChanged. The UUserWidget holds its SObjectWidget only weakly, so the caller must keep the returned
    // ref alive (Hold): dropping it destroys the SObjectWidget, whose destructor runs NativeDestruct and unbinds (SObjectWidget.cpp:42-62).
    template <typename T>
    T* MakeLive(FAutomationTestBase& Test, const TCHAR* What, TArray<TSharedRef<SWidget>>& Hold)
    {
        T* Widget = NewObject<T>(GetTransientPackage(), T::StaticClass());
        if (!Test.TestNotNull(FString::Printf(TEXT("%s created"), What), Widget))
        {
            return nullptr;
        }
        Widget->Initialize();
        if (!Test.TestNotNull(FString::Printf(TEXT("%s tree"), What), Widget->WidgetTree.Get()))
        {
            return nullptr;
        }
        Hold.Add(Widget->TakeWidget());
        return Widget;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLLocPoCompleteTest, "PSL.Loc.PoComplete", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLLocPoCompleteTest::RunTest(const FString& Parameters)
{
    TMap<FString, FPoEntry> Vi, En;
    FString Error;
    if (!LoadPo(PoPath(TEXT("vi")), Vi, Error) || !LoadPo(PoPath(TEXT("en")), En, Error))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("vi has entries"), Vi.Num() > 0);
    for (const TPair<FString, FPoEntry>& Pair : Vi)
    {
        TestFalse(FString::Printf(TEXT("vi %s has a non-empty msgstr"), *Pair.Key), Pair.Value.Str.IsEmpty());
        TestFalse(FString::Printf(TEXT("vi %s has a msgid"), *Pair.Key), Pair.Value.Id.IsEmpty());
        TestEqual(FString::Printf(TEXT("vi %s keeps the msgid placeholders"), *Pair.Key), Placeholders(Pair.Value.Str), Placeholders(Pair.Value.Id));
        if (const FPoEntry* Other = En.Find(Pair.Key))
        {
            TestEqual(FString::Printf(TEXT("%s: vi and en share the English msgid"), *Pair.Key), Pair.Value.Id, Other->Id);
        }
        else
        {
            AddError(FString::Printf(TEXT("%s is in vi but not en"), *Pair.Key));
        }
    }
    for (const TPair<FString, FPoEntry>& Pair : En)
    {
        if (!Vi.Contains(Pair.Key))
        {
            AddError(FString::Printf(TEXT("%s is in en but not vi"), *Pair.Key));
        }
    }

    static const TCHAR* const Pinned[][2] = {
        {TEXT("PSL,Menu_Title"), TEXT("PREHISTORIC SURVIVAL")}, {TEXT("PSL,Menu_NewGame"), TEXT("New Game")},
        {TEXT("PSL,Species_Heading"), TEXT("CHOOSE YOUR DINO")}, {TEXT("PSL,Pause_Title"), TEXT("PAUSED")},
        {TEXT("PSL,Pause_Save"), TEXT("Save")}, {TEXT("PSL,GameOver_Died"), TEXT("YOU DIED")},
        {TEXT("PSL,HudHealth"), TEXT("HEALTH")}, {TEXT("PSL,Settings_Title"), TEXT("SETTINGS")},
        {TEXT("PSL,HintBiteToEat"), TEXT("Bite to eat")}};
    for (const TCHAR* const (&Key)[2] : Pinned)
    {
        const FPoEntry* E = En.Find(Key[0]);
        if (TestNotNull(FString::Printf(TEXT("en has %s"), Key[0]), E))
        {
            TestEqual(FString::Printf(TEXT("en msgid %s"), Key[0]), E->Id, FString(Key[1]));
            TestEqual(FString::Printf(TEXT("en msgstr %s"), Key[0]), E->Str, FString(Key[1]));
        }
        const FPoEntry* V = Vi.Find(Key[0]);
        if (TestNotNull(FString::Printf(TEXT("vi has %s"), Key[0]), V))
        {
            TestEqual(FString::Printf(TEXT("vi msgid %s"), Key[0]), V->Id, FString(Key[1]));
        }
    }

    // GatherText output, compiled by the parent.
    const FString Dir = FPaths::ProjectContentDir() / TEXT("Localization/Game");
    for (const FString& Rel : {FString(TEXT("vi/Game.locres")), FString(TEXT("en/Game.locres")), FString(TEXT("Game.manifest")), FString(TEXT("en/Game.archive")), FString(TEXT("vi/Game.archive"))})
    {
        TestTrue(FString::Printf(TEXT("%s exists"), *Rel), FPaths::FileExists(Dir / Rel));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLUiLocalizationTest, "PSL.UI.Localization", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLUiLocalizationTest::RunTest(const FString& Parameters)
{
    // Non-native game locres loads in the editor only with this CVar (TextLocalizationManager.cpp:71-72, :1949).
    IConsoleVariable* Force = IConsoleManager::Get().FindConsoleVariable(TEXT("Localization.ForceLoadGameLocalizationInEditor"));
    if (!TestNotNull(TEXT("ForceLoadGameLocalizationInEditor CVar exists"), Force))
    {
        return false;
    }
    const FString OldCulture = FInternationalization::Get().GetCurrentCulture()->GetName();
    const bool bOldForce = Force->GetBool();
    ON_SCOPE_EXIT
    {
        FInternationalization::Get().SetCurrentCulture(OldCulture);
        Force->Set(bOldForce ? TEXT("1") : TEXT("0"), ECVF_SetByCode);
    };
    Force->Set(TEXT("1"), ECVF_SetByCode);

    TMap<FString, FPoEntry> Vi;
    FString Error;
    if (!LoadPo(PoPath(TEXT("vi")), Vi, Error))
    {
        AddError(Error);
        return false;
    }
    FInternationalization::Get().SetCurrentCulture(TEXT("en")); // known starting point; also loads native data

    TArray<TSharedRef<SWidget>> Hold;
    UPSLMainMenuWidget* Menu = MakeLive<UPSLMainMenuWidget>(*this, TEXT("Main menu"), Hold);
    UPSLSpeciesSelectWidget* Species = MakeLive<UPSLSpeciesSelectWidget>(*this, TEXT("Species select"), Hold);
    UPSLPauseWidget* Pause = MakeLive<UPSLPauseWidget>(*this, TEXT("Pause"), Hold);
    UPSLGameOverWidget* GameOver = MakeLive<UPSLGameOverWidget>(*this, TEXT("Game over"), Hold);
    UPSLHudWidget* Hud = MakeLive<UPSLHudWidget>(*this, TEXT("HUD"), Hold);
    UPSLSettingsWidget* Settings = MakeLive<UPSLSettingsWidget>(*this, TEXT("Settings"), Hold);
    ON_SCOPE_EXIT
    {
        Hold.Reset(); // destroys the SObjectWidgets: NativeDestruct unbinds OnCultureChanged
    };
    if (!Menu || !Species || !Pause || !GameOver || !Hud || !Settings)
    {
        return false;
    }

    // Dynamic texts: HUD state is replayed from the last pushed values, the diet label from the selected species.
    Hud->SetValues(50.0f, 100.0f, 10.0f, 100.0f, false, true, false);
    const TCHAR* DietKey = Species->GetCurrentSpeciesId() == UPSLDinoSpecies::ParaId ? TEXT("PSL,Diet_Herbivore") : TEXT("PSL,Diet_Carnivore");

    struct FCase { UUserWidget* Widget; const TCHAR* Key; const TCHAR* What; };
    const FCase Cases[] = {
        {Menu, TEXT("PSL,Menu_NewGame"), TEXT("Main menu New Game")},
        {Species, TEXT("PSL,Species_Heading"), TEXT("Species heading")},
        {Species, DietKey, TEXT("Species diet")},
        {Pause, TEXT("PSL,Pause_Title"), TEXT("Pause title")},
        {GameOver, TEXT("PSL,GameOver_Died"), TEXT("Game over title")},
        {Hud, TEXT("PSL,HudHealth"), TEXT("HUD health label")},
        {Hud, TEXT("PSL,HudExhausted"), TEXT("HUD exhausted state")},
        {Settings, TEXT("PSL,Settings_Title"), TEXT("Settings title")}};

    TArray<UTextBlock*> Probes;
    for (const FCase& Case : Cases)
    {
        const FPoEntry* E = Vi.Find(Case.Key);
        if (!TestNotNull(FString::Printf(TEXT("vi.po has %s"), Case.Key), E))
        {
            return false;
        }
        TestNotEqual(FString::Printf(TEXT("%s: vi differs from en"), Case.What), E->Str, E->Id);
        UTextBlock* Block = FindText(Case.Widget, E->Id);
        if (!TestNotNull(FString::Printf(TEXT("%s starts in English"), Case.What), Block))
        {
            return false;
        }
        Probes.Add(Block);
    }

    // A culture-invariant sentinel never re-resolves on its own, so only the widgets' OnCultureChanged -> RefreshTexts can replace it.
    const FText Sentinel = FText::AsCultureInvariant(TEXT("__psl_sentinel__"));
    auto CheckSwitch = [&](const TCHAR* Culture, bool bVietnamese)
    {
        for (UTextBlock* Block : Probes)
        {
            Block->SetText(Sentinel);
        }
        FInternationalization::Get().SetCurrentCulture(Culture); // sync reload: AsyncLoadLocalizationDataOnLanguageChange defaults false (TextLocalizationManager.cpp:64)
        TestTrue(FString::Printf(TEXT("Culture is %s"), Culture), FInternationalization::Get().GetCurrentCulture()->GetName().StartsWith(Culture));
        for (int32 Index = 0; Index < Probes.Num(); ++Index)
        {
            const FPoEntry& E = Vi[Cases[Index].Key];
            TestEqual(FString::Printf(TEXT("%s refreshed to %s"), Cases[Index].What, Culture), Probes[Index]->GetText().ToString(), bVietnamese ? E.Str : E.Id);
        }
    };
    CheckSwitch(TEXT("vi"), true);
    CheckSwitch(TEXT("en"), false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLSettingsCultureAppliesTest, "PSL.Settings.CultureApplies", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLSettingsCultureAppliesTest::RunTest(const FString& Parameters)
{
    UPSLGameUserSettings* Settings = UPSLGameUserSettings::Get();
    if (!Settings)
    {
        AddError(TEXT("UPSLGameUserSettings::Get() is null: GameUserSettingsClassName is not the PSL class"));
        return false;
    }
    const FString OldName = Settings->CultureName;
    const FString OldCulture = FInternationalization::Get().GetCurrentCulture()->GetName();
    // SaveSettings flushes the whole live object; put the exact on-disk bytes back so unrelated unsaved state is not persisted.
    const FString IniPath = GGameUserSettingsIni;
    TArray<uint8> OldIni;
    const bool bHadIni = FFileHelper::LoadFileToArray(OldIni, *IniPath, FILEREAD_Silent);
    ON_SCOPE_EXIT
    {
        Settings->CultureName = OldName;
        Settings->SaveSettings();
        Settings->ApplyCulture();
        FInternationalization::Get().SetCurrentCulture(OldCulture);
        if (bHadIni)
        {
            FFileHelper::SaveArrayToFile(OldIni, *IniPath);
        }
        else
        {
            IFileManager::Get().Delete(*IniPath);
        }
    };

    Settings->CultureName = TEXT("vi");
    Settings->ApplyCulture();
    TestTrue(TEXT("ApplyCulture switches the current culture to vi"), FInternationalization::Get().GetCurrentCulture()->GetName().StartsWith(TEXT("vi")));

    Settings->SaveSettings();
    UPSLGameUserSettings* Fresh = NewObject<UPSLGameUserSettings>(GetTransientPackage());
    Fresh->LoadSettings(true);
    TestEqual(TEXT("CultureName vi persisted"), Fresh->CultureName, FString(TEXT("vi")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLUiVietnameseGlyphsTest, "PSL.UI.VietnameseGlyphs", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLUiVietnameseGlyphsTest::RunTest(const FString& Parameters)
{
    if (!FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer())
    {
        AddError(TEXT("Slate application or renderer unavailable: cannot query the font cache"));
        return false;
    }
    // FSlateRenderer::GetFontCache (SlateRenderer.h:427), FSlateFontCache::GetFontDataForCodepoint / CanLoadCodepoint (FontCache.h:898, :974).
    const TSharedRef<FSlateFontCache> Cache = FSlateApplication::Get().GetRenderer()->GetFontCache();
    FString Sample = TEXT("ăâđêôơư ĂÂĐÊÔƠƯ àảãáạ ằẳẵắặ ầẩẫấậ èẻẽéẹ ềểễếệ ìỉĩíị òỏõóọ ồổỗốộ ờởỡớợ ùủũúụ ừửữứự ỳỷỹýỵ");
    // UI strings (vi) in both cases, plus the decomposed combining marks U+0300 U+0301 U+0303 U+0309 U+0323.
    for (const TCHAR* Word : {TEXT("Tiếp tục"), TEXT("Cài đặt"), TEXT("Khủng long"), TEXT("Thể lực"), TEXT("Đói"), TEXT("Đã kiệt sức"), TEXT("Trở về menu chính")})
    {
        Sample += TEXT(" ");
        Sample += Word;
        Sample += TEXT(" ");
        Sample += FString(Word).ToUpper();
    }
    Sample += TEXT(" \u0300\u0301\u0303\u0309\u0323");

    for (const EPSLFontWeight Weight : {EPSLFontWeight::Regular, EPSLFontWeight::SemiBold, EPSLFontWeight::Bold})
    {
        const FSlateFontInfo Info = FPSLUiStyle::Font(FPSLUiStyle::BodySize, Weight);
        const FString FaceName = FPSLUiStyle::TypefaceName(Weight).ToString();
        const TCHAR* Face = *FaceName;
        FString MissingPrimary, MissingAny;
        for (const TCHAR Ch : Sample)
        {
            if (Ch == TEXT(' '))
            {
                continue;
            }
            float Scale = 1.0f;
            const FFontData& Data = Cache->GetFontDataForCodepoint(Info, static_cast<UTF32CHAR>(Ch), Scale);
            if (!Cache->CanLoadCodepoint(Data, static_cast<UTF32CHAR>(Ch), EFontFallback::FF_NoFallback))
            {
                MissingPrimary.AppendChar(Ch);
                if (!Cache->CanLoadCodepoint(Data, static_cast<UTF32CHAR>(Ch), EFontFallback::FF_LastResortFallback))
                {
                    MissingAny.AppendChar(Ch);
                }
            }
        }
        if (MissingPrimary.IsEmpty())
        {
            AddInfo(FString::Printf(TEXT("%s font (project font: %d): every glyph present at FF_NoFallback"), Face, FPSLUiStyle::HasProjectFont()));
        }
        else
        {
            AddInfo(FString::Printf(TEXT("%s font (project font: %d): primary font lacks '%s'; FF_LastResortFallback needed, still missing: '%s'"),
                Face, FPSLUiStyle::HasProjectFont(), *MissingPrimary, *MissingAny));
        }
        TestTrue(FString::Printf(TEXT("%s font renders every Vietnamese glyph (missing at last-resort fallback: '%s')"), Face, *MissingAny), MissingAny.IsEmpty());
        if (FPSLUiStyle::HasProjectFont())
        {
            TestTrue(FString::Printf(TEXT("%s project font has every glyph itself, no fallback needed (missing at FF_NoFallback: '%s')"), Face, *MissingPrimary), MissingPrimary.IsEmpty());
        }
    }
    return true;
}

#endif
