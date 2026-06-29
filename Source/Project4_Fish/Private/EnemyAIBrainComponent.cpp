#include "EnemyAIBrainComponent.h"

#include "AIController.h"
#include "NavigationSystem.h"
#include "GameFramework/Pawn.h"

UEnemyAIBrainComponent::UEnemyAIBrainComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UEnemyAIBrainComponent::BeginPlay()
{
    Super::BeginPlay();

    OwnerPawn = Cast<APawn>(GetOwner());

    if (OwnerPawn)
    {
        OwnerAIController = Cast<AAIController>(OwnerPawn->GetController());
    }

    SetNextDecisionTime(0.1f, 0.5f);
}

void UEnemyAIBrainComponent::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction
)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    AActor* OwnerActor = GetOwner();
    if (!OwnerActor || !OwnerActor->HasAuthority())
    {
        return;
    }

    if (!OwnerPawn)
    {
        OwnerPawn = Cast<APawn>(GetOwner());
    }

    if (!OwnerAIController && OwnerPawn)
    {
        OwnerAIController = Cast<AAIController>(OwnerPawn->GetController());
    }

    switch (CurrentMode)
    {
    case EEnemyAIMode::Wander:
        TickWander(DeltaTime);
        break;

    default:
        break;
    }
}

void UEnemyAIBrainComponent::TickWander(float DeltaTime)
{
    if (!ShouldMakeDecision())
    {
        return;
    }

    const float RandomValue = FMath::FRand();

    if (RandomValue < 0.3f)
    {
        if (OwnerAIController)
        {
            OwnerAIController->StopMovement();
        }

        SetNextDecisionTime(WanderWaitMin, WanderWaitMax);
        return;
    }

    const bool bMoved = MoveToRandomLocation();

    if (bMoved)
    {
        SetNextDecisionTime(WanderWaitMin, WanderWaitMax);
    }
    else
    {
        SetNextDecisionTime(0.5f, 1.0f);
    }
}

bool UEnemyAIBrainComponent::MoveToRandomLocation()
{
    if (!OwnerPawn || !OwnerAIController)
    {
        return false;
    }

    UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
    if (!NavSys)
    {
        return false;
    }

    FNavLocation RandomLocation;

    const bool bFound = NavSys->GetRandomReachablePointInRadius(
        OwnerPawn->GetActorLocation(),
        WanderRadius,
        RandomLocation
    );

    if (!bFound)
    {
        return false;
    }

    OwnerAIController->MoveToLocation(
        RandomLocation.Location,
        AcceptanceRadius
    );

    return true;
}

bool UEnemyAIBrainComponent::ShouldMakeDecision() const
{
    if (!GetWorld())
    {
        return false;
    }

    return GetWorld()->GetTimeSeconds() >= NextDecisionTime;
}

void UEnemyAIBrainComponent::SetNextDecisionTime(float MinTime, float MaxTime)
{
    if (!GetWorld())
    {
        return;
    }

    const float WaitTime = FMath::FRandRange(MinTime, MaxTime);
    NextDecisionTime = GetWorld()->GetTimeSeconds() + WaitTime;
}