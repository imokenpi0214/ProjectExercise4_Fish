#include "EnemyAIBrainComponent.h"

#include "AIController.h"
#include "EnemyAIAttackComponent.h"
#include "NavigationSystem.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UnrealType.h"

#include "DrawDebugHelpers.h"


// =========================================================
// static変数定義
// =========================================================

TMap<
    TWeakObjectPtr<AActor>,
    TWeakObjectPtr<UEnemyAIBrainComponent>
> UEnemyAIBrainComponent::TargetReservations;


// =========================================================
// Constructor
// =========================================================

UEnemyAIBrainComponent::UEnemyAIBrainComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}


// =========================================================
// BeginPlay
// =========================================================

void UEnemyAIBrainComponent::BeginPlay()
{
    Super::BeginPlay();

    OwnerPawn = Cast<APawn>(GetOwner());

    if (OwnerPawn)
    {
        OwnerAIController =
            Cast<AAIController>(OwnerPawn->GetController());
    }

    AIAttackComponent = GetOwner()
        ? GetOwner()->FindComponentByClass<UEnemyAIAttackComponent>()
        : nullptr;

    SetNextDecisionTime(0.1f, 0.5f);

    NextHookSearchTime = 0.0f;

    ResetStuckDetection();

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
        OwnerAIController =
            Cast<AAIController>(OwnerPawn->GetController());
    }

    if (!AIAttackComponent)
    {
        AIAttackComponent = GetOwner()
            ? GetOwner()->FindComponentByClass<UEnemyAIAttackComponent>()
            : nullptr;
    }

    // =========================================================
    // 既存ダッシュ中
    // =========================================================
    //
    // Attack目的・逃走目的・移動目的に関係なく、
    // ダッシュはすべてEnemyAIAttackComponentの
    // 同じダッシュ処理を使用する。
    //
    // チャージ中を含めてBrain側の通常移動を止める。
    // これにより、StartDashAttack(nullptr)を呼んだ後に
    // MoveToやSteeringで向きが変わるのを防ぐ。
    // =========================================================

    if (
        AIAttackComponent
        && AIAttackComponent->IsAttacking()
    )
    {
        DrawVisionConeDebug();

        return;
    }

    CleanupInvalidTargetReservations();

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

    UpdateStuckDetection(DeltaTime);

    if (
        bUseForwardOnlyMovement
        && bHasSteeringTargetLocation
        && !(AIAttackComponent && AIAttackComponent->IsAttacking())
    )
    {
        MoveForwardOnlyToLocation(
            SteeringTargetLocation,
            DeltaTime
        );
    }
    DrawVisionConeDebug();
}


// =========================================================
// BPから呼ぶ公開関数
// =========================================================

void UEnemyAIBrainComponent::SetAIMode(
    EEnemyAIMode NewMode
)
{
    if (
        NewMode == EEnemyAIMode::GoToHook
        && GetOwnerItemAmount() <= 0
    )
    {
        NewMode = EEnemyAIMode::Wander;
    }

    if (
        NewMode == EEnemyAIMode::Attack
        && !CanEnterAttackMode()
    )
    {
        if (CurrentMode == EEnemyAIMode::GoToHook)
        {
            NewMode = EEnemyAIMode::GoToHook;
        }
        else
        {
            NewMode = EEnemyAIMode::Wander;
        }
    }

    if (CurrentMode == NewMode)
    {
        return;
    }

    if (
        CurrentMode == EEnemyAIMode::GoToHook
        && NewMode == EEnemyAIMode::Attack
    )
    {
        bReturnToGoToHookAfterAttack = true;
    }

    if (
        AIAttackComponent
        && AIAttackComponent->IsAttacking()
    )
    {
        AIAttackComponent->CancelDashAttack();
    }

    CurrentMode = NewMode;

    ResetStuckDetection();

    SetNextDecisionTime(0.05f, 0.15f);

    switch (CurrentMode)
    {
    case EEnemyAIMode::Wander:
    {
        TargetPlanktonActor = nullptr;

        ReleaseCurrentTargetActor();

        bReturnToGoToHookAfterAttack = false;

        break;
    }

    case EEnemyAIMode::SpawnMove:
    {
        TargetPlanktonActor = nullptr;

        ReleaseCurrentTargetActor();

        break;
    }

    case EEnemyAIMode::Devour:
    {
        ReleaseCurrentTargetActor();

        SelectNearestPlankton();

        break;
    }

    case EEnemyAIMode::Attack:
    {
        TargetPlanktonActor = nullptr;

        TryChangeFishOnAttackMode();

        if (!IsTargetFishValid())
        {
            SelectNearestAttackTarget();
        }

        break;
    }

    case EEnemyAIMode::Flee:
    {
        TargetPlanktonActor = nullptr;

        ReleaseCurrentTargetActor();

        if (
            AIAttackComponent
            && AIAttackComponent->IsAttacking()
        )
        {
            AIAttackComponent->CancelDashAttack();
        }

        if (OwnerAIController)
        {
            OwnerAIController->StopMovement();
        }

        bHasSteeringTargetLocation = false;

        SetNextDecisionTime(0.05f, 0.15f);

        break;
    }

    case EEnemyAIMode::GoToHook:
    {
        TargetPlanktonActor = nullptr;

        ReleaseCurrentTargetActor();

        bReturnToGoToHookAfterAttack = false;
        bWaitingForHook = false;

        if (IsHookTargetValid())
        {
            TargetLocation =
                TargetHookActor->GetActorLocation();
        }
        else
        {
            TargetHookActor = nullptr;
            bHasSteeringTargetLocation = false;

            RequestActiveHookSearch();
        }

        break;
    }

    default:
        break;
    }

    // =========================================================
    // モードへ入った瞬間のダッシュ判定
    // =========================================================

    TryStartModeEntryDash(CurrentMode);
}


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


void UEnemyAIBrainComponent::SetHookTarget(
    AActor* NewHookActor
)
{
    AActor* OwnerActor = GetOwner();

    if (!OwnerActor || !OwnerActor->HasAuthority())
    {
        return;
    }

    if (!IsValid(NewHookActor))
    {
        return;
    }

    if (GetOwnerItemAmount() <= 0)
    {
        TargetHookActor = nullptr;
        bWaitingForHook = false;
        bHasSteeringTargetLocation = false;

        SetAIMode(EEnemyAIMode::Wander);

        return;
    }

    TargetHookActor = NewHookActor;

    if (!IsHookTargetValid())
    {
        TargetHookActor = nullptr;
        bWaitingForHook = true;
        bHasSteeringTargetLocation = false;

        SetAIMode(EEnemyAIMode::Wander);

        return;
    }

    TargetLocation =
        TargetHookActor->GetActorLocation();

    bWaitingForHook = false;

    ResetStuckDetection();

    if (CurrentMode != EEnemyAIMode::GoToHook)
    {
        SetAIMode(EEnemyAIMode::GoToHook);
    }
}


void UEnemyAIBrainComponent::NotifyPlanktonCollected()
{
    AActor* OwnerActor = GetOwner();

    if (!OwnerActor || !OwnerActor->HasAuthority())
    {
        return;
    }

    const int32 OwnerItemAmount =
        GetOwnerItemAmount();

    if (OwnerItemAmount != EatenPlanktonCount || OwnerItemAmount > 0)
    {
        EatenPlanktonCount = OwnerItemAmount;
    }
    else
    {
        EatenPlanktonCount++;
    }

    if (ShouldForceGoToHook())
    {
        SetAIMode(EEnemyAIMode::GoToHook);
        return;
    }

    if (TryChance(DevourAttackChance))
    {
        SetAIMode(EEnemyAIMode::Attack);
        return;
    }

    if (TryGoToHookByPlanktonCount())
    {
        SetAIMode(EEnemyAIMode::GoToHook);
        return;
    }

    if (CurrentMode == EEnemyAIMode::SpawnMove)
    {
        SetAIMode(EEnemyAIMode::Devour);
    }
}


