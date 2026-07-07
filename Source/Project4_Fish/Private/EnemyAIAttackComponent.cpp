#include "EnemyAIAttackComponent.h"

#include "AIController.h"
#include "EnemyAIBrainComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"

/**
 * AI専用攻撃コンポーネント。
 *
 * 役割：
 * - AI専用のチャージ処理
 * - チャージ後のダッシュ開放
 * - LaunchCharacterによる突進
 * - 突進中だけAttackSphereのCollisionをONにする
 * - 突進終了時にAttackSphereのCollisionをOFFにする
 * - AI攻撃用ダメージを計算する
 * - 必要ならEnemyAIBrainComponentへ攻撃終了通知
 *
 * 今回の流れ：
 * 1. EnemyAIBrainComponentのAttackモードからStartDashAttackを呼ぶ
 * 2. ChargeTime秒だけ待つ
 * 3. ターゲット方向へ向く
 * 4. CurrentDashSpeedを設定
 * 5. CurrentDamageAmountを計算
 * 6. AttackSphereのCollisionをON
 * 7. LaunchCharacterで突進
 * 8. DashDuration秒後にAttackSphereのCollisionをOFF
 * 9. 攻撃終了
 * 10. 必要ならBrainをWanderへ戻す
 */
UEnemyAIAttackComponent::UEnemyAIAttackComponent()
{
    // 今回はTimerで処理するのでTickは不要
    PrimaryComponentTick.bCanEverTick = false;
}

void UEnemyAIAttackComponent::BeginPlay()
{
    Super::BeginPlay();

    // このComponentを持っているActorをCharacterとして保持する
    // BP_EnemyはBP_PlayerBaseの子なので、Character系である想定
    OwnerCharacter = Cast<ACharacter>(GetOwner());

    // BP側にあるAttackSphereを探す
    FindAttackCollisionComponent();

    // ゲーム開始時は攻撃判定をOFFにしておく
    // これをしないと、攻撃していない時もOverlapが発生する可能性がある
    if (bDisableAttackCollisionOnBeginPlay)
    {
        SetAttackCollisionEnabled(false);
    }

    // 開始時点ではダメージなし
    CurrentDashSpeed = 0.0f;
    CurrentDamageAmount = 0.0f;
}

// =========================
// BP / Brain から呼ぶ公開関数
// =========================

/**
 * AI用ダッシュ攻撃を開始する。
 *
 * Brain側のAttackモードから呼ばれる想定。
 */
bool UEnemyAIAttackComponent::StartDashAttack(AActor* TargetActor)
{
    AActor* OwnerActor = GetOwner();

    // AI攻撃はサーバー側だけで開始する
    // クライアント側で開始すると同期ズレの原因になる
    if (!OwnerActor || !OwnerActor->HasAuthority())
    {
        return false;
    }

    // BeginPlay時に取れていなかった場合の保険
    if (!OwnerCharacter)
    {
        OwnerCharacter = Cast<ACharacter>(OwnerActor);
    }

    if (!OwnerCharacter)
    {
        return false;
    }

    if (!TargetActor)
    {
        return false;
    }

    // すでにチャージ中・ダッシュ中なら開始しない
    if (bIsAttacking)
    {
        return false;
    }

    CurrentTargetActor = TargetActor;

    bIsAttacking = true;
    bIsCharging = true;
    bIsDashing = false;

    // チャージ開始時点ではまだダッシュしていないので速度とダメージは0
    CurrentDashSpeed = 0.0f;
    CurrentDamageAmount = 0.0f;

    // チャージ中はまだ攻撃判定を出さない
    // つまり、チャージしているだけでは相手に当たらない
    SetAttackCollisionEnabled(false);

    // 既存タイマーを念のためクリア
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(ChargeTimerHandle);
        GetWorld()->GetTimerManager().ClearTimer(DashTimerHandle);

        // ChargeTime秒後にダッシュ開放
        GetWorld()->GetTimerManager().SetTimer(
            ChargeTimerHandle,
            this,
            &UEnemyAIAttackComponent::ReleaseDashAttack,
            ChargeTime,
            false
        );
    }

    return true;
}

