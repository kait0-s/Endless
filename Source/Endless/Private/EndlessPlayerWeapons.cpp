#include "EndlessPlayerWeapons.h"
#include "EndlessPlayerProjectile.h"
#include "EndlessSensory.h"
#include "EndlessWeaponLoot.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/VerticalBox.h"
#include "Components/Border.h"
#include "Blueprint/WidgetTree.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Camera/PlayerCameraManager.h"
#include "Rendering/DrawElements.h"
#include "GameFramework/DamageType.h"
#include "Camera/CameraTypes.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

namespace EndlessWeapons
{
    bool TraceShot(UWorld* World, const FVector& Start, const FVector& End, const AActor* Ignore, FHitResult& Hit)
    {
        FCollisionQueryParams Params(SCENE_QUERY_STAT(EndlessWeaponTrace), false, Ignore);
        FHitResult PawnHit;
        const bool Wall = World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
        FCollisionObjectQueryParams Pawns; Pawns.AddObjectTypesToQuery(ECC_Pawn);
        const bool Pawn = World->LineTraceSingleByObjectType(PawnHit, Start, End, Pawns, Params);
        if (Pawn && (!Wall || PawnHit.Distance < Hit.Distance)) Hit = PawnHit;
        return Wall || Pawn;
    }
    double ReadNumber(const UObject* Object, FName Name, double Fallback = 0)
    {
        if (!Object) return Fallback;
        if (const FNumericProperty* P = FindFProperty<FNumericProperty>(Object->GetClass(), Name))
        {
            const void* V = P->ContainerPtrToValuePtr<void>(Object);
            return P->IsFloatingPoint() ? P->GetFloatingPointPropertyValue(V) : P->GetSignedIntPropertyValue(V);
        }
        return Fallback;
    }
    void WriteNumber(UObject* Object, FName Name, double Value)
    {
        if (!Object) return;
        if (FNumericProperty* P = FindFProperty<FNumericProperty>(Object->GetClass(), Name))
        {
            void* V = P->ContainerPtrToValuePtr<void>(Object);
            if (P->IsFloatingPoint()) P->SetFloatingPointPropertyValue(V, Value);
            else P->SetIntPropertyValue(V, static_cast<int64>(Value));
        }
    }
    bool ReadBool(const UObject* Object, FName Name)
    {
        const FBoolProperty* P = FindFProperty<FBoolProperty>(Object->GetClass(), Name);
        return P && P->GetPropertyValue_InContainer(Object);
    }
}

AEndlessPlayerCharacter::AEndlessPlayerCharacter()
{
    Sensory=CreateDefaultSubobject<UEndlessSensoryComponent>(TEXT("Sensory"));
    PrimaryActorTick.bCanEverTick = true;
    EquippedMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("EquippedWeapon"));
    EquippedMesh->SetupAttachment(GetMesh(), TEXT("hand_r"));
    EquippedMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    EquippedMesh->SetCanEverAffectNavigation(false);
    WeaponMuzzle = CreateDefaultSubobject<USceneComponent>(TEXT("WeaponMuzzle"));
    WeaponMuzzle->SetupAttachment(EquippedMesh);
    GetCharacterMovement()->GetNavAgentPropertiesRef().bCanCrouch = true;
    GetCharacterMovement()->MaxWalkSpeedCrouched = 180.f;
    GetCharacterMovement()->SetCrouchedHalfHeight(74.f);
}

void AEndlessPlayerCharacter::BeginPlay()
{
    Super::BeginPlay();
    // Blueprint BeginPlay retains the existing health, HUD and inventory initialization.
    GetWorldTimerManager().SetTimerForNextTick(this, &AEndlessPlayerCharacter::InitializeWeapons);
}

