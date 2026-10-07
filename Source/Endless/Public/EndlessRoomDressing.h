#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EndlessRoomDressing.generated.h"
class UInstancedStaticMeshComponent;

/** 同一部屋の装飾をメッシュ別にまとめる。小物は移動・NavMesh・影に影響しない。 */
UCLASS()
class ENDLESS_API AEndlessRoomDressing : public AActor
{
    GENERATED_BODY()
public:
    AEndlessRoomDressing();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UInstancedStaticMeshComponent> SmallProps;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UInstancedStaticMeshComponent> MediumProps;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UInstancedStaticMeshComponent> Details;
};
