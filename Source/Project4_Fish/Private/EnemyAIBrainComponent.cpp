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
 *
 * =========================================================
 * 現在のAIモード
 * =========================================================
 *
 * - Wander
 *   自由遊泳。
 *
 *   通常時は近距離のFishタグActorを検知すると
 *   Attackへ移行する。
 *
 *   一定数以上のプランクトンを持っている場合は
 *   GoToHookへ移行する。
 *
 *   GoToHookへ行きたいが、
 *   現在アクティブなGoal Pointが存在しない場合は、
 *
 *   bWaitingForHook = true
 *
 *   の状態で一時的にWanderへ戻る。
 *
 *   その後、
 *   HookSearchIntervalMin ～ HookSearchIntervalMax
 *   の間隔でBP側へ検索要求を送る。
 *
 *
 * - SpawnMove
 *   プランクトン群れ中央へAルールで向かう。
 *
 *
 * - Devour
 *   周囲のプランクトンを探して貪る。
 *
 *
 * - Attack
 *   攻撃対象へ接近し、
 *   EnemyAIAttackComponentへ突進攻撃を命令する。
 *
 *
 * - Flee
 *   直前に自分を攻撃した相手からAルールで逃げる。
 *
 *
 * - GoToHook
 *   GameSystemの
 *   Get Active Nearby Goal Point
 *   が返した現在アクティブなGoal Pointへ
 *   Aルールで向かう。
 *
 *
 * =========================================================
 * アクティブGoal Point取得方法
 * =========================================================
 *
 * C++側
 * ↓
 * OnHookSearchRequested.Broadcast()
 * ↓
 * BP_Enemy
 * ↓
 * GameSystem
 * ↓
 * Get Active Nearby Goal Point
 * ↓
 * 有効Actor取得
 * ↓
 * SetHookTarget(Actor)
 * ↓
 * C++側でTargetHookActorへ保存
 *
 *
 * =========================================================
 * GoToHook中のAttack
 * =========================================================
 *
 * 周囲に魚が接近した場合のみ、
 * Attack移行判定を行う。
 *
 * Goal Pointから12セル以上：
 * → 最大30%
 *
 * 12～4セル：
 * → 近づくほど30%から0%へ低下
 *
 * 4セル以内：
 * → 0%
 *
 *
 * GoToHook
 * ↓
 * Attack
 * ↓
 * Attack終了
 * ↓
 * GoToHookへ復帰
 *
 *
 * =========================================================
 * 注意
 * =========================================================
 *
 * - AI判断はサーバー側のみ行う。
 *
 * - 通常移動はAIControllerのMoveToLocationを使用する。
 *
 * - 突進攻撃はEnemyAIAttackComponentが担当する。
 *
 * - GameSystemのBP専用関数はC++から直接呼ばない。
 *
 * - OnHookSearchRequestedをBP_Enemy側でBindする必要がある。
 */


// =========================================================
// Constructor
// =========================================================

UEnemyAIBrainComponent::UEnemyAIBrainComponent()
{
    // TickComponentを使用する。
    PrimaryComponentTick.bCanEverTick = true;
}


// =========================================================
// BeginPlay
// =========================================================

void UEnemyAIBrainComponent::BeginPlay()
{
    Super::BeginPlay();


    // このComponentを持つPawnを取得する。
    // 基本的にはBP_Enemy。
    OwnerPawn = Cast<APawn>(GetOwner());


    // AIControllerを取得する。
    if (OwnerPawn)
    {
        OwnerAIController =
            Cast<AAIController>(OwnerPawn->GetController());
    }


    // AI専用AttackComponentを取得する。
    AIAttackComponent = GetOwner()
        ? GetOwner()->FindComponentByClass<UEnemyAIAttackComponent>()
        : nullptr;


    // 全AIが完全に同時に判断しないように、
    // 初回判断タイミングを少しランダムにずらす。
    SetNextDecisionTime(0.1f, 0.5f);


    // 次回釣り針検索可能時間を初期化する。
    NextHookSearchTime = 0.0f;


    // ゲーム開始直後の攻撃合戦を防止する。
    LockAttackForSeconds(InitialAttackLockDuration);
}


// =========================================================
// TickComponent
// =========================================================

void UEnemyAIBrainComponent::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction
)
{
    Super::TickComponent(
        DeltaTime,
        TickType,
        ThisTickFunction
    );


    AActor* OwnerActor = GetOwner();


    // AI判断はサーバー側だけで行う。
    if (!OwnerActor || !OwnerActor->HasAuthority())
    {
        return;
    }


    // BeginPlay時にPawnを取得できなかった場合の保険。
    if (!OwnerPawn)
    {
        OwnerPawn = Cast<APawn>(GetOwner());
    }


    // BeginPlay時にControllerがまだ無かった場合の保険。
    if (!OwnerAIController && OwnerPawn)
    {
        OwnerAIController =
            Cast<AAIController>(OwnerPawn->GetController());
    }


    // BeginPlay時にAttackComponentが無かった場合の保険。
    if (!AIAttackComponent)
    {
        AIAttackComponent = GetOwner()
            ? GetOwner()->FindComponentByClass<UEnemyAIAttackComponent>()
            : nullptr;
    }


    // 現在のAIモードごとに処理する。
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

        TickGoToHook(DeltaTime);
        break;


    default:

        break;
    }
}


// =========================================================
// BPから呼ぶ公開関数
// =========================================================


/**
 * AIモードを変更する。
 */