void AEndlessPlayerCharacter::InitializeWeapons()
{
    if (!WeaponCatalog || WeaponCatalog->Weapons.IsEmpty()) return;
    // 銃口の判定に使う手のボーンは、画面外でも現在の照準姿勢へ更新する。
    GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    GetCharacterMovement()->GetNavAgentPropertiesRef().bCanCrouch = true;
    GetCharacterMovement()->SetCrouchedHalfHeight(74.f);
    WeaponAmmo.SetNum(WeaponCatalog->Weapons.Num());
    OwnedWeaponIndices.Reset();
    for (auto& Ammo : WeaponAmmo) Ammo = FEndlessWeaponAmmo();
    const int32 Starter = WeaponCatalog->Weapons.IndexOfByPredicate([](const FEndlessWeaponDefinition& W) { return W.Id == TEXT("SK_M1911"); });
    if (Starter == INDEX_NONE) return;
    OwnedWeaponIndices.Add(Starter);
    WeaponAmmo[Starter].Magazine = WeaponCatalog->Weapons[Starter].MagazineSize;
    WeaponAmmo[Starter].Reserve = WeaponCatalog->Weapons[Starter].StartingReserve;
    // Only the player's legacy weapon components are hidden; shared enemy assets remain intact.
    TArray<UStaticMeshComponent*> Components;
    GetComponents(Components);
    for (auto* Component : Components)
        if (Component != EquippedMesh && Component->GetStaticMesh()) Component->SetVisibility(false, true);
    EquipWeapon(Starter);
    // 銃口を画面へ投影する旧カーソルを外し、固定中心のレティクルへ置き換える。
    if (auto* Property = FindFProperty<FObjectProperty>(GetClass(), TEXT("Laser Widget")))
        if (auto* Laser = Cast<UUserWidget>(Property->GetObjectPropertyValue_InContainer(this))) Laser->RemoveFromParent();
    if (auto* Property=FindFProperty<FObjectProperty>(GetClass(),TEXT("HUDRef")))
        if (auto* Legacy=Cast<UUserWidget>(Property->GetObjectPropertyValue_InContainer(this)))
        {
            // The old ten-bullet strip and three-weapon icon cannot represent the new catalog.
            for (int32 I=0;I<10;++I)
                if (auto* Widget=Legacy->GetWidgetFromName(FName(*FString::Printf(TEXT("Bullet_%d"),I)))) Widget->SetVisibility(ESlateVisibility::Collapsed);
            for (FName Name : {FName(TEXT("TotalAmmo_Text")),FName(TEXT("weapon")),FName(TEXT("Image_3"))})
                if (auto* Widget=Legacy->GetWidgetFromName(Name)) Widget->SetVisibility(ESlateVisibility::Collapsed);
        }
    if (IsLocallyControlled())
    {
        WeaponReticle = CreateWidget<UEndlessWeaponReticle>(Cast<APlayerController>(GetController()));
        if (WeaponReticle) { WeaponReticle->SetVisibility(ESlateVisibility::HitTestInvisible); WeaponReticle->AddToViewport(21); }
        WeaponStatusWidget = CreateWidget<UEndlessWeaponStatusWidget>(Cast<APlayerController>(GetController()));
        if (WeaponStatusWidget)
        {
            WeaponStatusWidget->AddToViewport(20);
            WeaponStatusWidget->SetPositionInViewport(FVector2D(30,-30),false);
            WeaponStatusWidget->SetDesiredSizeInViewport(FVector2D(440,76));
            // Position and size reset the viewport anchors; apply anchors afterwards.
            WeaponStatusWidget->SetAnchorsInViewport(FAnchors(0,1));
            WeaponStatusWidget->SetAlignmentInViewport(FVector2D(0,1));
        }
    }
}

void AEndlessPlayerCharacter::SetupPlayerInputComponent(UInputComponent* Input)
{
    Super::SetupPlayerInputComponent(Input);
    if (auto* Enhanced = Cast<UEnhancedInputComponent>(Input))
    {
        auto Action = [](const TCHAR* Name) { return LoadObject<UInputAction>(nullptr, *FString::Printf(TEXT("/Game/TacticalSurvive/Input/Actions/%s.%s"), Name, Name)); };
        if (auto* A=Action(TEXT("IA_Shoot")))
        {
            Enhanced->BindAction(A,ETriggerEvent::Started,this,&AEndlessPlayerCharacter::StartWeaponFire);
            Enhanced->BindAction(A,ETriggerEvent::Completed,this,&AEndlessPlayerCharacter::StopWeaponFire);
            Enhanced->BindAction(A,ETriggerEvent::Canceled,this,&AEndlessPlayerCharacter::StopWeaponFire);
        }
        if (auto* A=Action(TEXT("IA_Reload"))) Enhanced->BindAction(A,ETriggerEvent::Started,this,&AEndlessPlayerCharacter::ReloadInput);
        if (auto* A=Action(TEXT("IA_weaponchange"))) Enhanced->BindAction(A,ETriggerEvent::Started,this,&AEndlessPlayerCharacter::NextWeapon);
        if (auto* A=Action(TEXT("IA_weaponchange1"))) Enhanced->BindAction(A,ETriggerEvent::Started,this,&AEndlessPlayerCharacter::PreviousWeapon);
        if (WeaponCatalog && WeaponCatalog->CrouchAction)
        {
            Enhanced->BindAction(WeaponCatalog->CrouchAction,ETriggerEvent::Started,this,&AEndlessPlayerCharacter::StartWeaponCrouch);
            Enhanced->BindAction(WeaponCatalog->CrouchAction,ETriggerEvent::Completed,this,&AEndlessPlayerCharacter::StopWeaponCrouch);
            Enhanced->BindAction(WeaponCatalog->CrouchAction,ETriggerEvent::Canceled,this,&AEndlessPlayerCharacter::StopWeaponCrouch);
        }
    }
}

