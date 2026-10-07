#include "EndlessWeaponLoot.h"
#include "EndlessPlayerWeapons.h"
#include "EndlessPlayerProjectile.h"
#include "EndlessSensory.h"
#include "EndlessEnemyController.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/DamageEvents.h"
#include "Animation/AnimSequence.h"
#include "TimerManager.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UnrealType.h"

AEndlessLootEnemy::AEndlessLootEnemy()
{
    EnemyWeaponMesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("EnemyEquippedWeapon"));
    EnemyWeaponMesh->SetupAttachment(GetMesh(),TEXT("hand_r"));
    EnemyWeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);EnemyWeaponMesh->SetCanEverAffectNavigation(false);
    Sensory=CreateDefaultSubobject<UEndlessSensoryComponent>(TEXT("Sensory"));
    GetCharacterMovement()->GetNavAgentPropertiesRef().bCanCrouch=true;
    GetCharacterMovement()->MaxWalkSpeedCrouched=140.f;
}
void AEndlessLootEnemy::BeginPlay()
{
    Super::BeginPlay();
    // 古いBPの固定銃タイマーは停止し、Controllerの射撃要求に一本化する。
    GetWorldTimerManager().ClearAllTimersForObject(this);
    TArray<UStaticMeshComponent*> OldMeshes;GetComponents(OldMeshes);
    for (auto* M:OldMeshes) if (M!=EnemyWeaponMesh) { M->SetHiddenInGame(true);M->SetCollisionEnabled(ECollisionEnabled::NoCollision); }
    TArray<int32> Choices;
    if (WeaponDropTable && WeaponDropTable->Catalog)
        for (int32 I=0;I<WeaponDropTable->Catalog->Weapons.Num();++I)
        {
            const auto& W=WeaponDropTable->Catalog->Weapons[I];
            if (W.Mesh && W.ExplosionRadius<=0.f && (AllowedWeaponIds.IsEmpty() || AllowedWeaponIds.Contains(W.Id))) Choices.Add(I);
        }
    if (!Choices.IsEmpty()) ConfigureLoadout(AppearanceMeshes.IsEmpty() ? -1 : FMath::RandRange(0,AppearanceMeshes.Num()-1),Choices[FMath::RandRange(0,Choices.Num()-1)]);
    GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
}
const FEndlessWeaponDefinition* AEndlessLootEnemy::GetEnemyWeapon() const
{
    return WeaponDropTable && WeaponDropTable->Catalog && WeaponDropTable->Catalog->Weapons.IsValidIndex(EnemyWeaponIndex)
        ? &WeaponDropTable->Catalog->Weapons[EnemyWeaponIndex] : nullptr;
}
bool AEndlessLootEnemy::ConfigureLoadout(int32 Appearance,int32 Weapon)
{
    if (!WeaponDropTable || !WeaponDropTable->Catalog || !WeaponDropTable->Catalog->Weapons.IsValidIndex(Weapon)) return false;
    UEndlessWeaponCatalog* Catalog=WeaponDropTable->Catalog;const auto& W=Catalog->Weapons[Weapon];
    if (!W.Mesh || W.ExplosionRadius>0.f) return false;
    if (AppearanceMeshes.IsValidIndex(Appearance) && AppearanceMeshes[Appearance])
    {
        AppearanceIndex=Appearance;GetMesh()->SetSkeletalMeshAsset(AppearanceMeshes[Appearance]);
        GetMesh()->EmptyOverrideMaterials();
    }
    EnemyWeaponIndex=Weapon;EnemyMagazine=W.MagazineSize;ReloadEnd=0.f;
    EnemyWeaponMesh->SetStaticMesh(W.Mesh);
    EnemyWeaponMesh->AttachToComponent(GetMesh(),FAttachmentTransformRules::SnapToTargetNotIncludingScale,TEXT("hand_r"));
    EnemyWeaponMesh->SetRelativeTransform(FTransform(W.HandRotation,W.HandOffset-W.HandRotation.RotateVector(W.Grip)));
    if (Catalog->WeaponMaterial) EnemyWeaponMesh->SetMaterial(0,Catalog->WeaponMaterial);
    GetMesh()->SetAnimInstanceClass(W.bTwoHanded ? Catalog->RifleAnimationClass : Catalog->PistolAnimationClass);
    return true;
}
bool AEndlessLootEnemy::IsEnemyReloading() const { return GetWorld() && ReloadEnd>GetWorld()->GetTimeSeconds(); }
bool AEndlessLootEnemy::FireEquippedWeapon(APawn* Target)
{
    const auto* W=GetEnemyWeapon();
    if (!W || !IsValid(Target) || bWeaponDropResolved || IsActorBeingDestroyed()) return false;
    const float Now=GetWorld()->GetTimeSeconds();
    if (Now<NextEnemyShot || IsEnemyReloading() || FVector::Dist(GetActorLocation(),Target->GetActorLocation())>W->EnemyRange) return false;
    if (ReloadEnd>0.f) { EnemyMagazine=W->MagazineSize;ReloadEnd=0.f; }
    if (EnemyMagazine<=0)
    {
        ReloadEnd=Now+W->ReloadSeconds;
        if (W->ReloadAnimation && GetMesh()->GetAnimInstance()) GetMesh()->GetAnimInstance()->PlaySlotAnimationAsDynamicMontage(W->ReloadAnimation,TEXT("WeaponReloadLeft"),.12f,.15f,W->ReloadAnimation->GetPlayLength()/W->ReloadSeconds);
        return false;
    }
    const FVector Muzzle=EnemyWeaponMesh->GetComponentTransform().TransformPosition(W->Muzzle);
    const FVector Aim=Target->GetActorLocation()+FVector(0,0,25);
    FHitResult Wall;FCollisionQueryParams Q(SCENE_QUERY_STAT(EnemyMuzzle),false,this);Q.AddIgnoredActor(Target);
    if (GetWorld()->LineTraceSingleByChannel(Wall,GetActorLocation()+FVector(0,0,50),Muzzle,ECC_Visibility,Q) ||
        GetWorld()->LineTraceSingleByChannel(Wall,Muzzle,Aim,ECC_Visibility,Q)) return false;
    LastEnemyMuzzle=Muzzle;NextEnemyShot=Now+FMath::Max(W->FireInterval,W->EnemyFireInterval);--EnemyMagazine;++EnemyShotsFired;
    FActorSpawnParameters P;P.Owner=this;P.Instigator=this;P.OverrideLevel=GetLevel();P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    for (int32 I=0;I<W->Pellets;++I)
    {
        const float Angle=FMath::Lerp(W->NearSpreadDegrees,W->SpreadDegrees,FMath::Clamp(FVector::Dist(Muzzle,Aim)/W->FarAccuracyDistance,0.f,1.f));
        const FVector Direction=FMath::VRandCone((Aim-Muzzle).GetSafeNormal(),FMath::DegreesToRadians(Angle));
        if (auto* Shot=GetWorld()->SpawnActor<AEndlessPlayerProjectile>(Muzzle,Direction.Rotation(),P))
        {
            Shot->Launch(W->Damage*W->EnemyDamageMultiplier,W->ProjectileSpeed,0.f);
            Shot->SetLifeSpan(W->EnemyRange/FMath::Max(1.f,W->ProjectileSpeed));
        }
    }
    if (W->FireAnimation && GetMesh()->GetAnimInstance()) GetMesh()->GetAnimInstance()->PlaySlotAnimationAsDynamicMontage(W->FireAnimation,TEXT("WeaponUpperBody"),.03f,.06f);
    Sensory->PlayShot(W->FireSound,Muzzle,W->NoiseRadius);return true;
}

