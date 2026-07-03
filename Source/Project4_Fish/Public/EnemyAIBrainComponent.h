#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FishAITypes.h"
#include "EnemyAIBrainComponent.generated.h"

class AAIController;

/**
 * 敵AIの「脳」になるコンポーネント。
 *
 * BP_Enemy に追加して使用する。
 *
 * 役割：
 * - AIモード管理
 * - ランダム遊泳
 * - Aルール移動
 * - 出現時モード
 * - 貪るモード
 * - 今後、攻撃 / 逃走 / 釣られに行くモードもここに追加していく
 *
 * 基本方針：
 * - BP_Enemy は BP_PlayerBase の子として作る
 * - この C++ Component が「どこへ行くか」「どのモードにするか」を判断する
 * - 実際の移動命令は AIController の MoveToLocation を使う
 * - BP側からは BlueprintCallable 関数でこのComponentへ指示を出す
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
     * 例：
     * - Wanderにする
     * - SpawnMoveにする
     * - Devourにする
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
     * 理由：
     * - 狙ったプランクトンに向かう途中で別のプランクトンを拾うことがあるため。
     *
     * 今後の処理：
     * - EatenPlanktonCount++
     * - 20%でAttack判定
     * - 食べた数でGoToHook判定
     */
    UFUNCTION(BlueprintCallable, Category="AI|Collect")
    void NotifyPlanktonCollected();

public:
    // =========================
    // 基本AI設定
    // =========================

    // 現在のAIモード。
    // 例：Wanderなら自由移動、SpawnMoveなら群れ中央へ移動、Devourならプランクトンを食べる。
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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|ARule")
    FName FishTagName = "Fish";

    // 現在の目的地。
    // SpawnMoveではプランクトン群れ中央。
    // Devourでは狙っているプランクトン位置。
    // GoToHookでは釣り糸位置。
    // Fleeでは逃走先。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Target")
    FVector TargetLocation = FVector::ZeroVector;

    // =========================
    // 出現時モード / SpawnMove
    // =========================

    // SpawnMove中、攻撃モードへ移行する確率。
    // 0.03 = 3%
    // Attackが未実装の間は0.0にしておくのがおすすめ。
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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Devour")
    float DevourAttackChance = 0.20f;

    // 食べたプランクトン数。
    // 狙っていたプランクトン以外を拾っても加算する。
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AI|Devour")
    int32 EatenPlanktonCount = 0;

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

    // 現在向かっているプランクトン群れActor。
    // BP_CollectItemGroup が入る想定。
    UPROPERTY()
    AActor* TargetGroupActor = nullptr;

    // 現在狙っているプランクトンActor。
    // Devour中に使う。
    UPROPERTY()
    AActor* TargetPlanktonActor = nullptr;

    // 次にAI判断をする時刻。
    // 毎フレーム判断すると重くなるので、一定間隔で判断する。
    float NextDecisionTime = 0.0f;

private:
    // =========================
    // モード別Tick
    // =========================

    // Wanderモードの毎回処理。
    // 30%で停止、70%でランダム移動する。
    void TickWander(float DeltaTime);

    // SpawnMoveモードの毎回処理。
    // TargetLocation、つまりプランクトン群れ中央へAルールで向かう。
    // 到着したらDevourへ移行する。
    void TickSpawnMove(float DeltaTime);

    // Devourモードの毎回処理。
    // 周囲のプランクトンを探し、狙ったプランクトンへ向かう。
    // 狙っていたプランクトンが消えたら別のプランクトンを探す。
    // 周囲にプランクトンが無ければAttackへ移行する予定。
    void TickDevour(float DeltaTime);

    // =========================
    // Wander用関数
    // =========================

    // NavMesh上のランダム地点を探して移動する。
    // 現在はAルールを通して少し補正してから移動する。
    bool MoveToRandomLocation();

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
    // GoToHook未実装の間は仮でWanderなどに逃がす予定。
    bool TryGoToHookByPlanktonCount();

    // 食べた数からGoToHook確率を返す。
    // 40以上：100%
    // 30以上：50%
    // 20以上：25%
    // 10以上：12.5%
    // それ未満：0%
    float GetGoToHookChanceByPlanktonCount() const;

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