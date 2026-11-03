#include "EnemyAIAttackComponent.h"

#include "AIController.h"
#include "EnemyAIBrainComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"


UEnemyAIAttackComponent::UEnemyAIAttackComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}


void UEnemyAIAttackComponent::BeginPlay()
{
    Super::BeginPlay();

    OwnerCharacter = Cast<ACharacter>(GetOwner());

    FindAttackCollisionComponent();

    if (bDisableAttackCollisionOnBeginPlay)
    {
        SetAttackCollisionEnabled(false);
    }

    CurrentDashSpeed = 0.0f;
    CurrentDamageAmount = 0.0f;
}


// =========================================================
// 公開関数
// =========================================================

bool UEnemyAIAttackComponent::StartDashAttack(
    AActor* TargetActor
)
{
    AActor* OwnerActor = GetOwner();

    if (!OwnerActor || !OwnerActor->HasAuthority())
    {
        return false;
    }


    if (!OwnerCharacter)
    {
        OwnerCharacter =
            Cast<ACharacter>(OwnerActor);
    }


    if (!OwnerCharacter)
    {
        return false;
    }

    if (
        !bUseCurrentForwardForDash
        && !IsValid(TargetActor)
    )
    {
        return false;
    }

    /*
     * =========================================================
     * ここが今回の変更点
     * =========================================================
     *
     * TargetActor == nullptr でも許可する。
     *
     * bUseCurrentForwardForDash == true なら、
     * ターゲットがなくても現在のForward方向へダッシュできる。
     *
     * 通常攻撃：
     * StartDashAttack(TargetFishActor)
     *
     * 移動・逃走など：
     * StartDashAttack(nullptr)
     */


    if (bIsAttacking)
    {
        return false;
    }


    CurrentTargetActor = TargetActor;


    bIsAttacking = true;
    bIsCharging = true;
    bIsDashing = false;


    CurrentDashSpeed = 0.0f;
    CurrentDamageAmount = 0.0f;


    // チャージ中は攻撃判定OFF
    SetAttackCollisionEnabled(false);


    if (GetWorld())
    {
        GetWorld()
            ->GetTimerManager()
            .ClearTimer(
                ChargeTimerHandle
            );


        GetWorld()
            ->GetTimerManager()
            .ClearTimer(
                DashTimerHandle
            );


        GetWorld()
            ->GetTimerManager()
            .SetTimer(
                ChargeTimerHandle,
                this,
                &UEnemyAIAttackComponent::ReleaseDashAttack,
                ChargeTime,
                false
            );
    }


    return true;
}


void UEnemyAIAttackComponent::CancelDashAttack()
{
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(ChargeTimerHandle);
        GetWorld()->GetTimerManager().ClearTimer(DashTimerHandle);
    }

    SetAttackCollisionEnabled(false);

    bIsAttacking = false;
    bIsCharging = false;
    bIsDashing = false;

    CurrentTargetActor = nullptr;

    CurrentDashSpeed = 0.0f;
    CurrentDamageAmount = 0.0f;

    SetOwnerMaxWalkSpeed(NormalMoveSpeed);

    if (OwnerCharacter)
    {
        UCharacterMovementComponent* MoveComp =
            OwnerCharacter->GetCharacterMovement();

        if (MoveComp && bStopVelocityOnFinish)
        {
            MoveComp->Velocity = FVector::ZeroVector;
        }

        if (bStopAIMovementOnFinish)
        {
            AAIController* AIController =
                Cast<AAIController>(OwnerCharacter->GetController());

            if (AIController)
            {
                AIController->StopMovement();
            }
        }
    }
}


bool UEnemyAIAttackComponent::IsAttacking() const
{
    return bIsAttacking;
}


bool UEnemyAIAttackComponent::IsCharging() const
{
    return bIsCharging;
}


bool UEnemyAIAttackComponent::IsDashing() const
{
    return bIsDashing;
}