void UEnemyAIBrainComponent::ResetAICarryState()
{
    AActor* OwnerActor = GetOwner();

    if (!OwnerActor || !OwnerActor->HasAuthority())
    {
        return;
    }

    EatenPlanktonCount = 0;

    bWaitingForHook = false;

    TargetHookActor = nullptr;
    TargetGroupActor = nullptr;
    TargetPlanktonActor = nullptr;

    ReleaseCurrentTargetActor();

    bHasSteeringTargetLocation = false;

    if (
        AIAttackComponent
        && AIAttackComponent->IsAttacking()
    )
    {
        AIAttackComponent->CancelDashAttack();
    }

    if (OwnerAIController)
    {
        OwnerAIController->StopMovement();
    }

    ResetStuckDetection();

    SetAIMode(EEnemyAIMode::Wander);
}


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

    LastDamageCauser = DamageCauser;

    if (
        IsAttackLocked()
        && bDisableAttackReactionDuringAttackLock
    )
    {
        if (
            AIAttackComponent
            && AIAttackComponent->IsAttacking()
        )
        {
            AIAttackComponent->CancelDashAttack();
        }

        bHasSteeringTargetLocation = false;

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

    if (HpRate < LowHpFleeThreshold)
    {
        if (TryChance(LowHpFleeChance))
        {
            SetAIMode(EEnemyAIMode::Flee);
            return;
        }

        if (IsValidAttackCandidate(DamageCauser))
        {
            ReserveTargetActor(DamageCauser);
            SetAIMode(EEnemyAIMode::Attack);
            return;
        }

        ReleaseCurrentTargetActor();
        SetAIMode(EEnemyAIMode::Wander);
        return;
    }

    if (HpRate < MidHpFleeThreshold)
    {
        if (TryChance(MidHpFleeChance))
        {
            SetAIMode(EEnemyAIMode::Flee);
            return;
        }

        if (IsValidAttackCandidate(DamageCauser))
        {
            ReserveTargetActor(DamageCauser);
            SetAIMode(EEnemyAIMode::Attack);
            return;
        }

        ReleaseCurrentTargetActor();
        SetAIMode(EEnemyAIMode::Wander);
        return;
    }

    if (IsValidAttackCandidate(DamageCauser))
    {
        ReserveTargetActor(DamageCauser);
        SetAIMode(EEnemyAIMode::Attack);
        return;
    }

    ReleaseCurrentTargetActor();
    SetAIMode(EEnemyAIMode::Wander);
}


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

    if (MaxHP <= KINDA_SMALL_NUMBER)
    {
        ReleaseCurrentTargetActor();

        ReturnFromAttack();

        return;
    }

    const float HpRate =
        CurrentHP / MaxHP;


    // =========================================================
    // HPが十分ある場合は攻撃継続
    // =========================================================

    if (HpRate >= AttackContinueHpRate)
    {
        if (IsValidAttackCandidate(HitActor))
        {
            ReserveTargetActor(HitActor);
        }
        else
        {
            ReleaseCurrentTargetActor();
        }


        // =========================================================
        // 同じ魚への攻撃継続時、退避ダッシュ予約
        // =========================================================
        //
        // この時点ではまだ攻撃ダッシュ中の可能性が高い。
        // そのため、ここでは退避ダッシュそのものは開始しない。
        //
        // 50%抽選だけ行い、
        // 元のダッシュ終了後にTickAttackから実行する。
        // =========================================================

        if (
            IsTargetFishValid()
            && CanSeeFishActor(TargetFishActor)
            && TryChance(AttackRetreatDashChance)
        )
        {
            bPendingAttackRetreatDash = true;
        }


        SetNextDecisionTime(
            AttackContinueDelayMin,
            AttackContinueDelayMax
        );


        SetAIMode(EEnemyAIMode::Attack);

        return;
    }


    // =========================================================
    // 相手が逃走した場合
    // 一定確率で追撃
    // =========================================================

    if (HitActor && bHitActorBecameFlee)
    {
        if (TryChance(ChaseFleeTargetChance))
        {
            if (IsValidAttackCandidate(HitActor))
            {
                ReserveTargetActor(HitActor);

                ChaseEndTime =
                    GetWorld()
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


    // =========================================================
    // 攻撃終了
    // =========================================================

    ReleaseCurrentTargetActor();

    ReturnFromAttack();
}


bool UEnemyAIBrainComponent::TryCancelAttackByBoredom()
{
    // =========================================================
    // 最大飽き確率を0～1へ制限
    // =========================================================

    const float ClampedMaxBoredomChance =
        FMath::Clamp(
            MaxBoredomChance,
            0.0f,
            1.0f
        );


    // =========================================================
    // 今回の攻撃分、飽きを加算
    // =========================================================

    CurrentBoredomChance =
        FMath::Clamp(
            CurrentBoredomChance
                + FMath::Max(
                    0.0f,
                    BoredomIncreasePerAttack
                ),
            0.0f,
            ClampedMaxBoredomChance
        );


    // =========================================================
    // 飽き0なら何もしない
    // =========================================================

    if (CurrentBoredomChance <= 0.0f)
    {
        return false;
    }


    // =========================================================
    // CurrentBoredomChanceそのものを
    // 攻撃キャンセル確率として使用
    // =========================================================

    if (!TryChance(CurrentBoredomChance))
    {
        return false;
    }


    // =========================================================
    // 飽き判定成功
    // =========================================================

    ResetBoredom();


    if (OwnerAIController)
    {
        OwnerAIController->StopMovement();
    }


    bHasSteeringTargetLocation = false;


    // =========================================================
    // 2～6秒など、設定範囲からランダム
    // =========================================================

    const float MinCooldown =
        FMath::Max(
            0.0f,
            FMath::Min(
                BoredomCooldownMin,
                BoredomCooldownMax
            )
        );


    const float MaxCooldown =
        FMath::Max(
            MinCooldown,
            FMath::Max(
                BoredomCooldownMin,
                BoredomCooldownMax
            )
        );


    const float CooldownDuration =
        FMath::FRandRange(
            MinCooldown,
            MaxCooldown
        );


    /*
     * CurrentModeはこの時点でAttack。
     *
     * LockAttackForSeconds()内部で、
     *
     * - 攻撃キャンセル
     * - ターゲット解除
     * - ReturnFromAttack()
     *
     * が行われる。
     *
     * GoToHookからAttackへ来ていた場合は
     * GoToHookへ戻る。
     *
     * 通常AttackならWanderへ戻る。
     */
    LockAttackForSeconds(
        CooldownDuration
    );


    return true;
}


void UEnemyAIBrainComponent::ResetBoredom()
{
    CurrentBoredomChance = 0.0f;
}


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

    AttackLockEndTime =
        FMath::Max(
            AttackLockEndTime,
            NewEndTime
        );

    if (CurrentMode == EEnemyAIMode::Attack)
    {
        if (
            AIAttackComponent
            && AIAttackComponent->IsAttacking()
        )
        {
            AIAttackComponent->CancelDashAttack();
        }

        ReleaseCurrentTargetActor();

        bHasSteeringTargetLocation = false;

        ReturnFromAttack();
    }
}


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


bool UEnemyAIBrainComponent::IsWaitingForHook() const
{
    return bWaitingForHook;
}


// =========================================================
// モード別Tick
// =========================================================

void UEnemyAIBrainComponent::TickWander(
    float DeltaTime
)
{
    if (bWaitingForHook && GetWorld())
    {
        const float CurrentTime =
            GetWorld()->GetTimeSeconds();

        if (CurrentTime >= NextHookSearchTime)
        {
            RequestActiveHookSearch();

            if (CurrentMode != EEnemyAIMode::Wander)
            {
                return;
            }
        }
    }

    if (!ShouldMakeDecision())
    {
        return;
    }

    if (
        !bWaitingForHook
        && ShouldForceGoToHook()
    )
    {
        SetAIMode(EEnemyAIMode::GoToHook);
        return;
    }

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

    const float RandomValue =
        FMath::FRand();

    if (RandomValue < 0.3f)
    {
        if (OwnerAIController)
        {
            OwnerAIController->StopMovement();
        }

        bHasSteeringTargetLocation = false;

        SetNextDecisionTime(
            WanderWaitMin,
            WanderWaitMax
        );

        return;
    }

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
        bHasSteeringTargetLocation = false;

        SetNextDecisionTime(0.5f, 1.0f);
    }
}


void UEnemyAIBrainComponent::TickSpawnMove(
    float DeltaTime
)
{
    if (!ShouldMakeDecision())
    {
        return;
    }

    if (IsValid(TargetGroupActor))
    {
        TargetLocation =
            TargetGroupActor->GetActorLocation();
    }

    if (TryChance(SpawnMoveAttackChance))
    {
        SetAIMode(EEnemyAIMode::Attack);
        return;
    }

    if (IsNearTargetLocation())
    {
        bHasSteeringTargetLocation = false;

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
        bHasSteeringTargetLocation = false;

        SetNextDecisionTime(0.5f, 1.0f);
    }
}


