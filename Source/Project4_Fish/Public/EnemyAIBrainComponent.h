#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FishAITypes.h"
#include "EnemyAIBrainComponent.generated.h"

class AAIController;
class UEnemyAIAttackComponent;

/**
 * 敵AIの「脳」になるコンポーネント。
 *
 * BP_Enemy に追加して使用する。
 *
 * 役割：
 * - AIモード管理
 * - ランダム遊泳
 * - Wander中の近距離敵検知
 * - 攻撃禁止時間 / AttackLock
 * - 安全地帯 / 隠れ場所にいるActorを攻撃対象から除外
 * - Aルール移動
 * - 出現時モード
 * - 貪るモード
 * - 攻撃モード
 * - 攻撃ヒット後の行動判断
 * - 被弾時の逃走判断
 * - 逃走モード
 *
 * 基本方針：
 * - BP_Enemy は BP_PlayerBase の子として作る
 * - この C++ Component が「どこへ行くか」「どのモードにするか」を判断する
 * - 実際の通常移動命令は AIController の MoveToLocation を使う
 * - 突進攻撃は EnemyAIAttackComponent が担当する
 * - BP側からは BlueprintCallable 関数でこのComponentへ通知を送る
 *
 * EnemyAIAttackComponentとの役割分担：
 *
 * EnemyAIBrainComponent：
 * - 攻撃対象を探す
 * - Attackモードへ入る
 * - 攻撃開始判断
 * - 攻撃後のモード判断
 * - 被弾後のFlee判断
 * - Flee中の逃走先判断
 * - 安全地帯にいるActorを攻撃対象から外す
 *
 * EnemyAIAttackComponent：
 * - チャージ
 * - ダッシュ
 * - LaunchCharacter
 * - AttackSphere ON/OFF
 * - ダメージ量計算
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
    // =========================
    // BPから呼ぶ公開関数
    // =========================

    /**
     * AIモードを変更する関数。
     *
     * BPからCurrentModeを直接Setするより、
     * この関数を通した方が後からEnter処理を追加しやすい。
     *
     * 注意：
     * - 攻撃禁止時間中にAttackへ入ろうとした場合、
     *   .cpp側でWanderへ変換する。
     */
    UFUNCTION(BlueprintCallable, Category="AI|Mode")
    void SetAIMode(EEnemyAIMode NewMode);

    /**
     * BP_CollectItemGroupなど、プランクトン群れActorをAIに渡す関数。
     *
     * BP_EnemyのAI_CheckCollectItemGroupから呼ぶ想定。
     *
     * 処理内容：
     * - TargetGroupActor に保存
     * - TargetLocation に群れ中央の座標を設定
     * - CurrentMode を SpawnMove に変更
     */
    UFUNCTION(BlueprintCallable, Category="AI|Target")
    void SetCollectItemGroupTarget(AActor* NewGroupActor);

    /**
     * AIがプランクトンを1つ取得した時に呼ぶ関数。
     *
     * 狙っていたプランクトンかどうかに関係なく呼ぶ。
     *
     * 処理：
     * - EatenPlanktonCount++
     * - 20%でAttack判定
     * - 食べた数でGoToHook判定
     *
     * 注意：
     * - 攻撃禁止時間中にAttackへ入ろうとしても、
     *   SetAIMode側でWanderへ変換される。
     */
    UFUNCTION(BlueprintCallable, Category="AI|Collect")
    void NotifyPlanktonCollected();

    /**
     * 自分が攻撃を食らった時にBP側から呼ぶ関数。
     *
     * 正式仕様：
     * - 自身HPが20%未満なら70%でFlee
     * - 自身HPが20%〜79%なら20%でFlee
     * - 自身HPが80%以上ならAttack継続
     *
     * 引数：
     * - CurrentHP：
     *   ダメージを受けた後の自分の現在HP。
     *
     * - MaxHP：
     *   自分の最大HP。
     *
     * - DamageCauser：
     *   自分にダメージを与えた相手。
     *   Flee時はこの相手から遠ざかる。
     *
     * 注意：
     * - これは「被弾時」の処理。
     * - 「攻撃を当てた時」の処理は NotifyAttackHit で行う。
     */
    UFUNCTION(BlueprintCallable, Category="AI|Combat")
    void NotifyDamaged(float CurrentHP, float MaxHP, AActor* DamageCauser);

    /**
     * 自分の攻撃が相手に当たった時にBP側から呼ぶ関数。
     *
     * BP側のAttackSphere BeginOverlap内で、
     * BPI_Player Apply Damage を呼んだ後に、この関数も呼ぶ。
     *
     * 引数：
     * - HitActor：
     *   攻撃が当たった相手。
     *   BPでは Other Actor を渡す。
     *
     * - CurrentHP：
     *   攻撃したAI自身の現在HP。
     *   相手のHPではないので注意。
     *
     * - MaxHP：
     *   攻撃したAI自身の最大HP。
     *
     * - bHitActorBecameFlee：
     *   攻撃を受けた相手AIがFleeへ移行したかどうか。
     *   今回は false 固定でOK。
     *
     * 現在の仕様：
     * - 攻撃を当てた時、自身HPが80%以上ならAttack継続
     * - それ以外はWanderへ戻る
     */
    UFUNCTION(BlueprintCallable, Category="AI|Combat")
    void NotifyAttackHit(AActor* HitActor, float CurrentHP, float MaxHP, bool bHitActorBecameFlee);

    /**
     * 指定秒数、Attackへ移行しないようにする。
     *
     * 使用タイミング：
     * - ゲーム開始直後
     * - リスポーン直後
     * - ゴール直後
     * - SafeZoneから出た直後
     *
     * 例：
     * LockAttackForSeconds(5.0f);
     *
     * 効果：
     * - Attackモードへ入ろうとしてもWanderへ変換する
     * - Wander中の3セル以内検知を止められる
     * - 攻撃中に呼ばれた場合は攻撃をキャンセルしてWanderへ戻す
     */
    UFUNCTION(BlueprintCallable, Category="AI|Protection")
    void LockAttackForSeconds(float Duration);

    /**
     * 現在Attack禁止中かどうか。
     *
     * BPデバッグやSafeZone制御で確認用に使える。
     */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="AI|Protection")
    bool IsAttackLocked() const;

