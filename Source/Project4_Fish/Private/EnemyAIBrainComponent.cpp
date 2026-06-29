#include "EnemyAIBrainComponent.h"

#include "AIController.h"
#include "NavigationSystem.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

/**
 * EnemyAIBrainComponent
 *
 * 敵AIの判断を担当するコンポーネント。
 *
 * 現在できること：
 * - Wanderモード：ランダム移動
 * - Aルール移動：目的地へ向かいつつ、Fishタグ持ちActorから距離を取る
 * - SpawnMoveモード：TargetLocationへ向かう
 *
 * 注意：
 * - AI判断はサーバー側のみで行う
 * - 実際の移動命令はAIControllerのMoveToLocationで行う
 * - BP_EnemyはBP_PlayerBaseの子として使う想定
 */

UEnemyAIBrainComponent::UEnemyAIBrainComponent()
{
    // TickComponentを使うためにtrueにする
    PrimaryComponentTick.bCanEverTick = true;
}

void UEnemyAIBrainComponent::BeginPlay()
{
    Super::BeginPlay();

    // このコンポーネントを持っているPawnを取得する
    // 基本的にはBP_Enemyが入る想定
    OwnerPawn = Cast<APawn>(GetOwner());

    // Pawnを操作しているAIControllerを取得する
    if (OwnerPawn)
    {
        OwnerAIController = Cast<AAIController>(OwnerPawn->GetController());
    }

    // 最初の判断タイミングを少しランダムにずらす
    // 14体が同時に判断して一斉に動くのを避けるため
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

    // AI判断はサーバー側だけで行う
    // クライアント側でもAI判断すると、移動や魚種変更がズレる原因になる
    if (!OwnerActor || !OwnerActor->HasAuthority())
    {
        return;
    }

    // BeginPlay時にOwnerPawnを取れなかった場合の保険
    if (!OwnerPawn)
    {
        OwnerPawn = Cast<APawn>(GetOwner());
    }

    // BeginPlay時にControllerがまだ入っていなかった場合の保険
    if (!OwnerAIController && OwnerPawn)
    {
        OwnerAIController = Cast<AAIController>(OwnerPawn->GetController());
    }

    // 現在のAIモードごとに処理を分ける
    switch (CurrentMode)
    {
    case EEnemyAIMode::SpawnMove:
        TickSpawnMove(DeltaTime);
        break;

    case EEnemyAIMode::Wander:
        TickWander(DeltaTime);
        break;

    default:
        // まだ未実装のモードは何もしない
        break;
    }
}

/**
 * Wanderモード。
 *
 * 仕様：
 * - 30%でその場に停止
 * - 70%でランダムなNavMesh地点へ移動
 *
 * 現在の移動はAルールを通しているため、
 * ランダム地点へ向かいつつ、近くの魚を軽く避ける。
 */
void UEnemyAIBrainComponent::TickWander(float DeltaTime)
{
    // 判断タイミングでなければ何もしない
    if (!ShouldMakeDecision())
    {
        return;
    }

    const float RandomValue = FMath::FRand();

    // 30%で停止
    if (RandomValue < 0.3f)
    {
        if (OwnerAIController)
        {
            OwnerAIController->StopMovement();
        }

        SetNextDecisionTime(WanderWaitMin, WanderWaitMax);
        return;
    }

    // 70%でランダム地点へ移動
    const bool bMoved = MoveToRandomLocation();

    if (bMoved)
    {
        SetNextDecisionTime(WanderWaitMin, WanderWaitMax);
    }
    else
    {
        // 移動先が見つからなかった場合は短めに待って再判断
        SetNextDecisionTime(0.5f, 1.0f);
    }
}

/**
 * SpawnMoveモード。
 *
 * 仕様：
 * - TargetLocationへAルールで向かう
 * - 3%でAttackへ行く可能性がある
 * - ただしAttack未実装のため、今はWanderへ逃がす
 * - TargetLocationへ到着したらWanderへ戻る
 */
