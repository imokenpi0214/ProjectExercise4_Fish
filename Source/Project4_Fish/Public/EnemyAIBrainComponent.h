#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FishAITypes.h"
#include "EnemyAIBrainComponent.generated.h"


class AAIController;
class UEnemyAIAttackComponent;


DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnHookSearchRequested);


/**
 * 敵AIの判断を担当するコンポーネント。
 *
 * 主な役割：
 * - AIモード管理
 * - Wander / SpawnMove / Devour / Attack / Flee / GoToHook
 * - Aルール移動
 * - 前進＋ゆっくり旋回移動
 * - アクティブGoal Point検索要求
 * - 攻撃前の角度チェック
 * - ターゲット予約管理
 * - 親BP_PlayerBase側のItemAmount参照
 * - ぐるぐる回り続ける状態の検知とリカバリー
 * - BP_GoalPoint側のIs Active確認
 * - 同じ魚への攻撃継続による飽き処理
 * - Wander中の魚の視界判定
 *
 *
 * =========================================================
 * Goal Active確認について
 * =========================================================
 *
 * TargetHookActorがActorとして存在していても、
 * GoalPoint側でActiveSelfがfalseになっている場合がある。
 *
 * その状態で IsValid(TargetHookActor) だけを見ると、
 * AIは非アクティブGoalへ向かい続けてしまう。
 *
 * そのため、IsHookTargetValid() では、
 *
 * - Actorとして有効か
 * - BP_GoalPoint側の Is Active / IsActive がtrueか
 *
 * の両方を見る。
 *
 *
 * =========================================================
 * Boredom / 飽きについて
 * =========================================================
 *
 * 同じ魚への攻撃が継続するたびに、
 * CurrentBoredomChanceを増加させる。
 *
 * CurrentBoredomChanceそのものが
 * 「攻撃をキャンセルする確率」になる。
 *
 * 飽きによって攻撃をキャンセルした場合：
 *
 * - CurrentBoredomChance = 0
 * - ターゲット予約解除
 * - Wanderへ戻る
 * - 2～6秒のAttackLock
 *
 *
 * =========================================================
 * Vision / 視界について
 * =========================================================
 *
 * Wander中に敵を発見する際、
 * 単純な距離だけではなく魚の正面方向を視界として扱う。
 *
 * 判定：
 *
 * 1. 対象までの距離
 * 2. 自分のForwardと対象方向との角度差
 * 3. LineTraceによる壁・遮蔽物
 *
 * をすべて満たした魚だけを発見する。
 *
 * VisionHalfAngle = 30 の場合、
 * 正面から左右30度、
 * 合計60度の扇形が視界になる。
 */
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

    // =========================================================
    // イベントディスパッチャ
    // =========================================================

    UPROPERTY(BlueprintAssignable, Category="AI|GoToHook")
    FOnHookSearchRequested OnHookSearchRequested;


public:

    // =========================================================
    // BPから呼ぶ公開関数
    // =========================================================

    UFUNCTION(BlueprintCallable, Category="AI|Mode")
    void SetAIMode(EEnemyAIMode NewMode);


    UFUNCTION(BlueprintCallable, Category="AI|Target")
    void SetCollectItemGroupTarget(AActor* NewGroupActor);


    UFUNCTION(BlueprintCallable, Category="AI|GoToHook")
    void SetHookTarget(AActor* NewHookActor);


    UFUNCTION(BlueprintCallable, Category="AI|Collect")
    void NotifyPlanktonCollected();


    UFUNCTION(BlueprintCallable, Category="AI|Collect")
    void ResetAICarryState();


    UFUNCTION(BlueprintCallable, Category="AI|Combat")
    void NotifyDamaged(
        float CurrentHP,
        float MaxHP,
        AActor* DamageCauser
    );


    UFUNCTION(BlueprintCallable, Category="AI|Combat")
    void NotifyAttackHit(
        AActor* HitActor,
        float CurrentHP,
        float MaxHP,
        bool bHitActorBecameFlee
    );


    UFUNCTION(BlueprintCallable, Category="AI|Protection")
    void LockAttackForSeconds(float Duration);


    UFUNCTION(BlueprintCallable, BlueprintPure, Category="AI|Protection")
    bool IsAttackLocked() const;


    UFUNCTION(BlueprintCallable, BlueprintPure, Category="AI|GoToHook")
    bool IsWaitingForHook() const;