const FEndlessWeaponDefinition* AEndlessPlayerCharacter::GetWeaponDefinition() const
{
    return WeaponCatalog && WeaponCatalog->Weapons.IsValidIndex(EquippedWeaponIndex) ? &WeaponCatalog->Weapons[EquippedWeaponIndex] : nullptr;
}
FEndlessWeaponAmmo AEndlessPlayerCharacter::GetEquippedAmmo() const
{
    return WeaponAmmo.IsValidIndex(EquippedWeaponIndex) ? WeaponAmmo[EquippedWeaponIndex] : FEndlessWeaponAmmo();
}
bool AEndlessPlayerCharacter::CanUseWeapon() const
{
    return GetWeaponDefinition() && WeaponAmmo.IsValidIndex(EquippedWeaponIndex) && EndlessWeapons::ReadNumber(GetGameInstance(),TEXT("CurrentHP"),100)>0 && !EndlessWeapons::ReadBool(this,TEXT("IsRolling"));
}
bool AEndlessPlayerCharacter::IsWeaponAiming() const { return EndlessWeapons::ReadBool(this,TEXT("IsAiming")); }

void AEndlessPlayerCharacter::ReadLegacyAmmo()
{
    if (!WeaponAmmo.IsValidIndex(EquippedWeaponIndex)) return;
    auto& Ammo=WeaponAmmo[EquippedWeaponIndex];
    // Pickups still write the GameInstance. Only import an actual external change.
    const int32 Magazine=FMath::RoundToInt(EndlessWeapons::ReadNumber(GetGameInstance(),TEXT("CurrentAmmo"),PublishedMagazine));
    const int32 Reserve=FMath::RoundToInt(EndlessWeapons::ReadNumber(GetGameInstance(),TEXT("TotalAmmo"),PublishedReserve));
    if (Magazine!=PublishedMagazine) Ammo.Magazine=FMath::Clamp(Magazine,0,GetWeaponDefinition()->MagazineSize);
    if (Reserve!=PublishedReserve) Ammo.Reserve=FMath::Max(0,Reserve);
}
void AEndlessPlayerCharacter::PublishAmmo()
{
    const auto* W=GetWeaponDefinition(); if (!W) return;
    const auto Ammo=GetEquippedAmmo(); PublishedMagazine=Ammo.Magazine; PublishedReserve=Ammo.Reserve;
    EndlessWeapons::WriteNumber(GetGameInstance(),TEXT("CurrentAmmo"),Ammo.Magazine);
    EndlessWeapons::WriteNumber(GetGameInstance(),TEXT("MaxAmmo"),W->MagazineSize);
    EndlessWeapons::WriteNumber(GetGameInstance(),TEXT("TotalAmmo"),Ammo.Reserve);
    EndlessWeapons::WriteNumber(this,TEXT("CurrentAmmo"),Ammo.Magazine);
}
bool AEndlessPlayerCharacter::OwnsWeapon(int32 Index) const { return OwnedWeaponIndices.Contains(Index); }

