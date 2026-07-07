#include "EnemyAIBrainComponent.h"

#include "AIController.h"
#include "EnemyAIAttackComponent.h"
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
 * - Wander中、3セル以内にFishタグActorがいたらAttackへ移行
 * - BeginPlay直後、一定時間Attackへ移行しない
 * - リスポーン後 / ゴール後にBPからAttackLockを付与できる
 * - SafeZoneタグ持ちActorを攻撃対象から除外
 * - SpawnMoveモード：プランクトン群れ中央へ移動
 * - Devourモード：周囲のプランクトンを探して向かう
 * - Attackモード：対象へ接近し、EnemyAIAttackComponentへ突進攻撃を命令する
 * - 攻撃ヒット後、自身HP80%以上ならAttack継続
 * - 攻撃ヒット後、自身HP80%未満ならWanderへ戻る
 * - 被弾時、HP割合と確率でFleeへ移行
 * - Fleeモード：攻撃してきた相手からAルールで逃げる
 * - Flee中、12セルほど離れたらWanderへ戻る
 * - Aルール移動：目的地へ向かいつつ、Fishタグ持ちActorから距離を取る
 *
 * 注意：
 * - AI判断はサーバー側のみで行う
 * - 実際の通常移動命令はAIControllerのMoveToLocationで行う
 * - 突進攻撃はEnemyAIAttackComponentが担当する
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

    // AI専用攻撃コンポーネントを取得する
    // BP_EnemyにEnemyAIAttackComponentを追加しておく必要がある
    AIAttackComponent = GetOwner()
        ? GetOwner()->FindComponentByClass<UEnemyAIAttackComponent>()
        : nullptr;

    // 最初の判断タイミングを少しランダムにずらす
    // 14体が同時に判断して一斉に動くのを避けるため
    SetNextDecisionTime(0.1f, 0.5f);

    // ゲーム開始直後の攻撃合戦防止
    // 例：開始から5秒間はAttackへ移行しない
    LockAttackForSeconds(InitialAttackLockDuration);
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

    // BeginPlay時にAIAttackComponentがまだ取れていなかった場合の保険
    if (!AIAttackComponent)
    {
        AIAttackComponent = GetOwner()
            ? GetOwner()->FindComponentByClass<UEnemyAIAttackComponent>()
            : nullptr;
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

    case EEnemyAIMode::Attack:
        TickAttack(DeltaTime);
        break;

    case EEnemyAIMode::Wander:
        TickWander(DeltaTime);
        break;

    case EEnemyAIMode::Flee:
        TickFlee(DeltaTime);
        break;

    case EEnemyAIMode::GoToHook:
        // TODO: GoToHookモード実装後に TickGoToHook を呼ぶ
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
 * 重要：
 * - AttackLock中にAttackへ入ろうとした場合、Wanderへ変換する。
 * - これにより、どこからSetAIMode(Attack)されても攻撃禁止を守れる。
 */
void UEnemyAIBrainComponent::SetAIMode(EEnemyAIMode NewMode)
{
    // 攻撃禁止時間中はAttackへ入らない
    if (NewMode == EEnemyAIMode::Attack && !CanEnterAttackMode())
    {
        NewMode = EEnemyAIMode::Wander;
    }

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
        TargetFishActor = nullptr;
        break;

    case EEnemyAIMode::SpawnMove:
        TargetPlanktonActor = nullptr;
        TargetFishActor = nullptr;
        break;

    case EEnemyAIMode::Devour:
        TargetFishActor = nullptr;

        // Devourに入ったら、まず狙うプランクトンを探す
        SelectNearestPlankton();
        break;

    case EEnemyAIMode::Attack:
        TargetPlanktonActor = nullptr;

        // Attackに入った時、強いAI/弱いAIに応じた魚種変更を後で行う
        TryChangeFishOnAttackMode();

        // Attackに入ったら、まず攻撃対象を探す
        // ただしNotifyAttackHitやWander検知でTargetFishActorがすでに入っている場合は維持する
        if (!IsTargetFishValid())
        {
            SelectNearestAttackTarget();
        }
        break;

    case EEnemyAIMode::Flee:
        TargetPlanktonActor = nullptr;

        // Fleeでは、基本的にLastDamageCauserから逃げる。
        // LastDamageCauserが無効ならTargetFishActorを逃走元の保険として使う。
        // そのため、ここではTargetFishActorを消さない。
        if (OwnerAIController)
        {
            OwnerAIController->StopMovement();
        }

        SetNextDecisionTime(0.05f, 0.15f);
        break;

    case EEnemyAIMode::GoToHook:
        TargetPlanktonActor = nullptr;
        TargetFishActor = nullptr;

        // TODO: GoToHook実装時に釣り糸位置をTargetLocationへ入れる
        if (OwnerAIController)
        {
            OwnerAIController->StopMovement();
        }
        break;

    default:
        break;
    }
}