public:

    // =========================================================
    // 基本AI設定
    // =========================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI")
    EEnemyAIMode CurrentMode = EEnemyAIMode::Wander;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI")
    EEnemyAILevel AILevel = EEnemyAILevel::Weak;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Cell")
    float CellSize = 300.0f;


    // =========================================================
    // AttackLock
    // =========================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Protection")
    float InitialAttackLockDuration = 5.0f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Protection")
    float DefaultAttackLockDuration = 5.0f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Protection")
    bool bDisableWanderDetectDuringAttackLock = true;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Protection")
    bool bDisableAttackReactionDuringAttackLock = true;


    // =========================================================
    // SafeZone
    // =========================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|SafeZone")
    FName SafeZoneActorTagName = "InSafeZone";


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|SafeZone")
    bool bIgnoreActorsInSafeZone = true;


    // =========================================================
    // Wander
    // =========================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Wander")
    float WanderRadius = 1500.0f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Wander")
    float WanderWaitMin = 1.0f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Wander")
    float WanderWaitMax = 3.0f;


    /**
     * 旧来のWander検知距離設定。
     *
     * VisionDistanceを使用する場合でも残しておく。
     * 他の処理から参照している可能性があるため削除しない。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Wander")
    float WanderEnemyDetectCells = 3.0f;


    // =========================================================
    // Vision / 魚の視界
    // =========================================================

    /**
     * 正面から左右何度まで見えるか。
     *
     * 30.0の場合：
     *
     * 左30度 + 右30度
     * = 合計60度の視界。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Vision")
    float VisionHalfAngle = 30.0f;


    /**
     * Wander中に魚を視認できる最大距離。
     *
     * Unreal Unit。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Vision")
    float VisionDistance = 1500.0f;


    /**
     * LineTraceを飛ばす高さ。
     *
     * ActorLocationそのままだと地面や足元へ
     * Traceが引っかかる可能性があるため、
     * 少し上から視線を飛ばす。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Vision")
    float VisionTraceHeight = 50.0f;


    // =========================================================
    // Move
    // =========================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Move")
    float AcceptanceRadius = 100.0f;


    // =========================================================
    // Steering
    // =========================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Steering")
    bool bUseForwardOnlyMovement = true;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Steering")
    float TurnInterpSpeed = 5.0f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Steering")
    float ForwardMoveInputScale = 1.0f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Steering")
    bool bSlowDownWhenTurning = true;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Steering")
    float SlowDownTurnAngle = 120.0f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Steering")
    float MinTurnMoveScale = 0.5f;


    // =========================================================
    // Stuck Detection
    // =========================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Stuck")
    bool bUseStuckDetection = true;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Stuck")
    float StuckMoveThreshold = 80.0f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Stuck")
    float StuckTimeThreshold = 2.0f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Stuck")
    float StuckEscapeDistance = 1200.0f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Stuck")
    float StuckAttackLockDuration = 1.5f;


    // =========================================================
    // Aルール
    // =========================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|ARule")
    float ARuleSearchRadius = 1000.0f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|ARule")
    float MinSeparationCells = 2.0f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|ARule")
    float MaxSeparationCells = 7.0f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|ARule")
    float GoalWeight = 1.0f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|ARule")
    float AvoidWeight = 1.5f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|ARule")
    float RandomWeight = 0.4f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|ARule")
    float ARuleStepDistance = 800.0f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|ARule")
    FName FishTagName = "Fish";


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Target")
    FVector TargetLocation = FVector::ZeroVector;


    // =========================================================
    // SpawnMove
    // =========================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|SpawnMove")
    float SpawnMoveAttackChance = 0.03f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|SpawnMove")
    float TargetReachedDistance = 150.0f;


    // =========================================================
    // Devour
    // =========================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Devour")
    FName PlanktonTagName = "Plankton";


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Devour")
    float DevourSearchRadius = 1200.0f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Devour")
    float PlanktonReachedDistance = 100.0f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Devour")
    float DevourAttackChance = 0.20f;


    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AI|Devour")
    int32 EatenPlanktonCount = 0;


    // =========================================================
    // Attack
    // =========================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float AttackSearchRadius = 3000.0f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float AttackRange = 400.0f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float AttackStartAngle = 35.0f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    bool bAimBeforeAttack = true;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float AttackAimMoveScale = 0.25f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float AttackThinkIntervalMin = 0.1f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float AttackThinkIntervalMax = 0.25f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float AttackContinueHpRate = 0.80f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float ChaseFleeTargetChance = 0.50f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float ChaseFleeTimeMin = 3.0f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float ChaseFleeTimeMax = 6.0f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float AdditionalAttackChance = 0.70f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float AttackContinueDelayMin = 0.3f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float AttackContinueDelayMax = 0.8f;


    // =========================================================
    // Boredom
    // =========================================================

    /**
     * 同じ魚への攻撃を行うたびに増える
     * 攻撃キャンセル確率。
     *
     * 0.15 = 15%
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Boredom")
    float BoredomIncreasePerAttack = 0.15f;


    /**
     * 飽き確率の最大値。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Boredom")
    float MaxBoredomChance = 0.90f;


    /**
     * 飽きによるAttackLock最短時間。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Boredom")
    float BoredomCooldownMin = 2.0f;


    /**
     * 飽きによるAttackLock最長時間。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Boredom")
    float BoredomCooldownMax = 6.0f;

    // =========================================================
    // Mode Entry Dash / 行動ダッシュ
    // =========================================================

    /**
     * SpawnMoveへ入った瞬間、
     * 主戦場（プランクトンエリア）へダッシュする確率。
     *
     * 0.85 = 85%
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|ModeDash")
    float SpawnMoveDashChance = 0.85f;


    /**
     * Fleeへ入った瞬間、
     * 攻撃してきた魚から離れる方向へダッシュする確率。
     *
     * 基本100%。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|ModeDash")
    float FleeDashChance = 1.0f;


    /**
     * Devourへ入り、
     * プランクトンへ向かう時の通常ダッシュ確率。
     *
     * 0.50 = 50%
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|ModeDash")
    float DevourDashChance = 0.50f;


    /**
     * 視界内に、
     * 「同じプランクトン先へダッシュ中」の魚がいた場合の
     * ダッシュ確率。
     *
     * 0.90 = 90%
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|ModeDash")
    float DevourCompetitiveDashChance = 0.90f;


    /**
     * Attackへ入った際、
     * 同じ魚を継続して視界に捉えていた場合に
     * 退避ダッシュを行う確率。
     *
     * 0.50 = 50%
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|ModeDash")
    float AttackRetreatDashChance = 0.50f;


    /**
     * Attack退避ダッシュの最小回数。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|ModeDash")
    int32 AttackRetreatDashCountMin = 1;


    /**
     * Attack退避ダッシュの最大回数。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|ModeDash")
    int32 AttackRetreatDashCountMax = 2;


    /**
     * Attack退避時、
     * ターゲットから真後ろへ離れる方向に対して
     * 左右へどれくらいランダムにずらすか。
     *
     * 45なら、退避方向から左右45度以内。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|ModeDash")
    float AttackRetreatRandomAngle = 45.0f;


    /**
     * GoToHookへ入った瞬間、
     * Goal方向へダッシュする確率。
     *
     * 0.90 = 90%
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|ModeDash")
    float GoToHookDashChance = 0.90f;

    // =========================================================
    // Flee
    // =========================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Flee")
    float LowHpFleeThreshold = 0.20f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Flee")
    float MidHpFleeThreshold = 0.80f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Flee")
    float LowHpFleeChance = 0.70f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Flee")
    float MidHpFleeChance = 0.20f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Flee")
    float FleeEndDistanceCells = 12.0f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Flee")
    float FleeThinkIntervalMin = 0.15f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Flee")
    float FleeThinkIntervalMax = 0.35f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Flee")
    float FleeAwayWeight = 2.0f;


    // =========================================================
    // GoToHook
    // =========================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|GoToHook")
    int32 ForceGoToHookPlanktonCount = 40;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|GoToHook")
    float GoToHookEnemyDetectCells = 3.0f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|GoToHook")
    float GoToHookMaxAttackChance = 0.30f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|GoToHook")
    float GoToHookNoAttackDistanceCells = 4.0f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|GoToHook")
    float GoToHookMaxAttackDistanceCells = 12.0f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|GoToHook")
    float HookReachedDistance = 150.0f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|GoToHook")
    float GoToHookThinkIntervalMin = 0.15f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|GoToHook")
    float GoToHookThinkIntervalMax = 0.35f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|GoToHook")
    float HookSearchIntervalMin = 1.0f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|GoToHook")
    float HookSearchIntervalMax = 2.0f;


    // =========================================================
    // 強いAI / 弱いAI
    // =========================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|FishChange")
    float StrongFishChangeChance = 0.80f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|FishChange")
    float WeakFishChangeChance = 0.20f;

    // =========================================================
    // Vision Debug
    // =========================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Vision|Debug")
    bool bDrawVisionConeDebug = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Vision|Debug")
    float VisionConeDebugDuration = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Vision|Debug")
    float VisionConeDebugThickness = 2.0f;


private:

    // =========================================================
    // ターゲット予約管理
    // =========================================================

    static TMap<
        TWeakObjectPtr<AActor>,
        TWeakObjectPtr<UEnemyAIBrainComponent>
    > TargetReservations;


    // =========================================================
    // 内部参照
    // =========================================================

    UPROPERTY()
    APawn* OwnerPawn = nullptr;


    UPROPERTY()
    AAIController* OwnerAIController = nullptr;


    UPROPERTY()
    UEnemyAIAttackComponent* AIAttackComponent = nullptr;


    UPROPERTY()
    AActor* TargetGroupActor = nullptr;


    UPROPERTY()
    AActor* TargetPlanktonActor = nullptr;


    UPROPERTY()
    AActor* TargetFishActor = nullptr;


    UPROPERTY()
    AActor* TargetHookActor = nullptr;


    UPROPERTY()
    AActor* LastDamageCauser = nullptr;

    void DrawVisionConeDebug() const;

    bool bReturnToGoToHookAfterAttack = false;


    bool bWaitingForHook = false;


    FVector SteeringTargetLocation = FVector::ZeroVector;


    bool bHasSteeringTargetLocation = false;


    // =========================================================
    // Stuck Detection内部状態
    // =========================================================

    FVector LastStuckCheckLocation = FVector::ZeroVector;


    float LastMovedEnoughTime = 0.0f;


    bool bInitializedStuckCheck = false;


    // =========================================================
    // Boredom内部状態
    // =========================================================

    /**
     * 現在の飽き確率。
     *
     * 0.0 = 0%
     * 0.5 = 50%
     * 1.0 = 100%
     */
    float CurrentBoredomChance = 0.0f;


    float ChaseEndTime = 0.0f;


    float NextDecisionTime = 0.0f;


    float AttackLockEndTime = 0.0f;


    float NextHookSearchTime = 0.0f;

    // =========================================================
    // Mode Entry Dash内部状態
    // =========================================================

    /**
     * 前回Attackモードで狙っていた魚。
     *
     * Attackへ再び入った際に、
     * 同じ魚を継続して狙っているか確認するために使う。
     */
    UPROPERTY()
    AActor* LastAttackModeTargetActor = nullptr;


    /**
     * Attack退避ダッシュが連続実行中か。
     */
    bool bAttackRetreatDashSequenceActive = false;


    /**
     * あと何回退避ダッシュするか。
     *
     * 1～2回の連続退避を管理する。
     */
    int32 RemainingAttackRetreatDashCount = 0;


    /**
     * 現在の退避方向。
     *
     * ターゲットから離れる方向を基準にして、
     * 少し左右へランダム化した方向。
     */
    FVector AttackRetreatDashDirection = FVector::ZeroVector;

