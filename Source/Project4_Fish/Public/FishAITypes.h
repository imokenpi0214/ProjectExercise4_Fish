#pragma once

#include "CoreMinimal.h"
#include "FishAITypes.generated.h"

UENUM(BlueprintType)
enum class EEnemyAIMode : uint8
{
    SpawnMove UMETA(DisplayName = "Spawn Move"),
    Devour    UMETA(DisplayName = "Devour"),
    GoToHook  UMETA(DisplayName = "Go To Hook"),
    Attack    UMETA(DisplayName = "Attack"),
    Wander    UMETA(DisplayName = "Wander"),
    Flee      UMETA(DisplayName = "Flee")
};

UENUM(BlueprintType)
enum class EEnemyAILevel : uint8
{
    Weak   UMETA(DisplayName = "Weak"),
    Strong UMETA(DisplayName = "Strong")
};