void UEnemyAIBrainComponent::TickDevour(
    float DeltaTime
)
{
    if (!ShouldMakeDecision())
    {
        return;
    }

    if (ShouldForceGoToHook())
    {
        SetAIMode(EEnemyAIMode::GoToHook);
        return;
    }

    if (!IsTargetPlanktonValid())
    {
        const bool bFound =
            SelectNearestPlankton();

        if (!bFound)
        {
            bHasSteeringTargetLocation = false;

            SetAIMode(EEnemyAIMode::Attack);
            return;
        }
    }

    if (!IsTargetPlanktonValid())
    {
        bHasSteeringTargetLocation = false;

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

        bHasSteeringTargetLocation = false;

        SetNextDecisionTime(0.1f, 0.2f);
    }
}


void UEnemyAIBrainComponent::TickAttack(
    float DeltaTime
)
{
    // =========================================================
    // AttackLock中
    // =========================================================

    if (!CanEnterAttackMode())
    {
        if (
            AIAttackComponent
            && AIAttackComponent->IsAttacking()
        )
        {
            AIAttackComponent->CancelDashAttack();
        }

        ReleaseCurrentTargetActor();

        bHasSteeringTargetLocation = false;

        ReturnFromAttack();

        return;
    }


    // =========================================================
    // すでに攻撃動作中
    // =========================================================

    if (
        AIAttackComponent
        && AIAttackComponent->IsAttacking()
    )
    {
        bHasSteeringTargetLocation = false;

        return;
    }

    // =========================================================
    // 攻撃命中後の退避ダッシュ予約
    // =========================================================
    //
    // 元の攻撃ダッシュが終わったあと、
    // ここへ来たタイミングで退避ダッシュを開始する。
    // =========================================================

    if (bPendingAttackRetreatDash)
    {
        bPendingAttackRetreatDash = false;

        if (TryStartAttackRetreatDash())
        {
            return;
        }
    }

    // =========================================================
    // Attack退避ダッシュ継続
    // =========================================================

    if (bAttackRetreatDashSequenceActive)
    {
        if (TryContinueAttackRetreatDash())
        {
            return;
        }
    }

    // =========================================================
    // 判断タイミング待ち
    // =========================================================

    if (!ShouldMakeDecision())
    {
        return;
    }


    // =========================================================
    // 現在ターゲットが無効
    // =========================================================

    if (!IsTargetFishValid())
    {
        ReleaseCurrentTargetActor();

        const bool bFound =
            SelectNearestAttackTarget();

        if (!bFound)
        {
            ReleaseCurrentTargetActor();

            bHasSteeringTargetLocation = false;

            SetAIMode(EEnemyAIMode::Wander);

            return;
        }
    }


    // =========================================================
    // 再確認
    // =========================================================

    if (!IsTargetFishValid())
    {
        ReleaseCurrentTargetActor();

        bHasSteeringTargetLocation = false;

        SetAIMode(EEnemyAIMode::Wander);

        return;
    }


    TargetLocation =
        TargetFishActor->GetActorLocation();


    // =========================================================
    // 攻撃範囲内
    // =========================================================

    if (IsNearAttackTarget())
    {
        // -----------------------------------------------------
        // 正面に捉える
        // -----------------------------------------------------

        if (
            bAimBeforeAttack
            && !IsTargetInAttackAngle()
        )
        {
            SteeringTargetLocation =
                TargetLocation;

            bHasSteeringTargetLocation =
                true;

            SetNextDecisionTime(
                AttackThinkIntervalMin,
                AttackThinkIntervalMax
            );

            return;
        }


        // -----------------------------------------------------
        // 移動停止
        // -----------------------------------------------------

        if (OwnerAIController)
        {
            OwnerAIController->StopMovement();
        }

        bHasSteeringTargetLocation = false;


        // -----------------------------------------------------
        // ダッシュ攻撃開始
        // -----------------------------------------------------

        if (AIAttackComponent)
        {
            const bool bStarted =
                AIAttackComponent->StartDashAttack(
                    TargetFishActor
                );


            if (bStarted)
            {
                // =================================================
                // 攻撃を「行った」ので飽きを加算
                //
                // 命中したかどうかは関係ない。
                // StartDashAttack成功時点で1回として数える。
                // =================================================

                if (TryCancelAttackByBoredom())
                {
                    return;
                }


                SetNextDecisionTime(
                    1.0f,
                    1.2f
                );

                return;
            }
        }


        // -----------------------------------------------------
        // 攻撃開始失敗
        // -----------------------------------------------------

        FinishAttackTemporarily();

        return;
    }


    // =========================================================
    // 攻撃範囲外
    // =========================================================

    const bool bMoved =
        MoveByARuleToLocation(
            TargetLocation
        );


    if (bMoved)
    {
        SetNextDecisionTime(
            AttackThinkIntervalMin,
            AttackThinkIntervalMax
        );
    }
    else
    {
        ReleaseCurrentTargetActor();

        bHasSteeringTargetLocation = false;

        SetNextDecisionTime(
            0.2f,
            0.4f
        );
    }
}


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

    if (!FleeSourceActor)
    {
        bHasSteeringTargetLocation = false;

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

    if (IsFarEnoughFromFleeSource())
    {
        LastDamageCauser = nullptr;

        bHasSteeringTargetLocation = false;

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
        bHasSteeringTargetLocation = false;

        SetNextDecisionTime(0.3f, 0.6f);
    }
}


void UEnemyAIBrainComponent::TickGoToHook(
    float DeltaTime
)
{
    if (!ShouldMakeDecision())
    {
        return;
    }

    if (GetOwnerItemAmount() <= 0)
    {
        TargetHookActor = nullptr;
        bWaitingForHook = false;
        bHasSteeringTargetLocation = false;

        SetAIMode(EEnemyAIMode::Wander);

        return;
    }

    if (!IsHookTargetValid())
    {
        TargetHookActor = nullptr;
        bHasSteeringTargetLocation = false;

        RequestActiveHookSearch();

        if (!IsHookTargetValid())
        {
            bWaitingForHook = true;

            SetAIMode(EEnemyAIMode::Wander);
            return;
        }
    }

    bWaitingForHook = false;

    TargetLocation =
        TargetHookActor->GetActorLocation();

    if (IsNearHook())
    {
        if (OwnerAIController)
        {
            OwnerAIController->StopMovement();
        }

        bHasSteeringTargetLocation = false;

        SetNextDecisionTime(
            GoToHookThinkIntervalMin,
            GoToHookThinkIntervalMax
        );

        return;
    }

    if (CanEnterAttackMode())
    {
        if (TryAttackDuringGoToHook())
        {
            return;
        }
    }

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
        bHasSteeringTargetLocation = false;

        SetNextDecisionTime(0.3f, 0.6f);
    }
}


// =========================================================
// Wander
// =========================================================

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


bool UEnemyAIBrainComponent::TryDetectEnemyInWander()
{
    AActor* EnemyActor =
        FindNearestWanderEnemy();

    if (!EnemyActor)
    {
        return false;
    }

    ReserveTargetActor(EnemyActor);

    SetAIMode(EEnemyAIMode::Attack);

    return CurrentMode == EEnemyAIMode::Attack;
}


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


    AActor* NearestVisibleEnemy =
        nullptr;


    float NearestDistanceSq =
        TNumericLimits<float>::Max();


    for (AActor* FishActor : FishActors)
    {
        // =====================================================
        // 視界判定
        // =====================================================

        if (!CanSeeFishActor(FishActor))
        {
            continue;
        }


        const float DistanceSq =
            FVector::DistSquared2D(
                MyLocation,
                FishActor->GetActorLocation()
            );


        if (DistanceSq < NearestDistanceSq)
        {
            NearestDistanceSq =
                DistanceSq;

            NearestVisibleEnemy =
                FishActor;
        }
    }


    return NearestVisibleEnemy;
}