public:
    // =========================
    // 基本AI設定
    // =========================

    // 現在のAIモード。
    // 例：
    // - Wander：自由遊泳
    // - SpawnMove：プランクトン群れ中央へ移動
    // - Devour：プランクトンを貪る
    // - Attack：他の魚を攻撃する
    // - Flee：攻撃してきた相手から逃げる
    // - GoToHook：釣り糸へ向かう
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI")
    EEnemyAIMode CurrentMode = EEnemyAIMode::Wander;

    // AIの強さ。
    // Weakなら弱いAI、Strongなら強いAI。
    // 後で魚種変更確率や購入方針に使う。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI")
    EEnemyAILevel AILevel = EEnemyAILevel::Weak;

    // 1セルあたりのUnreal Unit。
    // プランナーが「3セル」「7セル」「12セル」などで調整できるようにする基準値。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Cell")
    float CellSize = 300.0f;

    // =========================
    // 攻撃禁止時間 / Spawn Protection
    // =========================

    // BeginPlay直後にAttackへ移行しない時間。
    //
    // 目的：
    // - ゲーム開始直後、敵同士が近くにいて即攻撃合戦になるのを防ぐ。
    //
    // 例：
    // 5.0なら、開始から5秒はAttackへ入らない。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Protection")
    float InitialAttackLockDuration = 5.0f;

    // リスポーン直後やゴール直後に使うデフォルト攻撃禁止時間。
    //
    // BP側で、
    // LockAttackForSeconds(DefaultAttackLockDuration)
    // のように呼ぶ想定。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Protection")
    float DefaultAttackLockDuration = 5.0f;

    // trueなら、攻撃禁止時間中はWander中の近距離敵検知を無効にする。
    //
    // true推奨。
    // これがfalseだと、検知自体は行うが、SetAIMode側でAttackがWanderへ変換される。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Protection")
    bool bDisableWanderDetectDuringAttackLock = true;

    // trueなら、攻撃禁止時間中はNotifyDamagedからの反撃Attackを抑制する。
    //
    // true推奨。
    // 保護時間中に攻撃されても、即反撃して攻撃合戦になりにくくする。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Protection")
    bool bDisableAttackReactionDuringAttackLock = true;

    // =========================
    // 安全地帯 / 隠れ場所
    // =========================

    // 安全地帯・隠れ場所にいるActorへ付けるタグ。
    //
    // BP_SafeZone側で、
    // BeginOverlap時に OtherActor の Tags に InSafeZone を Add Unique
    // EndOverlap時に InSafeZone を Remove
    // する想定。
    //
    // このタグを持つActorは攻撃対象から除外される。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|SafeZone")
    FName SafeZoneActorTagName = "InSafeZone";

    // trueなら、SafeZoneActorTagNameを持つActorを攻撃対象から除外する。
    //
    // 対象：
    // - Wander中の3セル検知
    // - Attack中の攻撃対象検索
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|SafeZone")
    bool bIgnoreActorsInSafeZone = true;

    // =========================
    // 遊泳モード / Wander
    // =========================

    // ランダム移動先を探す半径。
    // Wanderモード中、この範囲内のNavMesh上からランダム地点を選ぶ。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Wander")
    float WanderRadius = 1500.0f;

    // Wander中、停止または次の判断までの最短待機時間。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Wander")
    float WanderWaitMin = 1.0f;

    // Wander中、停止または次の判断までの最長待機時間。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Wander")
    float WanderWaitMax = 3.0f;

    // Wander中、このセル数以内にFishタグ持ちActorがいたらAttackへ移行する。
    //
    // 仕様：
    // - 自分から3セル以内
    // - 敵に視界に入ったと思われる範囲
    //
    // 実距離：
    // WanderEnemyDetectCells * CellSize
    //
    // 例：
    // CellSize = 300
    // WanderEnemyDetectCells = 3
    // なら 900uu 以内でAttackへ移行。
    //
    // 注意：
    // - 攻撃禁止時間中は、この検知を止める設定がある。
    // - SafeZoneタグ持ちActorは検知対象から除外できる。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Wander")
    float WanderEnemyDetectCells = 3.0f;

    // MoveToLocation の到着許容距離。
    // 値が小さいほど目的地ぴったりを目指す。
    // 大きいほど「近づいたら到着扱い」になる。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Move")
    float AcceptanceRadius = 100.0f;

    // =========================
    // Aルール用
    // =========================

    // 近くの魚を探す範囲。
    // 現在の簡易版ではタグ検索を使っているため未使用寄り。
    // 後でSphereOverlapに変更した時に使う予定。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|ARule")
    float ARuleSearchRadius = 1000.0f;

    // これより近い魚から離れる。
    // 例：2セルなら CellSize * 2 の距離より近い魚を避ける。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|ARule")
    float MinSeparationCells = 2.0f;

    // 仕様上の最大距離。
    // 「他の魚から最大7セル以上は離れない」などに使う予定。
    // 今の簡易Aルールではまだ本格使用しない。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|ARule")
    float MaxSeparationCells = 7.0f;

    // 目的地へ向かう強さ。
    // 高くすると目的地へまっすぐ向かいやすくなる。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|ARule")
    float GoalWeight = 1.0f;

    // 他の魚を避ける強さ。
    // 高くすると魚同士が離れやすくなる。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|ARule")
    float AvoidWeight = 1.5f;

    // ランダムにずれる強さ。
    // 高くすると移動がふらつく。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|ARule")
    float RandomWeight = 0.4f;

    // 一度の判断でどれくらい先の地点へMoveToするか。
    // 大きいほど長めに進み、小さいほど細かく進路変更する。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|ARule")
    float ARuleStepDistance = 800.0f;

    // 回避対象として探すActorタグ。
    // BP_PlayerBase や BP_Enemy に Fish タグを付けると回避対象になる。
    //
    // 今回のWander敵検知やAttack対象検索にも使う。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|ARule")
    FName FishTagName = "Fish";

    // 現在の目的地。
    // SpawnMoveではプランクトン群れ中央。
    // Devourでは狙っているプランクトン位置。
    // Attackでは狙っている魚の位置。
    // Fleeでは逃走方向の一時目的地。
    // GoToHookでは釣り糸位置。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Target")
    FVector TargetLocation = FVector::ZeroVector;

    // =========================
    // 出現時モード / SpawnMove
    // =========================

    // SpawnMove中、攻撃モードへ移行する確率。
    // 0.03 = 3%
    //
    // 注意：
    // 攻撃禁止時間中はSetAIMode側でAttackがWanderへ変換される。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|SpawnMove")
    float SpawnMoveAttackChance = 0.03f;

    // TargetLocationへ到着したと判断する距離。
    // SpawnMoveでは、群れ中央にこの距離以内まで近づいたらDevourへ移行する。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|SpawnMove")
    float TargetReachedDistance = 150.0f;

    // =========================
    // 貪るモード / Devour
    // =========================

    // プランクトンActorを探すためのタグ。
    // 実際のプランクトンBPに Plankton タグを付ける想定。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Devour")
    FName PlanktonTagName = "Plankton";

    // Devour中にプランクトンを探す範囲。
    // この範囲内にプランクトンが無ければ、周囲のプランクトンが消えた扱いにする。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Devour")
    float DevourSearchRadius = 1200.0f;

    // 狙っているプランクトンへ到着したと判断する距離。
    // 実際の取得処理はOverlap側で行われる想定なので、
    // ここでは「近くまで向かう」判断に使う。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Devour")
    float PlanktonReachedDistance = 100.0f;

    // プランクトンを取得した時、Attackへ移行する確率。
    // 0.20 = 20%
    //
    // 注意：
    // 攻撃禁止時間中はSetAIMode側でAttackがWanderへ変換される。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Devour")
    float DevourAttackChance = 0.20f;

    // 食べたプランクトン数。
    // 狙っていたプランクトン以外を拾っても加算する。
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AI|Devour")
    int32 EatenPlanktonCount = 0;

    // =========================
    // 攻撃モード / Attack
    // =========================

    // Attack中に攻撃対象を探す範囲。
    // Fishタグを持つActorから、自分以外の一番近いActorを探す。
    //
    // SafeZoneタグ持ちActorは除外できる。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float AttackSearchRadius = 3000.0f;

    // 攻撃可能距離。
    // この距離以内に入ったら、EnemyAIAttackComponentへStartDashAttackを命令する。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float AttackRange = 400.0f;

    // Attack中の進路更新間隔の最小値。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float AttackThinkIntervalMin = 0.1f;

    // Attack中の進路更新間隔の最大値。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float AttackThinkIntervalMax = 0.25f;

    // 攻撃ヒット後、自身HPがこの値以上ならAttack継続。
    // 0.80 = 80%
    //
    // NotifyAttackHit内で、
    // CurrentHP / MaxHP >= AttackContinueHpRate
    // ならAttack継続する。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float AttackContinueHpRate = 0.80f;

    // 攻撃対象がAIで、相手がFleeに移行した時、追いかける確率。
    // 0.50 = 50%
    //
    // 今回はまだ本格使用しない。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float ChaseFleeTargetChance = 0.50f;

    // 相手がFleeに移行した時の追跡時間の最小値。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float ChaseFleeTimeMin = 3.0f;

    // 相手がFleeに移行した時の追跡時間の最大値。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float ChaseFleeTimeMax = 6.0f;

    // 追跡後、Attack継続する確率。
    // 0.70 = 70%
    //
    // 今回はまだ本格使用しない。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float AdditionalAttackChance = 0.70f;

    // 攻撃ヒット後、Attackを継続する時に少し待つ時間。
    // 連続攻撃が速すぎる場合に使う。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float AttackContinueDelayMin = 0.3f;

    // 攻撃ヒット後、Attackを継続する時に少し待つ時間の最大値。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Attack")
    float AttackContinueDelayMax = 0.8f;

    // =========================
    // 逃走モード / Flee
    // =========================

    // HP20%未満判定。
    // NotifyDamagedで使用する。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Flee")
    float LowHpFleeThreshold = 0.20f;

    // HP80%未満までを中HP帯として扱う。
    // HP20%〜79%ならMidHpFleeChanceで逃走判定。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Flee")
    float MidHpFleeThreshold = 0.80f;

    // HP20%未満の時、Fleeへ移行する確率。
    // 0.70 = 70%
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Flee")
    float LowHpFleeChance = 0.70f;

    // HP20%〜79%の時、Fleeへ移行する確率。
    // 0.20 = 20%
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Flee")
    float MidHpFleeChance = 0.20f;

    // 逃走完了距離。
    // 12セルほど離れたらWanderへ戻る。
    //
    // 実距離：
    // FleeEndDistanceCells * CellSize
    //
    // 例：
    // CellSize = 300
    // FleeEndDistanceCells = 12
    // なら 3600uu 離れたら逃走終了。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Flee")
    float FleeEndDistanceCells = 12.0f;

    // Flee中の判断間隔の最小値。
    // 小さいほど細かく逃走方向を更新する。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Flee")
    float FleeThinkIntervalMin = 0.15f;

    // Flee中の判断間隔の最大値。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Flee")
    float FleeThinkIntervalMax = 0.35f;

    // 逃走時、攻撃してきた相手から離れる方向の強さ。
    //
    // MoveByARuleAwayFromActorで、
    // 自分の位置 + 離れる方向 * ARuleStepDistance * FleeAwayWeight
    // を逃走用GoalLocationとして作る。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Flee")
    float FleeAwayWeight = 2.0f;

    // =========================
    // 強いAI / 弱いAI用
    // =========================

    // Strong AI がAttack移行時に魚種変更する確率。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|FishChange")
    float StrongFishChangeChance = 0.80f;

    // Weak AI がAttack移行時に魚種変更する確率。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|FishChange")
    float WeakFishChangeChance = 0.20f;

