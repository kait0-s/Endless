#include "EndlessPlayerProjectile.h"
#include "EndlessSensory.h"
#include "EndlessWeaponLoot.h"
#include "GameFramework/DamageType.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"
#include "EngineUtils.h"

AEndlessPlayerProjectile::AEndlessPlayerProjectile()
{
    Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
    SetRootComponent(Collision);
    Collision->InitSphereRadius(1.5f);
    Collision->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    Collision->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    Collision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
    Collision->SetNotifyRigidBodyCollision(true);
    Collision->OnComponentHit.AddDynamic(this, &AEndlessPlayerProjectile::OnImpact);
    Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
    Movement->UpdatedComponent = Collision;
    Movement->InitialSpeed = Movement->MaxSpeed = 12000.f;
    Movement->ProjectileGravityScale = 0.f;
    Movement->bRotationFollowsVelocity = true;
    Movement->bForceSubStepping = true;
    auto* Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Tracer"));
    Visual->SetupAttachment(Collision);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    Visual->SetStaticMesh(Sphere.Object);
    Visual->SetRelativeScale3D(FVector(.08f, .018f, .018f));
    Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    InitialLifeSpan = 3.f;
}
void AEndlessPlayerProjectile::BeginPlay()
{
    Super::BeginPlay();
    Collision->IgnoreActorWhenMoving(GetOwner(), true);
    Collision->IgnoreActorWhenMoving(GetInstigator(), true);
    // 同じ銃口から発生する散弾同士で衝突・消滅しないよう相互に除外する。
    for (TActorIterator<AEndlessPlayerProjectile> It(GetWorld()); It; ++It)
    {
        if (*It == this) continue;
        Collision->IgnoreActorWhenMoving(*It, true);
        It->Collision->IgnoreActorWhenMoving(this, true);
    }
}
void AEndlessPlayerProjectile::Launch(float InDamage, float Speed, float Radius)
{
    Damage = InDamage;
    ExplosionRadius = Radius;
    Movement->InitialSpeed = Movement->MaxSpeed = Speed;
    Movement->Velocity = GetActorForwardVector() * Speed;
}
void AEndlessPlayerProjectile::OnImpact(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*, FVector, const FHitResult& Hit)
{
    if (!Other || Other == GetOwner() || Other == GetInstigator()) return;
    if (Cast<AEndlessLootEnemy>(Other) && Cast<AEndlessLootEnemy>(GetInstigator())) { Destroy();return; }
    if (!Cast<APawn>(Other))
        if (auto* Feedback=GetInstigator() ? GetInstigator()->FindComponentByClass<UEndlessSensoryComponent>() : nullptr)
            Feedback->PlayWallImpact(Hit.ImpactPoint);
    if (ExplosionRadius > 0)
    {
        TArray<AActor*> Ignore{this, GetOwner()};
        UGameplayStatics::ApplyRadialDamage(this, Damage, Hit.ImpactPoint, ExplosionRadius,
            UDamageType::StaticClass(), Ignore, this, GetInstigatorController(), false);
    }
    else
    {
        UGameplayStatics::ApplyPointDamage(Other, Damage, GetActorForwardVector(), Hit,
            GetInstigatorController(), this, UDamageType::StaticClass());
    }
    Destroy();
}
