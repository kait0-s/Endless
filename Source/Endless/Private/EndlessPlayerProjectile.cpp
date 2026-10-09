#include "EndlessPlayerProjectile.h"
#include "EndlessCombatEffects.h"
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
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickGroup=TG_PostPhysics;
    Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
    SetRootComponent(Collision);
    Collision->InitSphereRadius(1.5f);
    Collision->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    Collision->SetCollisionObjectType(ECC_GameTraceChannel2);
    Collision->SetCollisionResponseToChannel(ECC_GameTraceChannel2,ECR_Ignore);
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
    LastTrailEnd=GetActorLocation();
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
    if (bImpacted || !Other || Other == GetOwner() || Other == GetInstigator()) return;
    bImpacted=true;
    EndlessCombatEffects::Trail(GetWorld(),LastTrailEnd,Hit.ImpactPoint,TrailColor,TrailWidth,TrailSeconds);
    LastTrailEnd=Hit.ImpactPoint;
    if (Cast<AEndlessLootEnemy>(Other) && Cast<AEndlessLootEnemy>(GetInstigator())) { Destroy();return; }
    if (!Cast<APawn>(Other))
        if (auto* Feedback=GetInstigator() ? GetInstigator()->FindComponentByClass<UEndlessSensoryComponent>() : nullptr)
            Feedback->PlayWallImpact(Hit.ImpactPoint);
    if (ExplosionRadius > 0)
    {
        TArray<AActor*> Ignore{this, GetOwner()};
        EndlessCombatEffects::Flash(GetWorld(),Hit.ImpactPoint+Hit.ImpactNormal*2.f,Hit.ImpactNormal,FLinearColor(1,.3f,.05f),ExplosionRadius*.35f,.2f);
        UGameplayStatics::ApplyRadialDamage(this, Damage, Hit.ImpactPoint+Hit.ImpactNormal*2.f, ExplosionRadius,
            UDamageType::StaticClass(), Ignore, this, GetInstigatorController(), false);
    }
    else
    {
        UGameplayStatics::ApplyPointDamage(Other, Damage, GetActorForwardVector(), Hit,
            GetInstigatorController(), this, UDamageType::StaticClass());
    }
    Destroy();
}

void AEndlessPlayerProjectile::SetTracer(FLinearColor Color,float Width,float Seconds)
{
    TrailColor=Color;TrailWidth=Width;TrailSeconds=Seconds;
}
void AEndlessPlayerProjectile::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bImpacted)
    {
        EndlessCombatEffects::Trail(GetWorld(),LastTrailEnd,GetActorLocation(),TrailColor,TrailWidth,TrailSeconds);
        LastTrailEnd=GetActorLocation();
    }
}
