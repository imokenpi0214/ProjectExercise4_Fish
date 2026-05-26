#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CameraStateComponent.generated.h"

class UCameraComponent;

USTRUCT(BlueprintType)
struct FCameraStateSettings
{
    GENERATED_BODY()

    // この状態で最終的に目指すFOV
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
    float TargetFOV = 90.0f;

    // この状態へ移行する滑らかさ。小さいほど速い
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.01"))
    float SmoothTime = 0.25f;
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROJECT4_FISH_API UCameraStateComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UCameraStateComponent();

protected:
    virtual void BeginPlay() override;

public:
    virtual void TickComponent(
        float DeltaTime,
        ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction
    ) override;

public:
    // Blueprint側から状態を切り替える関数
    UFUNCTION(BlueprintCallable, Category = "Camera State")
    void SetChargeNow(bool bNewCharge);

    UFUNCTION(BlueprintCallable, Category = "Camera State")
    void SetDashNow(bool bNewDash);

    // 現在の状態確認用
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera State")
    bool Charge_Now = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera State")
    bool Dash_Now = false;

    // プランナーがDetailsで調整する項目
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Settings")
    FCameraStateSettings NormalSettings;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Settings")
    FCameraStateSettings ChargeSettings;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Settings")
    FCameraStateSettings DashSettings;

private:
    UPROPERTY()
    UCameraComponent* CameraComponent = nullptr;

    float FOVVelocity = 0.0f;

private:
    const FCameraStateSettings& GetCurrentCameraSettings() const;

    static float SmoothDampFloat(
        float Current,
        float Target,
        float& CurrentVelocity,
        float SmoothTime,
        float DeltaTime
    );
};