private:

    // =========================================================
    // モード別Tick
    // =========================================================

    void TickWander(float DeltaTime);

    void TickSpawnMove(float DeltaTime);

    void TickDevour(float DeltaTime);

    void TickAttack(float DeltaTime);

    void TickFlee(float DeltaTime);

    void TickGoToHook(float DeltaTime);

    // =========================================================
    // Mode Entry Dash
    // =========================================================

    /**
     * モードが実際に切り替わった瞬間に1回だけ呼ぶ。
     *
     * 各モードに応じて、
     * 既存のStartDashAttack(nullptr)を使うか判断する。
     */
    void TryStartModeEntryDash(
        EEnemyAIMode EnteredMode
    );


    /**
     * SpawnMove開始時。
     *
     * 85%で主戦場方向へ既存ダッシュ。
     */
    bool TryStartSpawnMoveDash();


    /**
     * Flee開始時。
     *
     * 攻撃してきた魚の反対方向へ
     * 100%で既存ダッシュ。
     */
    bool TryStartFleeDash();


    /**
     * Devour開始時。
     *
     * 通常50%。
     *
     * 視界内に同じプランクトン先へ
     * ダッシュしている魚がいれば90%。
     */
    bool TryStartDevourDash();


    /**
     * Attack開始時。
     *
     * 同じ魚を視界に捉え続けている場合、
     * 50%で1～2回の退避ダッシュを開始する。
     */
    bool TryStartAttackRetreatDash();


    /**
     * Attack中の退避ダッシュ連続処理。
     *
     * 1回目終了後、
     * RemainingAttackRetreatDashCountが残っていれば
     * 同じ退避処理をもう一度行う。
     */
    bool TryContinueAttackRetreatDash();


    /**
     * GoToHook開始時。
     *
     * 90%でGoal方向へ既存ダッシュ。
     */
    bool TryStartGoToHookDash();


    /**
     * 指定したWorld位置へ向きを合わせ、
     * 既存のStartDashAttack(nullptr)を開始する。
     *
     * 新しいダッシュではなく、
     * AttackComponentの既存ダッシュそのものを使用する。
     */
    bool StartExistingDashTowardLocation(
        const FVector& Destination
    );


    /**
     * 指定Actorから離れる方向へ向きを合わせ、
     * 既存ダッシュを開始する。
     */
    bool StartExistingDashAwayFromActor(
        AActor* SourceActor
    );


    /**
     * 指定方向へActorを向け、
     * 既存ダッシュを開始する。
     */
    bool StartExistingDashInDirection(
        const FVector& DashDirection
    );


    /**
     * 視界内に、
     * 自分と同じプランクトン目的地へ向かって
     * 現在ダッシュ中の魚がいるか確認する。
     */
    bool HasVisibleFishDashingToSamePlankton() const;


    /**
     * Attack退避用の方向を計算する。
     *
     * TargetFishActorと反対方向を基準に、
     * 左右へAttackRetreatRandomAngleだけランダム化する。
     */
    FVector CalculateAttackRetreatDashDirection() const;


    /**
     * Attack退避ダッシュ状態を終了する。
     */
    void ResetAttackRetreatDash();

    /**
     * 攻撃命中後、
     * 元のダッシュが終わったら退避ダッシュを開始する予約。
     */
    bool bPendingAttackRetreatDash = false;


    // =========================================================
    // Wander
    // =========================================================

    bool MoveToRandomLocation();

    bool TryDetectEnemyInWander();

    AActor* FindNearestWanderEnemy() const;


    // =========================================================
    // Vision
    // =========================================================

    /**
     * CandidateActorが現在の魚の視界内にいるか確認する。
     *
     * 以下をすべて確認する：
     *
     * - 攻撃可能Actor
     * - VisionDistance以内
     * - VisionHalfAngle以内
     * - 壁などで遮られていない
     */
    bool CanSeeFishActor(AActor* CandidateActor) const;


    /**
     * 自分のForward方向と、
     * 自分→CandidateActor方向との
     * 符号付き角度差を度数で返す。
     *
     * atan2を使用する。
     *
     * 正面 = 0度
     * 左右 = +/-角度
     * 後ろ = +/-180度付近
     */
    float GetVisionAngleToActor(AActor* CandidateActor) const;


    // =========================================================
    // SpawnMove / Target
    // =========================================================

    bool IsNearTargetLocation() const;


    // =========================================================
    // Devour
    // =========================================================

    AActor* FindNearestPlankton() const;

    bool IsTargetPlanktonValid() const;

    bool IsNearTargetPlankton() const;

    bool SelectNearestPlankton();

    bool TryGoToHookByPlanktonCount();

    float GetGoToHookChanceByPlanktonCount() const;


    // =========================================================
    // ItemAmount参照
    // =========================================================

    int32 GetOwnerItemAmount() const;


    // =========================================================
    // Attack
    // =========================================================

    AActor* FindNearestAttackTarget() const;

    bool IsTargetFishValid() const;

    bool SelectNearestAttackTarget();

    bool IsNearAttackTarget() const;

    bool IsTargetInAttackAngle() const;

    void FinishAttackTemporarily();

    void ReturnFromAttack();

    void TryChangeFishOnAttackMode();

    bool IsValidAttackCandidate(AActor* CandidateActor) const;


    // =========================================================
    // Boredom
    // =========================================================

    /**
     * 攻撃を開始した時に飽き確率を増加させ、
     * 攻撃キャンセル抽選を行う。
     *
     * true：
     * 飽きによって攻撃をキャンセルした。
     *
     * false：
     * 攻撃を続行する。
     */
    bool TryCancelAttackByBoredom();


    /**
     * 飽き確率を0へ戻す。
     */
    void ResetBoredom();


    // =========================================================
    // ターゲット予約管理
    // =========================================================

    static void CleanupInvalidTargetReservations();


    bool IsActorTargetedByOtherAI(AActor* CandidateActor) const;


    void ReserveTargetActor(AActor* NewTargetActor);


    void ReleaseCurrentTargetActor();


    // =========================================================
    // Stuck Detection
    // =========================================================

    void UpdateStuckDetection(float DeltaTime);


    void ResetStuckDetection();


    bool IsStuckNow() const;


    void HandleStuck();


    void HandleGoToHookStuck();


    void HandleAttackStuck();


    bool MoveToStuckEscapeLocation(
        const FVector& BaseAwayDirection
    );


    // =========================================================
    // Flee
    // =========================================================

    AActor* GetFleeSourceActor() const;


    bool IsFarEnoughFromFleeSource() const;


    bool MoveByARuleAwayFromActor(AActor* SourceActor);


    // =========================================================
    // GoToHook
    // =========================================================

    bool ShouldForceGoToHook() const;


    bool IsHookTargetValid() const;


    bool IsHookTargetActive() const;


    bool TryCallBoolFunctionNoParams(
        AActor* TargetActor,
        const FName& FunctionName,
        bool& OutReturnValue
    ) const;


    void RequestActiveHookSearch();


    bool IsNearHook() const;


    AActor* FindNearestGoToHookEnemy() const;


    bool TryAttackDuringGoToHook();


    float CalculateGoToHookAttackChance() const;


    // =========================================================
    // AttackLock
    // =========================================================

    bool CanEnterAttackMode() const;


    // =========================================================
    // Aルール / Steering
    // =========================================================

    FVector CalculateARuleMoveLocation(
        const FVector& GoalLocation
    );


    FVector CalculateAvoidDirection() const;


    bool MoveByARuleToLocation(
        const FVector& GoalLocation
    );


    bool MoveForwardOnlyToLocation(
        const FVector& GoalLocation,
        float DeltaTime
    );


    // =========================================================
    // 汎用
    // =========================================================

    bool ShouldMakeDecision() const;


    void SetNextDecisionTime(
        float MinTime,
        float MaxTime
    );


    bool TryChance(float Chance) const;
};