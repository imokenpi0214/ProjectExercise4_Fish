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
 * - Wanderモード：自由移動
 * - SpawnMoveモード：プランクトン群れ中央へ移動
 * - Devourモード：周囲のプランクトンを探して向かう
 * - Aルール移動：目的地へ向かいつつ、Fishタグ持ちActorから距離を取る
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

    case EEnemyAIMode::Devour:
        TickDevour(DeltaTime);
        break;

    case EEnemyAIMode::Wander:
        TickWander(DeltaTime);
        break;

    case EEnemyAIMode::Attack:
        // TODO: Attackモード実装後に TickAttack を呼ぶ
        break;

    case EEnemyAIMode::GoToHook:
        // TODO: GoToHookモード実装後に TickGoToHook を呼ぶ
        break;

    case EEnemyAIMode::Flee:
        // TODO: Fleeモード実装後に TickFlee を呼ぶ
        break;

    default:
        break;
    }
}

// =========================
// BPから呼ぶ公開関数
// =========================

/**
 * AIモードを変更する。
 *
 * BPからCurrentModeを直接Setする代わりに、この関数を使う。
 * 後で「モードに入った瞬間の処理」を追加しやすくするため。
 */
void UEnemyAIBrainComponent::SetAIMode(EEnemyAIMode NewMode)
{
    if (CurrentMode == NewMode)
    {
        return;
    }

    CurrentMode = NewMode;

    // モード変更直後は早めに判断させる
    SetNextDecisionTime(0.05f, 0.15f);

    // モード別の初期化
    switch (CurrentMode)
    {
    case EEnemyAIMode::Wander:
        TargetPlanktonActor = nullptr;
        break;

    case EEnemyAIMode::SpawnMove:
        TargetPlanktonActor = nullptr;
        break;

    case EEnemyAIMode::Devour:
        // Devourに入ったら、まず狙うプランクトンを探す
        SelectNearestPlankton();
        break;

    case EEnemyAIMode::Attack:
        // TODO: Attack実装時に初期化処理を追加
        if (OwnerAIController)
        {
            OwnerAIController->StopMovement();
        }
        break;

    case EEnemyAIMode::GoToHook:
        // TODO: GoToHook実装時に釣り糸位置をTargetLocationへ入れる
        if (OwnerAIController)
        {
            OwnerAIController->StopMovement();
        }
        break;

    case EEnemyAIMode::Flee:
        // TODO: Flee実装時に逃走先を設定する
        break;

    default:
        break;
    }
}

/**
 * BP_Enemy側から、GameSystemで取得したプランクトン群れActorを渡す。
 *
 * 想定：
 * - BP_EnemyのAI_CheckCollectItemGroupで、
 *   GameSystemからBP_CollectItemGroupを取得する
 * - そのActorをこの関数へ渡す
 *
 * この関数内で：
 * - TargetGroupActorに保存
 * - TargetLocationに群れ中央位置を保存
 * - SpawnMoveへ移行
 */
void UEnemyAIBrainComponent::SetCollectItemGroupTarget(AActor* NewGroupActor)
{
    if (!NewGroupActor)
    {
        return;
    }

    AActor* OwnerActor = GetOwner();
    if (!OwnerActor || !OwnerActor->HasAuthority())
    {
        return;
    }

    TargetGroupActor = NewGroupActor;
    TargetLocation = NewGroupActor->GetActorLocation();

    SetAIMode(EEnemyAIMode::SpawnMove);
}

/**
 * AIがプランクトンを1つ取得した時に呼ぶ。
 *
 * 狙っていたプランクトンかどうかに関係なく呼ぶ。
 * 理由：
 * - 狙ったプランクトンへ向かう途中で、別のプランクトンを拾うことがあるため。
 */