float UEnemyAIBrainComponent::GetVisionAngleToActor(
    AActor* CandidateActor
) const
{
    if (
        !OwnerPawn
        || !IsValid(CandidateActor)
    )
    {
        return 180.0f;
    }


    // =========================================================
    // 自分の正面方向
    // =========================================================

    FVector ForwardDirection =
        OwnerPawn->GetActorForwardVector();


    ForwardDirection.Z = 0.0f;


    if (ForwardDirection.IsNearlyZero())
    {
        return 180.0f;
    }


    ForwardDirection.Normalize();


    // =========================================================
    // 自分 → 対象 の方向
    // =========================================================

    FVector ToTargetDirection =
        CandidateActor->GetActorLocation()
        - OwnerPawn->GetActorLocation();


    ToTargetDirection.Z = 0.0f;


    if (ToTargetDirection.IsNearlyZero())
    {
        return 0.0f;
    }


    ToTargetDirection.Normalize();


    // =========================================================
    // atan2
    //
    // Dot:
    // 正面に近いほど +1
    //
    // Cross.Z:
    // 左右どちらにいるか
    //
    // atan2(Cross, Dot)によって
    // -180 ～ +180度を取得できる
    // =========================================================

    const float Dot =
        FVector::DotProduct(
            ForwardDirection,
            ToTargetDirection
        );


    const float CrossZ =
        FVector::CrossProduct(
            ForwardDirection,
            ToTargetDirection
        ).Z;


    const float AngleRadians =
        FMath::Atan2(
            CrossZ,
            Dot
        );


    return FMath::RadiansToDegrees(
        AngleRadians
    );
}

bool UEnemyAIBrainComponent::CanSeeFishActor(
    AActor* CandidateActor
) const
{
    // =========================================================
    // 基本チェック
    // =========================================================

    if (
        !OwnerPawn
        || !IsValid(CandidateActor)
        || !GetWorld()
    )
    {
        return false;
    }


    if (CandidateActor == OwnerPawn)
    {
        return false;
    }


    // =========================================================
    // 攻撃対象として有効か
    //
    // SafeZone
    // ターゲット予約
    // 自分自身
    //
    // などの既存判定もここで行われる
    // =========================================================

    if (!IsValidAttackCandidate(CandidateActor))
    {
        return false;
    }


    const FVector MyLocation =
        OwnerPawn->GetActorLocation();


    const FVector CandidateLocation =
        CandidateActor->GetActorLocation();


    // =========================================================
    // 距離判定
    // =========================================================

    const float DistanceSq =
        FVector::DistSquared2D(
            MyLocation,
            CandidateLocation
        );


    const float VisionDistanceSq =
        VisionDistance
        * VisionDistance;


    if (DistanceSq > VisionDistanceSq)
    {
        return false;
    }


    // =========================================================
    // 視野角判定
    //
    // VisionHalfAngle = 30なら
    //
    // -30 ～ +30
    //
    // 合計60度の扇形
    // =========================================================

    const float Angle =
        GetVisionAngleToActor(
            CandidateActor
        );


    if (
        FMath::Abs(Angle)
        > VisionHalfAngle
    )
    {
        return false;
    }


    // =========================================================
    // LineTrace
    // =========================================================

    FVector TraceStart =
        MyLocation;


    FVector TraceEnd =
        CandidateLocation;


    TraceStart.Z +=
        VisionTraceHeight;


    TraceEnd.Z +=
        VisionTraceHeight;


    FCollisionQueryParams QueryParams;


    // 自分自身には当てない
    QueryParams.AddIgnoredActor(
        OwnerPawn
    );


    FHitResult HitResult;


    const bool bHit =
        GetWorld()->LineTraceSingleByChannel(
            HitResult,
            TraceStart,
            TraceEnd,
            ECC_Visibility,
            QueryParams
        );

        // =========================================================
        // Debug：視線のLineTraceを表示
        // =========================================================

        if (bHit)
        {
            // 候補の魚そのものに当たった
            if (HitResult.GetActor() == CandidateActor)
            {
                DrawDebugLine(
                    GetWorld(),
                    TraceStart,
                    TraceEnd,
                    FColor::Green,
                    false,
                    0.15f,
                    0,
                    3.0f
                );
            }
            else
            {
                // 壁・別Actorで遮られた
                DrawDebugLine(
                    GetWorld(),
                    TraceStart,
                    HitResult.ImpactPoint,
                    FColor::Red,
                    false,
                    0.15f,
                    0,
                    3.0f
                );

                // 本来到達したかった魚まで
                DrawDebugLine(
                    GetWorld(),
                    HitResult.ImpactPoint,
                    TraceEnd,
                    FColor::Yellow,
                    false,
                    0.15f,
                    0,
                    1.5f
                );
            }
        }
        else
        {
            // 途中に何も遮蔽物がなかった
            DrawDebugLine(
                GetWorld(),
                TraceStart,
                TraceEnd,
                FColor::Green,
                false,
                0.15f,
                0,
                3.0f
            );
        }


    // =========================================================
    // 何かに当たった場合
    // =========================================================

    if (bHit)
    {
        AActor* HitActor =
            HitResult.GetActor();


        // 対象魚より先に別Actorへ当たった
        // = 壁などで遮られている
        if (HitActor != CandidateActor)
        {
            return false;
        }
    }


    /*
     * bHit == false の場合も視認成功扱い。
     *
     * 理由：
     * CandidateActor側がVisibilityをBlockしていない場合、
     * 対象そのものにはTraceがHitしないことがある。
     *
     * TraceEndまでの途中に壁がなければ、
     * 「遮蔽物なし」と判断できる。
     */


    return true;
}

// =========================================================
// SpawnMove / Target
// =========================================================

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
// Devour
// =========================================================

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


bool UEnemyAIBrainComponent::IsTargetPlanktonValid() const
{
    return IsValid(TargetPlanktonActor);
}


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


float UEnemyAIBrainComponent::GetGoToHookChanceByPlanktonCount() const
{
    const int32 CurrentItemAmount =
        GetOwnerItemAmount();

    if (CurrentItemAmount >= 40)
    {
        return 1.0f;
    }

    if (CurrentItemAmount >= 30)
    {
        return 0.5f;
    }

    if (CurrentItemAmount >= 20)
    {
        return 0.25f;
    }

    if (CurrentItemAmount >= 10)
    {
        return 0.125f;
    }

    return 0.0f;
}


// =========================================================
// ItemAmount参照
// =========================================================

int32 UEnemyAIBrainComponent::GetOwnerItemAmount() const
{
    AActor* OwnerActor = GetOwner();

    if (!OwnerActor)
    {
        return EatenPlanktonCount;
    }

    FIntProperty* ItemAmountProperty =
        FindFProperty<FIntProperty>(
            OwnerActor->GetClass(),
            TEXT("ItemAmount")
        );

    if (!ItemAmountProperty)
    {
        ItemAmountProperty =
            FindFProperty<FIntProperty>(
                OwnerActor->GetClass(),
                TEXT("ItemAmout")
            );
    }

    if (!ItemAmountProperty)
    {
        return EatenPlanktonCount;
    }

    return ItemAmountProperty->GetPropertyValue_InContainer(
        OwnerActor
    );
}


// =========================================================
// Attack
// =========================================================

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


bool UEnemyAIBrainComponent::IsTargetFishValid() const
{
    return IsValidAttackCandidate(TargetFishActor);
}


bool UEnemyAIBrainComponent::SelectNearestAttackTarget()
{
    AActor* NewTargetActor =
        FindNearestAttackTarget();

    if (!NewTargetActor)
    {
        ReleaseCurrentTargetActor();
        return false;
    }

    ReserveTargetActor(NewTargetActor);

    return true;
}


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


bool UEnemyAIBrainComponent::IsTargetInAttackAngle() const
{
    if (
        !OwnerPawn
        || !IsTargetFishValid()
    )
    {
        return false;
    }

    FVector ToTarget =
        TargetFishActor->GetActorLocation()
        - OwnerPawn->GetActorLocation();

    ToTarget.Z = 0.0f;

    if (ToTarget.IsNearlyZero())
    {
        return true;
    }

    const FVector TargetDirection =
        ToTarget.GetSafeNormal();

    FVector ForwardDirection =
        OwnerPawn->GetActorForwardVector();

    ForwardDirection.Z = 0.0f;
    ForwardDirection.Normalize();

    const float Dot =
        FVector::DotProduct(
            ForwardDirection,
            TargetDirection
        );

    const float Angle =
        FMath::RadiansToDegrees(
            FMath::Acos(
                FMath::Clamp(
                    Dot,
                    -1.0f,
                    1.0f
                )
            )
        );

    return Angle <= AttackStartAngle;
}