/**
 * チャージ中またはダッシュ中ならtrue。
 */
bool UEnemyAIAttackComponent::IsAttacking() const
{
    return bIsAttacking;
}

/**
 * チャージ中ならtrue。
 */
bool UEnemyAIAttackComponent::IsCharging() const
{
    return bIsCharging;
}

/**
 * ダッシュ中ならtrue。
 */
bool UEnemyAIAttackComponent::IsDashing() const
{
    return bIsDashing;
}

/**
 * 現在のAI攻撃ダメージ量を返す。
 *
 * BPのAttackSphere BeginOverlap内で呼び、
 * BPI_Player Apply Damage に渡す。
 */
float UEnemyAIAttackComponent::GetCurrentDamageAmount() const
{
    return CurrentDamageAmount;
}

/**
 * 攻撃を強制終了する。
 *
 * 死亡時、モード強制変更時、デバッグ時などに使う。
 */
void UEnemyAIAttackComponent::CancelDashAttack()
{
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(ChargeTimerHandle);
        GetWorld()->GetTimerManager().ClearTimer(DashTimerHandle);
    }

    // 強制終了時も攻撃判定をOFFにする
    SetAttackCollisionEnabled(false);

    bIsAttacking = false;
    bIsCharging = false;
    bIsDashing = false;

    CurrentTargetActor = nullptr;

    // 強制終了時は速度とダメージをリセット
    CurrentDashSpeed = 0.0f;
    CurrentDamageAmount = 0.0f;

    SetOwnerMaxWalkSpeed(NormalMoveSpeed);

    if (bStopVelocityOnFinish)
    {
        StopOwnerVelocity();
    }

    if (bStopAIMovementOnFinish)
    {
        StopAIMovement();
    }
}

// =========================
// 内部処理
// =========================

/**
 * チャージ終了後、ダッシュを開放する。
 */
void UEnemyAIAttackComponent::ReleaseDashAttack()
{
    AActor* OwnerActor = GetOwner();

    if (!OwnerActor || !OwnerActor->HasAuthority())
    {
        CancelDashAttack();
        return;
    }

    if (!OwnerCharacter)
    {
        OwnerCharacter = Cast<ACharacter>(OwnerActor);
    }

    if (!OwnerCharacter)
    {
        CancelDashAttack();
        return;
    }

    if (!CurrentTargetActor)
    {
        CancelDashAttack();
        return;
    }

    bIsCharging = false;
    bIsDashing = true;

    // ダッシュ直前にAIのMoveToを止める
    // これをしないと、WanderやAttack接近中の移動方向が残って横突進になりやすい
    if (bStopAIMovementBeforeDash)
    {
        StopAIMovement();
    }

    const FVector DashDirection = CalculateDashDirection();

    if (DashDirection.IsNearlyZero())
    {
        CancelDashAttack();
        return;
    }

    // ダッシュ前にターゲット方向へ向く
    if (bFaceTargetBeforeDash)
    {
        FaceToDashDirection(DashDirection);
    }

    // プレイヤー側ADashに近づけるため、ダッシュ中のMaxWalkSpeedを上げる
    SetOwnerMaxWalkSpeed(DashMoveSpeed);

    // AIは基本最大チャージ扱い。
    // そのため、現在ダッシュ速度は最大速度として扱う。
    //
    // 後で「弱いAIは溜めが浅い」などを入れる場合は、
    // ここを CurrentDashSpeed = MaxDashSpeed * ChargeRate; のように変える。
    CurrentDashSpeed = MaxDashSpeed;

    // ダメージ量を計算する。
    //
    // 式：
    // CurrentDamageAmount = MaximumAttackPower * Clamp(CurrentDashSpeed / MaxDashSpeed, 0.0f, 1.0f)
    CalculateCurrentDamageAmount();

    // ダッシュ中だけ攻撃判定をONにする
    // 既存のAttackSphere BeginOverlap処理があるなら、このタイミングで発火可能になる
    if (bUseAttackCollisionDuringDash)
    {
        SetAttackCollisionEnabled(true);
    }

    // 地上移動想定なのでZは使わず、XY方向へ突進する
    OwnerCharacter->LaunchCharacter(
        DashDirection * DashPower,
        true,
        false
    );

    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(DashTimerHandle);

        // DashDuration秒後に攻撃終了
        GetWorld()->GetTimerManager().SetTimer(
            DashTimerHandle,
            this,
            &UEnemyAIAttackComponent::FinishDashAttack,
            DashDuration,
            false
        );
    }
}

