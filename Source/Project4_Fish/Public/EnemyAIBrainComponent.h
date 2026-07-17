#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FishAITypes.h"
#include "EnemyAIBrainComponent.generated.h"


class AAIController;
class UEnemyAIAttackComponent;


/**
 * C++側からBP側へ、
 * 「現在アクティブな釣り針 / Goal Pointを探してほしい」
 * と通知するイベントディスパッチャ。
 *
 * BP_Enemy側でBindし、
 * GameSystemの
 * Get Active Nearby Goal Point
 * を呼ぶ。
 *
 * 取得したActorは、
 * SetHookTarget()
 * でC++側へ返す。
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnHookSearchRequested);


/**
 * 敵AIの「脳」になるコンポーネント。
 *
 * BP_Enemyに追加して使用する。
 *
 *
 * =========================================================
 * 現在担当しているAIモード
 * =========================================================
 *
 * - Wander
 *   自由遊泳。
 *
 *   通常時は近距離の魚を検知するとAttackへ移行する。
 *
 *   プランクトン数が強制GoToHook条件以上なら、
 *   GoToHookへ移行する。
 *
 *   GoToHookへ行きたいのに、
 *   現在アクティブな釣り針が存在しない場合は、
 *   bWaitingForHook = true の状態で一時的にWanderへ戻る。
 *
 *   その後、一定間隔で
 *   OnHookSearchRequested
 *   をBroadcastし、
 *   BP側へアクティブなGoal Point検索を要求する。
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
 *   から取得された、
 *   現在アクティブなGoal PointへAルールで向かう。
 *
 *   周囲に魚が近づいた場合のみ、
 *   Attackへの移行抽選を行う。
 *
 *   Attack移行確率は最大30%。
 *
 *   釣り針へ近づくほど確率が下がり、
 *   4セル以内では0%になる。
 *
 *   GoToHookからAttackへ移行した場合は、
 *   Attack終了後に再びGoToHookへ戻る。
 *
 *
 * =========================================================
 * アクティブな釣り針検索
 * =========================================================
 *
 * C++側はGameSystemのBP関数を直接呼ばない。
 *
 * 代わりに、
 *
 * C++
 * ↓
 * OnHookSearchRequested.Broadcast()
 * ↓
 * BP_Enemy
 * ↓
 * Get Active Nearby Goal Point
 * ↓
 * SetHookTarget(取得したActor)
 *
 * という構成にする。
 *
 *
 * =========================================================
 * 基本方針
 * =========================================================
 *
 * - BP_EnemyはBP_PlayerBaseの子として作る。
 *
 * - このC++ Componentが、
 *   「どこへ行くか」
 *   「どのモードへ移行するか」
 *   を判断する。
 *
 * - 通常移動はAIControllerのMoveToLocationを使用する。
 *
 * - 突進攻撃そのものはEnemyAIAttackComponentが担当する。
 *
 * - GameSystemのBP専用処理が必要な場合は、
 *   イベントディスパッチャでBP側へ要求を送る。
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


    /**
     * C++側からBP側へ、
     *
     * 「現在アクティブなGoal Pointを探してほしい」
     *
     * と通知するイベントディスパッチャ。
     *
     *
     * BP_Enemy側：
     *
     * Event BeginPlay
     * ↓
     * Bind Event to OnHookSearchRequested
     * ↓
     * カスタムイベント
     * ↓
     * GameSystem
     * ↓
     * Get Active Nearby Goal Point
     * ↓
     * SetHookTarget
     *
     *
     * 注意：
     * C++側からBroadcastするだけでは、
     * アクティブなGoal Pointは取得できない。
     *
     * BP側で必ずBindする必要がある。
     */
    UPROPERTY(BlueprintAssignable, Category="AI|GoToHook")
    FOnHookSearchRequested OnHookSearchRequested;


