
#include "DashWallBounceComponent.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Pawn.h"
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

void UDashWallBounceComponent::SetDashActive(bool bActive)
{
    // ダッシュ判定はサーバーで管理
    if (!GetOwner() || !GetOwner()->HasAuthority())
    {
        return;
    }

    // スタン中はダッシュ開始不可
    if (bIsStunned && bActive)
    {
        return;
    }

    bDashActive = bActive;
}

bool UDashWallBounceComponent::IsWallStunned() const
{
    return bIsStunned;
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

    if (!Character || !Character->HasAuthority())
    {
        return;
    }

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("WallHit | Actor=%s | Dash=%d | Stun=%d | Speed=%.1f"),
        *GetNameSafe(Character),
        bDashActive ? 1 : 0,
        bIsStunned ? 1 : 0,
        Character->GetVelocity().Size2D()
    );

    if (!bDashActive || bIsStunned || !Hit.bBlockingHit)
    {
        return;
    }

    // 他の魚ではなく、壁などの障害物を対象にする
    if (OtherActor && OtherActor->IsA(APawn::StaticClass()))
    {
        return;
    }

    UCharacterMovementComponent* Movement =
        Character->GetCharacterMovement();

    if (!Movement)
    {
        return;
    }

    // 衝突直前の水平移動方向
    FVector Velocity = Movement->Velocity;
    Velocity.Z = 0.0f;

    // 壁の法線方向
    FVector WallNormal = Hit.ImpactNormal;

    // 床や天井への衝突は除外
    if (FMath::Abs(WallNormal.Z) > 0.5f)
    {
        return;
    }

    WallNormal.Z = 0.0f;

    if (Velocity.IsNearlyZero() || WallNormal.IsNearlyZero())
    {
        return;
    }

    Velocity.Normalize();
    WallNormal.Normalize();

    // 正面衝突なら1、斜めになるほど小さくなる
    const float Dot = FVector::DotProduct(
        Velocity,
        -WallNormal
    );

    const float Limit = FMath::Cos(
        FMath::DegreesToRadians(
            FMath::Clamp(BounceAngle, 0.0f, 90.0f)
        )
    );

    if (Dot < Limit)
    {
        return;
    }

    // 二重発動を防止
    bDashActive = false;

    // スタン開始
    bIsStunned = true;
    StunElapsedTime = 0.0f;

    // 壁の反対方向へ跳ね返る
    BounceDirection = WallNormal;
    CurrentBounceSpeed = FMath::Max(0.0f, BounceSpeed);

    // BPに通知して既存のSlow Downなどを呼ぶ
    OnWallBounceStarted.Broadcast();

    // Slow Down後にノックバック初速を設定
    Movement->StopMovementImmediately();

    FVector NewVelocity =
        BounceDirection * CurrentBounceSpeed;

    Movement->Velocity = FVector(
        NewVelocity.X,
        NewVelocity.Y,
        0.0f
    );

    // スタン状態を早めにクライアントへ送信
    Character->ForceNetUpdate();

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("Wall Bounce Started")
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

    // サーバーだけがスタンと速度を管理
    if (!GetOwner() || !GetOwner()->HasAuthority())
    {
        return;
    }

    if (!bIsStunned)
    {
        return;
    }

    ACharacter* Character = Cast<ACharacter>(GetOwner());

    if (!Character)
    {
        return;
    }

    UCharacterMovementComponent* Movement =
        Character->GetCharacterMovement();

    if (!Movement)
    {
        return;
    }

    // スタン経過時間
    StunElapsedTime += DeltaTime;

    // ノックバック速度を徐々に小さくする
    CurrentBounceSpeed = FMath::FInterpConstantTo(
        CurrentBounceSpeed,
        0.0f,
        DeltaTime,
        FMath::Max(0.0f, BounceDeceleration)
    );

    // 水平方向のノックバック速度を更新
    const FVector HorizontalVelocity =
        BounceDirection * CurrentBounceSpeed;

    Movement->Velocity = FVector(
        HorizontalVelocity.X,
        HorizontalVelocity.Y,
        Movement->Velocity.Z
    );

    // スタン時間が終わったら解除
    if (StunElapsedTime >= StunDuration)
    {
        EndStun();
    }
}

void UDashWallBounceComponent::EndStun()
{
    if (!bIsStunned)
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

        Character->ForceNetUpdate();
    }

    bIsStunned = false;

    CurrentBounceSpeed = 0.0f;
    StunElapsedTime = 0.0f;
    BounceDirection = FVector::ZeroVector;

    OnWallBounceEnded.Broadcast();

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("Wall Bounce Ended")
    );
}