/**
 * BP_Enemy側から、GameSystemで取得したプランクトン群れActorを渡す。
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
    // ただしAttackLock中ならSetAIMode側でWanderへ変換される
    if (TryChance(DevourAttackChance))
    {
        SetAIMode(EEnemyAIMode::Attack);
        return;
    }

    // 食べた数によってGoToHookへ移行するか判定
    // GoToHook未実装中に止まる場合は、ここを一時的にコメントアウトしてOK
    if (TryGoToHookByPlanktonCount())
    {
        SetAIMode(EEnemyAIMode::GoToHook);
        return;
    }

    // SpawnMove中にプランクトンを拾った場合は、Devourへ入る
    if (CurrentMode == EEnemyAIMode::SpawnMove)
    {
        SetAIMode(EEnemyAIMode::Devour);
    }
}

/**
 * 自分が攻撃を食らった時にBP側から呼ぶ。
 *
 * AttackLock中：
 * - 反撃Attackを抑制する
 * - 攻撃合戦の再発を防ぐ
 *
 * 通常時：
 * - HP20%未満なら70%でFlee
 * - HP20%〜79%なら20%でFlee
 * - HP80%以上ならAttack継続
 */
void UEnemyAIBrainComponent::NotifyDamaged(float CurrentHP, float MaxHP, AActor* DamageCauser)
{
    AActor* OwnerActor = GetOwner();

    if (!OwnerActor || !OwnerActor->HasAuthority())
    {
        return;
    }

    // 直前に自分へダメージを与えた相手を保存
    // Fleeでは基本的にこのActorから逃げる
    LastDamageCauser = DamageCauser;

    // 攻撃禁止時間中は反撃Attackへ入らない
    // ここで即反撃すると、開始直後やリスポーン直後の攻撃合戦が再発する
    if (IsAttackLocked() && bDisableAttackReactionDuringAttackLock)
    {
        // 攻撃中だった場合は止める
        if (AIAttackComponent && AIAttackComponent->IsAttacking())
        {
            AIAttackComponent->CancelDashAttack();
        }

        SetAIMode(EEnemyAIMode::Wander);
        return;
    }

    if (MaxHP <= KINDA_SMALL_NUMBER)
    {
        return;
    }

    const float HpRate = CurrentHP / MaxHP;

    // HP20%未満：70%でFlee
    if (HpRate < LowHpFleeThreshold)
    {
        if (TryChance(LowHpFleeChance))
        {
            SetAIMode(EEnemyAIMode::Flee);
            return;
        }

        // 逃走しなかったらAttack継続
        if (IsValidAttackCandidate(DamageCauser))
        {
            TargetFishActor = DamageCauser;
        }

        SetAIMode(EEnemyAIMode::Attack);
        return;
    }

    // HP20%〜79%：20%でFlee
    if (HpRate < MidHpFleeThreshold)
    {
        if (TryChance(MidHpFleeChance))
        {
            SetAIMode(EEnemyAIMode::Flee);
            return;
        }

        // 逃走しなかったらAttack継続
        if (IsValidAttackCandidate(DamageCauser))
        {
            TargetFishActor = DamageCauser;
        }

        SetAIMode(EEnemyAIMode::Attack);
        return;
    }

    // HP80%以上：Attack継続
    if (IsValidAttackCandidate(DamageCauser))
    {
        TargetFishActor = DamageCauser;
    }

    SetAIMode(EEnemyAIMode::Attack);
}

/**
 * 自分の攻撃が相手に当たった時にBP側から呼ぶ。
 *
 * 処理：
 * - 自身HP割合を計算
 * - HP80%以上ならAttack継続
 * - HP80%未満ならWanderへ戻る
 */