bool AEndlessPlayerCharacter::AcquireWeapon(FName Id, int32 Magazine, int32 Reserve)
{
    if (!WeaponCatalog || EndlessWeapons::ReadNumber(GetGameInstance(),TEXT("CurrentHP"),100)<=0) return false;
    const int32 Index = WeaponCatalog->Weapons.IndexOfByPredicate([Id](const FEndlessWeaponDefinition& W) { return W.Id == Id; });
    if (!WeaponAmmo.IsValidIndex(Index)) return false;
    ReadLegacyAmmo();
    auto& Ammo = WeaponAmmo[Index];
    if (!OwnsWeapon(Index))
    {
        OwnedWeaponIndices.Add(Index);
        Ammo.Magazine = FMath::Clamp(Magazine,0,WeaponCatalog->Weapons[Index].MagazineSize);
        Ammo.Reserve = FMath::Max(0,Reserve) + FMath::Max(0,Magazine-Ammo.Magazine);
    }
    else Ammo.Reserve += FMath::Max(0,Magazine) + FMath::Max(0,Reserve);
    PublishAmmo();
    return true;
}

bool AEndlessPlayerCharacter::EquipWeapon(int32 Index)
{
    if (!OwnsWeapon(Index)) return false;
    if (!WeaponCatalog || !WeaponCatalog->Weapons.IsValidIndex(Index) || !WeaponAmmo.IsValidIndex(Index)) return false;
    if (EndlessWeapons::ReadNumber(GetGameInstance(),TEXT("CurrentHP"),100)<=0) return false;
    ReadLegacyAmmo(); CancelWeaponReload(); StopWeaponFire();
    EquippedWeaponIndex=Index;
    const auto& W=WeaponCatalog->Weapons[Index];
    EquippedMesh->SetStaticMesh(W.Mesh);
    EquippedMesh->AttachToComponent(GetMesh(),FAttachmentTransformRules::SnapToTargetNotIncludingScale,TEXT("hand_r"));
    EquippedMesh->SetRelativeTransform(FTransform(W.HandRotation,W.HandOffset-W.HandRotation.RotateVector(W.Grip)));
    if (WeaponCatalog->WeaponMaterial) EquippedMesh->SetMaterial(0,WeaponCatalog->WeaponMaterial);
    WeaponMuzzle->SetRelativeTransform(FTransform(FRotator::ZeroRotator,W.Muzzle));
    const auto AnimClass=W.bTwoHanded ? WeaponCatalog->RifleAnimationClass : WeaponCatalog->PistolAnimationClass;
    if (AnimClass && GetMesh()->GetAnimClass()!=AnimClass) GetMesh()->SetAnimInstanceClass(AnimClass);
    PublishAmmo(); return true;
}
void AEndlessPlayerCharacter::CycleWeapon(int32 Direction)
{
    if (!WeaponCatalog) return;
    const int32 Count=WeaponCatalog->Weapons.Num();
    for (int32 Step=1;Step<Count;++Step)
    {
        const int32 Index=(EquippedWeaponIndex+Direction*Step+Count)%Count;
        if (OwnsWeapon(Index)) { EquipWeapon(Index); return; }
    }
}
void AEndlessPlayerCharacter::NextWeapon() { CycleWeapon(1); }
void AEndlessPlayerCharacter::PreviousWeapon() { CycleWeapon(-1); }