void UEnemyAIBrainComponent::TickSpawnMove(float DeltaTime)
{
    // 判断タイミングでなければ何もしない
    if (!ShouldMakeDecision())
    {
        return;
    }

    // 3%で攻撃モードへ移行する想定
    // ただしAttackはまだ未実装なので、今はWanderへ移行させる
    if (TryChance(SpawnMoveAttackChance))
    {
        // 後でAttack実装後にこう変える：
        // CurrentMode = EEnemyAIMode::Attack;

        CurrentMode = EEnemyAIMode::Wander;
        SetNextDecisionTime(0.5f, 1.0f);
        return;
    }

    // TargetLocationに近づいたら到着扱い
    if (IsNearTargetLocation())
    {
        CurrentMode = EEnemyAIMode::Wander;
        SetNextDecisionTime(0.5f, 1.0f);
        return;
    }

    // TargetLocationへAルールで向かう
    const bool bMoved = MoveByARuleToLocation(TargetLocation);

    if (bMoved)
    {
        // SpawnMove中はやや短い間隔で進路更新する
        SetNextDecisionTime(0.3f, 0.6f);
    }
    else
    {
        // 目的地へ移動できなかった場合はWanderへ戻す
        CurrentMode = EEnemyAIMode::Wander;
        SetNextDecisionTime(0.5f, 1.0f);
    }
}

/**
 * Wander用。
 *
 * NavMesh上のランダム地点を探し、
 * その地点を目的地としてAルールで移動する。
 */
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

    // 現在位置の周囲から、NavMesh上の到達可能なランダム地点を探す
    const bool bFound = NavSys->GetRandomReachablePointInRadius(
        OwnerPawn->GetActorLocation(),
        WanderRadius,
        RandomLocation
    );

    if (!bFound)
    {
        return false;
    }

    // ランダム地点を目的地として、Aルールで補正した地点へ向かう
    return MoveByARuleToLocation(RandomLocation.Location);
}

/**
 * Aルール用。
 *
 * 近くにいるFishタグ持ちActorから離れる方向を計算する。
 *
 * 今は簡易版として、
 * UGameplayStatics::GetAllActorsWithTag を使っている。
 *
 * 14体程度ならまず問題ないが、
 * 敵数がかなり増える場合はSphereOverlapActorsに変更する。
 */
FVector UEnemyAIBrainComponent::CalculateAvoidDirection() const
{
    if (!OwnerPawn)
    {
        return FVector::ZeroVector;
    }

    TArray<AActor*> FishActors;

    // Fishタグ持ちActorを全部取得する
    // BP_EnemyやBP_PlayerBaseにFishタグを付けておく
    UGameplayStatics::GetAllActorsWithTag(GetWorld(), FishTagName, FishActors);

    const FVector MyLocation = OwnerPawn->GetActorLocation();

    FVector AvoidDirection = FVector::ZeroVector;

    // 何UU以内なら避けるか
    // 例：CellSize=300, MinSeparationCells=2なら600UU以内を避ける
    const float MinSeparationDistance = MinSeparationCells * CellSize;

    for (AActor* OtherActor : FishActors)
    {
        if (!OtherActor || OtherActor == OwnerPawn)
        {
            continue;
        }

        const FVector OtherLocation = OtherActor->GetActorLocation();

        // 地上移動なのでXY距離で判定する
        const float Distance = FVector::Dist2D(MyLocation, OtherLocation);

        if (Distance <= KINDA_SMALL_NUMBER)
        {
            continue;
        }

        // 近すぎる魚だけ避ける
        if (Distance < MinSeparationDistance)
        {
            // 相手から自分へ向かう方向
            FVector FromOtherToMe = MyLocation - OtherLocation;
            FromOtherToMe.Z = 0.0f;
            FromOtherToMe.Normalize();

            // 近いほど強く避ける
            const float Strength = 1.0f - FMath::Clamp(
                Distance / MinSeparationDistance,
                0.0f,
                1.0f
            );

            AvoidDirection += FromOtherToMe * Strength;
        }
    }

    AvoidDirection.Z = 0.0f;
    return AvoidDirection.GetSafeNormal();
}

/**
 * Aルール用。
 *
 * 目的地へ向かう方向、魚を避ける方向、ランダム方向を合成して、
 * 実際にMoveToする候補地点を作る。
 */