void UEnemyAIBrainComponent::SetAIMode(
    EEnemyAIMode NewMode
)
{
    // =====================================================
    // AttackLock確認
    // =====================================================

    if (
        NewMode == EEnemyAIMode::Attack
        && !CanEnterAttackMode()
    )
    {
        /*
         * GoToHookからAttackしようとしていた場合は、
         * Wanderへ飛ばさずGoToHookを継続する。
         */
        if (CurrentMode == EEnemyAIMode::GoToHook)
        {
            NewMode = EEnemyAIMode::GoToHook;
        }
        else
        {
            NewMode = EEnemyAIMode::Wander;
        }
    }


    // 同じモードなら何もしない。
    if (CurrentMode == NewMode)
    {
        return;
    }


    // =====================================================
    // GoToHook → Attackを記録
    // =====================================================

    if (
        CurrentMode == EEnemyAIMode::GoToHook
        && NewMode == EEnemyAIMode::Attack
    )
    {
        /*
         * Attack終了後、
         * WanderではなくGoToHookへ戻る。
         */
        bReturnToGoToHookAfterAttack = true;
    }


    // 実際にモードを変更する。
    CurrentMode = NewMode;


    // モード変更直後は早めに判断する。
    SetNextDecisionTime(0.05f, 0.15f);


    // =====================================================
    // モード別初期化
    // =====================================================

    switch (CurrentMode)
    {
    case EEnemyAIMode::Wander:
    {
        TargetPlanktonActor = nullptr;
        TargetFishActor = nullptr;

        /*
         * bWaitingForHookはここではfalseにしない。
         *
         * 理由：
         *
         * GoToHook
         * ↓
         * Goal Pointなし
         * ↓
         * bWaitingForHook = true
         * ↓
         * Wander
         *
         * という流れで使うため。
         */

        /*
         * 通常のWanderへ戻った場合は、
         * GoToHook復帰フラグを解除する。
         *
         * ただしReturnFromAttack()から
         * GoToHookへ戻る場合はここへ来ない。
         */
        bReturnToGoToHookAfterAttack = false;

        break;
    }


    case EEnemyAIMode::SpawnMove:
    {
        TargetPlanktonActor = nullptr;
        TargetFishActor = nullptr;

        break;
    }


    case EEnemyAIMode::Devour:
    {
        TargetFishActor = nullptr;

        // Devourへ入ったら、
        // 最初に狙うプランクトンを選ぶ。
        SelectNearestPlankton();

        break;
    }


    case EEnemyAIMode::Attack:
    {
        TargetPlanktonActor = nullptr;

        // AIレベルに応じた魚種変更を試す。
        TryChangeFishOnAttackMode();

        /*
         * すでに攻撃対象が設定されていれば維持。
         *
         * Wander検知
         * NotifyDamaged
         * GoToHook中Attack判定
         *
         * などから対象が設定されている可能性がある。
         */
        if (!IsTargetFishValid())
        {
            SelectNearestAttackTarget();
        }

        break;
    }


    case EEnemyAIMode::Flee:
    {
        TargetPlanktonActor = nullptr;

        /*
         * すでに攻撃中なら、
         * 逃走開始時に突進攻撃を止める。
         */
        if (
            AIAttackComponent
            && AIAttackComponent->IsAttacking()
        )
        {
            AIAttackComponent->CancelDashAttack();
        }


        /*
         * FleeではTargetFishActorを消さない。
         *
         * LastDamageCauserが無効な場合、
         * TargetFishActorを逃走元候補として使うため。
         */
        if (OwnerAIController)
        {
            OwnerAIController->StopMovement();
        }


        SetNextDecisionTime(0.05f, 0.15f);

        break;
    }


    case EEnemyAIMode::GoToHook:
    {
        TargetPlanktonActor = nullptr;
        TargetFishActor = nullptr;

        // Attackから正常復帰したのでフラグ解除。
        bReturnToGoToHookAfterAttack = false;

        // GoToHookへ入ったので、
        // 一旦待機状態を解除する。
        bWaitingForHook = false;


        // すでに有効なGoal Pointがあるなら、
        // その位置を目的地へ設定する。
        if (IsHookTargetValid())
        {
            TargetLocation =
                TargetHookActor->GetActorLocation();
        }
        else
        {
            /*
             * 現在有効なGoal Pointが無いため、
             * BP側へ検索要求を送る。
             *
             * Broadcastは同期的なので、
             * BP側でGoal Pointが見つかって
             * SetHookTarget()が呼ばれれば、
             * この時点でTargetHookActorが設定される可能性がある。
             */
            RequestActiveHookSearch();
        }

        break;
    }


    default:
        break;
    }
}


/**
 * プランクトン群れActorを設定する。
 *
 * 通常Wander中のみ受け付ける。
 */
void UEnemyAIBrainComponent::SetCollectItemGroupTarget(
    AActor* NewGroupActor
)
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


    /*
     * 通常Wander中だけ受け付ける。
     *
     * bWaitingForHook == true の場合、
     * 釣り針待ち中なのでSpawnMoveへ移行しない。
     */
    if (
        CurrentMode != EEnemyAIMode::Wander
        || bWaitingForHook
    )
    {
        return;
    }


    TargetGroupActor = NewGroupActor;


    TargetLocation =
        NewGroupActor->GetActorLocation();


    SetAIMode(EEnemyAIMode::SpawnMove);
}


/**
 * BP側のGameSystemから取得した、
 * 現在アクティブなGoal PointをC++へ渡す。
 */
void UEnemyAIBrainComponent::SetHookTarget(
    AActor* NewHookActor
)
{
    AActor* OwnerActor = GetOwner();


    if (!OwnerActor || !OwnerActor->HasAuthority())
    {
        return;
    }


    // 無効Actorは受け付けない。
    if (!IsValid(NewHookActor))
    {
        return;
    }


    // 現在アクティブなGoal Pointを保存する。
    TargetHookActor = NewHookActor;


    // 最新位置を目的地へ設定する。
    TargetLocation =
        TargetHookActor->GetActorLocation();


    // Goal Pointが見つかったため待機終了。
    bWaitingForHook = false;


    /*
     * 現在GoToHookでない場合は、
     * GoToHookへ移行する。
     *
     * 例えば：
     *
     * 釣り針待ちWander
     * ↓
     * BP検索でGoal Point発見
     * ↓
     * SetHookTarget
     * ↓
     * GoToHook
     */
    if (CurrentMode != EEnemyAIMode::GoToHook)
    {
        SetAIMode(EEnemyAIMode::GoToHook);
    }
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


    // 食べた数を加算する。
    EatenPlanktonCount++;


    // =====================================================
    // 強制GoToHook
    // =====================================================

    if (ShouldForceGoToHook())
    {
        SetAIMode(EEnemyAIMode::GoToHook);
        return;
    }


    // =====================================================
    // 通常Attack判定
    // =====================================================

    // デフォルト20%でAttack。
    if (TryChance(DevourAttackChance))
    {
        SetAIMode(EEnemyAIMode::Attack);
        return;
    }


    // =====================================================
    // プランクトン数によるGoToHook抽選
    // =====================================================

    /*
     * 10個以上 → 12.5%
     * 20個以上 → 25%
     * 30個以上 → 50%
     * 40個以上 → 100%
     */
    if (TryGoToHookByPlanktonCount())
    {
        SetAIMode(EEnemyAIMode::GoToHook);
        return;
    }


    // SpawnMove中にプランクトンを拾った場合はDevourへ。
    if (CurrentMode == EEnemyAIMode::SpawnMove)
    {
        SetAIMode(EEnemyAIMode::Devour);
    }
}