void UEnemyAIBrainComponent::FinishAttackTemporarily()
{
    if (OwnerAIController)
    {
        OwnerAIController->StopMovement();
    }

    bHasSteeringTargetLocation = false;

    ReleaseCurrentTargetActor();

    ReturnFromAttack();
}


void UEnemyAIBrainComponent::ReturnFromAttack()
{
    ReleaseCurrentTargetActor();

    bHasSteeringTargetLocation = false;

    if (bReturnToGoToHookAfterAttack)
    {
        SetAIMode(EEnemyAIMode::GoToHook);
        return;
    }

    SetAIMode(EEnemyAIMode::Wander);
}


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
     * ここでBP_PlayerBase側の魚種変更処理を呼ぶ。
     */
}


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

    if (
        bIgnoreActorsInSafeZone
        && CandidateActor->ActorHasTag(
            SafeZoneActorTagName
        )
    )
    {
        return false;
    }

    if (IsActorTargetedByOtherAI(CandidateActor))
    {
        return false;
    }

    return true;
}


// =========================================================
// ターゲット予約管理
// =========================================================

void UEnemyAIBrainComponent::CleanupInvalidTargetReservations()
{
    for (
        auto It = TargetReservations.CreateIterator();
        It;
        ++It
    )
    {
        const TWeakObjectPtr<AActor> TargetActor =
            It.Key();

        const TWeakObjectPtr<UEnemyAIBrainComponent> BrainComponent =
            It.Value();

        if (
            !TargetActor.IsValid()
            || !BrainComponent.IsValid()
        )
        {
            It.RemoveCurrent();
        }
    }
}


bool UEnemyAIBrainComponent::IsActorTargetedByOtherAI(
    AActor* CandidateActor
) const
{
    if (!IsValid(CandidateActor))
    {
        return false;
    }

    CleanupInvalidTargetReservations();

    const TWeakObjectPtr<AActor> CandidateKey(CandidateActor);

    const TWeakObjectPtr<UEnemyAIBrainComponent>* FoundBrain =
        TargetReservations.Find(CandidateKey);

    if (!FoundBrain)
    {
        return false;
    }

    if (!FoundBrain->IsValid())
    {
        return false;
    }

    return FoundBrain->Get() != this;
}


void UEnemyAIBrainComponent::ReserveTargetActor(
    AActor* NewTargetActor
)
{
    if (!IsValid(NewTargetActor))
    {
        return;
    }

    CleanupInvalidTargetReservations();

    if (TargetFishActor != NewTargetActor)
    {
        ResetBoredom();
    }

    if (
        IsValid(TargetFishActor)
        && TargetFishActor != NewTargetActor
    )
    {
        ReleaseCurrentTargetActor();
    }

    TargetFishActor = NewTargetActor;

    TargetLocation =
        NewTargetActor->GetActorLocation();

    const TWeakObjectPtr<AActor> TargetKey(NewTargetActor);

    TargetReservations.FindOrAdd(TargetKey) = this;
}


void UEnemyAIBrainComponent::ReleaseCurrentTargetActor()
{
    CleanupInvalidTargetReservations();


    if (IsValid(TargetFishActor))
    {
        const TWeakObjectPtr<AActor> TargetKey(
            TargetFishActor
        );


        const TWeakObjectPtr<UEnemyAIBrainComponent>* FoundBrain =
            TargetReservations.Find(
                TargetKey
            );


        if (
            FoundBrain
            && FoundBrain->IsValid()
            && FoundBrain->Get() == this
        )
        {
            TargetReservations.Remove(
                TargetKey
            );
        }
    }


    // =========================================================
    // この魚への攻撃は終了
    // 飽きを0へ戻す
    // =========================================================

    ResetBoredom();


    TargetFishActor = nullptr;
}


// =========================================================
// Stuck Detection
// =========================================================

void UEnemyAIBrainComponent::UpdateStuckDetection(
    float DeltaTime
)
{
    if (!bUseStuckDetection)
    {
        return;
    }

    if (!OwnerPawn || !GetWorld())
    {
        return;
    }

    if (
        AIAttackComponent
        && AIAttackComponent->IsAttacking()
    )
    {
        ResetStuckDetection();
        return;
    }

    if (
        CurrentMode != EEnemyAIMode::Attack
        && CurrentMode != EEnemyAIMode::GoToHook
    )
    {
        ResetStuckDetection();
        return;
    }

    const FVector CurrentLocation =
        OwnerPawn->GetActorLocation();

    if (!bInitializedStuckCheck)
    {
        LastStuckCheckLocation = CurrentLocation;

        LastMovedEnoughTime =
            GetWorld()->GetTimeSeconds();

        bInitializedStuckCheck = true;
        return;
    }

    const float MovedDistance =
        FVector::Dist2D(
            CurrentLocation,
            LastStuckCheckLocation
        );

    const bool bIsAttackAimingButNotAligned =
        CurrentMode == EEnemyAIMode::Attack
        && bAimBeforeAttack
        && IsTargetFishValid()
        && IsNearAttackTarget()
        && !IsTargetInAttackAngle();

    if (
        MovedDistance >= StuckMoveThreshold
        && !bIsAttackAimingButNotAligned
    )
    {
        LastStuckCheckLocation = CurrentLocation;

        LastMovedEnoughTime =
            GetWorld()->GetTimeSeconds();

        return;
    }

    if (IsStuckNow())
    {
        HandleStuck();
    }
}


void UEnemyAIBrainComponent::ResetStuckDetection()
{
    if (!OwnerPawn || !GetWorld())
    {
        bInitializedStuckCheck = false;
        return;
    }

    LastStuckCheckLocation =
        OwnerPawn->GetActorLocation();

    LastMovedEnoughTime =
        GetWorld()->GetTimeSeconds();

    bInitializedStuckCheck = true;
}


bool UEnemyAIBrainComponent::IsStuckNow() const
{
    if (!GetWorld())
    {
        return false;
    }

    return
        GetWorld()->GetTimeSeconds()
        - LastMovedEnoughTime
        >= StuckTimeThreshold;
}


void UEnemyAIBrainComponent::HandleStuck()
{
    switch (CurrentMode)
    {
    case EEnemyAIMode::GoToHook:
        HandleGoToHookStuck();
        break;

    case EEnemyAIMode::Attack:
        HandleAttackStuck();
        break;

    default:
        ResetStuckDetection();
        break;
    }
}


void UEnemyAIBrainComponent::HandleGoToHookStuck()
{
    TargetHookActor = nullptr;
    bWaitingForHook = false;
    bHasSteeringTargetLocation = false;

    if (OwnerAIController)
    {
        OwnerAIController->StopMovement();
    }

    ResetStuckDetection();

    RequestActiveHookSearch();

    if (!IsHookTargetValid())
    {
        bWaitingForHook = true;

        SetAIMode(EEnemyAIMode::Wander);

        return;
    }

    SetAIMode(EEnemyAIMode::GoToHook);
}


void UEnemyAIBrainComponent::HandleAttackStuck()
{
    AActor* OldTargetActor =
        TargetFishActor;

    FVector AwayDirection =
        FVector::ZeroVector;

    if (OwnerPawn && IsValid(OldTargetActor))
    {
        AwayDirection =
            OwnerPawn->GetActorLocation()
            - OldTargetActor->GetActorLocation();

        AwayDirection.Z = 0.0f;
    }

    if (AwayDirection.IsNearlyZero())
    {
        AwayDirection = FVector(
            FMath::FRandRange(-1.0f, 1.0f),
            FMath::FRandRange(-1.0f, 1.0f),
            0.0f
        );
    }

    AwayDirection.Normalize();

    if (
        AIAttackComponent
        && AIAttackComponent->IsAttacking()
    )
    {
        AIAttackComponent->CancelDashAttack();
    }

    if (OwnerAIController)
    {
        OwnerAIController->StopMovement();
    }

    ReleaseCurrentTargetActor();

    bHasSteeringTargetLocation = false;

    ResetStuckDetection();

    SetAIMode(EEnemyAIMode::Wander);

    LockAttackForSeconds(StuckAttackLockDuration);

    MoveToStuckEscapeLocation(AwayDirection);

    SetNextDecisionTime(
        StuckAttackLockDuration,
        StuckAttackLockDuration + 0.25f
    );
}