bool AEndlessPlayerCharacter::BeginWeaponReload()
{
    ReadLegacyAmmo();
    if (!CanUseWeapon() || bWeaponReloading) return false;
    const auto& W=*GetWeaponDefinition(); const auto& A=WeaponAmmo[EquippedWeaponIndex];
    if (A.Magazine>=W.MagazineSize || A.Reserve<=0) return false;
    StopWeaponFire(); bWeaponReloading=true; ReloadWeapon=EquippedWeaponIndex;
    const float Duration=FMath::Max(.1f,W.ReloadSeconds);
    ReloadEndTime=GetWorld()->GetTimeSeconds()+Duration;
    GetWorldTimerManager().SetTimer(ReloadTimer,this,&AEndlessPlayerCharacter::FinishWeaponReload,Duration,false);
    if (W.ReloadAnimation && GetMesh()->GetAnimInstance())
        ReloadMontage=GetMesh()->GetAnimInstance()->PlaySlotAnimationAsDynamicMontage(W.ReloadAnimation,TEXT("WeaponReloadLeft"),.12f,.15f,W.ReloadAnimation->GetPlayLength()/Duration);
    PublishAmmo(); return true;
}
void AEndlessPlayerCharacter::ReloadInput() { BeginWeaponReload(); }
void AEndlessPlayerCharacter::FinishWeaponReload()
{
    ReadLegacyAmmo();
    if (bWeaponReloading && ReloadWeapon==EquippedWeaponIndex && CanUseWeapon())
    {
        auto& A=WeaponAmmo[EquippedWeaponIndex]; const auto& W=*GetWeaponDefinition();
        const int32 Transfer=FMath::Min(FMath::Max(0,W.MagazineSize-A.Magazine),FMath::Max(0,A.Reserve));
        A.Magazine+=Transfer; A.Reserve-=Transfer;
    }
    bWeaponReloading=false; ReloadWeapon=INDEX_NONE; ReloadEndTime=0; ReloadMontage=nullptr;
    PublishAmmo();
}
void AEndlessPlayerCharacter::CancelWeaponReload()
{
    GetWorldTimerManager().ClearTimer(ReloadTimer);
    if (ReloadMontage && GetMesh()->GetAnimInstance()) GetMesh()->GetAnimInstance()->Montage_Stop(.12f,ReloadMontage);
    bWeaponReloading=false; ReloadWeapon=INDEX_NONE; ReloadEndTime=0; ReloadMontage=nullptr;
}
float AEndlessPlayerCharacter::GetReloadRemaining() const { return bWeaponReloading ? FMath::Max(0.f,ReloadEndTime-GetWorld()->GetTimeSeconds()) : 0.f; }
void AEndlessPlayerCharacter::StartWeaponFire()
{
    if (!CanUseWeapon() || bWeaponReloading) return;
    bWeaponTriggerHeld=true; TryWeaponShot();
    if (GetWeaponDefinition()->bAutomatic) GetWorldTimerManager().SetTimer(FireTimer,this,&AEndlessPlayerCharacter::StartWeaponFire,FMath::Max(.03f,GetWeaponDefinition()->FireInterval),false);
}
void AEndlessPlayerCharacter::StopWeaponFire() { bWeaponTriggerHeld=false; GetWorldTimerManager().ClearTimer(FireTimer); }
FTransform AEndlessPlayerCharacter::GetWeaponMuzzleTransform() const { return WeaponMuzzle->GetComponentTransform(); }
float AEndlessPlayerCharacter::GetSpreadAtDistance(float Distance) const
{
    const auto* W=GetWeaponDefinition(); if (!W) return 0.f;
    const float Alpha=FMath::Clamp((Distance-W->NearAccuracyDistance)/FMath::Max(1.f,W->FarAccuracyDistance-W->NearAccuracyDistance),0.f,1.f);
    return FMath::Lerp(W->NearSpreadDegrees,W->SpreadDegrees,Alpha*Alpha*(3.f-2.f*Alpha))*(IsWeaponAiming()?W->AimSpreadMultiplier:1.f);
}
FVector AEndlessPlayerCharacter::GetAimTarget() const
{
    FVector View; FRotator Rotation;
    if (GetController()) GetController()->GetPlayerViewPoint(View,Rotation); else GetActorEyesViewPoint(View,Rotation);
    const FVector End=View+Rotation.Vector()*30000.f;
    FHitResult Hit;
    return EndlessWeapons::TraceShot(GetWorld(),View,End,this,Hit)?Hit.ImpactPoint:End;
}
bool AEndlessPlayerCharacter::TryWeaponShot()
{
    ReadLegacyAmmo();
    if (!CanUseWeapon() || bWeaponReloading || GetWorld()->GetTimeSeconds()+.001f<NextShotTime || WeaponAmmo[EquippedWeaponIndex].Magazine<=0) return false;
    const auto& W=*GetWeaponDefinition();
    LastAimTarget=GetAimTarget();
    LastShotOrigin=WeaponMuzzle->GetComponentLocation();
    LastShotDirection=(LastAimTarget-LastShotOrigin).GetSafeNormal();
    LastShotSpreadDegrees=GetSpreadAtDistance(FVector::Distance(GetActorLocation(),LastAimTarget));
    // 銃口が近接した敵や壁を越えた場合、体から銃口までの最初の障害物を優先する。
    FHitResult Obstruction;
    const FVector SafetyStart=GetActorLocation()+FVector(0,0,35);
    const bool Blocked=EndlessWeapons::TraceShot(GetWorld(),SafetyStart,LastShotOrigin,this,Obstruction);
    for (int32 Pellet=0;Pellet<FMath::Max(1,W.Pellets);++Pellet)
    {
        const FVector Direction=FMath::VRandCone(LastShotDirection,FMath::DegreesToRadians(LastShotSpreadDegrees));
        if (Blocked)
        {
            if (W.ExplosionRadius>0)
                UGameplayStatics::ApplyRadialDamage(this,W.Damage,Obstruction.ImpactPoint,W.ExplosionRadius,UDamageType::StaticClass(),{this},this,GetController(),false);
            else UGameplayStatics::ApplyPointDamage(Obstruction.GetActor(),W.Damage,Direction,Obstruction,GetController(),this,UDamageType::StaticClass());
        }
        else
        {
            FActorSpawnParameters Spawn; Spawn.Owner=this; Spawn.Instigator=this; Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            if (auto* Projectile=GetWorld()->SpawnActor<AEndlessPlayerProjectile>(LastShotOrigin,Direction.Rotation(),Spawn)) Projectile->Launch(W.Damage,W.ProjectileSpeed,W.ExplosionRadius);
        }
    }
    --WeaponAmmo[EquippedWeaponIndex].Magazine; ++ShotsFired; NextShotTime=GetWorld()->GetTimeSeconds()+FMath::Max(.03f,W.FireInterval);
    Sensory->PlayShot(W.FireSound,LastShotOrigin,W.NoiseRadius);
    if (W.FireAnimation && GetMesh()->GetAnimInstance()) GetMesh()->GetAnimInstance()->PlaySlotAnimationAsDynamicMontage(W.FireAnimation,TEXT("WeaponUpperBody"),.03f,.08f,1.f);
    PublishAmmo(); return true;
}

