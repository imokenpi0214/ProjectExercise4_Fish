#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LockOnMathLibrary.generated.h"

UCLASS()
class PROJECT4_FISH_API ULockOnMathLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:

    UFUNCTION(BlueprintCallable, Category = "LockOn|Math")
    static bool CalculateInterceptDirection2D(
        const FVector& PlayerLocation,
        float DashSpeed,
        const FVector& TargetLocation,
        const FVector& TargetVelocity,
        FVector& OutDirection,
        FVector& OutPredictedLocation,
        float& OutTime
    );
};