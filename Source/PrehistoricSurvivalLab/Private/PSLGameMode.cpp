#include "PSLGameMode.h"

#include "PSLCharacter.h"
#include "PSLHUD.h"

APSLGameMode::APSLGameMode()
{
    DefaultPawnClass = APSLCharacter::StaticClass();
    HUDClass = APSLHUD::StaticClass();
}
