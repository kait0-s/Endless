#pragma once
#include "CoreMinimal.h"
#include "Navigation/PathFollowingComponent.h"
#include "EndlessRoomPathFollowing.generated.h"

/** 再経路探索とは別に、移動直前にも部屋境界を守る。 */
UCLASS()
class ENDLESS_API UEndlessRoomPathFollowing : public UPathFollowingComponent
{
    GENERATED_BODY()
protected:
    virtual void FollowPathSegment(float DeltaTime) override;
};
