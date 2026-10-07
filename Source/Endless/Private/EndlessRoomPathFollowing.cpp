#include "EndlessRoomPathFollowing.h"
#include "EndlessEnemyController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"

void UEndlessRoomPathFollowing::FollowPathSegment(float DeltaTime)
{
    auto* Brain = Cast<AEndlessEnemyController>(GetOwner());
    auto* Character = Brain ? Cast<ACharacter>(Brain->GetPawn()) : nullptr;
    if (Character)
    {
        const FVector Feet = Character->GetNavAgentLocation();
        const float Margin = Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
        const FVector Ahead = Feet + Character->GetVelocity() * FMath::Max(DeltaTime, .05f);
        if (!Brain->IsInsideHomeRoom(Ahead, Margin))
        {
            Brain->StopMovement();
            Character->GetCharacterMovement()->StopMovementImmediately();
            return;
        }
    }
    Super::FollowPathSegment(DeltaTime);
}