float AEndlessLootEnemy::TakeDamage(float Amount, FDamageEvent const& Event, AController* InstigatorController, AActor* Causer)
{
    if (bWeaponDropResolved || IsActorBeingDestroyed()) return 0.f;
    const FVector DeathLocation = GetActorLocation();
    ULevel* DeathLevel = GetLevel();
    const FNumericProperty* Health = FindFProperty<FNumericProperty>(GetClass(), TEXT("Health"));
    const double Before=Health ? Health->GetFloatingPointPropertyValue(Health->ContainerPtrToValuePtr<void>(this)) : 0.;
    // Super内で既存BPのAnyDamageとDestroyが実行される。EndPlayでは抽選しない。
    const float Applied = Super::TakeDamage(Amount, Event, InstigatorController, Causer);
    const double After=Health ? Health->GetFloatingPointPropertyValue(Health->ContainerPtrToValuePtr<void>(this)) : Before;
    if (Applied>0.f && Before>After)
    {
        if (auto* Player=Cast<AEndlessPlayerCharacter>(InstigatorController ? InstigatorController->GetPawn() : nullptr))
        {
            FHitResult Hit;FVector Direction;Event.GetBestHitInfo(this,Causer,Hit,Direction);
            Player->Sensory->ConfirmHit(Hit.ImpactPoint.IsNearlyZero() ? DeathLocation+FVector(0,0,30) : Hit.ImpactPoint,Hit.ImpactNormal,DeathLevel);
        }
        if (!IsActorBeingDestroyed()) if (auto* Brain=Cast<AEndlessEnemyController>(GetController())) Brain->AlertToPlayer();
    }
    if (Applied > 0.f && Health && Health->GetFloatingPointPropertyValue(Health->ContainerPtrToValuePtr<void>(this)) <= 0.)
        ResolveWeaponDrop(DeathLocation, DeathLevel);
    return FMath::Min(Applied,float(FMath::Max(0.,Before-After)));
}

