
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DashWallBounceComponent.generated.h"

class UPrimitiveComponent;
class ACharacter;
class FLifetimeProperty;

// 壁衝突開始イベント
DECLARE_DYNAMIC_MULTICAST_DELEGATE(
    FOnWallBounceStarted
);

// スタン終了イベント
DECLARE_DYNAMIC_MULTICAST_DELEGATE(
    FOnWallBounceEnded
);

// 壁衝突ダメージ用イベント
// 衝突直前の水平速度をBlueprintへ通知する
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnWallBounceDamage,
    float,
    ImpactSpeed
);

UCLASS(
    ClassGroup=(Custom),
    meta=(BlueprintSpawnableComponent)
)
class PROJECT4_FISH_API UDashWallBounceComponent
    : public UActorComponent
{
    GENERATED_BODY()

public:

    UDashWallBounceComponent();

    virtual void GetLifetimeReplicatedProps(
        TArray<FLifetimeProperty>& OutLifetimeProps
    ) const override;

    // ダッシュ状態を設定
    UFUNCTION(BlueprintCallable, Category="Wall Bounce")
    void SetDashActive(bool bActive);

    // スタン状態を取得
    UFUNCTION(BlueprintPure, Category="Wall Bounce")
    bool IsWallStunned() const;

    // 壁衝突開始時にBPへ通知
    UPROPERTY(BlueprintAssignable, Category="Wall Bounce")
    FOnWallBounceStarted OnWallBounceStarted;

    // スタン終了時にBPへ通知
    UPROPERTY(BlueprintAssignable, Category="Wall Bounce")
    FOnWallBounceEnded OnWallBounceEnded;

    // 壁衝突がサーバーで確定したときに通知
    // ImpactSpeedには衝突前の水平速度を渡す
    UPROPERTY(BlueprintAssignable, Category="Wall Bounce")
    FOnWallBounceDamage OnWallBounceDamage;

protected:

    virtual void BeginPlay() override;

    virtual void TickComponent(
        float DeltaTime,
        ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction
    ) override;

    // ダッシュ中かどうか
    UPROPERTY(BlueprintReadOnly, Category="Wall Bounce")
    bool bDashActive = false;

    // サーバーが確定したスタン状態
    UPROPERTY(
        ReplicatedUsing=OnRep_IsStunned,
        BlueprintReadOnly,
        Category="Wall Bounce"
    )
    bool bIsStunned = false;

    // 壁に対する衝突角度
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Wall Bounce|Settings"
    )
    float BounceAngle = 45.0f;

    // スタン時間
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Wall Bounce|Settings"
    )
    float StunDuration = 0.8f;

    // ノックバック初速
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Wall Bounce|Settings"
    )
    float BounceSpeed = 600.0f;

    // ノックバック減速度
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category="Wall Bounce|Settings"
    )
    float BounceDeceleration = 900.0f;

    // スタン状態の同期通知
    UFUNCTION()
    void OnRep_IsStunned();

    // クライアントからサーバーへ衝突を報告
    UFUNCTION(Server, Reliable)
    void ServerRequestWallBounce(
        FVector HitLocation,
        FVector WallNormal
    );

    // キャラクターのカプセル衝突イベント
    UFUNCTION()
    void OnCapsuleHit(
        UPrimitiveComponent* HitComponent,
        AActor* OtherActor,
        UPrimitiveComponent* OtherComp,
        FVector NormalImpulse,
        const FHitResult& Hit
    );

private:

    // ローカルでノックバック中かどうか
    bool bLocalBounceActive = false;

    // ノックバック方向
    FVector BounceDirection = FVector::ZeroVector;

    // 現在のノックバック速度
    float CurrentBounceSpeed = 0.0f;

    // スタン経過時間
    float StunElapsedTime = 0.0f;

    // 操作している本人か判定
    bool IsLocallyControlledCharacter() const;

    // 壁衝突の角度などを判定
    bool CanBounce(
        const FVector& Velocity,
        const FVector& WallNormal
    ) const;

    // ノックバック開始
    void StartWallBounce(
        const FVector& WallNormal,
        bool bAuthoritative
    );

    // スタン終了
    void EndStun();
};
