#include "CameraStateComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Actor.h"

UCameraStateComponent::UCameraStateComponent()
{
    PrimaryComponentTick.bCanEverTick = true;

    // 初期値
    NormalSettings.TargetFOV = 90.0f;
    NormalSettings.SmoothTime = 0.25f;

    ChargeSettings.TargetFOV = 75.0f;
    ChargeSettings.SmoothTime = 0.35f;

    DashSettings.TargetFOV = 105.0f;
    DashSettings.SmoothTime = 0.12f;
}

void UCameraStateComponent::BeginPlay()
{
    Super::BeginPlay();

    AActor* Owner = GetOwner();

    if (Owner)
    {
        CameraComponent = Owner->FindComponentByClass<UCameraComponent>();
    }
}

void UCameraStateComponent::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction
)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!CameraComponent)
    {
        return;
    }

    const FCameraStateSettings& CurrentSettings = GetCurrentCameraSettings();

    const float CurrentFOV = CameraComponent->FieldOfView;

    const float NewFOV = SmoothDampFloat(
        CurrentFOV,
        CurrentSettings.TargetFOV,
        FOVVelocity,
        CurrentSettings.SmoothTime,
        DeltaTime
    );

    CameraComponent->SetFieldOfView(NewFOV);
}

void UCameraStateComponent::SetChargeNow(bool bNewCharge)
{
    Charge_Now = bNewCharge;
}

void UCameraStateComponent::SetDashNow(bool bNewDash)
{
    Dash_Now = bNewDash;
}

const FCameraStateSettings& UCameraStateComponent::GetCurrentCameraSettings() const
{
    // 優先順位：Dash > Charge > Normal
    if (Dash_Now)
    {
        return DashSettings;
    }

    if (Charge_Now)
    {
        return ChargeSettings;
    }

    return NormalSettings;
}

float UCameraStateComponent::SmoothDampFloat(
    float Current,
    float Target,
    float& CurrentVelocity,
    float SmoothTime,
    float DeltaTime
)
{
    SmoothTime = FMath::Max(0.0001f, SmoothTime);

    const float Omega = 2.0f / SmoothTime;
    const float X = Omega * DeltaTime;
    const float Exp = 1.0f / (1.0f + X + 0.48f * X * X + 0.235f * X * X * X);

    const float Change = Current - Target;
    const float Temp = (CurrentVelocity + Omega * Change) * DeltaTime;

    CurrentVelocity = (CurrentVelocity - Omega * Temp) * Exp;

    float Output = Target + (Change + Temp) * Exp;

    // 目標値を通り過ぎないように補正
    if ((Target - Current > 0.0f) == (Output > Target))
    {
        Output = Target;
        CurrentVelocity = 0.0f;
    }

    return Output;
}