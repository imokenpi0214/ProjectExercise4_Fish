
#include "DashWallBounceComponent.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "CollisionShape.h"
#include "Net/UnrealNetwork.h"

UDashWallBounceComponent::UDashWallBounceComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    SetIsReplicatedByDefault(true);
}

void UDashWallBounceComponent::BeginPlay()
{
    Super::BeginPlay();

    ACharacter* Character = Cast<ACharacter>(GetOwner());

    if (!Character)
    {
        return;
    }

    Character->GetCapsuleComponent()->OnComponentHit.AddDynamic(
        this,
        &UDashWallBounceComponent::OnCapsuleHit
    );
}

void UDashWallBounceComponent::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps
) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(
        UDashWallBounceComponent,
        bIsStunned
    );
}

bool UDashWallBounceComponent::IsLocallyControlledCharacter() const
{
    const ACharacter* Character = Cast<ACharacter>(GetOwner());

    return Character && Character->IsLocallyControlled();
}

void UDashWallBounceComponent::SetDashActive(bool bActive)
{
    ACharacter* Character = Cast<ACharacter>(GetOwner());

    if (!Character)
    {
        return;
    }

    // サーバーまたは所有クライアントのみ変更
    if (!Character->HasAuthority() &&
        !Character->IsLocallyControlled())
    {
        return;
    }

    // スタン中はダッシュ開始不可
    if (IsWallStunned() && bActive)
    {
        return;
    }

    bDashActive = bActive;
}

bool UDashWallBounceComponent::IsWallStunned() const
{
    return bIsStunned || bLocalBounceActive;
}

bool UDashWallBounceComponent::CanBounce(
    const FVector& Velocity,
    const FVector& WallNormal
) const
{
    FVector HorizontalVelocity = Velocity;
    HorizontalVelocity.Z = 0.0f;

    FVector HorizontalNormal = WallNormal;

    // 床や天井への衝突は対象外
    if (FMath::Abs(HorizontalNormal.Z) > 0.5f)
    {
        return false;
    }

    HorizontalNormal.Z = 0.0f;

    if (HorizontalVelocity.IsNearlyZero() ||
        HorizontalNormal.IsNearlyZero())
    {
        return false;
    }

    HorizontalVelocity.Normalize();
    HorizontalNormal.Normalize();

    // 壁に対する衝突角度
    const float Dot = FVector::DotProduct(
        HorizontalVelocity,
        -HorizontalNormal
    );

    const float Limit = FMath::Cos(
        FMath::DegreesToRadians(
            FMath::Clamp(BounceAngle, 0.0f, 90.0f)
        )
    );

    return Dot >= Limit;
}

void UDashWallBounceComponent::OnCapsuleHit(
    UPrimitiveComponent* HitComponent,
    AActor* OtherActor,
    UPrimitiveComponent* OtherComp,
    FVector NormalImpulse,
    const FHitResult& Hit
)
{
    ACharacter* Character = Cast<ACharacter>(GetOwner());

    if (!Character)
    {
        return;
    }

    // 操作している本人だけローカル判定
    if (!Character->IsLocallyControlled())
    {
        return;
    }

    if (!bDashActive ||
        IsWallStunned() ||
        !Hit.bBlockingHit)
    {
        return;
    }

    // 魚同士の衝突を除外
    if (OtherActor &&
        OtherActor->IsA(APawn::StaticClass()))
    {
        return;
    }

    UCharacterMovementComponent* Movement =
        Character->GetCharacterMovement();

    if (!Movement)
    {
        return;
    }

    if (!CanBounce(
        Movement->Velocity,
        Hit.ImpactNormal))
    {
        return;
    }

    // ★追加：ノックバック前の水平速度を記録
    const float ImpactSpeed =
        Movement->Velocity.Size2D();

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[WallBounce] LOCAL HIT | Actor=%s | Speed=%.1f | Authority=%d"),
        *GetNameSafe(Character),
        ImpactSpeed,
        Character->HasAuthority() ? 1 : 0
    );

    if (!Character->HasAuthority())
    {
        // クライアントからサーバーへ衝突報告
        ServerRequestWallBounce(
            Hit.ImpactPoint,
            Hit.ImpactNormal
        );
    }

    // ローカルで即座にノックバック
    StartWallBounce(
        Hit.ImpactNormal,
        Character->HasAuthority()
    );

    // ★追加：ホスト自身の衝突ダメージ通知
    if (Character->HasAuthority() && IsValid(this))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[WallBounce] DAMAGE EVENT | Host | Speed=%.1f"),
            ImpactSpeed
        );

        OnWallBounceDamage.Broadcast(ImpactSpeed);
    }
}

