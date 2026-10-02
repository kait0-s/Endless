#include "RoomRuntimeContent.h"

#include "Components/SceneComponent.h"

AEndlessRoomSpawnPoint::AEndlessRoomSpawnPoint()
{
    PrimaryActorTick.bCanEverTick = false;
    SetActorEnableCollision(false);

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);
}

AEndlessRoomContentConfig::AEndlessRoomContentConfig()
{
    PrimaryActorTick.bCanEverTick = false;
    SetActorEnableCollision(false);

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);
}
