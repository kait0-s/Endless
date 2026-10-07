#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EndlessPlayerProjectile.generated.h"
class USphereComponent;
class UProjectileMovementComponent;
UCLASS()
class ENDLESS_API AEndlessPlayerProjectile : public AActor
{
    GENERATED_BODY()
public:
    AEndlessPlayerProjectile();
    UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> Collision;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProjectileMovementComponent> Movement;
    UPROPERTY(BlueprintReadOnly) float Damage = 10.f;
    UPROPERTY(BlueprintReadOnly) float ExplosionRadius = 0.f;
    void Launch(float InDamage, float Speed, float Radius);
protected:
    virtual void BeginPlay() override;
    UFUNCTION() void OnImpact(UPrimitiveComponent* HitComponent, AActor* Other, UPrimitiveComponent* OtherComponent, FVector Impulse, const FHitResult& Hit);
};