void UEnemyAIBrainComponent::NotifyAttackHit(
    AActor* HitActor,
    float CurrentHP,
    float MaxHP,
    bool bHitActorBecameFlee
)
{
    AActor* OwnerActor = GetOwner();

    if (!OwnerActor || !OwnerActor->HasAuthority())
    {
        return;
    }

    // 最大HPが0以下なら割合計算できないので、安全のためWanderへ戻る
    if (MaxHP <= KINDA_SMALL_NUMBER)
    {
        TargetFishActor = nullptr;
        SetAIMode(EEnemyAIMode::Wander);
        return;
    }

    const float HpRate = CurrentHP / MaxHP;

    // =========================
    // 自身HP80%以上ならAttack継続
    // =========================
    if (HpRate >= AttackContinueHpRate)
    {
        // 当てた相手を次の攻撃対象として保持する
        // SafeZone中のActorは攻撃対象にしない
        if (IsValidAttackCandidate(HitActor))
        {
            TargetFishActor = HitActor;
            TargetLocation = HitActor->GetActorLocation();
        }
        else
        {
            TargetFishActor = nullptr;
        }

        // 攻撃継続時に少し待つ
        // 連続で即突進しすぎる場合の調整用
        SetNextDecisionTime(AttackContinueDelayMin, AttackContinueDelayMax);

        // AttackLock中ならSetAIMode側でWanderへ変換される
        SetAIMode(EEnemyAIMode::Attack);
        return;
    }

    // =========================
    // 今後追加予定：
    // 相手がAIで、相手がFleeへ移行した場合の追撃処理
    // =========================
    if (HitActor && bHitActorBecameFlee)
    {
        if (TryChance(ChaseFleeTargetChance))
        {
            if (IsValidAttackCandidate(HitActor))
            {
                TargetFishActor = HitActor;
                TargetLocation = HitActor->GetActorLocation();

                ChaseEndTime = GetWorld()
                    ? GetWorld()->GetTimeSeconds() + FMath::FRandRange(ChaseFleeTimeMin, ChaseFleeTimeMax)
                    : 0.0f;

                SetNextDecisionTime(AttackContinueDelayMin, AttackContinueDelayMax);
                SetAIMode(EEnemyAIMode::Attack);
                return;
            }
        }
    }

    // =========================
    // それ以外はWanderへ戻る
    // =========================
    TargetFishActor = nullptr;
    SetAIMode(EEnemyAIMode::Wander);
}

/**
 * 指定秒数、Attackへ移行しないようにする。
 *
 * 使用例：
 * - BeginPlay
 * - リスポーン後
 * - ゴール後
 * - SafeZoneから出た直後
 */
void UEnemyAIBrainComponent::LockAttackForSeconds(float Duration)
{
    if (!GetWorld())
    {
        return;
    }

    if (Duration <= 0.0f)
    {
        return;
    }

    const float NewEndTime = GetWorld()->GetTimeSeconds() + Duration;

    // すでに長い攻撃禁止時間がある場合、短い時間で上書きしない
    AttackLockEndTime = FMath::Max(AttackLockEndTime, NewEndTime);

    // 攻撃中に保護が入った場合は、攻撃を止めてWanderへ戻す
    if (CurrentMode == EEnemyAIMode::Attack)
    {
        if (AIAttackComponent && AIAttackComponent->IsAttacking())
        {
            AIAttackComponent->CancelDashAttack();
        }

        TargetFishActor = nullptr;
        SetAIMode(EEnemyAIMode::Wander);
    }
}

/**
 * 現在Attack禁止中かどうか。
 */
bool UEnemyAIBrainComponent::IsAttackLocked() const
{
    if (!GetWorld())
    {
        return false;
    }

    return GetWorld()->GetTimeSeconds() < AttackLockEndTime;
}

// =========================
// モード別Tick
// =========================

/**
 * Wanderモード。
 *
 * 追加仕様：
 * - Wander中、3セル以内にFishタグActorがいたらAttackへ移行する。
 * - ただしAttackLock中は検知しない。
 * - SafeZoneタグ持ちActorは検知対象から除外する。
 */