bool UEnemyAIBrainComponent::MoveToStuckEscapeLocation(
    const FVector& BaseAwayDirection
)
{
    if (!OwnerPawn)
    {
        return false;
    }

    UNavigationSystemV1* NavSys =
        UNavigationSystemV1::GetCurrent(GetWorld());

    if (!NavSys)
    {
        return false;
    }

    FVector EscapeDirection =
        BaseAwayDirection;

    EscapeDirection.Z = 0.0f;

    if (EscapeDirection.IsNearlyZero())
    {
        return false;
    }

    EscapeDirection.Normalize();

    const FVector RandomDirection =
        FVector(
            FMath::FRandRange(-0.5f, 0.5f),
            FMath::FRandRange(-0.5f, 0.5f),
            0.0f
        );

    EscapeDirection =
        (EscapeDirection + RandomDirection)
        .GetSafeNormal();

    const FVector CandidateLocation =
        OwnerPawn->GetActorLocation()
        + EscapeDirection
        * StuckEscapeDistance;

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

    SteeringTargetLocation =
        ProjectedLocation.Location;

    bHasSteeringTargetLocation = true;

    return true;
}


// =========================================================
// Flee
// =========================================================

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
// GoToHook
// =========================================================

bool UEnemyAIBrainComponent::ShouldForceGoToHook() const
{
    return
        GetOwnerItemAmount()
        >= ForceGoToHookPlanktonCount;
}


bool UEnemyAIBrainComponent::IsHookTargetValid() const
{
    return
        IsValid(TargetHookActor)
        && IsHookTargetActive();
}


bool UEnemyAIBrainComponent::IsHookTargetActive() const
{
    if (!IsValid(TargetHookActor))
    {
        return false;
    }

    bool bGoalActive = true;

    if (TryCallBoolFunctionNoParams(
        TargetHookActor,
        TEXT("IsActive"),
        bGoalActive
    ))
    {
        return bGoalActive;
    }

    if (TryCallBoolFunctionNoParams(
        TargetHookActor,
        TEXT("Is Active"),
        bGoalActive
    ))
    {
        return bGoalActive;
    }

    /*
     * 関数が見つからなかった場合だけtrue扱い。
     *
     * 理想はBP_GoalPoint側の関数名を
     * IsActive に統一すること。
     */
    return true;
}


bool UEnemyAIBrainComponent::TryCallBoolFunctionNoParams(
    AActor* TargetActor,
    const FName& FunctionName,
    bool& OutReturnValue
) const
{
    OutReturnValue = false;

    if (!IsValid(TargetActor))
    {
        return false;
    }

    UFunction* Function =
        TargetActor->FindFunction(FunctionName);

    if (!Function)
    {
        return false;
    }

    struct FBoolReturnParams
    {
        bool ReturnValue = false;
    };

    FBoolReturnParams Params;

    TargetActor->ProcessEvent(
        Function,
        &Params
    );

    OutReturnValue = Params.ReturnValue;

    return true;
}


void UEnemyAIBrainComponent::RequestActiveHookSearch()
{
    AActor* OwnerActor = GetOwner();

    if (!OwnerActor || !OwnerActor->HasAuthority())
    {
        return;
    }

    if (GetOwnerItemAmount() <= 0)
    {
        TargetHookActor = nullptr;
        bWaitingForHook = false;
        return;
    }

    OnHookSearchRequested.Broadcast();

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


bool UEnemyAIBrainComponent::TryAttackDuringGoToHook()
{
    if (!CanEnterAttackMode())
    {
        return false;
    }

    AActor* NearbyEnemy =
        FindNearestGoToHookEnemy();

    if (!NearbyEnemy)
    {
        return false;
    }

    const float AttackChance =
        CalculateGoToHookAttackChance();

    if (AttackChance <= 0.0f)
    {
        return false;
    }

    if (!TryChance(AttackChance))
    {
        return false;
    }

    ReserveTargetActor(NearbyEnemy);

    SetAIMode(EEnemyAIMode::Attack);

    return CurrentMode == EEnemyAIMode::Attack;
}


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

    if (DistanceToHook <= NoAttackDistance)
    {
        return 0.0f;
    }

    if (DistanceToHook >= MaxAttackDistance)
    {
        return GoToHookMaxAttackChance;
    }

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
// AttackLock
// =========================================================

bool UEnemyAIBrainComponent::CanEnterAttackMode() const
{
    return !IsAttackLocked();
}


// =========================================================
// Aルール / Steering
// =========================================================

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

    FVector GoalDirection =
        GoalLocation - MyLocation;

    GoalDirection.Z = 0.0f;

    GoalDirection =
        GoalDirection.GetSafeNormal();

    const FVector AvoidDirection =
        CalculateAvoidDirection();

    FVector RandomDirection =
        FVector(
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

    FinalDirection =
        FinalDirection.GetSafeNormal();

    FVector CandidateLocation =
        MyLocation
        + FinalDirection
        * ARuleStepDistance;

    const float DistanceToGoal =
        FVector::Dist2D(
            MyLocation,
            GoalLocation
        );

    if (DistanceToGoal < ARuleStepDistance)
    {
        CandidateLocation = GoalLocation;
    }

    return CandidateLocation;
}


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

    if (bUseForwardOnlyMovement)
    {
        SteeringTargetLocation =
            ProjectedLocation.Location;

        bHasSteeringTargetLocation = true;

        return true;
    }

    OwnerAIController->MoveToLocation(
        ProjectedLocation.Location,
        AcceptanceRadius
    );

    return true;
}


bool UEnemyAIBrainComponent::MoveForwardOnlyToLocation(
    const FVector& GoalLocation,
    float DeltaTime
)
{
    if (!OwnerPawn)
    {
        return false;
    }

    const FVector MyLocation =
        OwnerPawn->GetActorLocation();

    FVector ToGoal =
        GoalLocation - MyLocation;

    ToGoal.Z = 0.0f;

    if (ToGoal.IsNearlyZero())
    {
        return false;
    }

    const FVector DesiredDirection =
        ToGoal.GetSafeNormal();

    const FRotator DesiredRotation =
        DesiredDirection.Rotation();

    const FRotator CurrentRotation =
        OwnerPawn->GetActorRotation();

    const FRotator CurrentYawRotation(
        0.0f,
        CurrentRotation.Yaw,
        0.0f
    );

    const FRotator TargetYawRotation(
        0.0f,
        DesiredRotation.Yaw,
        0.0f
    );

    const FRotator NewRotation =
        FMath::RInterpTo(
            CurrentYawRotation,
            TargetYawRotation,
            DeltaTime,
            TurnInterpSpeed
        );

    OwnerPawn->SetActorRotation(NewRotation);

    const float DeltaYaw =
        FMath::Abs(
            FMath::FindDeltaAngleDegrees(
                CurrentRotation.Yaw,
                DesiredRotation.Yaw
            )
        );

    float MoveScale = ForwardMoveInputScale;

    if (bSlowDownWhenTurning && DeltaYaw >= SlowDownTurnAngle)
    {
        MoveScale = MinTurnMoveScale;
    }

    if (
        CurrentMode == EEnemyAIMode::Attack
        && bAimBeforeAttack
        && IsTargetFishValid()
        && IsNearAttackTarget()
        && !IsTargetInAttackAngle()
    )
    {
        MoveScale = AttackAimMoveScale;
    }

    OwnerPawn->AddMovementInput(
        OwnerPawn->GetActorForwardVector(),
        MoveScale
    );

    return true;
}


// =========================================================
// 汎用
// =========================================================

bool UEnemyAIBrainComponent::TryChance(
    float Chance
) const
{
    return FMath::FRand() <= Chance;
}


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

// =========================================================
// Mode Entry Dash
// =========================================================

void UEnemyAIBrainComponent::TryStartModeEntryDash(
    EEnemyAIMode EnteredMode
)
{
    if (
        !OwnerPawn
        || !AIAttackComponent
    )
    {
        return;
    }

    // =========================================================
    // Attack以外へ移ったら、
    // 古い退避ダッシュ予約・連続状態を破棄
    // =========================================================
    if (EnteredMode != EEnemyAIMode::Attack)
    {
        ResetAttackRetreatDash();
    }

    switch (EnteredMode)
    {
        case EEnemyAIMode::SpawnMove:
        {
            TryStartSpawnMoveDash();
            break;
        }


        case EEnemyAIMode::Devour:
        {
            TryStartDevourDash();
            break;
        }


        case EEnemyAIMode::Flee:
        {
            TryStartFleeDash();
            break;
        }


        case EEnemyAIMode::Attack:
        {
            // Attackへ入った瞬間には退避しない。
            // 退避ダッシュは、
            // 「同じ魚への攻撃継続」が決まった時だけ行う。
            break;
        }


        case EEnemyAIMode::GoToHook:
        {
            TryStartGoToHookDash();
            break;
        }


        case EEnemyAIMode::Wander:
        default:
        {
            break;
        }
    }
}


