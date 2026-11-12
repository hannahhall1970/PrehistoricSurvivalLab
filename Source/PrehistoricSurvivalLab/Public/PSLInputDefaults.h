#pragma once

#include "CoreMinimal.h"

class UInputAction;
class UInputMappingContext;

namespace PSLInputDefaults
{
    /**
     * Fills each null argument with a transient, code-built default; assigned objects are never replaced.
     * A defaulted Context maps keys to the resolved actions (assigned or defaulted):
     * W/S/A/D + arrows -> Move, Mouse2D -> Look, SpaceBar -> Jump, LeftShift -> Sprint.
     */
    PREHISTORICSURVIVALLAB_API void Resolve(
        UObject* Outer,
        TObjectPtr<UInputMappingContext>& Context,
        TObjectPtr<UInputAction>& Move,
        TObjectPtr<UInputAction>& Look,
        TObjectPtr<UInputAction>& Jump,
        TObjectPtr<UInputAction>& Sprint);
}
