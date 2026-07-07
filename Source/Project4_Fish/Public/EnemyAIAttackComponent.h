#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TimerManager.h"
#include "EnemyAIAttackComponent.generated.h"

class ACharacter;
class AActor;
class UPrimitiveComponent;

/**
 * AI専用の攻撃コンポーネント。
 *
 * 役割：
 * - AIのチャージ攻撃
 * - チャージ後のダッシュ開放
 * - LaunchCharacterによる突進
 * - ダッシュ中だけAttackSphereのCollisionをONにする
 * - AI攻撃用のダメージ量を計算する
 * - 攻撃終了管理
 *
 * 重要：
 * - BPイベントは使わない
 * - C++から直接AttackSphereのCollisionを切り替える
 * - ダメージ量もC++側で計算し、BP側からGetCurrentDamageAmountで読む
 * - EnemyAIBrainComponentは「攻撃しろ」と命令するだけ
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROJECT4_FISH_API UEnemyAIAttackComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UEnemyAIAttackComponent();

protected:
    virtual void BeginPlay() override;

public:
    // =========================
    // BP / Brain から呼ぶ公開関数
    // =========================

    /**
     * AI用ダッシュ攻撃を開始する。
     *
     * EnemyAIBrainComponent の Attackモードから呼ぶ想定。
     *
     * 流れ：
     * - TargetActorを保存
     * - チャージ開始
     * - チャージ中はAttackSphere OFF
     * - ChargeTime後にDashを開放
     * - TargetActor方向へ向く
     * - ダメージ量を計算
     * - AttackSphere ON
     * - LaunchCharacterで突進
     * - DashDuration後にAttackSphere OFF
     * - 攻撃終了
     */
    UFUNCTION(BlueprintCallable, Category="AI|Attack")
    bool StartDashAttack(AActor* TargetActor);

    /**
     * 現在、攻撃中かどうか。
     *
     * true：
     * - チャージ中
     * - ダッシュ中
     *
     * false：
     * - 攻撃していない
     *
     * Brain側では、これがtrueの間はMoveToを出さない。
     */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="AI|Attack")
    bool IsAttacking() const;

    /**
     * チャージ中かどうか。
     */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="AI|Attack")
    bool IsCharging() const;

    /**
     * ダッシュ中かどうか。
     */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="AI|Attack")
    bool IsDashing() const;

    /**
     * 現在のAI攻撃ダメージ量を取得する。
     *
     * BP_Enemy / BP_PlayerBase の AttackSphere BeginOverlap 内で呼び、
     * BPI_Player の Apply Damage に渡す想定。
     *
     * 使い方：
     * AttackSphere BeginOverlap
     * ↓
     * Get Component by Class : EnemyAIAttackComponent
     * ↓
     * Get Current Damage Amount
     * ↓
     * BPI_Player Apply Damage に渡す
     */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="AI|Damage")
    float GetCurrentDamageAmount() const;

    /**
     * 攻撃を強制終了する。
     *
     * 使用例：
     * - 死亡時
     * - Fleeへ強制移行する時
     * - GoToHookへ強制移行する時
     * - デバッグで攻撃を止めたい時
     */
    UFUNCTION(BlueprintCallable, Category="AI|Attack")
    void CancelDashAttack();