void UEnemyAIBrainComponent::TickWander(float DeltaTime)
{
    if (!ShouldMakeDecision())
    {
        return;
    }

    // Wander中の近距離敵検知。
    // AttackLock中は攻撃合戦を避けるため検知しない。
    if (!IsAttackLocked() || !bDisableWanderDetectDuringAttackLock)
    {
        if (TryDetectEnemyInWander())
        {
            return;
        }
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
        SetNextDecisionTime(0.5f, 1.0f);
    }
}

/**
 * SpawnMoveモード。
 */
void UEnemyAIBrainComponent::TickSpawnMove(float DeltaTime)
{
    if (!ShouldMakeDecision())
    {
        return;
    }

    // 群れActorが有効なら、群れ中央位置を更新する
    if (TargetGroupActor)
    {
        TargetLocation = TargetGroupActor->GetActorLocation();
    }

    // SpawnMove中、3%でAttackへ移行
    // AttackLock中ならSetAIMode側でWanderへ変換される
    if (TryChance(SpawnMoveAttackChance))
    {
        SetAIMode(EEnemyAIMode::Attack);
        return;
    }

    // 群れ中央にだいたい近づいたらDevourへ移行
    if (IsNearTargetLocation())
    {
        SetAIMode(EEnemyAIMode::Devour);
        return;
    }

    const bool bMoved = MoveByARuleToLocation(TargetLocation);

    if (bMoved)
    {
        SetNextDecisionTime(0.3f, 0.6f);
    }
    else
    {
        SetNextDecisionTime(0.5f, 1.0f);
    }
}

/**
 * Devourモード。
 */
void UEnemyAIBrainComponent::TickDevour(float DeltaTime)
{
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
            // AttackLock中ならSetAIMode側でWanderへ変換される
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

    TargetLocation = TargetPlanktonActor->GetActorLocation();

    const bool bMoved = MoveByARuleToLocation(TargetLocation);

    if (bMoved)
    {
        SetNextDecisionTime(0.15f, 0.35f);
    }
    else
    {
        // 移動できないなら別のプランクトンを探す
        TargetPlanktonActor = nullptr;
        SetNextDecisionTime(0.1f, 0.2f);
    }
}

/**
 * Attackモード。
 */
void UEnemyAIBrainComponent::TickAttack(float DeltaTime)
{
    // 念のため、AttackLock中にAttackへ残っていたらWanderへ戻す
    if (!CanEnterAttackMode())
    {
        if (AIAttackComponent && AIAttackComponent->IsAttacking())
        {
            AIAttackComponent->CancelDashAttack();
        }

        TargetFishActor = nullptr;
        SetAIMode(EEnemyAIMode::Wander);
        return;
    }

    // 攻撃コンポーネントが攻撃中なら、Brain側は何もしない
    // ここでMoveToを出すと、突進方向が崩れる
    if (AIAttackComponent && AIAttackComponent->IsAttacking())
    {
        return;
    }

    if (!ShouldMakeDecision())
    {
        return;
    }

    // 攻撃対象がいなければ探す
    if (!IsTargetFishValid())
    {
        const bool bFound = SelectNearestAttackTarget();

        if (!bFound)
        {
            SetAIMode(EEnemyAIMode::Wander);
            return;
        }
    }

    // ここでもう一度確認
    if (!IsTargetFishValid())
    {
        SetAIMode(EEnemyAIMode::Wander);
        return;
    }

    // 対象の現在位置を目的地にする
    TargetLocation = TargetFishActor->GetActorLocation();

    // 攻撃可能距離に入ったら、AI専用ダッシュ攻撃を開始する
    if (IsNearAttackTarget())
    {
        if (OwnerAIController)
        {
            OwnerAIController->StopMovement();
        }

        if (AIAttackComponent)
        {
            const bool bStarted = AIAttackComponent->StartDashAttack(TargetFishActor);

            if (bStarted)
            {
                // 攻撃開始できたら、攻撃コンポーネント側がチャージ/突進を担当する
                // Brain側はしばらく再判断を遅らせる
                SetNextDecisionTime(1.0f, 1.2f);
                return;
            }
        }

        // AIAttackComponentが無い、または攻撃開始に失敗した場合だけ仮終了
        FinishAttackTemporarily();
        return;
    }

    const bool bMoved = MoveByARuleToLocation(TargetLocation);

    if (bMoved)
    {
        SetNextDecisionTime(AttackThinkIntervalMin, AttackThinkIntervalMax);
    }
    else
    {
        // 移動できない場合は対象を取り直す
        TargetFishActor = nullptr;
        SetNextDecisionTime(0.2f, 0.4f);
    }
}