private:
    // =========================
    // 内部参照
    // =========================

    // このコンポーネントを持っているPawn。
    // 基本的には BP_Enemy。
    UPROPERTY()
    APawn* OwnerPawn = nullptr;

    // OwnerPawnを操作しているAIController。
    // MoveToLocationやStopMovementに使う。
    UPROPERTY()
    AAIController* OwnerAIController = nullptr;

    // AI専用攻撃コンポーネント。
    // Attackモード中、AttackRange以内に入ったらStartDashAttackを呼ぶ。
    //
    // 重要：
    // - このComponentが攻撃中なら、Brain側はMoveToを出さない。
    // - MoveToを出すと突進方向が崩れる。
    UPROPERTY()
    UEnemyAIAttackComponent* AIAttackComponent = nullptr;

    // 現在向かっているプランクトン群れActor。
    // BP_CollectItemGroup が入る想定。
    UPROPERTY()
    AActor* TargetGroupActor = nullptr;

    // 現在狙っているプランクトンActor。
    // Devour中に使う。
    UPROPERTY()
    AActor* TargetPlanktonActor = nullptr;

    // 現在狙っている攻撃対象。
    // Attack中に使う。
    //
    // NotifyAttackHitでAttack継続する場合、
    // HitActorをここに入れて、同じ相手を狙い続けられるようにする。
    //
    // Fleeでも、LastDamageCauserが無効だった時の逃走元として使う。
    UPROPERTY()
    AActor* TargetFishActor = nullptr;

    // 直前に自分へダメージを与えたActor。
    //
    // Fleeでは基本的にこのActorから逃げる。
    // TargetFishActorより優先する。
    UPROPERTY()
    AActor* LastDamageCauser = nullptr;

    // 逃げた相手を追いかける時の終了時刻。
    // 今回のStepではまだ本格使用しない。
    float ChaseEndTime = 0.0f;

    // 次にAI判断をする時刻。
    // 毎フレーム判断すると重くなるので、一定間隔で判断する。
    float NextDecisionTime = 0.0f;

    // この時間まではAttackへ移行しない。
    //
    // GetWorld()->GetTimeSeconds() と比較する。
    //
    // 例：
    // AttackLockEndTime = 現在時刻 + 5.0f
    // なら、5秒間はAttack禁止。
    float AttackLockEndTime = 0.0f;

