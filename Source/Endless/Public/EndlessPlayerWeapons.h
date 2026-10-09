#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Character.h"
#include "Animation/AnimInstance.h"
#include "Blueprint/UserWidget.h"
#include "EndlessPlayerWeapons.generated.h"

class UStaticMeshComponent;
class UTextBlock;
class UProgressBar;
class UAnimSequence;
class UAnimMontage;

USTRUCT(BlueprintType)
struct FEndlessWeaponDefinition
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Id;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FText DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UStaticMesh> Mesh;
    // Asset-space trigger grip is placed at the right-hand socket. All dimensions are centimetres.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector Grip = FVector(-8, 0, -6);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FRotator HandRotation = FRotator(0, 0, 90);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector HandOffset = FVector(3, 0, 0);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector Muzzle = FVector(40, 0, 0);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector SupportGrip = FVector(16, 0, -3);
    // 銃ローカルの接触点・手首回転と、hand_lローカルの手のひら中心。
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FRotator SupportGripRotation = FRotator(0, 0, 90);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector SupportPalmOffset = FVector(5, 0, 0);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bUseSupportHand = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bTwoHanded = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MagazineSize = 30;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 StartingReserve = 90;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.1")) float ReloadSeconds = 2.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.03")) float FireInterval = 0.1f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bAutomatic = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Damage = 15.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Pellets = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float SpreadDegrees = 1.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float NearSpreadDegrees = .03f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float NearAccuracyDistance = 300.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1")) float FarAccuracyDistance = 3000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", ClampMax="1")) float AimSpreadMultiplier = .35f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float ProjectileSpeed = 12000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float ExplosionRadius = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float TracerSeconds = .12f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float TracerWidth = 1.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FLinearColor TracerColor = FLinearColor(1.f,.65f,.12f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float MuzzleFlashSize = 12.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float MuzzleFlashSeconds = .055f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float VisualRecoilDegrees = 2.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UAnimSequence> ReloadAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UAnimSequence> FireAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class USoundBase> FireSound;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class USoundBase> ReloadStartSound;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class USoundBase> ReloadCompleteSound;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float ReloadSoundPitch = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float NoiseRadius = 3000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float EnemyRange = 2200.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float EnemyFireInterval = .35f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float EnemyDamageMultiplier = .5f;
};

UCLASS(BlueprintType)
class ENDLESS_API UEndlessWeaponCatalog : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FEndlessWeaponDefinition> Weapons;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UMaterialInterface> WeaponMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TSubclassOf<UAnimInstance> PistolAnimationClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TSubclassOf<UAnimInstance> RifleAnimationClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UInputAction> CrouchAction;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class USoundBase> PickupSound;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class UEndlessSensorySettings> SensorySettings;
};

USTRUCT(BlueprintType)
struct FEndlessWeaponAmmo
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Magazine = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Reserve = 0;
};

UCLASS()
class ENDLESS_API UEndlessWeaponStatusWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void Refresh(class AEndlessPlayerCharacter* Player);
protected:
    virtual void NativeOnInitialized() override;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
    UPROPERTY(Transient) TObjectPtr<UProgressBar> ReloadProgress;
};

/** 画面中央は固定し、距離別の拡散範囲だけを周囲に描く。 */
UCLASS()
class ENDLESS_API UEndlessWeaponReticle : public UUserWidget
{
    GENERATED_BODY()
protected:
    virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
        FSlateWindowElementList& OutElements, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const override;
};