/**
 * 自分が攻撃を受けた時に呼ぶ。
 */
void UEnemyAIBrainComponent::NotifyDamaged(
    float CurrentHP,
    float MaxHP,
    AActor* DamageCauser
)
{
    AActor* OwnerActor = GetOwner();


    if (!OwnerActor || !OwnerActor->HasAuthority())
    {
        return;
    }


    // 最後に自分へダメージを与えた相手を保存。
    LastDamageCauser = DamageCauser;


    // =====================================================
    // AttackLock中
    // =====================================================

    if (
        IsAttackLocked()
        && bDisableAttackReactionDuringAttackLock
    )
    {
        // 攻撃中ならキャンセル。
        if (
            AIAttackComponent
            && AIAttackComponent->IsAttacking()
        )
        {
            AIAttackComponent->CancelDashAttack();
        }


        /*
         * GoToHookから始まったAttackなら
         * GoToHookへ戻す。
         */
        if (bReturnToGoToHookAfterAttack)
        {
            ReturnFromAttack();
        }
        else
        {
            SetAIMode(EEnemyAIMode::Wander);
        }


        return;
    }


    if (MaxHP <= KINDA_SMALL_NUMBER)
    {
        return;
    }


    const float HpRate =
        CurrentHP / MaxHP;


    // =====================================================
    // HP20%未満
    // 70%でFlee
    // =====================================================

    if (HpRate < LowHpFleeThreshold)
    {
        if (TryChance(LowHpFleeChance))
        {
            SetAIMode(EEnemyAIMode::Flee);
            return;
        }


        // Fleeしなければ反撃。
        if (IsValidAttackCandidate(DamageCauser))
        {
            TargetFishActor = DamageCauser;
        }


        SetAIMode(EEnemyAIMode::Attack);

        return;
    }


    // =====================================================
    // HP20%～79%
    // 20%でFlee
    // =====================================================

    if (HpRate < MidHpFleeThreshold)
    {
        if (TryChance(MidHpFleeChance))
        {
            SetAIMode(EEnemyAIMode::Flee);
            return;
        }


        if (IsValidAttackCandidate(DamageCauser))
        {
            TargetFishActor = DamageCauser;
        }


        SetAIMode(EEnemyAIMode::Attack);

        return;
    }


    // =====================================================
    // HP80%以上
    // Attack
    // =====================================================

    if (IsValidAttackCandidate(DamageCauser))
    {
        TargetFishActor = DamageCauser;
    }


    SetAIMode(EEnemyAIMode::Attack);
}


/**
 * 自分の攻撃が相手へ当たった時に呼ぶ。
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


    // 最大HPが不正ならAttack終了。
    if (MaxHP <= KINDA_SMALL_NUMBER)
    {
        TargetFishActor = nullptr;

        ReturnFromAttack();

        return;
    }


    const float HpRate =
        CurrentHP / MaxHP;


    // =====================================================
    // 自身HP80%以上
    // Attack継続
    // =====================================================

    if (HpRate >= AttackContinueHpRate)
    {
        if (IsValidAttackCandidate(HitActor))
        {
            TargetFishActor = HitActor;

            TargetLocation =
                HitActor->GetActorLocation();
        }
        else
        {
            TargetFishActor = nullptr;
        }


        SetNextDecisionTime(
            AttackContinueDelayMin,
            AttackContinueDelayMax
        );


        /*
         * すでにAttack中の場合、
         * SetAIModeは同一モードとしてreturnする。
         *
         * ただしTargetFishActorとNextDecisionTimeは
         * 上ですでに更新済み。
         */
        SetAIMode(EEnemyAIMode::Attack);

        return;
    }


    // =====================================================
    // 相手がFleeへ入った場合
    // 50%で追跡
    // =====================================================

    if (HitActor && bHitActorBecameFlee)
    {
        if (TryChance(ChaseFleeTargetChance))
        {
            if (IsValidAttackCandidate(HitActor))
            {
                TargetFishActor = HitActor;


                TargetLocation =
                    HitActor->GetActorLocation();


                ChaseEndTime = GetWorld()
                    ? GetWorld()->GetTimeSeconds()
                        + FMath::FRandRange(
                            ChaseFleeTimeMin,
                            ChaseFleeTimeMax
                        )
                    : 0.0f;


                SetNextDecisionTime(
                    AttackContinueDelayMin,
                    AttackContinueDelayMax
                );


                SetAIMode(EEnemyAIMode::Attack);

                return;
            }
        }
    }


    // =====================================================
    // それ以外
    // Attack終了
    // =====================================================

    TargetFishActor = nullptr;


    ReturnFromAttack();
}


/**
 * 指定秒数Attackを禁止する。
 */
void UEnemyAIBrainComponent::LockAttackForSeconds(
    float Duration
)
{
    if (!GetWorld())
    {
        return;
    }


    if (Duration <= 0.0f)
    {
        return;
    }


    const float NewEndTime =
        GetWorld()->GetTimeSeconds() + Duration;


    // すでに長いAttackLockがある場合、
    // 短い時間で上書きしない。
    AttackLockEndTime =
        FMath::Max(
            AttackLockEndTime,
            NewEndTime
        );


    // Attack中なら攻撃キャンセル。
    if (CurrentMode == EEnemyAIMode::Attack)
    {
        if (
            AIAttackComponent
            && AIAttackComponent->IsAttacking()
        )
        {
            AIAttackComponent->CancelDashAttack();
        }


        TargetFishActor = nullptr;


        ReturnFromAttack();
    }
}


/**
 * 現在AttackLock中か確認する。
 */
bool UEnemyAIBrainComponent::IsAttackLocked() const
{
    if (!GetWorld())
    {
        return false;
    }


    return
        GetWorld()->GetTimeSeconds()
        < AttackLockEndTime;
}


/**
 * 現在、
 * アクティブなGoal Pointを待っているか確認する。
 */
bool UEnemyAIBrainComponent::IsWaitingForHook() const
{
    return bWaitingForHook;
}


// =========================================================
// モード別Tick
// =========================================================


/**
 * Wanderモード。
 *
 * 優先順位：
 *
 * 1.
 * 釣り針待ち中なら定期的にBPへ検索要求
 *
 * 2.
 * 強制GoToHook
 *
 * 3.
 * 近距離Attack判定
 *
 * 4.
 * 通常ランダム遊泳
 */