void AEndlessLootEnemy::ResolveWeaponDrop(const FVector& Location, ULevel* Level)
{
    if (bWeaponDropResolved) return;
    bWeaponDropResolved = true;
    if (!GetWorld() || !Level || !WeaponDropTable || !WeaponDropTable->Catalog ||
        WeaponDropChance <= 0.f || FMath::FRand() >= FMath::Clamp(WeaponDropChance, 0.f, 1.f)) return;
    float Total = 0.f;
    auto Valid = [this](const FEndlessWeaponDropEntry& E)
    {
        return E.Weight > 0.f && (!GetEnemyWeapon() || E.WeaponId==GetEnemyWeapon()->Id) && WeaponDropTable->Catalog->Weapons.ContainsByPredicate(
            [&E](const FEndlessWeaponDefinition& W) { return W.Id == E.WeaponId && W.Mesh; });
    };
    for (const auto& Entry : WeaponDropTable->Entries) if (Valid(Entry)) Total += Entry.Weight;
    if (Total <= 0.f) return;
    float Choice = FMath::FRand() * Total;
    for (const auto& Entry : WeaponDropTable->Entries)
    {
        if (!Valid(Entry)) continue;
        Choice -= Entry.Weight;
        if (Choice > 0.f) continue;
        FHitResult Floor;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(WeaponLootFloor), false, this);
        FVector Position = Location;
        if (GetWorld()->LineTraceSingleByChannel(Floor, Location, Location - FVector(0,0,250), ECC_WorldStatic, Query))
            Position = Floor.ImpactPoint + FVector(0,0,35);
        FActorSpawnParameters Params;
        // 未取得ドロップは死亡した敵と同じ部屋の寿命に従う。取得状態はプレイヤー側に移す。
        Params.OverrideLevel = Level;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        if (auto* Pickup = GetWorld()->SpawnActor<AEndlessWeaponPickup>(Position, FRotator::ZeroRotator, Params))
            Pickup->Initialize(WeaponDropTable->Catalog, Entry);
        return;
    }
}

AEndlessWeaponPickup::AEndlessWeaponPickup()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = .1f;
    PickupArea = CreateDefaultSubobject<USphereComponent>(TEXT("PickupArea"));
    SetRootComponent(PickupArea);
    PickupArea->InitSphereRadius(85.f);
    PickupArea->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    PickupArea->SetCollisionResponseToAllChannels(ECR_Ignore);
    PickupArea->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
    WeaponMesh->SetupAttachment(PickupArea);
    WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    WeaponMesh->SetCanEverAffectNavigation(false);
    WeaponMesh->SetRenderCustomDepth(true);
    Tags.Add(TEXT("EndlessWeaponDrop"));
}

void AEndlessWeaponPickup::Initialize(UEndlessWeaponCatalog* Catalog, const FEndlessWeaponDropEntry& Entry)
{
    if (!Catalog) return;
    const auto* Weapon = Catalog->Weapons.FindByPredicate([&Entry](const FEndlessWeaponDefinition& W) { return W.Id == Entry.WeaponId; });
    if (!Weapon) return;
    Contents = Entry;
    WeaponMesh->SetStaticMesh(Weapon->Mesh);
    WeaponMesh->SetMaterial(0, Catalog->WeaponMaterial);
    AvailableTime = GetWorld()->GetTimeSeconds() + .35f;
    bInitialized = true;
}

bool AEndlessWeaponPickup::TryCollect(AEndlessPlayerCharacter* Player)
{
    if (!bInitialized || bCollected || !IsValid(Player) || GetWorld()->GetTimeSeconds() < AvailableTime ||
        FVector::DistSquared(Player->GetActorLocation(), GetActorLocation()) > FMath::Square(120.f)) return false;
    FHitResult Wall;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(WeaponPickupSight), false, Player);
    Query.AddIgnoredActor(this);
    if (GetWorld()->LineTraceSingleByChannel(Wall, Player->GetActorLocation(), GetActorLocation(), ECC_Visibility, Query)) return false;
    if (!Player->AcquireWeapon(Contents.WeaponId, Contents.Magazine, Contents.Reserve)) return false;
    bCollected = true;
    SetActorEnableCollision(false);
    Destroy();
    return true;
}

void AEndlessWeaponPickup::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (auto* Player = Cast<AEndlessPlayerCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0))) TryCollect(Player);
}