/** Weapon state is native; the existing Blueprint still owns health, inventory, camera and room interaction. */
UCLASS(Blueprintable)
class ENDLESS_API AEndlessPlayerCharacter : public ACharacter
{
    GENERATED_BODY()
public:
    AEndlessPlayerCharacter();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<class UEndlessSensoryComponent> Sensory;
    virtual void Tick(float DeltaSeconds) override;
    virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera") float CameraBodyClearance = 55.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Camera") bool bCameraShoulderFallback = false;
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Weapons") TObjectPtr<UEndlessWeaponCatalog> WeaponCatalog;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Weapons") TObjectPtr<UStaticMeshComponent> EquippedMesh;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Weapons") TObjectPtr<USceneComponent> WeaponMuzzle;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Weapons") int32 EquippedWeaponIndex = INDEX_NONE;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Weapons") TArray<FEndlessWeaponAmmo> WeaponAmmo;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Weapons") TArray<int32> OwnedWeaponIndices;
    UFUNCTION(BlueprintPure, Category="Weapons") bool OwnsWeapon(int32 Index) const;
    UFUNCTION(BlueprintCallable, Category="Weapons") void InteractWithTarget();
    UFUNCTION(BlueprintCallable, Category="Weapons") void CancelWeaponExchange();
    UFUNCTION(BlueprintPure, Category="Weapons") FText GetInteractionPrompt() const;
    UFUNCTION(BlueprintPure, Category="Weapons") AActor* FindInteractionTarget() const;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 ExchangeSlot = INDEX_NONE;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool bAimNeedsRelease = false;
    UFUNCTION(BlueprintCallable, Category="Weapons") bool AcquireWeapon(FName WeaponId, int32 Magazine, int32 Reserve);
    UFUNCTION(BlueprintPure, Category="Weapons") float GetSpreadAtDistance(float Distance) const;
    UFUNCTION(BlueprintPure, Category="Weapons") FVector GetAimTarget() const;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Weapons") FVector LastAimTarget;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Weapons") float LastShotSpreadDegrees = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Weapons") bool bWeaponReloading = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Weapons") bool bWeaponTriggerHeld = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Weapons") int32 ShotsFired = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 ReloadStartsPlayed = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 ReloadCompletionsPlayed = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 PickupsPlayed = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Weapons") FVector LastShotOrigin;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Weapons") FVector LastShotDirection;
    UFUNCTION(BlueprintCallable, Category="Weapons") bool EquipWeapon(int32 Index);
    UFUNCTION(BlueprintCallable, Category="Weapons") bool BeginWeaponReload();
    UFUNCTION(BlueprintCallable, Category="Weapons") void CancelWeaponReload();
    UFUNCTION(BlueprintCallable, Category="Weapons") bool TryWeaponShot();
    UFUNCTION(BlueprintCallable, Category="Weapons") void StartWeaponFire();
    UFUNCTION(BlueprintCallable, Category="Weapons") void StopWeaponFire();
    UFUNCTION(BlueprintCallable, Category="Weapons") void NextWeapon();
    UFUNCTION(BlueprintCallable, Category="Weapons") void PreviousWeapon();
    UFUNCTION(BlueprintCallable, Category="Weapons") void StartWeaponCrouch();
    UFUNCTION(BlueprintCallable, Category="Weapons") void StopWeaponCrouch();
    UFUNCTION(BlueprintPure, Category="Weapons") float GetReloadRemaining() const;
    UFUNCTION(BlueprintPure, Category="Weapons") FTransform GetWeaponMuzzleTransform() const;
    UFUNCTION(BlueprintPure, Category="Weapons") FText GetWeaponStatus() const;
    UFUNCTION(BlueprintPure, Category="Weapons") FEndlessWeaponAmmo GetEquippedAmmo() const;
    UFUNCTION(BlueprintPure, Category="Weapons") FVector GetSupportHandTarget() const;
    UFUNCTION(BlueprintPure, Category="Weapons") FRotator GetSupportHandRotation() const;
    const FEndlessWeaponDefinition* GetWeaponDefinition() const;
    bool IsWeaponAiming() const;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    void AimPressed();
    void AimReleased();
    void CancelAimForRoll();
    bool bWasRolling = false;
    TWeakObjectPtr<class AEndlessWeaponPickup> ExchangePickup;
    void InitializeWeapons();
    void FinishWeaponReload();
    void ReloadInput();
    bool CanUseWeapon() const;
    void ReadLegacyAmmo();
    void PublishAmmo();
    void UpdateVisuals();
    void CycleWeapon(int32 Direction);
    UPROPERTY(Transient) TObjectPtr<class UAudioComponent> ReloadAudio;
    FTimerHandle ReloadTimer;
    FTimerHandle FireTimer;
    int32 ReloadWeapon = INDEX_NONE;
    float ReloadEndTime = 0.f;
    float NextShotTime = 0.f;
    float NextStatusUpdate = 0.f;
    int32 PublishedMagazine = 0;
    int32 PublishedReserve = 0;
    UPROPERTY(Transient) TObjectPtr<UEndlessWeaponStatusWidget> WeaponStatusWidget;
    UPROPERTY(Transient) TObjectPtr<UAnimMontage> ReloadMontage;
    UPROPERTY(Transient) TObjectPtr<UEndlessWeaponReticle> WeaponReticle;
};

UCLASS(Blueprintable)
class ENDLESS_API UEndlessWeaponAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;
    UPROPERTY(BlueprintReadOnly, Category="Weapons") FVector SupportHandTarget = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category="Weapons") FRotator SupportHandRotation = FRotator::ZeroRotator;
    UPROPERTY(BlueprintReadOnly, Category="Weapons") float SupportHandAlpha = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="Weapons") FVector WeaponCrouchOffset = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category="Weapons") float WeaponCrouchAlpha = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="Weapons") FRotator WeaponAimRotation = FRotator::ZeroRotator;
    UPROPERTY(BlueprintReadOnly, Category="Weapons") float WeaponPoseAlpha = 1.f;
};