FVector AEndlessPlayerCharacter::GetSupportHandTarget() const
{
    const auto* W=GetWeaponDefinition();
    if (!W) return FVector::ZeroVector;
    // 前フレームの装備Componentは読まず、右手ボーン空間で接触点から手首位置を求める。
    const FTransform Attachment(W->HandRotation,W->HandOffset-W->HandRotation.RotateVector(W->Grip));
    return Attachment.TransformPosition(W->SupportGrip-W->SupportGripRotation.RotateVector(W->SupportPalmOffset));
}
FRotator AEndlessPlayerCharacter::GetSupportHandRotation() const
{
    const auto* W=GetWeaponDefinition();
    return W ? (W->HandRotation.Quaternion()*W->SupportGripRotation.Quaternion()).Rotator() : FRotator::ZeroRotator;
}
void AEndlessPlayerCharacter::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
    Super::CalcCamera(DeltaTime,OutResult);
    bCameraShoulderFallback=false;
    if (!GetWorld() || FVector::DistSquared2D(OutResult.Location,GetActorLocation())>=FMath::Square(CameraBodyClearance)) return;
    // 通常のSpringArm衝突を保ち、背中まで圧縮された時だけ壁を再検査して肩側へ逃がす。
    const FVector Right=FRotationMatrix(OutResult.Rotation).GetUnitAxis(EAxis::Y);
    const FVector Original=OutResult.Location;
    FVector Best=Original;
    float BestDistance=FVector::DistSquared2D(Original,GetActorLocation());
    FCollisionQueryParams Query(SCENE_QUERY_STAT(EndlessShoulderCamera),false,this);
    const FCollisionShape Probe=FCollisionShape::MakeSphere(12.f);
    for (float Sign : {1.f,-1.f})
    {
        const FVector Desired=Original+Right*CameraBodyClearance*Sign;
        FHitResult Wall;
        const bool Hit=GetWorld()->SweepSingleByChannel(Wall,Original,Desired,FQuat::Identity,ECC_Camera,Probe,Query);
        const FVector Candidate=Hit ? Wall.Location : Desired;
        if (GetWorld()->OverlapBlockingTestByChannel(Candidate,FQuat::Identity,ECC_Camera,Probe,Query)) continue;
        const float Distance=FVector::DistSquared2D(Candidate,GetActorLocation());
        if (Distance>BestDistance+1.f) { BestDistance=Distance;Best=Candidate; }
        if (Distance>=FMath::Square(CameraBodyClearance)) break;
    }
    OutResult.Location=Best;
    bCameraShoulderFallback=!Best.Equals(Original,.1f);
    // 肩へ平行移動するだけでは腕が画面外へ出るため、近い照準軸へ向け直す。
    // 射撃もGetPlayerViewPointを使うので、補正後の画面中心と命中判定は一致する。
    if (bCameraShoulderFallback)
        OutResult.Rotation=(Original+OutResult.Rotation.Vector()*100.f-Best).Rotation();
}
void AEndlessPlayerCharacter::UpdateVisuals()
{
    const bool WantsCrouch=EndlessWeapons::ReadBool(this,TEXT("IsCrouching"));
    if (WantsCrouch && !bIsCrouched) Crouch(); else if (!WantsCrouch && bIsCrouched) UnCrouch();
}
void AEndlessPlayerCharacter::StartWeaponCrouch()
{
    if (FBoolProperty* P=FindFProperty<FBoolProperty>(GetClass(),TEXT("IsCrouching"))) P->SetPropertyValue_InContainer(this,true);
    Crouch();
}
void AEndlessPlayerCharacter::StopWeaponCrouch()
{
    if (FBoolProperty* P=FindFProperty<FBoolProperty>(GetClass(),TEXT("IsCrouching"))) P->SetPropertyValue_InContainer(this,false);
    UnCrouch();
}
void AEndlessPlayerCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!CanUseWeapon()) { CancelWeaponReload(); StopWeaponFire(); }
    else { ReadLegacyAmmo(); PublishAmmo(); UpdateVisuals(); }
    if (WeaponStatusWidget) WeaponStatusWidget->Refresh(this);
}
void AEndlessPlayerCharacter::EndPlay(const EEndPlayReason::Type Reason)
{
    CancelWeaponReload(); StopWeaponFire();
    if (WeaponStatusWidget) WeaponStatusWidget->RemoveFromParent();
    if (WeaponReticle) WeaponReticle->RemoveFromParent();
    Super::EndPlay(Reason);
}
FText AEndlessPlayerCharacter::GetWeaponStatus() const
{
    const auto* W=GetWeaponDefinition(); if (!W) return FText::GetEmpty();
    const auto A=GetEquippedAmmo();
    return FText::FromString(FString::Printf(TEXT("%s   %d / %d%s"),*W->DisplayName.ToString(),A.Magazine,A.Reserve,bWeaponReloading ? *FString::Printf(TEXT("   RELOAD %.1fs"),GetReloadRemaining()) : TEXT("")));
}
void UEndlessWeaponStatusWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized(); SetVisibility(ESlateVisibility::HitTestInvisible);
    auto* Background=WidgetTree->ConstructWidget<UBorder>();Background->SetBrushColor(FLinearColor(0,0,0,.65f));Background->SetPadding(FMargin(12,8));WidgetTree->RootWidget=Background;
    auto* Box=WidgetTree->ConstructWidget<UVerticalBox>();Background->SetContent(Box);
    StatusText=WidgetTree->ConstructWidget<UTextBlock>(); StatusText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
    FSlateFontInfo Font=StatusText->GetFont(); Font.Size=18; StatusText->SetFont(Font); Box->AddChild(StatusText);
    ReloadProgress=WidgetTree->ConstructWidget<UProgressBar>(); ReloadProgress->SetFillColorAndOpacity(FLinearColor(.15f,.7f,1.f)); Box->AddChild(ReloadProgress);
}
void UEndlessWeaponStatusWidget::Refresh(AEndlessPlayerCharacter* Player)
{
    if (!StatusText || !Player) return;
    StatusText->SetText(Player->GetWeaponStatus());
    const auto* W=Player->GetWeaponDefinition();
    ReloadProgress->SetVisibility(Player->bWeaponReloading ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
    ReloadProgress->SetPercent(W && Player->bWeaponReloading ? 1.f-Player->GetReloadRemaining()/FMath::Max(.1f,W->ReloadSeconds) : 0.f);
}
void UEndlessWeaponAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);
    if (auto* Player=Cast<AEndlessPlayerCharacter>(TryGetPawnOwner()))
    {
        SupportHandTarget=Player->GetSupportHandTarget();
        SupportHandRotation=Player->GetSupportHandRotation();
        const auto* W=Player->GetWeaponDefinition();
        const bool Rolling=EndlessWeapons::ReadBool(Player,TEXT("IsRolling"));
        const float Target=W && W->bUseSupportHand && !Player->bWeaponReloading && !Rolling ? 1.f : 0.f;
        SupportHandAlpha=FMath::FInterpTo(SupportHandAlpha,Target,DeltaSeconds,12.f);
        WeaponCrouchAlpha=FMath::FInterpTo(WeaponCrouchAlpha,Player->bIsCrouched ? 1.f : 0.f,DeltaSeconds,12.f);
        WeaponCrouchOffset=FVector(0,0,-35.f*WeaponCrouchAlpha);
        WeaponPoseAlpha=FMath::FInterpTo(WeaponPoseAlpha,Rolling ? 0.f : 1.f,DeltaSeconds,16.f);
        const float Pitch=FMath::Clamp(FRotator::NormalizeAxis(Player->GetBaseAimRotation().Pitch),-60.f,60.f);
        WeaponAimRotation=FRotator(0,0,-Pitch*WeaponPoseAlpha);
    }
    else if (auto* Enemy=Cast<AEndlessLootEnemy>(TryGetPawnOwner()))
    {
        if (const auto* W=Enemy->GetEnemyWeapon())
        {
            const FTransform Attachment(W->HandRotation,W->HandOffset-W->HandRotation.RotateVector(W->Grip));
            SupportHandTarget=Attachment.TransformPosition(W->SupportGrip-W->SupportGripRotation.RotateVector(W->SupportPalmOffset));
            SupportHandRotation=(W->HandRotation.Quaternion()*W->SupportGripRotation.Quaternion()).Rotator();
            SupportHandAlpha=FMath::FInterpTo(SupportHandAlpha,Enemy->IsEnemyReloading() ? 0.f : 1.f,DeltaSeconds,12.f);
            WeaponAimRotation=FRotator(0,0,-FMath::Clamp(FRotator::NormalizeAxis(Enemy->GetBaseAimRotation().Pitch),-60.f,60.f));
            WeaponPoseAlpha=1.f;
            WeaponCrouchAlpha=FMath::FInterpTo(WeaponCrouchAlpha,Enemy->bIsCrouched ? 1.f : 0.f,DeltaSeconds,12.f);
            WeaponCrouchOffset=FVector(0,0,-35.f*WeaponCrouchAlpha);
        }
    }
}