// =========================================================
// SpawnMove
// =========================================================

bool UEnemyAIBrainComponent::TryStartSpawnMoveDash()
{
    if (
        !OwnerPawn
        || !AIAttackComponent
    )
    {
        return false;
    }


    // 85%抽選
    if (!TryChance(SpawnMoveDashChance))
    {
        return false;
    }


    /*
     * SpawnMoveではTargetGroupActorがある場合、
     * 最新の群れ位置を使用する。
     */
    if (!IsValid(TargetGroupActor))
    {
        return false;
    }

    TargetLocation =
        TargetGroupActor->GetActorLocation();

    return StartExistingDashTowardLocation(
        TargetLocation
    );
}


// =========================================================
// Flee
// =========================================================

bool UEnemyAIBrainComponent::TryStartFleeDash()
{
    if (
        !OwnerPawn
        || !AIAttackComponent
    )
    {
        return false;
    }


    // 基本100%
    if (!TryChance(FleeDashChance))
    {
        return false;
    }


    AActor* FleeSourceActor =
        GetFleeSourceActor();


    if (!IsValid(FleeSourceActor))
    {
        return false;
    }


    return StartExistingDashAwayFromActor(
        FleeSourceActor
    );
}


// =========================================================
// Devour
// =========================================================

bool UEnemyAIBrainComponent::TryStartDevourDash()
{
    if (
        !OwnerPawn
        || !AIAttackComponent
    )
    {
        return false;
    }


    if (!IsTargetPlanktonValid())
    {
        return false;
    }


    /*
     * 通常50%。
     *
     * 視界内に、
     * 同じプランクトン群へ向かって
     * 実際にダッシュ中の魚がいる場合は90%。
     */
    const bool bHasCompetitor =
        HasVisibleFishDashingToSamePlankton();


    const float DashChance =
        bHasCompetitor
        ? DevourCompetitiveDashChance
        : DevourDashChance;


    if (!TryChance(DashChance))
    {
        return false;
    }


    TargetLocation =
        TargetPlanktonActor->GetActorLocation();


    return StartExistingDashTowardLocation(
        TargetLocation
    );
}


// =========================================================
// Attack Retreat
// =========================================================

bool UEnemyAIBrainComponent::TryStartAttackRetreatDash()
{
    ResetAttackRetreatDash();

    if (
        !OwnerPawn
        || !AIAttackComponent
        || !IsTargetFishValid()
    )
    {
        return false;
    }


    // 今も同じ攻撃対象が視界内にいるか
    if (!CanSeeFishActor(TargetFishActor))
    {
        return false;
    }

    const int32 MinDashCount =
        FMath::Max(
            1,
            FMath::Min(
                AttackRetreatDashCountMin,
                AttackRetreatDashCountMax
            )
        );


    const int32 MaxDashCount =
        FMath::Max(
            MinDashCount,
            FMath::Max(
                AttackRetreatDashCountMin,
                AttackRetreatDashCountMax
            )
        );


    RemainingAttackRetreatDashCount =
        FMath::RandRange(
            MinDashCount,
            MaxDashCount
        );


    bAttackRetreatDashSequenceActive =
        true;


    AttackRetreatDashDirection =
        CalculateAttackRetreatDashDirection();


    if (AttackRetreatDashDirection.IsNearlyZero())
    {
        ResetAttackRetreatDash();
        return false;
    }


    const bool bStarted =
        StartExistingDashInDirection(
            AttackRetreatDashDirection
        );


    if (!bStarted)
    {
        ResetAttackRetreatDash();
        return false;
    }


    /*
     * 今開始した1回分を消費。
     */
    RemainingAttackRetreatDashCount--;


    return true;
}


// =========================================================
// Attack Retreat 継続
// =========================================================

bool UEnemyAIBrainComponent::TryContinueAttackRetreatDash()
{
    if (!bAttackRetreatDashSequenceActive)
    {
        return false;
    }


    /*
     * まだ前のダッシュが実行中なら何もしない。
     *
     * 通常はTickComponent側で先にreturnされるので
     * ここまで来ないが、保険として残す。
     */
    if (
        AIAttackComponent
        && AIAttackComponent->IsAttacking()
    )
    {
        return true;
    }


    /*
     * 全回数終了。
     *
     * ここから通常Attack処理へ戻す。
     */
    if (RemainingAttackRetreatDashCount <= 0)
    {
        ResetAttackRetreatDash();

        return false;
    }


    /*
     * ターゲットが消えていたら終了。
     */
    if (!IsTargetFishValid())
    {
        ResetAttackRetreatDash();

        return false;
    }


    /*
     * 2回目は現在の相手位置を基準に
     * もう一度退避方向を作り直す。
     */
    AttackRetreatDashDirection =
        CalculateAttackRetreatDashDirection();


    if (AttackRetreatDashDirection.IsNearlyZero())
    {
        ResetAttackRetreatDash();

        return false;
    }


    const bool bStarted =
        StartExistingDashInDirection(
            AttackRetreatDashDirection
        );


    if (!bStarted)
    {
        ResetAttackRetreatDash();

        return false;
    }


    RemainingAttackRetreatDashCount--;


    return true;
}


// =========================================================
// GoToHook
// =========================================================

bool UEnemyAIBrainComponent::TryStartGoToHookDash()
{
    if (
        !OwnerPawn
        || !AIAttackComponent
    )
    {
        return false;
    }


    if (!IsHookTargetValid())
    {
        return false;
    }


    // 90%
    if (!TryChance(GoToHookDashChance))
    {
        return false;
    }


    TargetLocation =
        TargetHookActor->GetActorLocation();


    return StartExistingDashTowardLocation(
        TargetLocation
    );
}


// =========================================================
// 指定地点へ既存ダッシュ
// =========================================================

bool UEnemyAIBrainComponent::StartExistingDashTowardLocation(
    const FVector& Destination
)
{
    if (!OwnerPawn)
    {
        return false;
    }


    FVector DashDirection =
        Destination
        - OwnerPawn->GetActorLocation();


    DashDirection.Z = 0.0f;


    return StartExistingDashInDirection(
        DashDirection
    );
}


// =========================================================
// 指定Actorから離れる既存ダッシュ
// =========================================================

bool UEnemyAIBrainComponent::StartExistingDashAwayFromActor(
    AActor* SourceActor
)
{
    if (
        !OwnerPawn
        || !IsValid(SourceActor)
    )
    {
        return false;
    }


    FVector DashDirection =
        OwnerPawn->GetActorLocation()
        - SourceActor->GetActorLocation();


    DashDirection.Z = 0.0f;


    return StartExistingDashInDirection(
        DashDirection
    );
}


// =========================================================
// 指定方向へ既存ダッシュ
// =========================================================

bool UEnemyAIBrainComponent::StartExistingDashInDirection(
    const FVector& DashDirection
)
{
    if (
        !OwnerPawn
        || !AIAttackComponent
    )
    {
        return false;
    }


    if (AIAttackComponent->IsAttacking())
    {
        return false;
    }


    FVector FlatDirection =
        DashDirection;


    FlatDirection.Z = 0.0f;


    if (FlatDirection.IsNearlyZero())
    {
        return false;
    }


    FlatDirection.Normalize();


    /*
     * 古いMoveToを止める。
     *
     * これをしないとChargeTime中に、
     * AIControllerの以前の移動命令によって
     * 向きが変化する可能性がある。
     */
    if (OwnerAIController)
    {
        OwnerAIController->StopMovement();
    }


    /*
     * 既存ダッシュは
     * bUseCurrentForwardForDash = true
     *
     * なので、先にActorを目的方向へ向ける。
     */
    FRotator DashRotation =
        FlatDirection.Rotation();


    DashRotation.Pitch = 0.0f;
    DashRotation.Roll = 0.0f;


    OwnerPawn->SetActorRotation(
        DashRotation
    );


    /*
     * 新しいダッシュではない。
     *
     * 通常攻撃でも使用している
     * StartDashAttackそのものを使う。
     *
     * TargetActorは不要なのでnullptr。
     *
     * AttackSphereも通常どおり有効になる。
     */
    return AIAttackComponent->StartDashAttack(
        nullptr
    );
}