float UEnemyAIAttackComponent::GetCurrentDamageAmount() const
{
    return CurrentDamageAmount;
}


// =========================================================
// 内部処理
// =========================================================

void UEnemyAIAttackComponent::ReleaseDashAttack()
{
    AActor* OwnerActor = GetOwner();

    if (!OwnerActor || !OwnerActor->HasAuthority())
    {
        CancelDashAttack();
        return;
    }


    if (!OwnerCharacter)
    {
        OwnerCharacter =
            Cast<ACharacter>(OwnerActor);
    }


    if (!OwnerCharacter)
    {
        CancelDashAttack();
        return;
    }


    /*
     * =========================================================
     * 以前はここに
     *
     * if (!CurrentTargetActor)
     * {
     *     CancelDashAttack();
     *     return;
     * }
     *
     * があった。
     *
     * 今回は削除する。
     *
     * bUseCurrentForwardForDash == true なら、
     * CurrentTargetActorがnullptrでも
     * 現在のForward方向へダッシュできるため。
     * =========================================================
     */


    bIsCharging = false;
    bIsDashing = true;


    // =========================================================
    // AIの通常移動を止める
    // =========================================================

    if (bStopAIMovementBeforeDash)
    {
        AAIController* AIController =
            Cast<AAIController>(
                OwnerCharacter->GetController()
            );


        if (AIController)
        {
            AIController->StopMovement();
        }
    }


    // =========================================================
    // ダッシュ方向取得
    // =========================================================

    const FVector DashDirection =
        GetDashDirection();


    if (DashDirection.IsNearlyZero())
    {
        CancelDashAttack();
        return;
    }


    // =========================================================
    // 必要ならターゲット方向へ向く
    //
    // 現在の設定：
    // bFaceTargetBeforeDash = false
    //
    // なので通常はここでは向きを変更しない。
    // =========================================================

    if (bFaceTargetBeforeDash)
    {
        FRotator NewRotation =
            DashDirection.Rotation();


        NewRotation.Pitch = 0.0f;
        NewRotation.Roll = 0.0f;


        OwnerCharacter->SetActorRotation(
            NewRotation
        );


        if (bSetControllerRotationBeforeDash)
        {
            AController* Controller =
                OwnerCharacter->GetController();


            if (Controller)
            {
                Controller->SetControlRotation(
                    NewRotation
                );
            }
        }
    }


    // =========================================================
    // ダッシュ速度設定
    // =========================================================

    SetOwnerMaxWalkSpeed(
        DashMoveSpeed
    );


    CurrentDashSpeed =
        MaxDashSpeed;


    CalculateCurrentDamageAmount();


    // =========================================================
    // ダッシュ中は必ずAttackSphere ON
    // =========================================================

    SetAttackCollisionEnabled(
        true
    );


    // =========================================================
    // 既存ダッシュ
    // =========================================================

    OwnerCharacter->LaunchCharacter(
        DashDirection * DashPower,
        true,
        false
    );


    // =========================================================
    // ダッシュ終了タイマー
    // =========================================================

    if (GetWorld())
    {
        GetWorld()
            ->GetTimerManager()
            .ClearTimer(
                DashTimerHandle
            );


        GetWorld()
            ->GetTimerManager()
            .SetTimer(
                DashTimerHandle,
                this,
                &UEnemyAIAttackComponent::FinishDashAttack,
                DashDuration,
                false
            );
    }
}