void UEnemyAIBrainComponent::TickWander(
    float DeltaTime
)
{
    // =====================================================
    // 1. 釣り針待ち中の再検索
    // =====================================================

    /*
     * ここはShouldMakeDecision()より前に置く。
     *
     * 理由：
     *
     * 通常Wander判断間隔と、
     * Goal Point再検索間隔を分離するため。
     */
    if (bWaitingForHook && GetWorld())
    {
        const float CurrentTime =
            GetWorld()->GetTimeSeconds();


        if (CurrentTime >= NextHookSearchTime)
        {
            RequestActiveHookSearch();


            /*
             * Broadcast先のBPでGoal Pointが見つかり、
             * SetHookTarget()が同期的に呼ばれた場合、
             * CurrentModeがGoToHookへ変わっている。
             *
             * その場合はWander処理を続けない。
             */
            if (CurrentMode != EEnemyAIMode::Wander)
            {
                return;
            }
        }
    }


    // 通常AI判断時間でなければ、
    // ここから下は実行しない。
    if (!ShouldMakeDecision())
    {
        return;
    }


    // =====================================================
    // 2. 強制GoToHook
    // =====================================================

    /*
     * bWaitingForHook中に実行すると、
     *
     * Wander
     * ↓
     * GoToHook
     * ↓
     * Goal Pointなし
     * ↓
     * Wander
     *
     * を繰り返すため、
     * 待機中は強制移行しない。
     */
    if (
        !bWaitingForHook
        && ShouldForceGoToHook()
    )
    {
        SetAIMode(EEnemyAIMode::GoToHook);

        return;
    }


    // =====================================================
    // 3. 近距離Attack判定
    // =====================================================

    // Goal Point待ち中はAttackしない。
    if (!bWaitingForHook)
    {
        if (
            !IsAttackLocked()
            || !bDisableWanderDetectDuringAttackLock
        )
        {
            if (TryDetectEnemyInWander())
            {
                return;
            }
        }
    }


    // =====================================================
    // 4. 通常Wander
    // =====================================================

    const float RandomValue =
        FMath::FRand();


    // 30%で停止。
    if (RandomValue < 0.3f)
    {
        if (OwnerAIController)
        {
            OwnerAIController->StopMovement();
        }


        SetNextDecisionTime(
            WanderWaitMin,
            WanderWaitMax
        );


        return;
    }


    // 70%でランダム移動。
    const bool bMoved =
        MoveToRandomLocation();


    if (bMoved)
    {
        SetNextDecisionTime(
            WanderWaitMin,
            WanderWaitMax
        );
    }
    else
    {
        SetNextDecisionTime(0.5f, 1.0f);
    }
}


/**
 * SpawnMoveモード。
 */