/**
 * ダッシュ攻撃を終了する。
 */
void UEnemyAIAttackComponent::FinishDashAttack()
{
    // ダッシュ終了なので攻撃判定をOFFにする
    SetAttackCollisionEnabled(false);

    bIsAttacking = false;
    bIsCharging = false;
    bIsDashing = false;

    CurrentTargetActor = nullptr;

    // ダッシュ速度は終了したので0へ戻す
    // CurrentDamageAmountはデバッグしやすいように残してもよいが、
    // 攻撃していない時に誤って読まれないよう、ここでは0へ戻す。
    CurrentDashSpeed = 0.0f;
    CurrentDamageAmount = 0.0f;

    // 通常速度へ戻す
    SetOwnerMaxWalkSpeed(NormalMoveSpeed);

    // 終了時に速度を止める
    if (bStopVelocityOnFinish)
    {
        StopOwnerVelocity();
    }

    // AIControllerの移動も止める
    if (bStopAIMovementOnFinish)
    {
        StopAIMovement();
    }

    // 今回は簡易的に、攻撃終了後Wanderへ戻す
    // 後で「HP80%以上ならAttack継続」などを入れる時はここを調整する
    if (bReturnToWanderOnFinish)
    {
        NotifyBrainAttackFinished();
    }
}

/**
 * 現在のターゲット方向を計算する。
 */
FVector UEnemyAIAttackComponent::CalculateDashDirection() const
{
    if (!OwnerCharacter || !CurrentTargetActor)
    {
        return FVector::ZeroVector;
    }

    FVector Direction = CurrentTargetActor->GetActorLocation() - OwnerCharacter->GetActorLocation();

    // 水中風でも実体は地上移動なのでZは消す
    Direction.Z = 0.0f;

    return Direction.GetSafeNormal();
}

/**
 * ターゲット方向へOwnerの向きを合わせる。
 */
void UEnemyAIAttackComponent::FaceToDashDirection(const FVector& DashDirection)
{
    if (!OwnerCharacter)
    {
        return;
    }

    if (DashDirection.IsNearlyZero())
    {
        return;
    }

    FRotator NewRotation = DashDirection.Rotation();

    // 地上移動なのでPitch/Rollは使わない
    NewRotation.Pitch = 0.0f;
    NewRotation.Roll = 0.0f;

    // Actor本体を向ける
    OwnerCharacter->SetActorRotation(NewRotation);

    // Controllerの向きも合わせる
    // CharacterMovementやAIControllerが向きを上書きする対策
    if (bSetControllerRotationBeforeDash)
    {
        AController* Controller = OwnerCharacter->GetController();

        if (Controller)
        {
            Controller->SetControlRotation(NewRotation);
        }
    }
}

/**
 * CharacterMovementのMaxWalkSpeedを変更する。
 */
void UEnemyAIAttackComponent::SetOwnerMaxWalkSpeed(float NewSpeed)
{
    if (!OwnerCharacter)
    {
        return;
    }

    UCharacterMovementComponent* MoveComp = OwnerCharacter->GetCharacterMovement();

    if (!MoveComp)
    {
        return;
    }

    MoveComp->MaxWalkSpeed = NewSpeed;
}

/**
 * OwnerのVelocityを止める。
 */
void UEnemyAIAttackComponent::StopOwnerVelocity()
{
    if (!OwnerCharacter)
    {
        return;
    }

    UCharacterMovementComponent* MoveComp = OwnerCharacter->GetCharacterMovement();

    if (!MoveComp)
    {
        return;
    }

    MoveComp->Velocity = FVector::ZeroVector;
}

