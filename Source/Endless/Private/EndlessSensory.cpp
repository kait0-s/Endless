#include "EndlessSensory.h"
#include "EndlessCombatEffects.h"
#include "EndlessEnemyController.h"
#include "EndlessPlayerWeapons.h"
#include "EndlessWeaponLoot.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundConcurrency.h"
#include "UObject/ConstructorHelpers.h"

UEndlessSensoryComponent::UEndlessSensoryComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickGroup=TG_PostPhysics;
    PrimaryComponentTick.TickInterval=1.f/30.f;
}
void UEndlessSensoryComponent::BeginPlay()
{
    Super::BeginPlay();
    UEndlessWeaponCatalog* Catalog=nullptr;
    if (auto* Player=Cast<AEndlessPlayerCharacter>(GetOwner())) Catalog=Player->WeaponCatalog;
    if (auto* Enemy=Cast<AEndlessLootEnemy>(GetOwner())) Catalog=Enemy->WeaponDropTable ? Enemy->WeaponDropTable->Catalog : nullptr;
    if (!Settings && Catalog) Settings=Catalog->SensorySettings;
    if (auto* Character=Cast<ACharacter>(GetOwner())) AddTickPrerequisiteComponent(Character->GetMesh());
}
void UEndlessSensoryComponent::PlayAt(USoundBase* Sound,FVector Location,float Volume,float Pitch)
{
    if (Sound && Settings) UGameplayStatics::PlaySoundAtLocation(this,Sound,Location,FRotator::ZeroRotator,
        Volume,Pitch,0.f,Settings->Attenuation,Settings->Concurrency,GetOwner());
}
void UEndlessSensoryComponent::ReportNoise(AActor* Source,FVector Location,EEndlessNoiseKind Kind,float Radius)
{
    if (!IsValid(Source) || !Source->GetWorld() || !Cast<AEndlessPlayerCharacter>(Source)) return;
    AEndlessEnemyController::DispatchNoise(Source,Location,Kind,Radius);
}
void UEndlessSensoryComponent::PlayShot(USoundBase* Sound,FVector Location,float Radius)
{
    if (Sound && Settings) UGameplayStatics::PlaySoundAtLocation(this,Sound,Location,FRotator::ZeroRotator,
        1.f,1.f,0.f,Settings->ShotAttenuation,Settings->Concurrency,GetOwner());
    ++ShotsPlayed;LastShotTime=GetWorld()->GetTimeSeconds();
    ReportNoise(GetOwner(),Location,EEndlessNoiseKind::Gunshot,Radius);
}
void UEndlessSensoryComponent::PlayWallImpact(FVector Location)
{
    EndlessCombatEffects::Flash(GetWorld(),Location,FVector::UpVector,FLinearColor(.8f,.7f,.35f),10.f,.1f);
    if (!Settings) return;
    PlayAt(Settings->WallImpact,Location,.5f);
    ReportNoise(GetOwner(),Location,EEndlessNoiseKind::Impact,Settings->ImpactRadius);
}
void UEndlessSensoryComponent::ConfirmHit(FVector Location,FVector Normal,ULevel* Level)
{
    ++ConfirmedHits;LastConfirmedHitTime=GetWorld()->GetTimeSeconds();
    EndlessCombatEffects::Flash(GetWorld(),Location+Normal*2.f,Normal,FLinearColor(.9f,.02f,.015f),18.f,.16f);
    if (!Settings) return;
    if (Settings->HitConfirm) UGameplayStatics::PlaySound2D(this,Settings->HitConfirm,.3f);
    if (Settings->BloodMaterial)
    {
        FActorSpawnParameters P;P.OverrideLevel=Level;
        P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        if (auto* Burst=GetWorld()->SpawnActor<AEndlessImpactBurst>(Location,FRotator::ZeroRotator,P))
            Burst->Initialize(Settings->BloodMaterial,Normal);
    }
}
void UEndlessSensoryComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Function)
{
    Super::TickComponent(Dt,Type,Function);
    auto* C=Cast<ACharacter>(GetOwner());
    if (!C || !Settings || C->IsActorBeingDestroyed() || !C->GetCharacterMovement()->IsMovingOnGround()) return;
    const float Speed=C->GetVelocity().Size2D();
    // 静止中は足音が発生しない。接地状態を保持し、再移動時の連打も防ぐ。
    if (Speed<=25.f) return;
    const float Now=GetWorld()->GetTimeSeconds();
    for (int32 Foot=0;Foot<2;++Foot)
    {
        const FVector Position=C->GetMesh()->GetSocketLocation(Foot==0 ? TEXT("foot_l") : TEXT("foot_r"));
        FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(EndlessFootstep),false,C);
        const bool Ground=GetWorld()->LineTraceSingleByChannel(Hit,Position+FVector(0,0,15),Position-FVector(0,0,55),ECC_Visibility,Q);
        const bool Planted=Ground && Position.Z-Hit.ImpactPoint.Z<18.f;
        if (Planted && !Contact[Foot] && Speed>25.f && Now-LastStepTime>.14f)
        {
            int32 Surface=0;
            if (Hit.GetActor() && Hit.GetActor()->ActorHasTag(TEXT("SurfaceWood"))) Surface=1;
            else if (Hit.GetActor() && Hit.GetActor()->ActorHasTag(TEXT("SurfaceMetal"))) Surface=2;
            else if (Hit.GetActor() && Hit.GetActor()->ActorHasTag(TEXT("SurfaceTile"))) Surface=3;
            LastSurface=Surface==1 ? TEXT("Wood") : Surface==2 ? TEXT("Metal") : Surface==3 ? TEXT("Tile") : TEXT("Concrete");
            const int32 Index=Surface*3+FMath::RandRange(0,2);
            const bool Crouched=C->bIsCrouched;const bool Running=Speed>400.f;
            if (Settings->Footsteps.IsValidIndex(Index)) PlayAt(Settings->Footsteps[Index],Hit.ImpactPoint,Crouched ? .2f : Running ? .85f : .55f,FMath::FRandRange(.96f,1.04f));
            ReportNoise(C,Hit.ImpactPoint,Crouched ? EEndlessNoiseKind::Crouch : Running ? EEndlessNoiseKind::Run : EEndlessNoiseKind::Walk,
                Crouched ? Settings->CrouchRadius : Running ? Settings->RunRadius : Settings->WalkRadius);
            ++StepsPlayed;LastStepTime=Now;LastStepLocation=Hit.ImpactPoint;
        }
        Contact[Foot]=Planted;
    }
}
AEndlessImpactBurst::AEndlessImpactBurst()
{
    PrimaryActorTick.bCanEverTick=true;InitialLifeSpan=.25f;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    for (int32 I=0;I<6;++I)
    {
        auto* C=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Drop%d"),I));C->SetupAttachment(RootComponent);
        C->SetStaticMesh(Sphere.Object);C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetCanEverAffectNavigation(false);C->SetCastShadow(false);
        Drops.Add(C);
    }
}
void AEndlessImpactBurst::Initialize(UMaterialInterface* Material,FVector Normal)
{
    for (const auto& Drop:Drops)
    {
        Drop->SetMaterial(0,Material);Drop->SetRelativeScale3D(FVector(.025f,.025f,.05f));
        Velocities.Add(Normal*FMath::FRandRange(30.f,70.f)+FMath::VRand()*45.f);
    }
}
void AEndlessImpactBurst::Tick(float Dt)
{
    Super::Tick(Dt);Age+=Dt;
    for (int32 I=0;I<Velocities.Num();++I)
    {
        Velocities[I].Z-=220.f*Dt;Drops[I]->AddRelativeLocation(Velocities[I]*Dt);
        Drops[I]->SetRelativeScale3D(FVector(.025f,.025f,.05f)*FMath::Max(0.f,1.f-Age/.25f));
    }
}