public:

    // =========================================================
    // BPから呼ぶ公開関数
    // =========================================================


    /**
     * AIモードを変更する。
     *
     * BPからCurrentModeを直接Setするのではなく、
     * 基本的にはこの関数を通す。
     *
     *
     * 理由：
     *
     * - モード開始時の初期化
     * - Targetの整理
     * - AttackLock確認
     * - GoToHookからAttackへ入ったか記録
     *
     *
     * 注意：
     *
     * AttackLock中にAttackへ入ろうとした場合は、
     * .cpp側で移行を拒否する。
     */
    UFUNCTION(BlueprintCallable, Category="AI|Mode")
    void SetAIMode(EEnemyAIMode NewMode);


    /**
     * BP_CollectItemGroupなど、
     * プランクトン群れActorをAIへ渡す。
     *
     *
     * BP_Enemyの
     * AI_CheckCollectItemGroup
     * から呼ぶ想定。
     *
     *
     * 処理：
     *
     * NewGroupActor
     * ↓
     * TargetGroupActorへ保存
     * ↓
     * TargetLocation更新
     * ↓
     * SpawnMoveへ移行
     *
     *
     * 注意：
     *
     * .cpp側では通常のWander中のみ受け付ける。
     *
     * これにより、
     *
     * - Flee
     * - Attack
     * - Devour
     * - GoToHook
     * - 釣り針待ち中Wander
     *
     * が突然SpawnMoveで上書きされるのを防ぐ。
     */
    UFUNCTION(BlueprintCallable, Category="AI|Target")
    void SetCollectItemGroupTarget(AActor* NewGroupActor);


    /**
     * GameSystemの
     * Get Active Nearby Goal Point
     * で取得したActorを、
     * C++側へ渡す。
     *
     *
     * BP側：
     *
     * OnHookSearchRequested
     * ↓
     * Get Active Nearby Goal Point
     * ↓
     * Is Valid
     * ↓
     * SetHookTarget
     *
     *
     * NewHookActorが有効なら：
     *
     * TargetHookActorへ保存
     * ↓
     * TargetLocation更新
     * ↓
     * bWaitingForHook = false
     * ↓
     * GoToHookへ移行
     *
     *
     * 注意：
     *
     * この関数へ渡すActorは、
     * タグ検索で見つけた任意のActorではなく、
     * GameSystemが返した
     * 「現在アクティブなGoal Point」
     * を使用する。
     */
    UFUNCTION(BlueprintCallable, Category="AI|GoToHook")
    void SetHookTarget(AActor* NewHookActor);


    /**
     * AIがプランクトンを1つ取得した時に呼ぶ。
     *
     * 狙っていたプランクトンかどうかに関係なく呼ぶ。
     *
     *
     * 処理：
     *
     * EatenPlanktonCount++
     * ↓
     * 強制GoToHook条件確認
     * ↓
     * DevourAttackChanceによるAttack判定
     * ↓
     * 食べた数によるGoToHook確率判定
     *
     *
     * 現在のGoToHook確率：
     *
     * 10個以上 → 12.5%
     * 20個以上 → 25%
     * 30個以上 → 50%
     * 40個以上 → 100%
     */
    UFUNCTION(BlueprintCallable, Category="AI|Collect")
    void NotifyPlanktonCollected();


    /**
     * 自分が攻撃を受けた時にBP側から呼ぶ。
     *
     *
     * 正式仕様：
     *
     * 自身HP20%未満
     * → 70%でFlee
     *
     * 自身HP20%～79%
     * → 20%でFlee
     *
     * 自身HP80%以上
     * → Attack
     *
     *
     * CurrentHP：
     * ダメージ適用後の自分自身の現在HP。
     *
     * MaxHP：
     * 自分自身の最大HP。
     *
     * DamageCauser：
     * 自分へダメージを与えたActor。
     *
     * Fleeへ移行した場合、
     * 基本的にこのActorから逃げる。
     */
    UFUNCTION(BlueprintCallable, Category="AI|Combat")
    void NotifyDamaged(
        float CurrentHP,
        float MaxHP,
        AActor* DamageCauser
    );


    /**
     * 自分の攻撃が相手へ当たった時にBP側から呼ぶ。
     *
     *
     * HitActor：
     * 攻撃が当たった相手。
     *
     * CurrentHP：
     * 攻撃したAI自身の現在HP。
     *
     * MaxHP：
     * 攻撃したAI自身の最大HP。
     *
     * bHitActorBecameFlee：
     * 攻撃された相手AIがFleeへ移行したかどうか。
     *
     *
     * 通常：
     *
     * 自身HP80%以上
     * → Attack継続
     *
     * それ以外
     * → Wander
     *
     *
     * GoToHookからAttackへ入っていた場合：
     *
     * Attack終了
     * ↓
     * GoToHookへ復帰
     */
    UFUNCTION(BlueprintCallable, Category="AI|Combat")
    void NotifyAttackHit(
        AActor* HitActor,
        float CurrentHP,
        float MaxHP,
        bool bHitActorBecameFlee
    );


    /**
     * 指定秒数Attackへの移行を禁止する。
     *
     *
     * 使用例：
     *
     * - ゲーム開始直後
     * - リスポーン直後
     * - ゴール直後
     * - SafeZoneから出た直後
     */
    UFUNCTION(BlueprintCallable, Category="AI|Protection")
    void LockAttackForSeconds(float Duration);


    /**
     * 現在Attack禁止時間中か確認する。
     */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="AI|Protection")
    bool IsAttackLocked() const;


    /**
     * 現在、
     * 釣り針が存在せず、
     * WanderしながらアクティブなGoal Pointを待っているか確認する。
     *
     *
     * true：
     * GoToHookへ行きたいが、
     * 現在アクティブなGoal Pointが見つかっていない。
     *
     *
     * false：
     * 通常状態。
     *
     *
     * BP側でデバッグしたい場合にも使用できる。
     */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="AI|GoToHook")
    bool IsWaitingForHook() const;