void UDashWallBounceComponent::ServerRequestWallBounce_Implementation(
    FVector HitLocation,
    FVector WallNormal
)
{
    ACharacter* Character = Cast<ACharacter>(GetOwner());

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[WallBounce] RPC RECEIVED | Actor=%s"),
        *GetNameSafe(Character)
    );

    if (!Character)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[WallBounce] REJECT: Character null")
        );
        return;
    }

    if (!Character->HasAuthority())
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[WallBounce] REJECT: No authority")
        );
        return;
    }

    if (!bDashActive)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[WallBounce] REJECT: DashActive FALSE")
        );
        return;
    }

    if (bIsStunned)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[WallBounce] REJECT: Already stunned")
        );
        return;
    }

    UCharacterMovementComponent* Movement =
        Character->GetCharacterMovement();

    UCapsuleComponent* Capsule =
        Character->GetCapsuleComponent();

    if (!Movement || !Capsule || !GetWorld())
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[WallBounce] REJECT: Missing component")
        );
        return;
    }

    // 不正な入力値を確認
    if (HitLocation.ContainsNaN() ||
        WallNormal.ContainsNaN())
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[WallBounce] REJECT: Invalid hit data")
        );
        return;
    }

    if (WallNormal.IsNearlyZero())
    {
        return;
    }

    WallNormal.Normalize();

    // サーバーと衝突地点の距離を確認
    const float MaxHitDistance = 250.0f;

    const float Distance = FVector::Dist(
        Character->GetActorLocation(),
        HitLocation
    );

    if (Distance > MaxHitDistance)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[WallBounce] REJECT: DISTANCE | Actual=%.1f"),
            Distance
        );
        return;
    }

    // 壁の存在をサーバーで検証
    FVector TraceDirection = -WallNormal;
    TraceDirection.Z = 0.0f;
    TraceDirection.Normalize();

    if (TraceDirection.IsNearlyZero())
    {
        return;
    }

    const FVector TraceStart =
        Character->GetActorLocation();

    const FVector TraceEnd =
        TraceStart +
        TraceDirection *
        (Capsule->GetScaledCapsuleRadius() + 150.0f);

    FCollisionQueryParams QueryParams;
    QueryParams.AddIgnoredActor(Character);

    FHitResult ServerHit;

    const bool bFoundWall =
        GetWorld()->SweepSingleByChannel(
            ServerHit,
            TraceStart,
            TraceEnd,
            FQuat::Identity,
            ECC_Pawn,
            FCollisionShape::MakeSphere(8.0f),
            QueryParams
        );

    if (!bFoundWall || !ServerHit.bBlockingHit)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[WallBounce] REJECT: SWEEP FAILED")
        );
        return;
    }

    // 他の魚は壁扱いしない
    if (ServerHit.GetActor() &&
        ServerHit.GetActor()->IsA(APawn::StaticClass()))
    {
        return;
    }

    FVector VerifiedNormal =
        ServerHit.ImpactNormal;

    VerifiedNormal.Z = 0.0f;
    VerifiedNormal.Normalize();

    FVector ReportedNormal = WallNormal;
    ReportedNormal.Z = 0.0f;
    ReportedNormal.Normalize();

    const float NormalDot =
        FVector::DotProduct(
            VerifiedNormal,
            ReportedNormal
        );

    if (NormalDot < 0.8f)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[WallBounce] REJECT: NORMAL MISMATCH | Dot=%.3f"),
            NormalDot
        );
        return;
    }

    // ★追加：サーバー側の速度を保存
    // クライアントが送った速度は使用しない
    const float ImpactSpeed =
        Movement->Velocity.Size2D();

    FVector ServerVelocity = Movement->Velocity;
    ServerVelocity.Z = 0.0f;

    // 衝突処理で速度が消えていた場合の角度判定用
    if (ServerVelocity.IsNearlyZero())
    {
        ServerVelocity =
            Character->GetActorForwardVector();
    }

    if (!CanBounce(ServerVelocity, VerifiedNormal))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[WallBounce] REJECT: ANGLE | Speed=%.1f"),
            ImpactSpeed
        );
        return;
    }

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[WallBounce] SERVER ACCEPTED | Actor=%s | Speed=%.1f"),
        *GetNameSafe(Character),
        ImpactSpeed
    );

    // サーバー側でもノックバック開始
    StartWallBounce(VerifiedNormal, true);

    // ★追加：サーバー承認後にダメージイベント通知
    if (IsValid(this))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[WallBounce] DAMAGE EVENT | Server | Speed=%.1f"),
            ImpactSpeed
        );

        OnWallBounceDamage.Broadcast(ImpactSpeed);
    }
}