public:
    // =========================
    // 攻撃調整値
    // =========================

    // チャージ時間。
    // この秒数待ってからダッシュを開放する。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float ChargeTime = 0.5f;

    // ダッシュしている時間。
    // この時間が過ぎたら攻撃終了扱いにする。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float DashDuration = 0.4f;

    // LaunchCharacterに使う突進力。
    // 大きいほど強く前へ飛ぶ。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float DashPower = 2500.0f;

    // ダッシュ中のCharacterMovement MaxWalkSpeed。
    // プレイヤー側のADashに近い動きをしたい場合に使う。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float DashMoveSpeed = 3000.0f;

    // ダッシュ終了後に戻す通常MoveSpeed。
    // BP_PlayerBase側の通常速度と合わせる。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float NormalMoveSpeed = 600.0f;

    // trueなら、ダッシュ前にターゲット方向へActorの向きを合わせる。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    bool bFaceTargetBeforeDash = true;

    // trueなら、向きを合わせる時にControllerのControlRotationも合わせる。
    // AIControllerやCharacterMovementが向きを上書きする場合の対策。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    bool bSetControllerRotationBeforeDash = true;

    // trueなら、ダッシュ開始直前にAIControllerのMoveToを止める。
    // WanderやAttack接近中のMoveToが残っていると、横方向へ突進する原因になる。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    bool bStopAIMovementBeforeDash = true;

    // trueなら、ダッシュ終了時にVelocityを止める。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    bool bStopVelocityOnFinish = true;

    // trueなら、ダッシュ終了時にAIControllerのStopMovementも呼ぶ。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    bool bStopAIMovementOnFinish = true;

    // trueなら、ダッシュ終了後にEnemyAIBrainComponentをWanderへ戻す。
    // 最初はtrue推奨。
    // 後で「HP80%以上ならAttack継続」などを入れる時に調整する。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    bool bReturnToWanderOnFinish = true;

    // =========================
    // 攻撃判定 / AttackSphere
    // =========================

    // 攻撃判定として使うCollision Componentの名前。
    // BP_PlayerBase / BP_Enemy側のコンポーネント名と合わせる。
    //
    // 例：
    // - AttackSphere
    // - Attack_Sphere
    // - DashAttackSphere
    //
    // 名前が違うとC++から見つけられないので注意。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|AttackCollision")
    FName AttackCollisionComponentName = "AttackSphere";

    // trueなら、BeginPlay時に攻撃判定CollisionをOFFにする。
    // 攻撃していない時にOverlapが出ないようにするため。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|AttackCollision")
    bool bDisableAttackCollisionOnBeginPlay = true;

    // trueなら、ダッシュ中だけAttackSphereのCollisionをONにする。
    //
    // true：
    // - チャージ中OFF
    // - ダッシュ中ON
    // - 終了時OFF
    //
    // false：
    // - C++側ではAttackSphereを切り替えない
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|AttackCollision")
    bool bUseAttackCollisionDuringDash = true;

    // =========================
    // ダメージ計算
    // =========================

    // AI攻撃の最大攻撃力。
    // AIは基本最大チャージなので、最初はこの値がそのままダメージになる。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Damage")
    float MaximumAttackPower = 30.0f;

    // 最大ダッシュ速度。
    //
    // ダメージ計算式：
    // DamageAmount = MaximumAttackPower * Clamp(CurrentDashSpeed / MaxDashSpeed, 0.0f, 1.0f)
    //
    // AIは基本最大チャージなので、
    // ReleaseDashAttack時に CurrentDashSpeed = MaxDashSpeed として計算する。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Damage")
    float MaxDashSpeed = 3000.0f;

    // 現在のダッシュ速度。
    // AIは基本最大チャージなので、攻撃開始時に MaxDashSpeed を入れる。
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AI|Damage")
    float CurrentDashSpeed = 0.0f;

    // 最後に計算したダメージ量。
    // BPのOverlap側からこの値を読む。
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AI|Damage")
    float CurrentDamageAmount = 0.0f;

private:
    // =========================
    // 内部状態
    // =========================

    // このComponentを持っているCharacter。
    // BP_EnemyはBP_PlayerBaseの子なので、Character系である想定。
    UPROPERTY()
    ACharacter* OwnerCharacter = nullptr;

    // 現在狙っている攻撃対象。
    // ReleaseDashAttack時に、このActor方向へ突進する。
    UPROPERTY()
    AActor* CurrentTargetActor = nullptr;

    // 攻撃判定に使うCollision Component。
    // BP側にあるAttackSphereをC++から探して保持する。
    UPROPERTY()
    UPrimitiveComponent* AttackCollisionComponent = nullptr;

    // チャージ中またはダッシュ中ならtrue。
    bool bIsAttacking = false;

    // チャージ中ならtrue。
    bool bIsCharging = false;

    // ダッシュ中ならtrue。
    bool bIsDashing = false;

    // チャージ終了用タイマー。
    FTimerHandle ChargeTimerHandle;

    // ダッシュ終了用タイマー。
    FTimerHandle DashTimerHandle;

private:
    // =========================
    // 内部処理
    // =========================

    // チャージ終了後、ダッシュを開放する。
    void ReleaseDashAttack();

    // ダッシュ攻撃を終了する。
    void FinishDashAttack();

    // 現在のターゲット方向を計算する。
    FVector CalculateDashDirection() const;

    // ターゲット方向へOwnerの向きを合わせる。
    void FaceToDashDirection(const FVector& DashDirection);

    // CharacterMovementのMaxWalkSpeedを変更する。
    void SetOwnerMaxWalkSpeed(float NewSpeed);

    // OwnerのVelocityを止める。
    void StopOwnerVelocity();

    // AIControllerの移動を止める。
    void StopAIMovement();

    // ダッシュ終了後にBrainへ通知する。
    // 今回は簡易的にWanderへ戻すために使う。
    void NotifyBrainAttackFinished();

    // BP_Enemy / BP_PlayerBaseについているAttackSphereを探す。
    void FindAttackCollisionComponent();

    // AttackSphereのCollisionをON/OFFする。
    void SetAttackCollisionEnabled(bool bEnabled);

    // 現在のダッシュ速度からダメージ量を計算する。
    //
    // 式：
    // CurrentDamageAmount = MaximumAttackPower * Clamp(CurrentDashSpeed / MaxDashSpeed, 0.0f, 1.0f)
    void CalculateCurrentDamageAmount();
};