public:

    // =========================================================
    // 基本AI設定
    // =========================================================


    /**
     * 現在のAIモード。
     *
     * - Wander
     * - SpawnMove
     * - Devour
     * - Attack
     * - Flee
     * - GoToHook
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI")
    EEnemyAIMode CurrentMode = EEnemyAIMode::Wander;


    /**
     * AIの強さ。
     *
     * - Weak
     * - Strong
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI")
    EEnemyAILevel AILevel = EEnemyAILevel::Weak;


    /**
     * 1セルあたりのUnreal Unit。
     *
     *
     * 例：
     *
     * CellSize = 300
     *
     * 4セル = 1200uu
     * 12セル = 3600uu
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Cell")
    float CellSize = 300.0f;


    // =========================================================
    // AttackLock
    // =========================================================


    /**
     * BeginPlay直後にAttackを禁止する時間。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Protection")
    float InitialAttackLockDuration = 5.0f;


    /**
     * リスポーン直後やゴール直後などに使用する
     * デフォルト攻撃禁止時間。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Protection")
    float DefaultAttackLockDuration = 5.0f;


    /**
     * trueなら、
     * AttackLock中はWanderの近距離敵検知を無効にする。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Protection")
    bool bDisableWanderDetectDuringAttackLock = true;


    /**
     * trueなら、
     * AttackLock中はNotifyDamagedからの反撃Attackを抑制する。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Protection")
    bool bDisableAttackReactionDuringAttackLock = true;


    // =========================================================
    // SafeZone
    // =========================================================


    /**
     * SafeZone内にいるActorへ付けるActor Tag。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|SafeZone")
    FName SafeZoneActorTagName = "InSafeZone";


    /**
     * trueなら、
     * SafeZoneタグ持ちActorを攻撃対象から除外する。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|SafeZone")
    bool bIgnoreActorsInSafeZone = true;


    // =========================================================
    // Wander
    // =========================================================


    /**
     * ランダム移動先を探す半径。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Wander")
    float WanderRadius = 1500.0f;


    /**
     * Wander中の最小待機時間。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Wander")
    float WanderWaitMin = 1.0f;


    /**
     * Wander中の最大待機時間。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Wander")
    float WanderWaitMax = 3.0f;


    /**
     * Wander中、
     * このセル数以内にFishタグActorがいた場合、
     * Attackへ移行する。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Wander")
    float WanderEnemyDetectCells = 3.0f;


    // =========================================================
    // Move
    // =========================================================


    /**
     * MoveToLocationの到着許容距離。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Move")
    float AcceptanceRadius = 100.0f;


    // =========================================================
    // Aルール
    // =========================================================


    /**
     * Aルール用の周辺魚検索範囲。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|ARule")
    float ARuleSearchRadius = 1000.0f;


    /**
     * このセル数より近い魚を避ける。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|ARule")
    float MinSeparationCells = 2.0f;


    /**
     * 仕様上の最大分離セル数。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|ARule")
    float MaxSeparationCells = 7.0f;


    /**
     * 目的地へ向かう強さ。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|ARule")
    float GoalWeight = 1.0f;


    /**
     * 他の魚を避ける強さ。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|ARule")
    float AvoidWeight = 1.5f;


    /**
     * ランダム方向の強さ。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|ARule")
    float RandomWeight = 0.4f;


    /**
     * 1回の判断で、
     * どれくらい先を移動候補地点にするか。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|ARule")
    float ARuleStepDistance = 800.0f;


    /**
     * 魚Actorに付けるActor Tag。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|ARule")
    FName FishTagName = "Fish";


    /**
     * 現在の目的地。
     *
     * SpawnMove：
     * 群れ中央
     *
     * Devour：
     * プランクトン
     *
     * Attack：
     * 攻撃対象
     *
     * Flee：
     * 逃走先
     *
     * GoToHook：
     * アクティブなGoal Point
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Target")
    FVector TargetLocation = FVector::ZeroVector;


    // =========================================================
    // SpawnMove
    // =========================================================


    /**
     * SpawnMove中にAttackへ移行する確率。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|SpawnMove")
    float SpawnMoveAttackChance = 0.03f;


    /**
     * 群れ中央へ到着したと判断する距離。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|SpawnMove")
    float TargetReachedDistance = 150.0f;


    // =========================================================
    // Devour
    // =========================================================


    /**
     * プランクトンActorに付けるタグ。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Devour")
    FName PlanktonTagName = "Plankton";


    /**
     * Devour中にプランクトンを探す範囲。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Devour")
    float DevourSearchRadius = 1200.0f;


    /**
     * プランクトンへ到着したと判断する距離。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Devour")
    float PlanktonReachedDistance = 100.0f;


    /**
     * プランクトン取得時、
     * Attackへ移行する確率。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Devour")
    float DevourAttackChance = 0.20f;


    /**
     * AI自身が食べたプランクトン数。
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AI|Devour")
    int32 EatenPlanktonCount = 0;


    // =========================================================
    // Attack
    // =========================================================


    /**
     * Attack中に攻撃対象を探す範囲。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float AttackSearchRadius = 3000.0f;


    /**
     * 攻撃開始距離。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float AttackRange = 400.0f;


    /**
     * Attack中の判断間隔最小値。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float AttackThinkIntervalMin = 0.1f;


    /**
     * Attack中の判断間隔最大値。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float AttackThinkIntervalMax = 0.25f;


    /**
     * 攻撃ヒット後、
     * 自身HPがこの割合以上ならAttack継続。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float AttackContinueHpRate = 0.80f;


    /**
     * 攻撃した相手がFleeへ移行した場合、
     * 追いかける確率。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float ChaseFleeTargetChance = 0.50f;


    /**
     * Fleeした相手を追跡する最短時間。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float ChaseFleeTimeMin = 3.0f;


    /**
     * Fleeした相手を追跡する最長時間。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float ChaseFleeTimeMax = 6.0f;


    /**
     * 追跡後に追加Attackを行う確率。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float AdditionalAttackChance = 0.70f;


    /**
     * Attack継続時の最小待機時間。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float AttackContinueDelayMin = 0.3f;


    /**
     * Attack継続時の最大待機時間。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float AttackContinueDelayMax = 0.8f;


    // =========================================================
    // Flee
    // =========================================================


    /**
     * 低HP判定。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Flee")
    float LowHpFleeThreshold = 0.20f;


    /**
     * 中HP帯上限。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Flee")
    float MidHpFleeThreshold = 0.80f;


    /**
     * HP20%未満の場合のFlee確率。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Flee")
    float LowHpFleeChance = 0.70f;


    /**
     * HP20%～79%の場合のFlee確率。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Flee")
    float MidHpFleeChance = 0.20f;


    /**
     * 逃走終了距離。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Flee")
    float FleeEndDistanceCells = 12.0f;


    /**
     * Flee中の判断間隔最小値。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Flee")
    float FleeThinkIntervalMin = 0.15f;


    /**
     * Flee中の判断間隔最大値。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Flee")
    float FleeThinkIntervalMax = 0.35f;


    /**
     * 逃走方向の強さ。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Flee")
    float FleeAwayWeight = 2.0f;


    // =========================================================
    // GoToHook
    // =========================================================


    /**
     * Wander中に、
     * この数以上のプランクトンを持っていた場合、
     * 強制的にGoToHookを試す。
     *
     *
     * ただし、
     * 現在アクティブなGoal Pointが無い場合は、
     * bWaitingForHook = true
     * でWanderへ戻る。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|GoToHook")
    int32 ForceGoToHookPlanktonCount = 40;


    /**
     * GoToHook中、
     * 周囲の魚を検知する距離。
     *
     *
     * 実距離：
     *
     * GoToHookEnemyDetectCells * CellSize
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|GoToHook")
    float GoToHookEnemyDetectCells = 3.0f;


    /**
     * GoToHook中のAttack最大移行確率。
     *
     * 0.30 = 30%
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|GoToHook")
    float GoToHookMaxAttackChance = 0.30f;


    /**
     * このセル数以内までGoal Pointへ近づくと、
     * Attack確率が0%になる。
     *
     * デフォルト：
     * 4セル以内。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|GoToHook")
    float GoToHookNoAttackDistanceCells = 4.0f;


    /**
     * このセル数以上Goal Pointから離れている場合、
     * Attack確率が最大値になる。
     *
     *
     * デフォルト：
     *
     * 12セル以上
     * → 最大30%
     *
     *
     * 12～4セルでは、
     * 近づくほど30%から0%へ線形に低下する。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|GoToHook")
    float GoToHookMaxAttackDistanceCells = 12.0f;


    /**
     * Goal Pointへ到着したとみなす距離。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|GoToHook")
    float HookReachedDistance = 150.0f;


    /**
     * GoToHook中の判断間隔最小値。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|GoToHook")
    float GoToHookThinkIntervalMin = 0.15f;


    /**
     * GoToHook中の判断間隔最大値。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|GoToHook")
    float GoToHookThinkIntervalMax = 0.35f;


    /**
     * アクティブなGoal Pointが存在しない時、
     * BP側へ再検索要求を送る最小間隔。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|GoToHook")
    float HookSearchIntervalMin = 1.0f;


    /**
     * アクティブなGoal Pointが存在しない時、
     * BP側へ再検索要求を送る最大間隔。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|GoToHook")
    float HookSearchIntervalMax = 2.0f;


    // =========================================================
    // 強いAI / 弱いAI
    // =========================================================


    /**
     * Strong AIがAttack移行時に
     * 魚種変更を試す確率。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|FishChange")
    float StrongFishChangeChance = 0.80f;


    /**
     * Weak AIがAttack移行時に
     * 魚種変更を試す確率。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|FishChange")
    float WeakFishChangeChance = 0.20f;


private:

    // =========================================================
    // 内部参照
    // =========================================================


    /**
     * このComponentを持つPawn。
     *
     * 基本的にはBP_Enemy。
     */
    UPROPERTY()
    APawn* OwnerPawn = nullptr;


    /**
     * OwnerPawnを操作するAIController。
     */
    UPROPERTY()
    AAIController* OwnerAIController = nullptr;


    /**
     * AI専用攻撃Component。
     */
    UPROPERTY()
    UEnemyAIAttackComponent* AIAttackComponent = nullptr;


    /**
     * 現在向かっているプランクトン群れActor。
     */
    UPROPERTY()
    AActor* TargetGroupActor = nullptr;


    /**
     * Devour中に狙っているプランクトンActor。
     */
    UPROPERTY()
    AActor* TargetPlanktonActor = nullptr;


    /**
     * 現在の攻撃対象。
     */
    UPROPERTY()
    AActor* TargetFishActor = nullptr;


    /**
     * GoToHookで向かう現在アクティブなGoal Point Actor。
     *
     *
     * このActorはタグ検索では取得しない。
     *
     * BP側で、
     *
     * GameSystem
     * ↓
     * Get Active Nearby Goal Point
     * ↓
     * SetHookTarget
     *
     * として設定する。
     */
    UPROPERTY()
    AActor* TargetHookActor = nullptr;


    /**
     * 最後に自分へダメージを与えたActor。
     *
     * Fleeでは基本的にこのActorから逃げる。
     */
    UPROPERTY()
    AActor* LastDamageCauser = nullptr;


    /**
     * GoToHookからAttackへ移行したかどうか。
     *
     *
     * false：
     * 通常のAttack。
     *
     *
     * true：
     * GoToHook中に周囲の魚へ反応してAttackへ入った。
     *
     *
     * trueの場合、
     * Attack終了後はWanderではなく
     * GoToHookへ復帰する。
     */
    bool bReturnToGoToHookAfterAttack = false;


    /**
     * 現在アクティブなGoal Pointが見つからず、
     * 一時的にWanderしながら再検索中かどうか。
     *
     *
     * true：
     *
     * GoToHookへ行きたいが、
     * 現在有効なTargetHookActorがない。
     *
     * Wanderしながら一定間隔で、
     * OnHookSearchRequestedをBroadcastする。
     *
     *
     * false：
     *
     * 通常状態。
     *
     *
     * 重要：
     *
     * bWaitingForHook == true の間は、
     * Wander中の近距離Attack判定を行わない。
     *
     * また、
     * SetCollectItemGroupTargetによるSpawnMove移行も拒否する。
     */
    bool bWaitingForHook = false;


    /**
     * Fleeした相手を追跡する場合の終了時刻。
     */
    float ChaseEndTime = 0.0f;


    /**
     * 次回AI判断時刻。
     */
    float NextDecisionTime = 0.0f;


    /**
     * この時刻まではAttack禁止。
     */
    float AttackLockEndTime = 0.0f;


    /**
     * 次にBP側へアクティブGoal Point検索を
     * 要求してよい時刻。
     *
     *
     * bWaitingForHook == true の間、
     *
     * CurrentTime >= NextHookSearchTime
     *
     * になったら、
     * RequestActiveHookSearch()
     * を呼ぶ。
     */
    float NextHookSearchTime = 0.0f;