void UEnemyAIBrainComponent::NotifyPlanktonCollected()
{
    AActor* OwnerActor = GetOwner();
    if (!OwnerActor || !OwnerActor->HasAuthority())
    {
        return;
    }

    EatenPlanktonCount++;

    // プランクトン取得時、常に20%でAttackへ移行
    if (TryChance(DevourAttackChance))
    {
        // TODO: Attackモード実装後、ここで攻撃対象を設定する
        SetAIMode(EEnemyAIMode::Attack);
        return;
    }

    // 食べた数によってGoToHookへ移行するか判定
    // if (TryGoToHookByPlanktonCount())
    // {
    //     // TODO: GoToHookモード実装後、ここで釣り糸位置を設定する
    //     SetAIMode(EEnemyAIMode::GoToHook);
    //     return;
    // }

    // それ以外はDevour継続
    if (CurrentMode == EEnemyAIMode::SpawnMove)
    {
        SetAIMode(EEnemyAIMode::Devour);
    }
}

// =========================
// モード別Tick
// =========================

/**
 * Wanderモード。
 *
 * 仕様：
 * - 30%でその場に停止
 * - 70%でランダムなNavMesh地点へ移動
 *
 * プランクトン群れの検知はBP_Enemy側のTimerで行う。
 * Wander中に群れが見つかった場合のみ、BP側からSetCollectItemGroupTargetを呼ぶ。
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
 * - プランクトン群れ中央へAルールで向かう
 * - 群れ中央へ大体近づいたらDevourへ移行する
 * - Devourへ行けるのはこのSpawnMoveからのみ
 */
void UEnemyAIBrainComponent::TickSpawnMove(float DeltaTime)
{
    // 判断タイミングでなければ何もしない
    if (!ShouldMakeDecision())
    {
        return;
    }

    // 群れActorが有効なら、群れ中央位置を更新する
    // 群れActorが移動したり、位置が変わる可能性への保険
    if (TargetGroupActor)
    {
        TargetLocation = TargetGroupActor->GetActorLocation();
    }

    // 出現時モード中、3%でAttackへ移行する仕様
    // Attack未実装なら、BP側でSpawnMoveAttackChanceを0.0にしておくのがおすすめ
    if (TryChance(SpawnMoveAttackChance))
    {
        // TODO: Attackモード実装後、攻撃対象を設定する
        SetAIMode(EEnemyAIMode::Attack);
        return;
    }

    // 群れ中央にだいたい近づいたらDevourへ移行
    if (IsNearTargetLocation())
    {
        SetAIMode(EEnemyAIMode::Devour);
        return;
    }

    // 群れ中央へAルールで向かう
    const bool bMoved = MoveByARuleToLocation(TargetLocation);

    if (bMoved)
    {
        // SpawnMove中はやや短い間隔で進路更新する
        SetNextDecisionTime(0.3f, 0.6f);
    }
    else
    {
        // 目的地へ移動できなかった場合は少し待って再試行
        // DevourやWanderへは勝手に移行しない
        SetNextDecisionTime(0.5f, 1.0f);
    }
}

/**
 * Devourモード。
 *
 * 仕様：
 * - 周囲のプランクトンを探す
 * - 一番近いプランクトンへAルールで向かう
 * - 狙っていたプランクトンが消えたら別のプランクトンを探す
 * - 周囲のプランクトンが全て消えたらAttackへ強制移行
 * - DevourからWanderへは戻らない
 */
void UEnemyAIBrainComponent::TickDevour(float DeltaTime)
{
    // 判断タイミングでなければ何もしない
    if (!ShouldMakeDecision())
    {
        return;
    }

    // 狙っているプランクトンが無効なら、近くの別プランクトンを探す
    if (!IsTargetPlanktonValid())
    {
        const bool bFound = SelectNearestPlankton();

        if (!bFound)
        {
            // 周囲のプランクトンが全て消えた扱い
            // 仕様：強制的にAttackへ移行
            SetAIMode(EEnemyAIMode::Attack);
            return;
        }
    }

    // ここでもう一度確認
    if (!IsTargetPlanktonValid())
    {
        SetAIMode(EEnemyAIMode::Attack);
        return;
    }

    // 狙っているプランクトンの現在位置を目的地にする
    TargetLocation = TargetPlanktonActor->GetActorLocation();

    // 狙ったプランクトンへAルールで向かう
    const bool bMoved = MoveByARuleToLocation(TargetLocation);

    if (bMoved)
    {
        // Devour中はやや短い間隔で進路更新する
        SetNextDecisionTime(0.15f, 0.35f);
    }
    else
    {
        // 移動できないなら別のプランクトンを探す
        TargetPlanktonActor = nullptr;
        SetNextDecisionTime(0.1f, 0.2f);
    }
}