int32 UEndlessWeaponReticle::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
    FSlateWindowElementList& OutElements, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const
{
    const int32 Layer=Super::NativePaint(Args,Geometry,CullingRect,OutElements,LayerId,Style,bParentEnabled)+1;
    const auto* Player=Cast<AEndlessPlayerCharacter>(GetOwningPlayerPawn());
    if (!Player || !GetOwningPlayer() || !GetOwningPlayer()->PlayerCameraManager) return Layer;
    const FVector2D Center=Geometry.GetLocalSize()*.5f;
    const float Angle=Player->GetSpreadAtDistance(FVector::Distance(Player->GetActorLocation(),Player->GetAimTarget()));
    const float FOV=GetOwningPlayer()->PlayerCameraManager->GetFOVAngle();
    const float Radius=FMath::Max(6.f,FMath::Tan(FMath::DegreesToRadians(Angle))*Center.X/FMath::Tan(FMath::DegreesToRadians(FOV*.5f)));
    auto Line=[&](FVector2D A,FVector2D B,FLinearColor Color,float Width)
    {
        TArray<FVector2D> Points{A,B};
        FSlateDrawElement::MakeLines(OutElements,Layer,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,Width);
    };
    Line(Center-FVector2D(2,0),Center+FVector2D(2,0),FLinearColor::White,2.f);
    Line(Center-FVector2D(0,2),Center+FVector2D(0,2),FLinearColor::White,2.f);
    for (FVector2D Axis : {FVector2D(1,0),FVector2D(-1,0),FVector2D(0,1),FVector2D(0,-1)})
        Line(Center+Axis*Radius,Center+Axis*(Radius+6.f),FLinearColor(.2f,1.f,.8f,.9f),1.5f);
    if (Player->Sensory && Player->GetWorld()->GetTimeSeconds()-Player->Sensory->LastConfirmedHitTime<.16f)
        for (FVector2D D : {FVector2D(1,1),FVector2D(-1,1),FVector2D(1,-1),FVector2D(-1,-1)})
            Line(Center+D*6.f,Center+D*10.f,FLinearColor(1.f,.65f,.5f,.8f),1.2f);
    return Layer;
}