private:

    // =========================================================
    // モード別Tick
    // =========================================================


    /**
     * Wanderモード。
     *
     *
     * 優先順位：
     *
     * 1.
     * bWaitingForHookがtrueなら、
     * 一定間隔でBP側へ
     * アクティブGoal Point検索を要求する。
     *
     *
     * 2.
     * bWaitingForHookがfalseで、
     * 強制GoToHook条件を満たしているか確認する。
     *
     *
     * 3.
     * bWaitingForHookがfalseなら、
     * 近距離Attack判定。
     *
     *
     * 4.
     * 通常ランダム遊泳。
     */
    void TickWander(float DeltaTime);


    /**
     * SpawnMoveモード。
     */
    void TickSpawnMove(float DeltaTime);


    /**
     * Devourモード。
     */
    void TickDevour(float DeltaTime);


    /**
     * Attackモード。
     */
    void TickAttack(float DeltaTime);


    /**
     * Fleeモード。
     */
    void TickFlee(float DeltaTime);


    /**
     * GoToHookモード。
     *
     *
     * TargetHookActorが有効：
     *
     * → Goal Point位置をTargetLocationへ設定
     * → 周囲の魚によるAttack判定
     * → Aルールで移動
     *
     *
     * TargetHookActorが無効：
     *
     * → RequestActiveHookSearch()
     * → bWaitingForHook = true
     * → Wanderへ一時移行
     */
    void TickGoToHook(float DeltaTime);


    // =========================================================
    // Wander用関数
    // =========================================================


    /**
     * NavMesh上のランダム地点を探し、
     * Aルールで移動する。
     */
    bool MoveToRandomLocation();


    /**
     * Wander中に近距離の攻撃対象を探す。
     */
    bool TryDetectEnemyInWander();


    /**
     * Wander中に反応する最も近い魚を探す。
     */
    AActor* FindNearestWanderEnemy() const;


    // =========================================================
    // SpawnMove / Target
    // =========================================================


    /**
     * 現在位置がTargetLocationへ
     * 十分近いか確認する。
     */
    bool IsNearTargetLocation() const;


    // =========================================================
    // Devour
    // =========================================================


    /**
     * 一番近いプランクトンを探す。
     */
    AActor* FindNearestPlankton() const;


    /**
     * TargetPlanktonActorが有効か確認する。
     */
    bool IsTargetPlanktonValid() const;


    /**
     * TargetPlanktonActorへ十分近いか確認する。
     */
    bool IsNearTargetPlankton() const;


    /**
     * 一番近いプランクトンを
     * TargetPlanktonActorへ設定する。
     */
    bool SelectNearestPlankton();


    /**
     * 食べたプランクトン数に応じて、
     * GoToHookへ移行するか確率判定する。
     */
    bool TryGoToHookByPlanktonCount();


    /**
     * 食べたプランクトン数から
     * GoToHook確率を返す。
     *
     *
     * 40以上 → 100%
     * 30以上 → 50%
     * 20以上 → 25%
     * 10以上 → 12.5%
     * それ未満 → 0%
     */
    float GetGoToHookChanceByPlanktonCount() const;


    // =========================================================
    // Attack
    // =========================================================


    /**
     * 一番近い攻撃対象を探す。
     */
    AActor* FindNearestAttackTarget() const;


    /**
     * 現在のTargetFishActorが
     * 有効な攻撃対象か確認する。
     */
    bool IsTargetFishValid() const;


    /**
     * 一番近い攻撃対象を
     * TargetFishActorへ設定する。
     */
    bool SelectNearestAttackTarget();


    /**
     * 攻撃開始距離以内か確認する。
     */
    bool IsNearAttackTarget() const;


    /**
     * Attackを一時終了する。
     *
     *
     * 通常Attack：
     * → Wander
     *
     *
     * GoToHookからのAttack：
     * → GoToHook
     */
    void FinishAttackTemporarily();


    /**
     * Attack終了後の復帰先へ戻る。
     *
     *
     * bReturnToGoToHookAfterAttack == true
     * → GoToHook
     *
     *
     * false
     * → Wander
     */
    void ReturnFromAttack();


    /**
     * Attackモードへ入った時、
     * AIレベルに応じて魚種変更を試す。
     */
    void TryChangeFishOnAttackMode();


    /**
     * 攻撃対象として有効か確認する。
     *
     *
     * false：
     *
     * - nullptr
     * - 自分自身
     * - SafeZoneタグ持ちActor
     */
    bool IsValidAttackCandidate(AActor* CandidateActor) const;


    // =========================================================
    // Flee
    // =========================================================


    /**
     * 逃走元Actorを取得する。
     */
    AActor* GetFleeSourceActor() const;


    /**
     * 逃走元から十分離れたか確認する。
     */
    bool IsFarEnoughFromFleeSource() const;


    /**
     * 指定Actorから遠ざかる方向へ、
     * Aルールで移動する。
     */
    bool MoveByARuleAwayFromActor(AActor* SourceActor);


    // =========================================================
    // GoToHook
    // =========================================================


    /**
     * Wander中、
     * 強制GoToHook条件を満たしているか確認する。
     */
    bool ShouldForceGoToHook() const;


    /**
     * TargetHookActorが有効か確認する。
     */
    bool IsHookTargetValid() const;


    /**
     * BP側へ、
     *
     * 「GameSystemの
     * Get Active Nearby Goal Pointを呼んで、
     * 現在アクティブなGoal Pointを探してほしい」
     *
     * と要求する。
     *
     *
     * 内部では、
     *
     * OnHookSearchRequested.Broadcast();
     *
     * を実行する。
     *
     *
     * また、
     * 次回検索可能時刻である
     * NextHookSearchTimeも更新する。
     */
    void RequestActiveHookSearch();


    /**
     * Goal Pointへ十分近いか確認する。
     *
     * HookReachedDistance以内ならtrue。
     */
    bool IsNearHook() const;


    /**
     * GoToHook中、
     * 周囲にいる最も近い攻撃可能な魚を探す。
     */
    AActor* FindNearestGoToHookEnemy() const;


    /**
     * GoToHook中のAttack移行判定。
     *
     *
     * 処理：
     *
     * 1.
     * 周囲に魚がいるか確認。
     *
     * 2.
     * Goal Pointまでの距離からAttack確率を計算。
     *
     * 3.
     * 確率抽選。
     *
     * 4.
     * 成功したらAttackへ移行。
     *
     * 5.
     * Attack終了後はGoToHookへ復帰。
     */
    bool TryAttackDuringGoToHook();


    /**
     * GoToHook中のAttack確率を計算する。
     *
     *
     * 12セル以上：
     * → 最大30%
     *
     *
     * 12～4セル：
     * → 近づくほど線形に低下
     *
     *
     * 4セル以内：
     * → 0%
     */
    float CalculateGoToHookAttackChance() const;


    // =========================================================
    // AttackLock
    // =========================================================


    /**
     * Attackへ移行可能か確認する。
     */
    bool CanEnterAttackMode() const;


    // =========================================================
    // Aルール
    // =========================================================


    /**
     * 目的地方向
     * ＋
     * 魚を避ける方向
     * ＋
     * ランダム方向
     *
     * を合成して移動候補地点を計算する。
     */
    FVector CalculateARuleMoveLocation(const FVector& GoalLocation);


    /**
     * 近くの魚から離れる方向を計算する。
     */
    FVector CalculateAvoidDirection() const;


    /**
     * Aルールで補正した地点へMoveToする。
     */
    bool MoveByARuleToLocation(const FVector& GoalLocation);


    // =========================================================
    // 汎用
    // =========================================================


    /**
     * 次の判断時間になっているか確認する。
     */
    bool ShouldMakeDecision() const;


    /**
     * 次の判断時刻を設定する。
     */
    void SetNextDecisionTime(
        float MinTime,
        float MaxTime
    );


    /**
     * 確率判定。
     *
     *
     * 例：
     *
     * Chance = 0.30
     * → 30%でtrue。
     */
    bool TryChance(float Chance) const;
};