// =========================
// Wander用関数
// =========================

/**
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

// =========================
// Devour用関数
// =========================

/**
 * 周囲のプランクトンから一番近いものを探す。
 *
 * 今は簡易版としてタグ検索を使用。
 * 実際のプランクトンBPに Plankton タグを付ける必要がある。
 */
AActor* UEnemyAIBrainComponent::FindNearestPlankton() const
{
    if (!OwnerPawn)
    {
        return nullptr;
    }

    TArray<AActor*> PlanktonActors;
    UGameplayStatics::GetAllActorsWithTag(GetWorld(), PlanktonTagName, PlanktonActors);

    const FVector MyLocation = OwnerPawn->GetActorLocation();

    AActor* NearestPlankton = nullptr;
    float NearestDistanceSq = TNumericLimits<float>::Max();

    const float SearchRadiusSq = DevourSearchRadius * DevourSearchRadius;

    for (AActor* PlanktonActor : PlanktonActors)
    {
        if (!PlanktonActor)
        {
            continue;
        }

        const float DistanceSq = FVector::DistSquared2D(
            MyLocation,
            PlanktonActor->GetActorLocation()
        );

        // 探索範囲外は無視
        if (DistanceSq > SearchRadiusSq)
        {
            continue;
        }

        if (DistanceSq < NearestDistanceSq)
        {
            NearestDistanceSq = DistanceSq;
            NearestPlankton = PlanktonActor;
        }
    }

    return NearestPlankton;
}

/**
 * TargetPlanktonActorがまだ有効か確認する。
 *
 * 狙っていたプランクトンが他の魚に食べられて消えた場合、
 * falseになる想定。
 */
bool UEnemyAIBrainComponent::IsTargetPlanktonValid() const
{
    return IsValid(TargetPlanktonActor);
}

/**
 * TargetPlanktonActorに十分近いか確認する。
 *
 * 実際の取得はOverlap側で行われる想定。
 * この関数は現在は保険・今後の拡張用。
 */
bool UEnemyAIBrainComponent::IsNearTargetPlankton() const
{
    if (!OwnerPawn || !IsTargetPlanktonValid())
    {
        return false;
    }

    const float Distance = FVector::Dist2D(
        OwnerPawn->GetActorLocation(),
        TargetPlanktonActor->GetActorLocation()
    );

    return Distance <= PlanktonReachedDistance;
}

/**
 * 周囲のプランクトンから一番近いものを選んでTargetPlanktonActorに入れる。
 */
bool UEnemyAIBrainComponent::SelectNearestPlankton()
{
    TargetPlanktonActor = FindNearestPlankton();

    if (!TargetPlanktonActor)
    {
        return false;
    }

    TargetLocation = TargetPlanktonActor->GetActorLocation();
    return true;
}

/**
 * 食べた数に応じてGoToHookへ移行するか判定する。
 */
bool UEnemyAIBrainComponent::TryGoToHookByPlanktonCount()
{
    const float Chance = GetGoToHookChanceByPlanktonCount();

    if (Chance <= 0.0f)
    {
        return false;
    }

    return TryChance(Chance);
}

/**
 * 食べた数からGoToHook確率を返す。
 *
 * 仕様：
 * - 40個以上：100%
 * - 30個以上：50%
 * - 20個以上：25%
 * - 10個以上：12.5%
 * - それ未満：0%
 */
float UEnemyAIBrainComponent::GetGoToHookChanceByPlanktonCount() const
{
    if (EatenPlanktonCount >= 40)
    {
        return 1.0f;
    }

    if (EatenPlanktonCount >= 30)
    {
        return 0.5f;
    }

    if (EatenPlanktonCount >= 20)
    {
        return 0.25f;
    }

    if (EatenPlanktonCount >= 10)
    {
        return 0.125f;
    }

    return 0.0f;
}

// =========================
// Aルール関数
// =========================

/**
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

// =========================
// 汎用関数
// =========================

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