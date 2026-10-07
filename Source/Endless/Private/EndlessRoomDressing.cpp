#include "EndlessRoomDressing.h"
#include "Components/InstancedStaticMeshComponent.h"
AEndlessRoomDressing::AEndlessRoomDressing()
{
    PrimaryActorTick.bCanEverTick=false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    RootComponent->SetMobility(EComponentMobility::Static);
    SmallProps=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("SmallProps"));
    MediumProps=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("MediumProps"));
    Details=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Details"));
    for (UInstancedStaticMeshComponent* Component:{SmallProps.Get(),MediumProps.Get(),Details.Get()})
    {
        Component->SetupAttachment(RootComponent);
        Component->SetMobility(EComponentMobility::Static);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetCanEverAffectNavigation(false);
        Component->SetCastShadow(false);
    }
}
