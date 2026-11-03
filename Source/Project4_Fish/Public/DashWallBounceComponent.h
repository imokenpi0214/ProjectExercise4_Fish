
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DashWallBounceComponent.generated.h"

class UPrimitiveComponent;
class ACharacter;

// 壁に衝突したときのイベント
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnWallBounceStarted);

// スタン終了時のイベント
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnWallBounceEnded);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROJECT4_FISH_API UDashWallBounceComponent : public UActorComponent
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

    // スタン中か取得
    UFUNCTION(BlueprintPure, Category="Wall Bounce")
    bool IsWallStunned() const;

    // 壁衝突開始イベント
    UPROPERTY(BlueprintAssignable, Category="Wall Bounce")
    FOnWallBounceStarted OnWallBounceStarted;

    // スタン終了イベント
    UPROPERTY(BlueprintAssignable, Category="Wall Bounce")
    FOnWallBounceEnded OnWallBounceEnded;

protected:
    virtual void BeginPlay() override;

    virtual void TickComponent(
        float DeltaTime,
        ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction
    ) override;

    // ダッシュ中か
    UPROPERTY(BlueprintReadOnly, Category="Wall Bounce")
    bool bDashActive = false;

    // スタン状態（マルチプレイ同期）
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Wall Bounce")
    bool bIsStunned = false;

    // 正面からの許容衝突角度
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wall Bounce|Settings")
    float BounceAngle = 45.0f;

    // スタン時間
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wall Bounce|Settings")
    float StunDuration = 0.8f;

    // ノックバック初速
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wall Bounce|Settings")
    float BounceSpeed = 600.0f;

    // 減速の強さ
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wall Bounce|Settings")
    float BounceDeceleration = 900.0f;

private:
    // ノックバック方向
    FVector BounceDirection = FVector::ZeroVector;

    // 現在のノックバック速度
    float CurrentBounceSpeed = 0.0f;

    // スタン経過時間
    float StunElapsedTime = 0.0f;

    // スタン終了処理
    void EndStun();

    // カプセルの衝突イベント
    UFUNCTION()
    void OnCapsuleHit(
        UPrimitiveComponent* HitComponent,
        AActor* OtherActor,
        UPrimitiveComponent* OtherComp,
        FVector NormalImpulse,
        const FHitResult& Hit
    );
};
