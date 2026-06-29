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
 * 役割：
 * - AIモード管理
 * - ランダム遊泳
 * - Aルール移動
 * - 出現時モード
 * - 今後、貪る / 攻撃 / 逃走 / 釣られに行くモードもここに追加していく
 *
 * 注意：
 * - 実際に移動するPawnは BP_Enemy
 * - AIController は MoveToLocation を担当
 * - このComponentは「どこへ行くか」「どのモードにするか」を判断する
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
    // 基本AI設定
    // =========================

    // 現在のAIモード。
    // 例：Wanderならランダム遊泳、SpawnMoveなら目的地へ向かう。
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

    // 今後、プランクトン集積地・釣り糸・逃走先などの目的地を入れるための変数。
    // SpawnMoveでは、この位置へAルールで向かう。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Target")
    FVector TargetLocation = FVector::ZeroVector;

    // =========================
    // 出現時モード / SpawnMove
    // =========================

    // SpawnMove中、攻撃モードへ移行する確率。
    // 0.03 = 3%
    // Attackが未実装の間は、いったんWanderへ逃がす予定。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|SpawnMove")
    float SpawnMoveAttackChance = 0.03f;

    // TargetLocationへ到着したと判断する距離。
    // この距離以内に入ったら「目的地に着いた」と扱う。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|SpawnMove")
    float TargetReachedDistance = 150.0f;

private:
    // このコンポーネントを持っているPawn。
    // 基本的には BP_Enemy。
    UPROPERTY()
    APawn* OwnerPawn = nullptr;

    // OwnerPawnを操作しているAIController。
    // MoveToLocationやStopMovementに使う。
    UPROPERTY()
    AAIController* OwnerAIController = nullptr;

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
    // TargetLocationへAルールで向かう。
    void TickSpawnMove(float DeltaTime);

    // =========================
    // Wander用関数
    // =========================

    // NavMesh上のランダム地点を探して移動する。
    // 現在はAルールを通して少し補正してから移動する。
    bool MoveToRandomLocation();

    // 次の判断時間になっているか確認する。
    bool ShouldMakeDecision() const;

    // 次の判断時間を設定する。
    // MinTime〜MaxTimeの間でランダムに待つ。
    void SetNextDecisionTime(float MinTime, float MaxTime);

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
    // 汎用判定関数
    // =========================

    // 現在位置がTargetLocationに十分近いか判定する。
    bool IsNearTargetLocation() const;

    // 確率判定。
    // Chance = 0.03 なら3%でtrue。
    bool TryChance(float Chance) const;
};