FVector UEnemyAIBrainComponent::CalculateARuleMoveLocation(const FVector& GoalLocation)
{
    if (!OwnerPawn)
    {
        return GoalLocation;
    }

    const FVector MyLocation = OwnerPawn->GetActorLocation();

    // 目的地へ向かう方向
    FVector GoalDirection = GoalLocation - MyLocation;
    GoalDirection.Z = 0.0f;
    GoalDirection = GoalDirection.GetSafeNormal();

    // 近くの魚から離れる方向
    const FVector AvoidDirection = CalculateAvoidDirection();

    // 少しランダムにずれる方向
    FVector RandomDirection = FVector(
        FMath::FRandRange(-1.0f, 1.0f),
        FMath::FRandRange(-1.0f, 1.0f),
        0.0f
    ).GetSafeNormal();

    // 3つの方向を重み付きで合成する
    FVector FinalDirection =
        GoalDirection * GoalWeight
        + AvoidDirection * AvoidWeight
        + RandomDirection * RandomWeight;

    FinalDirection.Z = 0.0f;

    // 何らかの理由で方向がゼロになった場合は目的地方向を使う
    if (FinalDirection.IsNearlyZero())
    {
        FinalDirection = GoalDirection;
    }

    FinalDirection = FinalDirection.GetSafeNormal();

    // 現在位置から少し先の地点を候補地にする
    FVector CandidateLocation = MyLocation + FinalDirection * ARuleStepDistance;

    // 目的地が近い場合、通り過ぎないように目的地そのものを候補地にする
    const float DistanceToGoal = FVector::Dist2D(MyLocation, GoalLocation);

    if (DistanceToGoal < ARuleStepDistance)
    {
        CandidateLocation = GoalLocation;
    }

    return CandidateLocation;
}

/**
 * Aルール移動。
 *
 * GoalLocationへ直接MoveToするのではなく、
 * Aルールで少し補正した地点をNavMesh上へ投影し、
 * そこへMoveToする。
 */
bool UEnemyAIBrainComponent::MoveByARuleToLocation(const FVector& GoalLocation)
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

    // Aルールで移動候補地点を作る
    const FVector CandidateLocation = CalculateARuleMoveLocation(GoalLocation);

    FNavLocation ProjectedLocation;

    // 候補地点をNavMesh上の有効地点へ補正する
    const bool bProjected = NavSys->ProjectPointToNavigation(
        CandidateLocation,
        ProjectedLocation
    );

    if (!bProjected)
    {
        return false;
    }

    // AIControllerに移動命令を出す
    OwnerAIController->MoveToLocation(
        ProjectedLocation.Location,
        AcceptanceRadius
    );

    return true;
}

/**
 * TargetLocationに十分近いか判定する。
 *
 * SpawnMoveで使用。
 */
bool UEnemyAIBrainComponent::IsNearTargetLocation() const
{
    if (!OwnerPawn)
    {
        return false;
    }

    const float Distance = FVector::Dist2D(
        OwnerPawn->GetActorLocation(),
        TargetLocation
    );

    return Distance <= TargetReachedDistance;
}

/**
 * 確率判定。
 *
 * Chance = 0.03 なら3%
 * Chance = 0.20 なら20%
 * Chance = 1.00 なら100%
 */
bool UEnemyAIBrainComponent::TryChance(float Chance) const
{
    return FMath::FRand() <= Chance;
}

/**
 * AIが次の判断をしてよい時間か確認する。
 */
bool UEnemyAIBrainComponent::ShouldMakeDecision() const
{
    if (!GetWorld())
    {
        return false;
    }

    return GetWorld()->GetTimeSeconds() >= NextDecisionTime;
}

/**
 * 次の判断時間を設定する。
 *
 * MinTime〜MaxTimeの間でランダムに待つ。
 * 複数の敵が同時に判断し続けるのを避けるため。
 */
void UEnemyAIBrainComponent::SetNextDecisionTime(float MinTime, float MaxTime)
{
    if (!GetWorld())
    {
        return;
    }

    const float WaitTime = FMath::FRandRange(MinTime, MaxTime);
    NextDecisionTime = GetWorld()->GetTimeSeconds() + WaitTime;
}