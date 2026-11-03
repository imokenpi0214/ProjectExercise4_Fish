#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EnemyAIAttackComponent.generated.h"


class ACharacter;
class AAIController;
class UCharacterMovementComponent;
class UPrimitiveComponent;


/**
 * 敵AIの突進攻撃を担当するコンポーネント。
 *
 * BrainComponent側：
 * - 攻撃するか判断する
 * - 攻撃前に正面へ補足する
 * - StartDashAttackを呼ぶ
 *
 * AttackComponent側：
 * - チャージ
 * - 突進
 * - AttackSphereのON/OFF
 * - ダメージ量計算
 *
 *
 * 今回の重要設定：
 *
 * bFaceTargetBeforeDash = false
 * bSetControllerRotationBeforeDash = false
 * bUseCurrentForwardForDash = true
 *
 * にすると、攻撃直前に急にターゲット方向へ向き直らず、
 * 現在向いているForward方向へ突進する。
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

    // =========================================================
    // 攻撃開始 / 停止
    // =========================================================

    /**
     * 指定したターゲットへ突進攻撃を開始する。
     *
     * 実際には、
     * StartDashAttack
     * ↓
     * Charge
     * ↓
     * ReleaseDashAttack
     * ↓
     * FinishDashAttack
     *
     * の流れで動く。
     */
    UFUNCTION(BlueprintCallable, Category="AI|Attack")
    bool StartDashAttack(AActor* TargetActor);


    /**
     * 現在の突進攻撃をキャンセルする。
     */
    UFUNCTION(BlueprintCallable, Category="AI|Attack")
    void CancelDashAttack();


    // =========================================================
    // 状態取得
    // =========================================================

    UFUNCTION(BlueprintCallable, BlueprintPure, Category="AI|Attack")
    bool IsAttacking() const;


    UFUNCTION(BlueprintCallable, BlueprintPure, Category="AI|Attack")
    bool IsCharging() const;


    UFUNCTION(BlueprintCallable, BlueprintPure, Category="AI|Attack")
    bool IsDashing() const;


    // =========================================================
    // ダメージ取得
    // =========================================================

    /**
     * 現在の突進攻撃で与えるダメージ量を返す。
     *
     * BP側のAttackSphere BeginOverlapなどから呼ぶ想定。
     */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="AI|Damage")
    float GetCurrentDamageAmount() const;


public:

    // =========================================================
    // 攻撃時間
    // =========================================================

    /**
     * チャージ時間。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float ChargeTime = 0.5f;


    /**
     * 突進時間。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float DashDuration = 0.4f;


    // =========================================================
    // 突進速度 / 移動
    // =========================================================

    /**
     * LaunchCharacterに渡す突進の強さ。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float DashPower = 2500.0f;


    /**
     * 突進中のMaxWalkSpeed。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float DashMoveSpeed = 3000.0f;


    /**
     * 突進終了後に戻す通常MaxWalkSpeed。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float NormalMoveSpeed = 600.0f;


    /**
     * trueなら、突進直前にターゲット方向へActorを向ける。
     *
     * 今回の調整では基本false推奨。
     * trueだと、真後ろの敵にも攻撃直前で急に向き直る。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    bool bFaceTargetBeforeDash = false;


    /**
     * trueなら、突進直前にController Rotationもターゲット方向へ向ける。
     *
     * 今回の調整では基本false推奨。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    bool bSetControllerRotationBeforeDash = false;


    /**
     * trueなら、突進方向を現在のForward方向にする。
     *
     * Brain側で正面へ補足してから攻撃するため、
     * この設定をtrueにするとプレイヤーと同じように
     * 「今向いている方向へ突進」できる。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    bool bUseCurrentForwardForDash = true;


    /**
     * trueなら、突進開始前にAIControllerの移動を止める。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    bool bStopAIMovementBeforeDash = true;


    /**
     * trueなら、突進終了時にVelocityを止める。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    bool bStopVelocityOnFinish = true;


    /**
     * trueなら、突進終了時にAIControllerのMoveToを止める。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    bool bStopAIMovementOnFinish = true;


    /**
     * trueなら、突進終了後にBrainComponent側へ戻さず、
     * このコンポーネント単体でWanderへ戻す想定の設定。
     *
     * 現在はBrainComponent側のNotifyAttackHit / ReturnFromAttackで
     * モード管理するならfalse推奨。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    bool bReturnToWanderOnFinish = false;


    // =========================================================
    // AttackSphere
    // =========================================================

    /**
     * 攻撃判定用コンポーネント名。
     *
     * BP_Enemy内のAttackSphereなどの名前に合わせる。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|AttackCollision")
    FName AttackCollisionComponentName = "AttackSphere";


    /**
     * BeginPlayで攻撃判定をOFFにする。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|AttackCollision")
    bool bDisableAttackCollisionOnBeginPlay = true;


    /**
     * trueなら、突進中だけAttackSphereをONにする。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|AttackCollision")
    bool bUseAttackCollisionDuringDash = true;


    // =========================================================
    // ダメージ
    // =========================================================

    /**
     * 最大攻撃力。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Damage")
    float MaximumAttackPower = 30.0f;


    /**
     * ダメージ計算上の最大突進速度。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Damage")
    float MaxDashSpeed = 3000.0f;


    /**
     * 現在の突進速度。
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AI|Damage")
    float CurrentDashSpeed = 0.0f;


    /**
     * 現在の攻撃ダメージ量。
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AI|Damage")
    float CurrentDamageAmount = 0.0f;


private:

    // =========================================================
    // 内部参照
    // =========================================================

    UPROPERTY()
    ACharacter* OwnerCharacter = nullptr;


    UPROPERTY()
    AAIController* OwnerAIController = nullptr;


    UPROPERTY()
    UCharacterMovementComponent* OwnerMovementComponent = nullptr;


    UPROPERTY()
    AActor* CurrentTargetActor = nullptr;


    UPROPERTY()
    UPrimitiveComponent* AttackCollisionComponent = nullptr;


    // =========================================================
    // 内部状態
    // =========================================================

    bool bIsAttacking = false;

    bool bIsCharging = false;

    bool bIsDashing = false;


    FTimerHandle ChargeTimerHandle;

    FTimerHandle DashTimerHandle;


private:

    // =========================================================
    // 内部処理
    // =========================================================

    /**
     * チャージ完了後に突進を開始する。
     */
    void ReleaseDashAttack();


    /**
     * 突進終了処理。
     */
    void FinishDashAttack();


    /**
     * AttackSphereなどの攻撃判定コンポーネントを探す。
     */
    void FindAttackCollisionComponent();


    /**
     * AttackSphereのCollisionをON/OFFする。
     */
    void SetAttackCollisionEnabled(bool bEnabled);


    /**
     * CharacterMovementのMaxWalkSpeedを変更する。
     */
    void SetOwnerMaxWalkSpeed(float NewSpeed);


    /**
     * 現在の突進方向を計算する。
     *
     * bUseCurrentForwardForDash == true：
     * 現在のForward方向。
     *
     * false：
     * CurrentTargetActor方向。
     */
    FVector GetDashDirection() const;


    /**
     * ダメージ量を計算する。
     */
    void CalculateCurrentDamageAmount();
};