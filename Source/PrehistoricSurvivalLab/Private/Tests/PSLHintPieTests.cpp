#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Components/StaticMeshComponent.h"
#include "GameFramework/PlayerStart.h"
#include "PSLDinoSpecies.h"
#include "PSLFoodPlant.h"
#include "PSLGameInstance.h"
#include "PSLPieTestSupport.h"

// Proximity "Bite to eat" hint (APSLCharacter::UpdateProximityHint, 0.2 s timer) on the shipped valley plants.
namespace
{
using namespace PSLPie;

const FText& BiteToEatText()
{
    static const FText Text = NSLOCTEXT("PSL", "HintBiteToEat", "Bite to eat");
    return Text;
}

bool ShowsBiteToEat(const FPSLPieCtx& Ctx)
{
    return Ctx.Character->GetFoodHint().EqualTo(BiteToEatText());
}

// Runs `Body` repeatedly for Seconds of world time (the 0.2 s hint timer needs >= 0.5 s to settle), then returns true.
void AddSettleStep(const TSharedRef<FPSLPieCtx>& Context, const TCHAR* Name, double Seconds, TFunction<void(FPSLPieCtx&)> AfterSettle)
{
    AddPieStep(Context, Name, 5.0f, [Seconds, AfterSettle = MoveTemp(AfterSettle), StartTime = -1.0](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        const double Now = Ctx.World->GetTimeSeconds();
        if (StartTime < 0.0)
        {
            StartTime = Now;
        }
        if (Now - StartTime < Seconds)
        {
            return false;
        }
        AfterSettle(Ctx);
        return true;
    });
}

// Enters the valley as `SpeciesId`, then stands the pawn so its bite point lands on the PlayerStart-nearest plant, facing +X.
void QueueStandAtPlant(FAutomationTestBase& Test, const TSharedRef<FPSLPieCtx>& Context, const FName SpeciesId, const TSharedRef<TWeakObjectPtr<APSLFoodPlant>>& Target)
{
    AddPieStep(Context, TEXT("StartNewGame"), 3.0f, [SpeciesId](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        UPSLGameInstance* GameInstance = Cast<UPSLGameInstance>(Ctx.World->GetGameInstance());
        if (!GameInstance)
        {
            Ctx.MarkInvalid(TEXT("PIE GameInstance is not UPSLGameInstance"));
            return true;
        }
        Ctx.RequiredMap = TEXT("L_LostValley");
        Ctx.bReady = false;
        Ctx.World = nullptr;
        Ctx.GameViewport = nullptr;
        Ctx.PC = nullptr;
        Ctx.Character = nullptr;
        GameInstance->StartNewGame(SpeciesId);
        return true;
    });
    AddPieStep(Context, TEXT("WaitForValleyPawn"), 30.0f, [](FPSLPieCtx& Ctx)
    {
        return WaitForPlayer(Ctx);
    });
    AddPieStep(Context, TEXT("StandAtPlant"), 3.0f, [Target](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        TActorIterator<APlayerStart> StartIt(Ctx.World);
        if (!StartIt)
        {
            Ctx.MarkInvalid(TEXT("No PlayerStart in L_LostValley"));
            return true;
        }
        double Nearest = TNumericLimits<double>::Max();
        for (TActorIterator<APSLFoodPlant> It(Ctx.World); It; ++It)
        {
            const double Distance = FVector::Dist2D(It->GetActorLocation(), StartIt->GetActorLocation());
            if (Distance < Nearest)
            {
                Nearest = Distance;
                *Target = *It;
            }
        }
        APSLFoodPlant* Plant = Target->Get();
        if (!Plant)
        {
            Ctx.MarkInvalid(TEXT("No APSLFoodPlant in L_LostValley"));
            return true;
        }
        Ctx.Test->TestTrue(TEXT("Target plant starts available"), Plant->IsAvailable());
        const FVector Forward = FVector::ForwardVector;
        const FVector Stand = Plant->GetActorLocation() - Forward * Ctx.Character->BiteReach
            + FVector(0.0, 0.0, Ctx.Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 20.0);
        Ctx.Character->SetActorLocation(Stand, false, nullptr, ETeleportType::TeleportPhysics);
        Ctx.Character->GetCharacterMovement()->StopMovementImmediately();
        Ctx.PC->SetControlRotation(Forward.Rotation());
        Ctx.Character->SetActorRotation(Forward.Rotation());
        Ctx.Character->GetSurvivalComponent()->Hunger = 10.0f;
        return true;
    });
}

void Face(FPSLPieCtx& Ctx, const FVector& Direction)
{
    Ctx.PC->SetControlRotation(Direction.Rotation());
    Ctx.Character->SetActorRotation(Direction.Rotation());
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLHintHerbivoreEatHintTest, "PSL.PIE.Hint.HerbivoreEatHint", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// Regressions caught: hint missing/stuck, not cleared after eating or when facing away, and a second bite feeding twice.
bool FPSLHintHerbivoreEatHintTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("Hint_HerbivoreEatHint"));
    if (!QueuePieStart(*this, Context))
    {
        return false;
    }
    const TSharedRef<TWeakObjectPtr<APSLFoodPlant>> Target = MakeShared<TWeakObjectPtr<APSLFoodPlant>>();
    const TSharedRef<float> HungerAfterFirstBite = MakeShared<float>(0.0f);
    QueueStandAtPlant(*this, Context, UPSLDinoSpecies::ParaId, Target);

