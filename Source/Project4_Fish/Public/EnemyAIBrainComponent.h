#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FishAITypes.h"
#include "EnemyAIBrainComponent.generated.h"

class AAIController;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROJECT4_FISH_API UEnemyAIBrainComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UEnemyAIBrainComponent();

protected:
    virtual void BeginPlay() override;

public:
    virtual void TickComponent(
        float DeltaTime,
        ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction
    ) override;

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI")
    EEnemyAIMode CurrentMode = EEnemyAIMode::Wander;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI")
    EEnemyAILevel AILevel = EEnemyAILevel::Weak;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Cell")
    float CellSize = 300.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Wander")
    float WanderRadius = 1500.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Wander")
    float WanderWaitMin = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Wander")
    float WanderWaitMax = 3.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Move")
    float AcceptanceRadius = 100.0f;

private:
    UPROPERTY()
    APawn* OwnerPawn = nullptr;

    UPROPERTY()
    AAIController* OwnerAIController = nullptr;

    float NextDecisionTime = 0.0f;

private:
    void TickWander(float DeltaTime);

    bool MoveToRandomLocation();

    bool ShouldMakeDecision() const;

    void SetNextDecisionTime(float MinTime, float MaxTime);
};