void UDashWallBounceComponent::StartWallBounce(
    const FVector& WallNormal,
    bool bAuthoritative
)
{
    ACharacter* Character = Cast<ACharacter>(GetOwner());

    if (!Character || IsWallStunned())
    {
        return;
    }

    UCharacterMovementComponent* Movement =
        Character->GetCharacterMovement();

    if (!Movement)
    {
        return;
    }

    // ダッシュ解除とスタン開始
    bDashActive = false;
    bLocalBounceActive = true;

    if (bAuthoritative && Character->HasAuthority())
    {
        bIsStunned = true;
    }

    StunElapsedTime = 0.0f;

    // 壁の法線方向へ跳ね返る
    BounceDirection = WallNormal;
    BounceDirection.Z = 0.0f;
    BounceDirection.Normalize();

    CurrentBounceSpeed =
        FMath::Max(0.0f, BounceSpeed);

    // BlueprintのSlow Downを呼び出す
    OnWallBounceStarted.Broadcast();

    // ノックバック初速を設定
    Movement->StopMovementImmediately();

    Movement->Velocity =
        BounceDirection * CurrentBounceSpeed;

    if (Character->HasAuthority())
    {
        Character->ForceNetUpdate();
    }

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[WallBounce] BOUNCE STARTED | Actor=%s | Authority=%d"),
        *GetNameSafe(Character),
        Character->HasAuthority() ? 1 : 0
    );
}

void UDashWallBounceComponent::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction
)
{
    Super::TickComponent(
        DeltaTime,
        TickType,
        ThisTickFunction
    );

    ACharacter* Character = Cast<ACharacter>(GetOwner());

    if (!Character)
    {
        return;
    }

    // サーバーと所有クライアントだけ更新
    if (!Character->HasAuthority() &&
        !Character->IsLocallyControlled())
    {
        return;
    }

    if (!bLocalBounceActive)
    {
        return;
    }

    UCharacterMovementComponent* Movement =
        Character->GetCharacterMovement();

    if (!Movement)
    {
        return;
    }

    StunElapsedTime += DeltaTime;

    // ノックバック速度を徐々に減速
    CurrentBounceSpeed = FMath::FInterpConstantTo(
        CurrentBounceSpeed,
        0.0f,
        DeltaTime,
        FMath::Max(0.0f, BounceDeceleration)
    );

    const FVector HorizontalVelocity =
        BounceDirection * CurrentBounceSpeed;

    Movement->Velocity = FVector(
        HorizontalVelocity.X,
        HorizontalVelocity.Y,
        Movement->Velocity.Z
    );

    // スタン時間終了
    if (StunElapsedTime >= StunDuration)
    {
        EndStun();
    }
}

void UDashWallBounceComponent::EndStun()
{
    if (!bLocalBounceActive)
    {
        return;
    }

    ACharacter* Character = Cast<ACharacter>(GetOwner());

    if (Character)
    {
        UCharacterMovementComponent* Movement =
            Character->GetCharacterMovement();

        if (Movement)
        {
            Movement->StopMovementImmediately();
        }

        if (Character->HasAuthority())
        {
            bIsStunned = false;
            Character->ForceNetUpdate();
        }
    }

    bLocalBounceActive = false;
    CurrentBounceSpeed = 0.0f;
    StunElapsedTime = 0.0f;
    BounceDirection = FVector::ZeroVector;

    OnWallBounceEnded.Broadcast();
}

void UDashWallBounceComponent::OnRep_IsStunned()
{
    ACharacter* Character = Cast<ACharacter>(GetOwner());

    if (!Character || !Character->IsLocallyControlled())
    {
        return;
    }

    // サーバーのスタン状態を受信
    if (bIsStunned && !bLocalBounceActive)
    {
        bLocalBounceActive = true;

        BounceDirection = FVector::ZeroVector;
        CurrentBounceSpeed = 0.0f;
        StunElapsedTime = 0.0f;

        bDashActive = false;

        OnWallBounceStarted.Broadcast();
    }

    // スタン解除通知
    if (!bIsStunned && bLocalBounceActive)
    {
        EndStun();
    }
}