// =========================================================
// 同じプランクトン群へダッシュしている魚
// =========================================================

bool UEnemyAIBrainComponent::
HasVisibleFishDashingToSamePlankton() const
{
    if (
        !OwnerPawn
        || !GetWorld()
        || !IsValid(TargetGroupActor)
    )
    {
        return false;
    }


    TArray<AActor*> FishActors;


    UGameplayStatics::GetAllActorsWithTag(
        GetWorld(),
        FishTagName,
        FishActors
    );


    const FVector MyLocation =
        OwnerPawn->GetActorLocation();


    const float VisionDistanceSq =
        VisionDistance
        * VisionDistance;


    for (AActor* FishActor : FishActors)
    {
        if (
            !IsValid(FishActor)
            || FishActor == OwnerPawn
        )
        {
            continue;
        }


        UEnemyAIBrainComponent* OtherBrain =
            FishActor
            ->FindComponentByClass<
                UEnemyAIBrainComponent
            >();


        if (!OtherBrain)
        {
            continue;
        }


        /*
         * 同じプランクトン群を目指しているか。
         *
         * 個々のPlanktonActorではなく、
         * TargetGroupActor単位で判定する。
         */
        if (
            !IsValid(OtherBrain->TargetGroupActor)
            || OtherBrain->TargetGroupActor
                != TargetGroupActor
        )
        {
            continue;
        }


        /*
         * Devour中か。
         */
        if (
            OtherBrain->CurrentMode
            != EEnemyAIMode::Devour
        )
        {
            continue;
        }


        /*
         * 本当に「ダッシュ中」か。
         *
         * IsAttackingではなくIsDashingなので、
         * チャージ中は競争判定に入れない。
         */
        if (
            !OtherBrain->AIAttackComponent
            || !OtherBrain
                ->AIAttackComponent
                ->IsDashing()
        )
        {
            continue;
        }


        // =====================================================
        // 視界距離
        // =====================================================

        const float DistanceSq =
            FVector::DistSquared2D(
                MyLocation,
                FishActor->GetActorLocation()
            );


        if (DistanceSq > VisionDistanceSq)
        {
            continue;
        }


        // =====================================================
        // 視野角
        // =====================================================

        const float Angle =
            GetVisionAngleToActor(
                FishActor
            );


        if (
            FMath::Abs(Angle)
            > VisionHalfAngle
        )
        {
            continue;
        }


        // =====================================================
        // 壁チェック
        // =====================================================

        FVector TraceStart =
            MyLocation;


        FVector TraceEnd =
            FishActor->GetActorLocation();


        TraceStart.Z +=
            VisionTraceHeight;


        TraceEnd.Z +=
            VisionTraceHeight;


        FCollisionQueryParams QueryParams;

        QueryParams.AddIgnoredActor(
            OwnerPawn
        );


        FHitResult HitResult;


        const bool bHit =
            GetWorld()
            ->LineTraceSingleByChannel(
                HitResult,
                TraceStart,
                TraceEnd,
                ECC_Visibility,
                QueryParams
            );


        /*
         * 何かに遮られていて、
         * そのHitActorが対象魚ではないなら見えない。
         */
        if (
            bHit
            && HitResult.GetActor()
                != FishActor
        )
        {
            continue;
        }


        return true;
    }


    return false;
}


// =========================================================
// Attack退避方向
// =========================================================

FVector UEnemyAIBrainComponent::
CalculateAttackRetreatDashDirection() const
{
    if (
        !OwnerPawn
        || !IsTargetFishValid()
    )
    {
        return FVector::ZeroVector;
    }


    /*
     * 相手 → 自分
     *
     * つまり相手から離れる方向。
     */
    FVector AwayDirection =
        OwnerPawn->GetActorLocation()
        - TargetFishActor->GetActorLocation();


    AwayDirection.Z = 0.0f;


    if (AwayDirection.IsNearlyZero())
    {
        AwayDirection =
            -OwnerPawn
            ->GetActorForwardVector();


        AwayDirection.Z = 0.0f;
    }


    if (AwayDirection.IsNearlyZero())
    {
        return FVector::ZeroVector;
    }


    AwayDirection.Normalize();


    /*
     * 毎回完全な真後ろだと単調なので、
     * 左右へ少しランダム化する。
     */
    const float RandomAngle =
        FMath::FRandRange(
            -FMath::Abs(
                AttackRetreatRandomAngle
            ),
            FMath::Abs(
                AttackRetreatRandomAngle
            )
        );


    FVector ResultDirection =
        AwayDirection.RotateAngleAxis(
            RandomAngle,
            FVector::UpVector
        );


    ResultDirection.Z = 0.0f;


    return ResultDirection.GetSafeNormal();
}


// =========================================================
// Attack退避状態リセット
// =========================================================

void UEnemyAIBrainComponent::
ResetAttackRetreatDash()
{
    bPendingAttackRetreatDash = 
        false;

    bAttackRetreatDashSequenceActive =
        false;


    RemainingAttackRetreatDashCount =
        0;


    AttackRetreatDashDirection =
        FVector::ZeroVector;
}

void UEnemyAIBrainComponent::DrawVisionConeDebug() const
{
    if (
        !bDrawVisionConeDebug
        || !OwnerPawn
        || !GetWorld()
    )
    {
        return;
    }

    const FVector Origin =
        OwnerPawn->GetActorLocation()
        + FVector(0.0f, 0.0f, VisionTraceHeight);


    FVector Forward =
        OwnerPawn->GetActorForwardVector();

    Forward.Z = 0.0f;

    if (Forward.IsNearlyZero())
    {
        return;
    }

    Forward.Normalize();


    const FRotator ForwardRotation =
        Forward.Rotation();


    const FRotator LeftRotation(
        0.0f,
        ForwardRotation.Yaw - VisionHalfAngle,
        0.0f
    );


    const FRotator RightRotation(
        0.0f,
        ForwardRotation.Yaw + VisionHalfAngle,
        0.0f
    );


    const FVector LeftDirection =
        LeftRotation.Vector();


    const FVector RightDirection =
        RightRotation.Vector();


    const FVector LeftEnd =
        Origin
        + LeftDirection * VisionDistance;


    const FVector RightEnd =
        Origin
        + RightDirection * VisionDistance;


    // =====================================================
    // 左右の境界線
    // =====================================================

    DrawDebugLine(
        GetWorld(),
        Origin,
        LeftEnd,
        FColor::Cyan,
        false,
        VisionConeDebugDuration,
        0,
        VisionConeDebugThickness
    );


    DrawDebugLine(
        GetWorld(),
        Origin,
        RightEnd,
        FColor::Cyan,
        false,
        VisionConeDebugDuration,
        0,
        VisionConeDebugThickness
    );


    // =====================================================
    // 正面ライン
    // =====================================================

    DrawDebugLine(
        GetWorld(),
        Origin,
        Origin + Forward * VisionDistance,
        FColor::Blue,
        false,
        VisionConeDebugDuration,
        0,
        VisionConeDebugThickness
    );


    // =====================================================
    // 扇形の円弧
    // =====================================================

    constexpr int32 ArcSegments = 16;

    FVector PreviousPoint = LeftEnd;


    for (int32 Index = 1; Index <= ArcSegments; ++Index)
    {
        const float Alpha =
            static_cast<float>(Index)
            / static_cast<float>(ArcSegments);


        const float CurrentAngle =
            FMath::Lerp(
                -VisionHalfAngle,
                VisionHalfAngle,
                Alpha
            );


        const FRotator CurrentRotation(
            0.0f,
            ForwardRotation.Yaw + CurrentAngle,
            0.0f
        );


        const FVector CurrentPoint =
            Origin
            + CurrentRotation.Vector()
            * VisionDistance;


        DrawDebugLine(
            GetWorld(),
            PreviousPoint,
            CurrentPoint,
            FColor::Cyan,
            false,
            VisionConeDebugDuration,
            0,
            VisionConeDebugThickness
        );


        PreviousPoint =
            CurrentPoint;
    }
}