void UEnemyAIAttackComponent::FinishDashAttack()
{
    SetAttackCollisionEnabled(false);

    bIsAttacking = false;
    bIsCharging = false;
    bIsDashing = false;

    CurrentTargetActor = nullptr;

    CurrentDashSpeed = 0.0f;
    CurrentDamageAmount = 0.0f;

    SetOwnerMaxWalkSpeed(NormalMoveSpeed);

    if (OwnerCharacter)
    {
        UCharacterMovementComponent* MoveComp =
            OwnerCharacter->GetCharacterMovement();

        if (MoveComp && bStopVelocityOnFinish)
        {
            MoveComp->Velocity = FVector::ZeroVector;
        }

        if (bStopAIMovementOnFinish)
        {
            AAIController* AIController =
                Cast<AAIController>(OwnerCharacter->GetController());

            if (AIController)
            {
                AIController->StopMovement();
            }
        }
    }

    if (bReturnToWanderOnFinish)
    {
        AActor* OwnerActor = GetOwner();

        if (OwnerActor && OwnerActor->HasAuthority())
        {
            UEnemyAIBrainComponent* BrainComponent =
                OwnerActor->FindComponentByClass<UEnemyAIBrainComponent>();

            if (BrainComponent)
            {
                BrainComponent->SetAIMode(EEnemyAIMode::Wander);
            }
        }
    }
}


void UEnemyAIAttackComponent::FindAttackCollisionComponent()
{
    AttackCollisionComponent = nullptr;

    AActor* OwnerActor = GetOwner();

    if (!OwnerActor)
    {
        return;
    }

    TArray<UPrimitiveComponent*> PrimitiveComponents;

    OwnerActor->GetComponents<UPrimitiveComponent>(
        PrimitiveComponents
    );

    for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
    {
        if (!PrimitiveComponent)
        {
            continue;
        }

        if (PrimitiveComponent->GetFName() == AttackCollisionComponentName)
        {
            AttackCollisionComponent = PrimitiveComponent;
            return;
        }
    }
}


void UEnemyAIAttackComponent::SetAttackCollisionEnabled(
    bool bEnabled
)
{
    if (!AttackCollisionComponent)
    {
        FindAttackCollisionComponent();
    }

    if (!AttackCollisionComponent)
    {
        return;
    }

    if (bEnabled)
    {
        AttackCollisionComponent->SetCollisionEnabled(
            ECollisionEnabled::QueryOnly
        );

        AttackCollisionComponent->SetGenerateOverlapEvents(true);
    }
    else
    {
        AttackCollisionComponent->SetGenerateOverlapEvents(false);

        AttackCollisionComponent->SetCollisionEnabled(
            ECollisionEnabled::NoCollision
        );
    }
}


void UEnemyAIAttackComponent::SetOwnerMaxWalkSpeed(
    float NewSpeed
)
{
    if (!OwnerCharacter)
    {
        return;
    }

    UCharacterMovementComponent* MoveComp =
        OwnerCharacter->GetCharacterMovement();

    if (!MoveComp)
    {
        return;
    }

    MoveComp->MaxWalkSpeed = NewSpeed;
}


FVector UEnemyAIAttackComponent::GetDashDirection() const
{
    if (!OwnerCharacter)
    {
        return FVector::ZeroVector;
    }

    /*
     * 今回の推奨設定。
     *
     * true：
     * 現在向いているForward方向へ突進する。
     *
     * false：
     * ターゲット方向へ突進する。
     */
    if (bUseCurrentForwardForDash)
    {
        FVector ForwardDirection =
            OwnerCharacter->GetActorForwardVector();

        ForwardDirection.Z = 0.0f;

        return ForwardDirection.GetSafeNormal();
    }

    if (!CurrentTargetActor)
    {
        return FVector::ZeroVector;
    }

    FVector Direction =
        CurrentTargetActor->GetActorLocation()
        - OwnerCharacter->GetActorLocation();

    Direction.Z = 0.0f;

    return Direction.GetSafeNormal();
}


void UEnemyAIAttackComponent::CalculateCurrentDamageAmount()
{
    if (MaxDashSpeed <= KINDA_SMALL_NUMBER)
    {
        CurrentDamageAmount = 0.0f;
        return;
    }

    const float AttackRate =
        FMath::Clamp(
            CurrentDashSpeed / MaxDashSpeed,
            0.0f,
            1.0f
        );

    CurrentDamageAmount =
        MaximumAttackPower * AttackRate;
}