    AddSettleStep(Context, TEXT("HintShownFacingPlant"), 0.5, [](FPSLPieCtx& Ctx)
    {
        Ctx.Test->TestTrue(TEXT("Facing an available plant shows Bite to eat"), ShowsBiteToEat(Ctx));
        Face(Ctx, -FVector::ForwardVector);
    });
    AddSettleStep(Context, TEXT("HintClearedFacingAway"), 0.5, [](FPSLPieCtx& Ctx)
    {
        Ctx.Test->TestTrue(TEXT("Facing away from the plant leaves the hint empty"), Ctx.Character->GetFoodHint().IsEmpty());
        Face(Ctx, FVector::ForwardVector);
    });
    AddSettleStep(Context, TEXT("HintShownAgain"), 0.5, [](FPSLPieCtx& Ctx)
    {
        Ctx.Test->TestTrue(TEXT("Facing the plant again shows Bite to eat"), ShowsBiteToEat(Ctx));
        Ctx.Test->TestTrue(TEXT("Bite accepted"), Ctx.Character->TryBite());
    });
    // BiteHitDelay 0.35 s + cooldown 1.0 s: 1.3 s covers the hit, the hint refresh, and the next legal bite.
    AddSettleStep(Context, TEXT("HintClearedAfterEating"), 1.3, [Target, HungerAfterFirstBite](FPSLPieCtx& Ctx)
    {
        const float Hunger = Ctx.Character->GetSurvivalComponent()->GetHunger();
        *HungerAfterFirstBite = Hunger;
        Ctx.Test->TestTrue(FString::Printf(TEXT("Eating raised hunger above 30 (was %.1f)"), Hunger), Hunger > 30.0f);
        Ctx.Test->TestTrue(TEXT("Plant is unavailable after being eaten"), Target->IsValid() && !Target->Get()->IsAvailable());
        Ctx.Test->TestTrue(TEXT("Hint is empty after the plant was eaten"), Ctx.Character->GetFoodHint().IsEmpty());
        Ctx.Character->TryBite();
    });
    AddSettleStep(Context, TEXT("NoDuplicateFeeding"), 0.8, [HungerAfterFirstBite](FPSLPieCtx& Ctx)
    {
        const float Hunger = Ctx.Character->GetSurvivalComponent()->GetHunger();
        // Hunger only drains between the two samples; any gain means the regrowing plant fed twice.
        Ctx.Test->TestTrue(FString::Printf(TEXT("Second bite on the eaten plant gave no food (%.2f -> %.2f)"), *HungerAfterFirstBite, Hunger), Hunger <= *HungerAfterFirstBite);
        Record(Ctx, TEXT("herbivore_eat_hint"));
    });
    QueuePieEnd(Context);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLHintCarnivoreNoEatHintTest, "PSL.PIE.Hint.CarnivoreNoEatHint", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// Regression caught: the eat prompt leaking to carnivores (it promises food they cannot eat).
bool FPSLHintCarnivoreNoEatHintTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("Hint_CarnivoreNoEatHint"));
    if (!QueuePieStart(*this, Context))
    {
        return false;
    }
    const TSharedRef<TWeakObjectPtr<APSLFoodPlant>> Target = MakeShared<TWeakObjectPtr<APSLFoodPlant>>();
    const TSharedRef<float> HungerBeforeBite = MakeShared<float>(0.0f);
    QueueStandAtPlant(*this, Context, UPSLDinoSpecies::TRexId, Target);

    AddSettleStep(Context, TEXT("NoHintFacingPlant"), 0.6, [HungerBeforeBite](FPSLPieCtx& Ctx)
    {
        Ctx.Test->TestFalse(TEXT("Carnivore facing a plant is not offered Bite to eat"), ShowsBiteToEat(Ctx));
        *HungerBeforeBite = Ctx.Character->GetSurvivalComponent()->GetHunger();
        Ctx.Test->TestTrue(TEXT("Bite accepted"), Ctx.Character->TryBite());
    });
    AddSettleStep(Context, TEXT("CannotEatHintAfterBite"), 0.8, [Target, HungerBeforeBite](FPSLPieCtx& Ctx)
    {
        const FText CannotEat = NSLOCTEXT("PSL", "HintCannotEatPlants", "Carnivores can't eat plants. Hunt prey instead.");
        Ctx.Test->TestTrue(TEXT("Biting a plant shows the cannot-eat hint"), Ctx.Character->GetFoodHint().EqualTo(CannotEat));
        Ctx.Test->TestFalse(TEXT("Cannot-eat hint is not Bite to eat"), ShowsBiteToEat(Ctx));
        Ctx.Test->TestTrue(TEXT("Plant is still available"), Target->IsValid() && Target->Get()->IsAvailable());
        const float Hunger = Ctx.Character->GetSurvivalComponent()->GetHunger();
        Ctx.Test->TestTrue(FString::Printf(TEXT("Hunger did not rise (%.2f -> %.2f)"), *HungerBeforeBite, Hunger), Hunger <= *HungerBeforeBite);
        Record(Ctx, TEXT("carnivore_no_eat_hint"));
    });
    QueuePieEnd(Context);
    return true;
}

#endif