private:
    // =========================
    // モード別Tick
    // =========================

    // Wanderモードの毎回処理。
    //
    // 先頭で3セル以内のFishタグActorを探し、
    // 見つかったらAttackへ移行する。
    //
    // ただし、
    // - 攻撃禁止時間中
    // - 相手がSafeZoneタグ持ち
    // の場合はAttackしない。
    //
    // 見つからなければ、
    // 30%で停止、70%でランダム移動する。
    void TickWander(float DeltaTime);

    // SpawnMoveモードの毎回処理。
    // TargetLocation、つまりプランクトン群れ中央へAルールで向かう。
    // 到着したらDevourへ移行する。
    void TickSpawnMove(float DeltaTime);

    // Devourモードの毎回処理。
    // 周囲のプランクトンを探し、狙ったプランクトンへ向かう。
    // 狙っていたプランクトンが消えたら別のプランクトンを探す。
    // 周囲にプランクトンが無ければAttackへ移行する。
    void TickDevour(float DeltaTime);

    // Attackモードの毎回処理。
    //
    // 流れ：
    // - 攻撃対象を探す
    // - 攻撃対象へ近づく
    // - AttackRange以内に入る
    // - AIAttackComponent.StartDashAttack(TargetFishActor) を呼ぶ
    //
    // 注意：
    // - AIAttackComponentが攻撃中なら、この関数ではMoveToを出さない。
    void TickAttack(float DeltaTime);

    // Fleeモードの毎回処理。
    //
    // 仕様：
    // - たった今攻撃してきた相手から逃げる
    // - LastDamageCauserを優先して逃走元にする
    // - LastDamageCauserがなければTargetFishActorから逃げる
    // - 逃走移動にもAルールを使う
    // - 12セルほど離れたらWanderへ戻る
    void TickFlee(float DeltaTime);

    // =========================
    // Wander用関数
    // =========================

    // NavMesh上のランダム地点を探して移動する。
    // 現在はAルールを通して少し補正してから移動する。
    bool MoveToRandomLocation();

    // Wander中、3セル以内に攻撃対象がいるか確認する。
    //
    // 見つかった場合：
    // - TargetFishActorに保存
    // - TargetLocationを更新
    // - Attackモードへ移行
    // - trueを返す
    //
    // 見つからなかった場合：
    // - falseを返す
    bool TryDetectEnemyInWander();

    // Wander中に反応する近距離魚を探す。
    //
    // Fishタグ持ちActorの中から、
    // 自分以外で、WanderEnemyDetectCells * CellSize 以内の
    // 一番近いActorを返す。
    //
    // SafeZoneActorTagNameを持つActorは除外する。
    AActor* FindNearestWanderEnemy() const;

    // =========================
    // SpawnMove / Target用関数
    // =========================

    // 現在位置がTargetLocationに十分近いか判定する。
    bool IsNearTargetLocation() const;

    // =========================
    // Devour用関数
    // =========================

    // 周囲のプランクトンから一番近いものを探す。
    // 見つからなければnullptrを返す。
    AActor* FindNearestPlankton() const;

    // TargetPlanktonActorがまだ有効か確認する。
    bool IsTargetPlanktonValid() const;

    // TargetPlanktonActorに十分近いか確認する。
    bool IsNearTargetPlankton() const;

    // TargetPlanktonActorを近くのプランクトンに更新する。
    // 見つかったらtrue、見つからなければfalse。
    bool SelectNearestPlankton();

    // 食べた数に応じてGoToHookへ行くか判定する。
    // GoToHook未実装の場合は、.cpp側で一時的に無効化してもよい。
    bool TryGoToHookByPlanktonCount();

    // 食べた数からGoToHook確率を返す。
    // 40以上：100%
    // 30以上：50%
    // 20以上：25%
    // 10以上：12.5%
    // それ未満：0%
    float GetGoToHookChanceByPlanktonCount() const;

    // =========================
    // Attack用関数
    // =========================

    // 周囲のFishタグ持ちActorから一番近い攻撃対象を探す。
    // 自分自身とSafeZoneタグ持ちActorは除外する。
    AActor* FindNearestAttackTarget() const;

    // TargetFishActorがまだ有効か確認する。
    //
    // SafeZoneタグ持ちになったActorは無効扱いにする。
    bool IsTargetFishValid() const;

    // TargetFishActorを近くの攻撃対象に更新する。
    // 見つかったらtrue、見つからなければfalse。
    bool SelectNearestAttackTarget();

    // TargetFishActorに攻撃可能距離まで近づいているか確認する。
    bool IsNearAttackTarget() const;

    // Attackの仮終了処理。
    //
    // 本来はAIAttackComponentで突進するが、
    // AIAttackComponentが見つからない時の保険として使う。
    void FinishAttackTemporarily();

    // Attackモードに入った時、強いAI/弱いAIに応じて魚種変更する予定の関数。
    // 今回はコメントだけ入れて、処理は後で実装する。
    void TryChangeFishOnAttackMode();

    // 攻撃対象として有効か確認する。
    //
    // falseになる条件：
    // - nullptr
    // - 自分自身
    // - SafeZoneタグ持ちActor
    //
    // 使う場所：
    // - FindNearestWanderEnemy
    // - FindNearestAttackTarget
    // - IsTargetFishValid
    bool IsValidAttackCandidate(AActor* CandidateActor) const;

    // =========================
    // Flee用関数
    // =========================

    // 逃走元Actorを取得する。
    //
    // 優先順位：
    // 1. LastDamageCauser
    //    直前に自分へダメージを与えた相手。
    //
    // 2. TargetFishActor
    //    直前まで攻撃していた相手。
    //
    // 3. どちらも無効ならnullptr。
    AActor* GetFleeSourceActor() const;

    // 逃走元から十分離れたか確認する。
    //
    // FleeEndDistanceCells * CellSize 以上離れたらtrue。
    // trueならFlee終了してWanderへ戻る。
    bool IsFarEnoughFromFleeSource() const;

    // 指定Actorから遠ざかるようにAルール移動する。
    //
    // 処理：
    // - 自分の位置 - 相手の位置 で離れる方向を作る
    // - その方向に ARuleStepDistance * FleeAwayWeight 進んだ地点を仮Goalにする
    // - MoveByARuleToLocationでAルール移動する
    //
    // つまり、
    // 「相手から離れる」
    // ＋「周囲の魚を避ける」
    // ＋「少しランダム」
    // ＋「NavMesh上へ補正」
    // になる。
    bool MoveByARuleAwayFromActor(AActor* SourceActor);

    // =========================
    // AttackLock用関数
    // =========================

    // Attackへ移行してよいか確認する。
    //
    // 攻撃禁止時間中ならfalse。
    bool CanEnterAttackMode() const;

    // =========================
    // Aルール関数
    // =========================

    // 目的地へ向かいつつ、魚を避けて少しランダム性を入れた移動先を計算する。
    FVector CalculateARuleMoveLocation(const FVector& GoalLocation);

    // 近くの魚から離れる方向を計算する。
    FVector CalculateAvoidDirection() const;

    // Aルールで補正した地点へMoveToする。
    bool MoveByARuleToLocation(const FVector& GoalLocation);

    // =========================
    // 汎用関数
    // =========================

    // 次の判断時間になっているか確認する。
    bool ShouldMakeDecision() const;

    // 次の判断時間を設定する。
    // MinTime〜MaxTimeの間でランダムに待つ。
    void SetNextDecisionTime(float MinTime, float MaxTime);

    // 確率判定。
    // Chance = 0.03 なら3%でtrue。
    bool TryChance(float Chance) const;
};