void UEnemyAIBrainComponent::TickSpawnMove(
    float DeltaTime
)
{
    if (!ShouldMakeDecision())
    {
        return;
    }


    // 群れActorが有効なら、
    // 現在位置を更新する。
    if (IsValid(TargetGroupActor))
    {
        TargetLocation =
            TargetGroupActor->GetActorLocation();
    }


    // SpawnMove中、3%でAttack。
    if (TryChance(SpawnMoveAttackChance))
    {
        SetAIMode(EEnemyAIMode::Attack);

        return;
    }


    // 群れ中央へ到着。
    if (IsNearTargetLocation())
    {
        SetAIMode(EEnemyAIMode::Devour);

        return;
    }


    const bool bMoved =
        MoveByARuleToLocation(TargetLocation);


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
void UEnemyAIBrainComponent::TickDevour(
    float DeltaTime
)
{
    if (!ShouldMakeDecision())
    {
        return;
    }


    // 強制条件を満たした場合はGoToHook。
    if (ShouldForceGoToHook())
    {
        SetAIMode(EEnemyAIMode::GoToHook);

        return;
    }


    // 現在のプランクトンが無効なら、
    // 新しいものを探す。
    if (!IsTargetPlanktonValid())
    {
        const bool bFound =
            SelectNearestPlankton();


        if (!bFound)
        {
            /*
             * 周囲にプランクトンがなくなった場合、
             * 仕様上Attackへ強制移行。
             */
            SetAIMode(EEnemyAIMode::Attack);

            return;
        }
    }


    // 念のため再確認。
    if (!IsTargetPlanktonValid())
    {
        SetAIMode(EEnemyAIMode::Attack);

        return;
    }


    TargetLocation =
        TargetPlanktonActor->GetActorLocation();


    const bool bMoved =
        MoveByARuleToLocation(TargetLocation);


    if (bMoved)
    {
        SetNextDecisionTime(0.15f, 0.35f);
    }
    else
    {
        TargetPlanktonActor = nullptr;

        SetNextDecisionTime(0.1f, 0.2f);
    }
}


/**
 * Attackモード。
 */
void UEnemyAIBrainComponent::TickAttack(
    float DeltaTime
)
{
    // =====================================================
    // AttackLock確認
    // =====================================================

    if (!CanEnterAttackMode())
    {
        if (
            AIAttackComponent
            && AIAttackComponent->IsAttacking()
        )
        {
            AIAttackComponent->CancelDashAttack();
        }


        TargetFishActor = nullptr;


        ReturnFromAttack();

        return;
    }


    // =====================================================
    // 突進攻撃中
    // =====================================================

    if (
        AIAttackComponent
        && AIAttackComponent->IsAttacking()
    )
    {
        /*
         * 攻撃Componentが攻撃中なら、
         * Brain側からMoveToを出さない。
         *
         * MoveToを出すと突進方向を崩す可能性がある。
         */
        return;
    }


    if (!ShouldMakeDecision())
    {
        return;
    }


    // =====================================================
    // 攻撃対象確認
    // =====================================================

    if (!IsTargetFishValid())
    {
        const bool bFound =
            SelectNearestAttackTarget();


        if (!bFound)
        {
            ReturnFromAttack();

            return;
        }
    }


    if (!IsTargetFishValid())
    {
        ReturnFromAttack();

        return;
    }


    // 対象位置更新。
    TargetLocation =
        TargetFishActor->GetActorLocation();


    // =====================================================
    // 攻撃距離以内
    // =====================================================

    if (IsNearAttackTarget())
    {
        if (OwnerAIController)
        {
            OwnerAIController->StopMovement();
        }


        if (AIAttackComponent)
        {
            const bool bStarted =
                AIAttackComponent->StartDashAttack(
                    TargetFishActor
                );


            if (bStarted)
            {
                // チャージと突進はAttackComponent側へ任せる。
                SetNextDecisionTime(1.0f, 1.2f);

                return;
            }
        }


        // 攻撃開始失敗。
        FinishAttackTemporarily();

        return;
    }


    // =====================================================
    // 対象へ接近
    // =====================================================

    const bool bMoved =
        MoveByARuleToLocation(TargetLocation);


    if (bMoved)
    {
        SetNextDecisionTime(
            AttackThinkIntervalMin,
            AttackThinkIntervalMax
        );
    }
    else
    {
        // 移動できなければ対象を取り直す。
        TargetFishActor = nullptr;

        SetNextDecisionTime(0.2f, 0.4f);
    }
}


/**
 * Fleeモード。
 */
void UEnemyAIBrainComponent::TickFlee(
    float DeltaTime
)
{
    if (!ShouldMakeDecision())
    {
        return;
    }


    AActor* FleeSourceActor =
        GetFleeSourceActor();


    // 逃走元が無効。
    if (!FleeSourceActor)
    {
        if (ShouldForceGoToHook())
        {
            SetAIMode(EEnemyAIMode::GoToHook);
        }
        else
        {
            SetAIMode(EEnemyAIMode::Wander);
        }


        return;
    }


    // 12セル以上離れたら逃走終了。
    if (IsFarEnoughFromFleeSource())
    {
        LastDamageCauser = nullptr;


        if (ShouldForceGoToHook())
        {
            SetAIMode(EEnemyAIMode::GoToHook);
        }
        else
        {
            SetAIMode(EEnemyAIMode::Wander);
        }


        return;
    }


    // 相手からAルールで逃げる。
    const bool bMoved =
        MoveByARuleAwayFromActor(FleeSourceActor);


    if (bMoved)
    {
        SetNextDecisionTime(
            FleeThinkIntervalMin,
            FleeThinkIntervalMax
        );
    }
    else
    {
        SetNextDecisionTime(0.3f, 0.6f);
    }
}


/**
 * GoToHookモード。
 *
 * GameSystemからBP経由で受け取った、
 * 現在アクティブなGoal Pointへ向かう。
 */
void UEnemyAIBrainComponent::TickGoToHook(
    float DeltaTime
)
{
    if (!ShouldMakeDecision())
    {
        return;
    }


    // =====================================================
    // 1. Goal Point確認
    // =====================================================

    if (!IsHookTargetValid())
    {
        /*
         * 現在有効なGoal Pointがないため、
         * BP側へ検索要求を送る。
         */
        RequestActiveHookSearch();


        /*
         * Broadcast先のBPで、
         * すぐにSetHookTarget()が呼ばれた可能性がある。
         *
         * そのためもう一度確認する。
         */
        if (!IsHookTargetValid())
        {
            /*
             * まだ見つからない。
             *
             * 一時的にWanderへ戻り、
             * 一定間隔で再検索する。
             */
            bWaitingForHook = true;


            SetAIMode(EEnemyAIMode::Wander);


            return;
        }
    }


    // ここまで来たら有効Goal Pointあり。
    bWaitingForHook = false;


    // Goal Pointの現在位置を毎回更新する。
    TargetLocation =
        TargetHookActor->GetActorLocation();


    // =====================================================
    // 2. 到着判定
    // =====================================================

    if (IsNearHook())
    {
        /*
         * 実際のゴール処理がOverlapなら、
         * ここでは余計なモード変更をしない。
         *
         * Goal Point付近で停止する。
         */
        if (OwnerAIController)
        {
            OwnerAIController->StopMovement();
        }


        SetNextDecisionTime(
            GoToHookThinkIntervalMin,
            GoToHookThinkIntervalMax
        );


        return;
    }


    // =====================================================
    // 3. 周囲の魚によるAttack判定
    // =====================================================

    if (CanEnterAttackMode())
    {
        if (TryAttackDuringGoToHook())
        {
            return;
        }
    }


    // =====================================================
    // 4. AルールでGoal Pointへ移動
    // =====================================================

    const bool bMoved =
        MoveByARuleToLocation(TargetLocation);


    if (bMoved)
    {
        SetNextDecisionTime(
            GoToHookThinkIntervalMin,
            GoToHookThinkIntervalMax
        );
    }
    else
    {
        SetNextDecisionTime(0.3f, 0.6f);
    }
}


// =========================================================
// Wander用関数
// =========================================================


/**
 * NavMesh上のランダム地点を探し、
 * Aルールで移動する。
 */
bool UEnemyAIBrainComponent::MoveToRandomLocation()
{
    if (!OwnerPawn || !OwnerAIController)
    {
        return false;
    }


    UNavigationSystemV1* NavSys =
        UNavigationSystemV1::GetCurrent(GetWorld());


    if (!NavSys)
    {
        return false;
    }


    FNavLocation RandomLocation;


    const bool bFound =
        NavSys->GetRandomReachablePointInRadius(
            OwnerPawn->GetActorLocation(),
            WanderRadius,
            RandomLocation
        );


    if (!bFound)
    {
        return false;
    }


    return MoveByARuleToLocation(
        RandomLocation.Location
    );
}


/**
 * Wander中に近距離魚を検知する。
 */
bool UEnemyAIBrainComponent::TryDetectEnemyInWander()
{
    AActor* EnemyActor =
        FindNearestWanderEnemy();


    if (!EnemyActor)
    {
        return false;
    }


    TargetFishActor = EnemyActor;


    TargetLocation =
        EnemyActor->GetActorLocation();


    SetAIMode(EEnemyAIMode::Attack);


    return CurrentMode == EEnemyAIMode::Attack;
}


/**
 * Wander中の近距離対象を探す。
 */
AActor* UEnemyAIBrainComponent::FindNearestWanderEnemy() const
{
    if (!OwnerPawn)
    {
        return nullptr;
    }


    TArray<AActor*> FishActors;


    UGameplayStatics::GetAllActorsWithTag(
        GetWorld(),
        FishTagName,
        FishActors
    );


    const FVector MyLocation =
        OwnerPawn->GetActorLocation();


    AActor* NearestEnemy = nullptr;


    float NearestDistanceSq =
        TNumericLimits<float>::Max();


    const float DetectDistance =
        WanderEnemyDetectCells * CellSize;


    const float DetectDistanceSq =
        DetectDistance * DetectDistance;


    for (AActor* FishActor : FishActors)
    {
        if (!IsValidAttackCandidate(FishActor))
        {
            continue;
        }


        const float DistanceSq =
            FVector::DistSquared2D(
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


// =========================================================
// SpawnMove / Target用関数
// =========================================================


/**
 * TargetLocationへ十分近いか確認する。
 */
bool UEnemyAIBrainComponent::IsNearTargetLocation() const
{
    if (!OwnerPawn)
    {
        return false;
    }


    const float Distance =
        FVector::Dist2D(
            OwnerPawn->GetActorLocation(),
            TargetLocation
        );


    return Distance <= TargetReachedDistance;
}


// =========================================================
// Devour用関数
// =========================================================


/**
 * 一番近いプランクトンを探す。
 */
AActor* UEnemyAIBrainComponent::FindNearestPlankton() const
{
    if (!OwnerPawn)
    {
        return nullptr;
    }


    TArray<AActor*> PlanktonActors;


    UGameplayStatics::GetAllActorsWithTag(
        GetWorld(),
        PlanktonTagName,
        PlanktonActors
    );


    const FVector MyLocation =
        OwnerPawn->GetActorLocation();


    AActor* NearestPlankton = nullptr;


    float NearestDistanceSq =
        TNumericLimits<float>::Max();


    const float SearchRadiusSq =
        DevourSearchRadius * DevourSearchRadius;


    for (AActor* PlanktonActor : PlanktonActors)
    {
        if (!IsValid(PlanktonActor))
        {
            continue;
        }


        const float DistanceSq =
            FVector::DistSquared2D(
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
 * TargetPlanktonActorが有効か確認する。
 */
bool UEnemyAIBrainComponent::IsTargetPlanktonValid() const
{
    return IsValid(TargetPlanktonActor);
}


/**
 * TargetPlanktonActorへ十分近いか確認する。
 */
bool UEnemyAIBrainComponent::IsNearTargetPlankton() const
{
    if (
        !OwnerPawn
        || !IsTargetPlanktonValid()
    )
    {
        return false;
    }


    const float Distance =
        FVector::Dist2D(
            OwnerPawn->GetActorLocation(),
            TargetPlanktonActor->GetActorLocation()
        );


    return Distance <= PlanktonReachedDistance;
}


/**
 * 一番近いプランクトンを設定する。
 */
bool UEnemyAIBrainComponent::SelectNearestPlankton()
{
    TargetPlanktonActor =
        FindNearestPlankton();


    if (!TargetPlanktonActor)
    {
        return false;
    }


    TargetLocation =
        TargetPlanktonActor->GetActorLocation();


    return true;
}


/**
 * プランクトン数に応じて、
 * GoToHookへ行くか確率判定する。
 */
bool UEnemyAIBrainComponent::TryGoToHookByPlanktonCount()
{
    const float Chance =
        GetGoToHookChanceByPlanktonCount();


    if (Chance <= 0.0f)
    {
        return false;
    }


    return TryChance(Chance);
}


/**
 * プランクトン数からGoToHook確率を返す。
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


// =========================================================
// Attack用関数
// =========================================================


/**
 * 一番近い攻撃対象を探す。
 */
AActor* UEnemyAIBrainComponent::FindNearestAttackTarget() const
{
    if (!OwnerPawn)
    {
        return nullptr;
    }


    TArray<AActor*> FishActors;


    UGameplayStatics::GetAllActorsWithTag(
        GetWorld(),
        FishTagName,
        FishActors
    );


    const FVector MyLocation =
        OwnerPawn->GetActorLocation();


    AActor* NearestTarget = nullptr;


    float NearestDistanceSq =
        TNumericLimits<float>::Max();


    const float SearchRadiusSq =
        AttackSearchRadius * AttackSearchRadius;


    for (AActor* FishActor : FishActors)
    {
        if (!IsValidAttackCandidate(FishActor))
        {
            continue;
        }


        const float DistanceSq =
            FVector::DistSquared2D(
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
 * 現在の攻撃対象が有効か確認する。
 */
bool UEnemyAIBrainComponent::IsTargetFishValid() const
{
    return IsValidAttackCandidate(TargetFishActor);
}


/**
 * 一番近い攻撃対象を設定する。
 */
bool UEnemyAIBrainComponent::SelectNearestAttackTarget()
{
    TargetFishActor =
        FindNearestAttackTarget();


    if (!TargetFishActor)
    {
        return false;
    }


    TargetLocation =
        TargetFishActor->GetActorLocation();


    return true;
}


/**
 * 攻撃開始距離以内か確認する。
 */
bool UEnemyAIBrainComponent::IsNearAttackTarget() const
{
    if (
        !OwnerPawn
        || !IsTargetFishValid()
    )
    {
        return false;
    }


    const float Distance =
        FVector::Dist2D(
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


    ReturnFromAttack();
}


/**
 * Attack終了後の復帰先へ戻る。
 *
 * GoToHookからAttackへ入っていた場合：
 * → GoToHook
 *
 * 通常Attack：
 * → Wander
 */
void UEnemyAIBrainComponent::ReturnFromAttack()
{
    TargetFishActor = nullptr;


    if (bReturnToGoToHookAfterAttack)
    {
        SetAIMode(EEnemyAIMode::GoToHook);

        return;
    }


    SetAIMode(EEnemyAIMode::Wander);
}


/**
 * Attackモードへ入った時、
 * AIレベルに応じて魚種変更を試す。
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


    /*
     * TODO:
     *
     * ここでBP_PlayerBase側の魚種変更処理を呼ぶ。
     *
     * Strong：
     * 高性能魚種を優先。
     *
     * Weak：
     * 安価な魚種を優先。
     */
}


/**
 * 攻撃対象として有効か確認する。
 */
bool UEnemyAIBrainComponent::IsValidAttackCandidate(
    AActor* CandidateActor
) const
{
    if (!IsValid(CandidateActor))
    {
        return false;
    }


    if (CandidateActor == OwnerPawn)
    {
        return false;
    }


    // SafeZone中Actorは攻撃対象にしない。
    if (
        bIgnoreActorsInSafeZone
        && CandidateActor->ActorHasTag(
            SafeZoneActorTagName
        )
    )
    {
        return false;
    }


    return true;
}


// =========================================================
// Flee用関数
// =========================================================


/**
 * 逃走元Actorを取得する。
 */
AActor* UEnemyAIBrainComponent::GetFleeSourceActor() const
{
    if (
        IsValid(LastDamageCauser)
        && LastDamageCauser != OwnerPawn
    )
    {
        return LastDamageCauser;
    }


    if (
        IsValid(TargetFishActor)
        && TargetFishActor != OwnerPawn
    )
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


    AActor* FleeSourceActor =
        GetFleeSourceActor();


    if (!FleeSourceActor)
    {
        return true;
    }


    const float EndDistance =
        FleeEndDistanceCells * CellSize;


    const float Distance =
        FVector::Dist2D(
            OwnerPawn->GetActorLocation(),
            FleeSourceActor->GetActorLocation()
        );


    return Distance >= EndDistance;
}


/**
 * 指定Actorから遠ざかるように
 * Aルール移動する。
 */
bool UEnemyAIBrainComponent::MoveByARuleAwayFromActor(
    AActor* SourceActor
)
{
    if (!OwnerPawn || !SourceActor)
    {
        return false;
    }


    FVector AwayDirection =
        OwnerPawn->GetActorLocation()
        - SourceActor->GetActorLocation();


    AwayDirection.Z = 0.0f;


    // 同じ位置にいる場合はランダム方向。
    if (AwayDirection.IsNearlyZero())
    {
        AwayDirection = FVector(
            FMath::FRandRange(-1.0f, 1.0f),
            FMath::FRandRange(-1.0f, 1.0f),
            0.0f
        );
    }


    AwayDirection =
        AwayDirection.GetSafeNormal();


    const FVector FleeGoalLocation =
        OwnerPawn->GetActorLocation()
        + AwayDirection
        * ARuleStepDistance
        * FleeAwayWeight;


    TargetLocation = FleeGoalLocation;


    return MoveByARuleToLocation(
        FleeGoalLocation
    );
}


// =========================================================
// GoToHook用関数
// =========================================================


/**
 * 強制GoToHook条件を満たしているか確認する。
 */
bool UEnemyAIBrainComponent::ShouldForceGoToHook() const
{
    return
        EatenPlanktonCount
        >= ForceGoToHookPlanktonCount;
}


/**
 * TargetHookActorが有効か確認する。
 */
bool UEnemyAIBrainComponent::IsHookTargetValid() const
{
    return IsValid(TargetHookActor);
}


/**
 * BP側へ、
 * 現在アクティブなGoal Pointの検索を要求する。
 */
void UEnemyAIBrainComponent::RequestActiveHookSearch()
{
    AActor* OwnerActor = GetOwner();


    if (!OwnerActor || !OwnerActor->HasAuthority())
    {
        return;
    }


    /*
     * BP側へ通知する。
     *
     * BP_Enemy側では、
     *
     * OnHookSearchRequested
     * ↓
     * GameSystem
     * ↓
     * Get Active Nearby Goal Point
     * ↓
     * 有効Actor
     * ↓
     * SetHookTarget
     *
     * と接続する。
     */
    OnHookSearchRequested.Broadcast();


    /*
     * 次回検索時刻を設定する。
     *
     * 全AIが完全に同じタイミングで検索しないように、
     * Min～Maxの間でランダムにする。
     */
    if (GetWorld())
    {
        const float WaitTime =
            FMath::FRandRange(
                HookSearchIntervalMin,
                HookSearchIntervalMax
            );


        NextHookSearchTime =
            GetWorld()->GetTimeSeconds()
            + WaitTime;
    }
}


/**
 * Goal Pointへ十分近いか確認する。
 */
bool UEnemyAIBrainComponent::IsNearHook() const
{
    if (
        !OwnerPawn
        || !IsHookTargetValid()
    )
    {
        return false;
    }


    const float Distance =
        FVector::Dist2D(
            OwnerPawn->GetActorLocation(),
            TargetHookActor->GetActorLocation()
        );


    return Distance <= HookReachedDistance;
}


/**
 * GoToHook中、
 * 周囲にいる最も近い攻撃対象を探す。
 */
AActor* UEnemyAIBrainComponent::FindNearestGoToHookEnemy() const
{
    if (!OwnerPawn)
    {
        return nullptr;
    }


    TArray<AActor*> FishActors;


    UGameplayStatics::GetAllActorsWithTag(
        GetWorld(),
        FishTagName,
        FishActors
    );


    const FVector MyLocation =
        OwnerPawn->GetActorLocation();


    AActor* NearestEnemy = nullptr;


    float NearestDistanceSq =
        TNumericLimits<float>::Max();


    const float DetectDistance =
        GoToHookEnemyDetectCells * CellSize;


    const float DetectDistanceSq =
        DetectDistance * DetectDistance;


    for (AActor* FishActor : FishActors)
    {
        if (!IsValidAttackCandidate(FishActor))
        {
            continue;
        }


        const float DistanceSq =
            FVector::DistSquared2D(
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


/**
 * GoToHook中のAttack移行判定。
 */
bool UEnemyAIBrainComponent::TryAttackDuringGoToHook()
{
    // AttackLock中なら不可。
    if (!CanEnterAttackMode())
    {
        return false;
    }


    // 周囲に魚がいるか確認する。
    AActor* NearbyEnemy =
        FindNearestGoToHookEnemy();


    if (!NearbyEnemy)
    {
        return false;
    }


    // Goal Pointまでの距離からAttack確率を計算。
    const float AttackChance =
        CalculateGoToHookAttackChance();


    // 0%なら抽選しない。
    if (AttackChance <= 0.0f)
    {
        return false;
    }


    // 確率抽選失敗。
    if (!TryChance(AttackChance))
    {
        return false;
    }


    // 攻撃対象を設定。
    TargetFishActor = NearbyEnemy;


    TargetLocation =
        NearbyEnemy->GetActorLocation();


    /*
     * SetAIMode内で、
     *
     * CurrentMode == GoToHook
     * &&
     * NewMode == Attack
     *
     * の場合、
     *
     * bReturnToGoToHookAfterAttack = true
     *
     * にする。
     */
    SetAIMode(EEnemyAIMode::Attack);


    return CurrentMode == EEnemyAIMode::Attack;
}


/**
 * GoToHook中のAttack確率を計算する。
 *
 * 12セル以上：
 * → 最大30%
 *
 * 12～4セル：
 * → 線形に低下
 *
 * 4セル以内：
 * → 0%
 */
float UEnemyAIBrainComponent::CalculateGoToHookAttackChance() const
{
    if (
        !OwnerPawn
        || !IsHookTargetValid()
    )
    {
        return 0.0f;
    }


    const float DistanceToHook =
        FVector::Dist2D(
            OwnerPawn->GetActorLocation(),
            TargetHookActor->GetActorLocation()
        );


    const float NoAttackDistance =
        GoToHookNoAttackDistanceCells
        * CellSize;


    const float MaxAttackDistance =
        GoToHookMaxAttackDistanceCells
        * CellSize;


    // =====================================================
    // 4セル以内
    // 0%
    // =====================================================

    if (DistanceToHook <= NoAttackDistance)
    {
        return 0.0f;
    }


    // =====================================================
    // 12セル以上
    // 最大30%
    // =====================================================

    if (DistanceToHook >= MaxAttackDistance)
    {
        return GoToHookMaxAttackChance;
    }


    // =====================================================
    // 4～12セル
    // 線形補間
    // =====================================================

    /*
     * 4セル地点
     * → DistanceRate = 0
     *
     * 12セル地点
     * → DistanceRate = 1
     */
    const float DistanceRate =
        FMath::Clamp(
            (DistanceToHook - NoAttackDistance)
            /
            FMath::Max(
                MaxAttackDistance - NoAttackDistance,
                KINDA_SMALL_NUMBER
            ),
            0.0f,
            1.0f
        );


    return
        GoToHookMaxAttackChance
        * DistanceRate;
}


// =========================================================
// AttackLock用関数
// =========================================================


/**
 * Attackへ入れるか確認する。
 */
bool UEnemyAIBrainComponent::CanEnterAttackMode() const
{
    return !IsAttackLocked();
}


// =========================================================
// Aルール関数
// =========================================================


/**
 * 近くのFishタグActorから離れる方向を計算する。
 */
FVector UEnemyAIBrainComponent::CalculateAvoidDirection() const
{
    if (!OwnerPawn)
    {
        return FVector::ZeroVector;
    }


    TArray<AActor*> FishActors;


    UGameplayStatics::GetAllActorsWithTag(
        GetWorld(),
        FishTagName,
        FishActors
    );


    const FVector MyLocation =
        OwnerPawn->GetActorLocation();


    FVector AvoidDirection =
        FVector::ZeroVector;


    const float MinSeparationDistance =
        MinSeparationCells * CellSize;


    for (AActor* OtherActor : FishActors)
    {
        if (
            !OtherActor
            || OtherActor == OwnerPawn
        )
        {
            continue;
        }


        const FVector OtherLocation =
            OtherActor->GetActorLocation();


        const float Distance =
            FVector::Dist2D(
                MyLocation,
                OtherLocation
            );


        if (Distance <= KINDA_SMALL_NUMBER)
        {
            continue;
        }


        if (Distance < MinSeparationDistance)
        {
            FVector FromOtherToMe =
                MyLocation - OtherLocation;


            FromOtherToMe.Z = 0.0f;


            FromOtherToMe.Normalize();


            const float Strength =
                1.0f
                - FMath::Clamp(
                    Distance / MinSeparationDistance,
                    0.0f,
                    1.0f
                );


            AvoidDirection +=
                FromOtherToMe * Strength;
        }
    }


    AvoidDirection.Z = 0.0f;


    return AvoidDirection.GetSafeNormal();
}


/**
 * Aルール移動先を計算する。
 */
FVector UEnemyAIBrainComponent::CalculateARuleMoveLocation(
    const FVector& GoalLocation
)
{
    if (!OwnerPawn)
    {
        return GoalLocation;
    }


    const FVector MyLocation =
        OwnerPawn->GetActorLocation();


    // =====================================================
    // 目的地方向
    // =====================================================

    FVector GoalDirection =
        GoalLocation - MyLocation;


    GoalDirection.Z = 0.0f;


    GoalDirection =
        GoalDirection.GetSafeNormal();


    // =====================================================
    // 周囲の魚を避ける方向
    // =====================================================

    const FVector AvoidDirection =
        CalculateAvoidDirection();


    // =====================================================
    // ランダム方向
    // =====================================================

    FVector RandomDirection =
        FVector(
            FMath::FRandRange(-1.0f, 1.0f),
            FMath::FRandRange(-1.0f, 1.0f),
            0.0f
        ).GetSafeNormal();


    // =====================================================
    // 各方向を合成
    // =====================================================

    FVector FinalDirection =
        GoalDirection * GoalWeight
        + AvoidDirection * AvoidWeight
        + RandomDirection * RandomWeight;


    FinalDirection.Z = 0.0f;


    if (FinalDirection.IsNearlyZero())
    {
        FinalDirection = GoalDirection;
    }


    FinalDirection =
        FinalDirection.GetSafeNormal();


    // =====================================================
    // 候補地点作成
    // =====================================================

    FVector CandidateLocation =
        MyLocation
        + FinalDirection
        * ARuleStepDistance;


    const float DistanceToGoal =
        FVector::Dist2D(
            MyLocation,
            GoalLocation
        );


    /*
     * 目的地までARuleStepDistance未満なら、
     * 目的地そのものを候補にする。
     */
    if (DistanceToGoal < ARuleStepDistance)
    {
        CandidateLocation = GoalLocation;
    }


    return CandidateLocation;
}


/**
 * Aルールで補正した位置へMoveToする。
 */
bool UEnemyAIBrainComponent::MoveByARuleToLocation(
    const FVector& GoalLocation
)
{
    if (!OwnerPawn || !OwnerAIController)
    {
        return false;
    }


    UNavigationSystemV1* NavSys =
        UNavigationSystemV1::GetCurrent(GetWorld());


    if (!NavSys)
    {
        return false;
    }


    const FVector CandidateLocation =
        CalculateARuleMoveLocation(GoalLocation);


    FNavLocation ProjectedLocation;


    const bool bProjected =
        NavSys->ProjectPointToNavigation(
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


// =========================================================
// 汎用関数
// =========================================================


/**
 * 確率判定。
 */
bool UEnemyAIBrainComponent::TryChance(
    float Chance
) const
{
    return FMath::FRand() <= Chance;
}


/**
 * 次の判断時間になったか確認する。
 */
bool UEnemyAIBrainComponent::ShouldMakeDecision() const
{
    if (!GetWorld())
    {
        return false;
    }


    return
        GetWorld()->GetTimeSeconds()
        >= NextDecisionTime;
}


/**
 * 次の判断時刻を設定する。
 */
void UEnemyAIBrainComponent::SetNextDecisionTime(
    float MinTime,
    float MaxTime
)
{
    if (!GetWorld())
    {
        return;
    }


    const float WaitTime =
        FMath::FRandRange(
            MinTime,
            MaxTime
        );


    NextDecisionTime =
        GetWorld()->GetTimeSeconds()
        + WaitTime;
}