/**
 * AIControllerのMoveToを止める。
 */
void UEnemyAIAttackComponent::StopAIMovement()
{
    if (!OwnerCharacter)
    {
        return;
    }

    AController* Controller = OwnerCharacter->GetController();

    AAIController* AIController = Cast<AAIController>(Controller);

    if (!AIController)
    {
        return;
    }

    AIController->StopMovement();
}

/**
 * ダッシュ終了後にBrainへ通知する。
 *
 * 今回は簡易的にWanderへ戻す。
 */
void UEnemyAIAttackComponent::NotifyBrainAttackFinished()
{
    AActor* OwnerActor = GetOwner();

    if (!OwnerActor || !OwnerActor->HasAuthority())
    {
        return;
    }

    UEnemyAIBrainComponent* BrainComponent =
        OwnerActor->FindComponentByClass<UEnemyAIBrainComponent>();

    if (!BrainComponent)
    {
        return;
    }

    BrainComponent->SetAIMode(EEnemyAIMode::Wander);
}

/**
 * BP_Enemy / BP_PlayerBaseについているAttackSphereを探す。
 *
 * BPイベントは使わない。
 * C++からComponent名で直接探す。
 *
 * 注意：
 * - BP側のComponent名と AttackCollisionComponentName が一致している必要がある
 * - 例：BP側が AttackSphere なら、AttackCollisionComponentName も AttackSphere
 */
void UEnemyAIAttackComponent::FindAttackCollisionComponent()
{
    AttackCollisionComponent = nullptr;

    AActor* OwnerActor = GetOwner();

    if (!OwnerActor)
    {
        return;
    }

    TArray<UPrimitiveComponent*> PrimitiveComponents;
    OwnerActor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

    for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
    {
        if (!PrimitiveComponent)
        {
            continue;
        }

        // Component名が一致するものをAttackSphereとして扱う
        if (PrimitiveComponent->GetFName() == AttackCollisionComponentName)
        {
            AttackCollisionComponent = PrimitiveComponent;
            return;
        }
    }
}

/**
 * AttackSphereのCollisionをON/OFFする。
 *
 * ON：
 * - CollisionEnabled = QueryOnly
 * - GenerateOverlapEvents = true
 *
 * OFF：
 * - GenerateOverlapEvents = false
 * - CollisionEnabled = NoCollision
 *
 * これにより、
 * - 攻撃していない時はOverlapしない
 * - 突進中だけOverlapする
 */
void UEnemyAIAttackComponent::SetAttackCollisionEnabled(bool bEnabled)
{
    // まだ見つかっていなければ探す
    if (!AttackCollisionComponent)
    {
        FindAttackCollisionComponent();
    }

    if (!AttackCollisionComponent)
    {
        return;
    }

    if (bEnabled)
    {
        // Overlap判定だけ欲しいのでQueryOnly
        AttackCollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        AttackCollisionComponent->SetGenerateOverlapEvents(true);
    }
    else
    {
        // 攻撃していない時は完全に判定OFF
        AttackCollisionComponent->SetGenerateOverlapEvents(false);
        AttackCollisionComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
}

/**
 * 現在のダッシュ速度からダメージ量を計算する。
 *
 * 式：
 * CurrentDamageAmount = MaximumAttackPower * Clamp(CurrentDashSpeed / MaxDashSpeed, 0.0f, 1.0f)
 *
 * AIは基本最大チャージなので、
 * CurrentDashSpeed = MaxDashSpeed
 * ↓
 * CurrentDamageAmount = MaximumAttackPower
 * になる。
 */
void UEnemyAIAttackComponent::CalculateCurrentDamageAmount()
{
    if (MaxDashSpeed <= KINDA_SMALL_NUMBER)
    {
        CurrentDamageAmount = 0.0f;
        return;
    }

    const float AttackRate = FMath::Clamp(
        CurrentDashSpeed / MaxDashSpeed,
        0.0f,
        1.0f
    );

    CurrentDamageAmount = MaximumAttackPower * AttackRate;
}