/**
 * Fleeモード。
 */
void UEnemyAIBrainComponent::TickFlee(float DeltaTime)
{
    if (!ShouldMakeDecision())
    {
        return;
    }

    AActor* FleeSourceActor = GetFleeSourceActor();

    // 逃走元がいないなら逃げる理由がないのでWanderへ戻る
    if (!FleeSourceActor)
    {
        SetAIMode(EEnemyAIMode::Wander);
        return;
    }

    // 12セルほど離れたら逃走完了
    if (IsFarEnoughFromFleeSource())
    {
        LastDamageCauser = nullptr;
        SetAIMode(EEnemyAIMode::Wander);
        return;
    }

    // まだ近いなら、相手から遠ざかるようにAルール移動する
    const bool bMoved = MoveByARuleAwayFromActor(FleeSourceActor);

    if (bMoved)
    {
        SetNextDecisionTime(FleeThinkIntervalMin, FleeThinkIntervalMax);
    }
    else
    {
        // 移動先が取れなかった場合も、少し待って再試行する
        SetNextDecisionTime(0.3f, 0.6f);
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

    const bool bFound = NavSys->GetRandomReachablePointInRadius(
        OwnerPawn->GetActorLocation(),
        WanderRadius,
        RandomLocation
    );

    if (!bFound)
    {
        return false;
    }

    return MoveByARuleToLocation(RandomLocation.Location);
}

/**
 * Wander中、3セル以内に攻撃対象がいるか確認する。
 */
bool UEnemyAIBrainComponent::TryDetectEnemyInWander()
{
    AActor* EnemyActor = FindNearestWanderEnemy();

    if (!EnemyActor)
    {
        return false;
    }

    TargetFishActor = EnemyActor;
    TargetLocation = EnemyActor->GetActorLocation();

    SetAIMode(EEnemyAIMode::Attack);
    return true;
}

/**
 * Wander中に反応する近距離魚を探す。
 *
 * SafeZoneタグ持ちActorは除外する。
 */
AActor* UEnemyAIBrainComponent::FindNearestWanderEnemy() const
{
    if (!OwnerPawn)
    {
        return nullptr;
    }

    TArray<AActor*> FishActors;
    UGameplayStatics::GetAllActorsWithTag(GetWorld(), FishTagName, FishActors);

    const FVector MyLocation = OwnerPawn->GetActorLocation();

    AActor* NearestEnemy = nullptr;
    float NearestDistanceSq = TNumericLimits<float>::Max();

    const float DetectDistance = WanderEnemyDetectCells * CellSize;
    const float DetectDistanceSq = DetectDistance * DetectDistance;

    for (AActor* FishActor : FishActors)
    {
        if (!IsValidAttackCandidate(FishActor))
        {
            continue;
        }

        const float DistanceSq = FVector::DistSquared2D(
            MyLocation,
            FishActor->GetActorLocation()
        );

        if (DistanceSq > DetectDistanceSq)
        {
            continue;
        }

        if (DistanceSq < NearestDistanceSq)
        {
            NearestDistanceSq = DistanceSq;
            NearestEnemy = FishActor;
        }
    }

    return NearestEnemy;
}

// =========================
// SpawnMove / Target用関数
// =========================

/**
 * TargetLocationに十分近いか判定する。
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

// =========================
// Devour用関数
// =========================

/**
 * 周囲のプランクトンから一番近いものを探す。
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
 */
bool UEnemyAIBrainComponent::IsTargetPlanktonValid() const
{
    return IsValid(TargetPlanktonActor);
}

/**
 * TargetPlanktonActorに十分近いか確認する。
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
// Attack用関数
// =========================

/**
 * 周囲のFishタグ持ちActorから一番近い攻撃対象を探す。
 *
 * SafeZoneタグ持ちActorは除外する。
 */
AActor* UEnemyAIBrainComponent::FindNearestAttackTarget() const
{
    if (!OwnerPawn)
    {
        return nullptr;
    }

    TArray<AActor*> FishActors;
    UGameplayStatics::GetAllActorsWithTag(GetWorld(), FishTagName, FishActors);

    const FVector MyLocation = OwnerPawn->GetActorLocation();

    AActor* NearestTarget = nullptr;
    float NearestDistanceSq = TNumericLimits<float>::Max();

    const float SearchRadiusSq = AttackSearchRadius * AttackSearchRadius;

    for (AActor* FishActor : FishActors)
    {
        if (!IsValidAttackCandidate(FishActor))
        {
            continue;
        }

        const float DistanceSq = FVector::DistSquared2D(
            MyLocation,
            FishActor->GetActorLocation()
        );

        if (DistanceSq > SearchRadiusSq)
        {
            continue;
        }

        if (DistanceSq < NearestDistanceSq)
        {
            NearestDistanceSq = DistanceSq;
            NearestTarget = FishActor;
        }
    }

    return NearestTarget;
}

/**
 * TargetFishActorが有効か確認する。
 *
 * SafeZoneタグ持ちになったActorは無効扱いにする。
 */
bool UEnemyAIBrainComponent::IsTargetFishValid() const
{
    return IsValidAttackCandidate(TargetFishActor);
}

/**
 * 周囲の魚から一番近い攻撃対象を選ぶ。
 */
bool UEnemyAIBrainComponent::SelectNearestAttackTarget()
{
    TargetFishActor = FindNearestAttackTarget();

    if (!TargetFishActor)
    {
        return false;
    }

    TargetLocation = TargetFishActor->GetActorLocation();
    return true;
}

/**
 * 攻撃可能距離まで近づいているか確認する。
 */
bool UEnemyAIBrainComponent::IsNearAttackTarget() const
{
    if (!OwnerPawn || !IsTargetFishValid())
    {
        return false;
    }

    const float Distance = FVector::Dist2D(
        OwnerPawn->GetActorLocation(),
        TargetFishActor->GetActorLocation()
    );

    return Distance <= AttackRange;
}

/**
 * Attackの仮終了処理。
 */
void UEnemyAIBrainComponent::FinishAttackTemporarily()
{
    if (OwnerAIController)
    {
        OwnerAIController->StopMovement();
    }

    TargetFishActor = nullptr;

    SetAIMode(EEnemyAIMode::Wander);
}

/**
 * Attackモードに入った時に魚種変更する予定の関数。
 */
void UEnemyAIBrainComponent::TryChangeFishOnAttackMode()
{
    const float Chance =
        (AILevel == EEnemyAILevel::Strong)
        ? StrongFishChangeChance
        : WeakFishChangeChance;

    if (!TryChance(Chance))
    {
        return;
    }

    // TODO:
    // ここでBP_PlayerBase側の魚種変更関数を呼ぶ。
    // 例：
    // - 強いAIなら高性能魚種へ変更
    // - 弱いAIなら低確率で変更
    //
    // 今はまだ未実装なので何もしない。
}

/**
 * 攻撃対象として有効か確認する。
 *
 * falseになる条件：
 * - nullptr
 * - 自分自身
 * - SafeZoneタグ持ちActor
 */
bool UEnemyAIBrainComponent::IsValidAttackCandidate(AActor* CandidateActor) const
{
    if (!CandidateActor)
    {
        return false;
    }

    if (CandidateActor == OwnerPawn)
    {
        return false;
    }

    if (bIgnoreActorsInSafeZone && CandidateActor->ActorHasTag(SafeZoneActorTagName))
    {
        return false;
    }

    return true;
}

// =========================
// Flee用関数
// =========================

/**
 * 逃走元Actorを取得する。
 */
AActor* UEnemyAIBrainComponent::GetFleeSourceActor() const
{
    if (IsValid(LastDamageCauser) && LastDamageCauser != OwnerPawn)
    {
        return LastDamageCauser;
    }

    if (IsValid(TargetFishActor) && TargetFishActor != OwnerPawn)
    {
        return TargetFishActor;
    }

    return nullptr;
}

/**
 * 逃走元から十分離れたか確認する。
 */
bool UEnemyAIBrainComponent::IsFarEnoughFromFleeSource() const
{
    if (!OwnerPawn)
    {
        return true;
    }

    AActor* FleeSourceActor = GetFleeSourceActor();

    if (!FleeSourceActor)
    {
        return true;
    }

    const float EndDistance = FleeEndDistanceCells * CellSize;

    const float Distance = FVector::Dist2D(
        OwnerPawn->GetActorLocation(),
        FleeSourceActor->GetActorLocation()
    );

    return Distance >= EndDistance;
}

/**
 * 指定Actorから遠ざかるようにAルール移動する。
 */
bool UEnemyAIBrainComponent::MoveByARuleAwayFromActor(AActor* SourceActor)
{
    if (!OwnerPawn || !SourceActor)
    {
        return false;
    }

    FVector AwayDirection = OwnerPawn->GetActorLocation() - SourceActor->GetActorLocation();
    AwayDirection.Z = 0.0f;

    // ほぼ同じ位置にいる場合、離れる方向が作れないのでランダム方向を使う
    if (AwayDirection.IsNearlyZero())
    {
        AwayDirection = FVector(
            FMath::FRandRange(-1.0f, 1.0f),
            FMath::FRandRange(-1.0f, 1.0f),
            0.0f
        );
    }

    AwayDirection = AwayDirection.GetSafeNormal();

    // 逃走用の仮目的地
    const FVector FleeGoalLocation =
        OwnerPawn->GetActorLocation()
        + AwayDirection * ARuleStepDistance * FleeAwayWeight;

    TargetLocation = FleeGoalLocation;

    return MoveByARuleToLocation(FleeGoalLocation);
}

// =========================
// AttackLock用関数
// =========================

/**
 * Attackへ移行してよいか確認する。
 */
bool UEnemyAIBrainComponent::CanEnterAttackMode() const
{
    return !IsAttackLocked();
}

// =========================
// Aルール関数
// =========================

/**
 * 近くにいるFishタグ持ちActorから離れる方向を計算する。
 */
FVector UEnemyAIBrainComponent::CalculateAvoidDirection() const
{
    if (!OwnerPawn)
    {
        return FVector::ZeroVector;
    }

    TArray<AActor*> FishActors;
    UGameplayStatics::GetAllActorsWithTag(GetWorld(), FishTagName, FishActors);

    const FVector MyLocation = OwnerPawn->GetActorLocation();

    FVector AvoidDirection = FVector::ZeroVector;

    const float MinSeparationDistance = MinSeparationCells * CellSize;

    for (AActor* OtherActor : FishActors)
    {
        if (!OtherActor || OtherActor == OwnerPawn)
        {
            continue;
        }

        const FVector OtherLocation = OtherActor->GetActorLocation();

        const float Distance = FVector::Dist2D(MyLocation, OtherLocation);

        if (Distance <= KINDA_SMALL_NUMBER)
        {
            continue;
        }

        if (Distance < MinSeparationDistance)
        {
            FVector FromOtherToMe = MyLocation - OtherLocation;
            FromOtherToMe.Z = 0.0f;
            FromOtherToMe.Normalize();

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

    FVector GoalDirection = GoalLocation - MyLocation;
    GoalDirection.Z = 0.0f;
    GoalDirection = GoalDirection.GetSafeNormal();

    const FVector AvoidDirection = CalculateAvoidDirection();

    FVector RandomDirection = FVector(
        FMath::FRandRange(-1.0f, 1.0f),
        FMath::FRandRange(-1.0f, 1.0f),
        0.0f
    ).GetSafeNormal();

    FVector FinalDirection =
        GoalDirection * GoalWeight
        + AvoidDirection * AvoidWeight
        + RandomDirection * RandomWeight;

    FinalDirection.Z = 0.0f;

    if (FinalDirection.IsNearlyZero())
    {
        FinalDirection = GoalDirection;
    }

    FinalDirection = FinalDirection.GetSafeNormal();

    FVector CandidateLocation = MyLocation + FinalDirection * ARuleStepDistance;

    const float DistanceToGoal = FVector::Dist2D(MyLocation, GoalLocation);

    if (DistanceToGoal < ARuleStepDistance)
    {
        CandidateLocation = GoalLocation;
    }

    return CandidateLocation;
}

/**
 * Aルール移動。
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

    const FVector CandidateLocation = CalculateARuleMoveLocation(GoalLocation);

    FNavLocation ProjectedLocation;

    const bool bProjected = NavSys->ProjectPointToNavigation(
        CandidateLocation,
        ProjectedLocation
    );

    if (!bProjected)
    {
        return false;
    }

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
 * 確率判定。
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