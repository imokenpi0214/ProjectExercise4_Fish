#include "LockOnMathLibrary.h"

bool ULockOnMathLibrary::CalculateInterceptDirection2D(
    const FVector& PlayerLocation,
    float DashSpeed,
    const FVector& TargetLocation,
    const FVector& TargetVelocity,
    FVector& OutDirection,
    FVector& OutPredictedLocation,
    float& OutTime
)
{
    OutDirection = FVector::ZeroVector;
    OutPredictedLocation = TargetLocation;
    OutTime = 0.0f;

    if (DashSpeed <= KINDA_SMALL_NUMBER)
    {
        return false;
    }

    // XY平面で計算する
    FVector PlayerPos2D = PlayerLocation;
    FVector TargetPos2D = TargetLocation;
    FVector TargetVel2D = TargetVelocity;

    PlayerPos2D.Z = 0.0f;
    TargetPos2D.Z = 0.0f;
    TargetVel2D.Z = 0.0f;

    const FVector RelativePos = TargetPos2D - PlayerPos2D;

    // |RelativePos + TargetVel2D * t| = DashSpeed * t
    const float A = FVector::DotProduct(TargetVel2D, TargetVel2D) - DashSpeed * DashSpeed;
    const float B = 2.0f * FVector::DotProduct(RelativePos, TargetVel2D);
    const float C = FVector::DotProduct(RelativePos, RelativePos);

    float InterceptTime = -1.0f;

    // Aがほぼ0なら一次方程式として処理
    if (FMath::Abs(A) < KINDA_SMALL_NUMBER)
    {
        if (FMath::Abs(B) < KINDA_SMALL_NUMBER)
        {
            // 解けないので現在位置方向へフォールバック
            FVector FallbackDirection = TargetLocation - PlayerLocation;
            FallbackDirection.Z = 0.0f;

            OutDirection = FallbackDirection.GetSafeNormal();
            OutPredictedLocation = TargetLocation;
            OutTime = 0.0f;

            return false;
        }

        InterceptTime = -C / B;
    }
    else
    {
        const float Discriminant = B * B - 4.0f * A * C;

        if (Discriminant < 0.0f)
        {
            FVector FallbackDirection = TargetLocation - PlayerLocation;
            FallbackDirection.Z = 0.0f;

            OutDirection = FallbackDirection.GetSafeNormal();
            OutPredictedLocation = TargetLocation;
            OutTime = 0.0f;

            return false;
        }

        const float SqrtDiscriminant = FMath::Sqrt(Discriminant);

        const float T1 = (-B - SqrtDiscriminant) / (2.0f * A);
        const float T2 = (-B + SqrtDiscriminant) / (2.0f * A);

        if (T1 > KINDA_SMALL_NUMBER && T2 > KINDA_SMALL_NUMBER)
        {
            InterceptTime = FMath::Min(T1, T2);
        }
        else if (T1 > KINDA_SMALL_NUMBER)
        {
            InterceptTime = T1;
        }
        else if (T2 > KINDA_SMALL_NUMBER)
        {
            InterceptTime = T2;
        }
        else
        {
            FVector FallbackDirection = TargetLocation - PlayerLocation;
            FallbackDirection.Z = 0.0f;

            OutDirection = FallbackDirection.GetSafeNormal();
            OutPredictedLocation = TargetLocation;
            OutTime = 0.0f;

            return false;
        }
    }

    if (InterceptTime <= KINDA_SMALL_NUMBER)
    {
        FVector FallbackDirection = TargetLocation - PlayerLocation;
        FallbackDirection.Z = 0.0f;

        OutDirection = FallbackDirection.GetSafeNormal();
        OutPredictedLocation = TargetLocation;
        OutTime = 0.0f;

        return false;
    }

    // 予測位置
    FVector PredictedLocation = TargetLocation + TargetVel2D * InterceptTime;

    // 地上用なので高さはターゲット現在位置に合わせる
    PredictedLocation.Z = TargetLocation.Z;

    FVector Direction = PredictedLocation - PlayerLocation;
    Direction.Z = 0.0f;

    OutDirection = Direction.GetSafeNormal();
    OutPredictedLocation = PredictedLocation;
    OutTime = InterceptTime;

    return !OutDirection.IsNearlyZero();
}