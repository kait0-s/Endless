#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Character.h"
#include "EndlessWeaponLoot.generated.h"

class UEndlessWeaponCatalog;
class USphereComponent;
class UStaticMeshComponent;
class AEndlessPlayerCharacter;

USTRUCT(BlueprintType)
struct FEndlessWeaponDropEntry
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName WeaponId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float Weight = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) int32 Magazine = 10;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) int32 Reserve = 20;
};

UCLASS(BlueprintType)
class ENDLESS_API UEndlessWeaponDropTable : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UEndlessWeaponCatalog> Catalog;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FEndlessWeaponDropEntry> Entries;
};

/** 既存BPのHP・死亡・得点処理を保ち、ダメージによる死亡だけで一度抽選する。 */
UCLASS(Blueprintable)
class ENDLESS_API AEndlessLootEnemy : public ACharacter
{
    GENERATED_BODY()
public:
    AEndlessLootEnemy();
    virtual void BeginPlay() override;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy Loadout") TArray<TObjectPtr<USkeletalMesh>> AppearanceMeshes;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy Loadout") TArray<FName> AllowedWeaponIds;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> EnemyWeaponMesh;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<class UEndlessSensoryComponent> Sensory;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 AppearanceIndex = -1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 EnemyWeaponIndex = -1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 EnemyShotsFired = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 EnemyMagazine = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FVector LastEnemyMuzzle;
    UFUNCTION(BlueprintCallable) bool ConfigureLoadout(int32 Appearance, int32 Weapon);
    UFUNCTION(BlueprintCallable) bool FireEquippedWeapon(APawn* Target);
    UFUNCTION(BlueprintPure) bool IsEnemyReloading() const;
    void ResumeWeaponTimers(float PausedSeconds);
    const struct FEndlessWeaponDefinition* GetEnemyWeapon() const;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Weapon Drops") TObjectPtr<UEndlessWeaponDropTable> WeaponDropTable;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Weapon Drops", meta=(ClampMin="0", ClampMax="1")) float WeaponDropChance = .65f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Weapon Drops") bool bWeaponDropResolved = false;
    virtual float TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
private:
    float NextEnemyShot = 0.f;
    float ReloadEnd = 0.f;
    void ResolveWeaponDrop(const FVector& DeathLocation, ULevel* DeathLevel);
};

UCLASS()
class ENDLESS_API AEndlessWeaponPickup : public AActor
{
    GENERATED_BODY()
public:
    AEndlessWeaponPickup();
    UFUNCTION(BlueprintCallable) void Initialize(UEndlessWeaponCatalog* Catalog, const FEndlessWeaponDropEntry& Entry);
    UFUNCTION(BlueprintPure) bool CanCollect(AEndlessPlayerCharacter* Player) const;
    UFUNCTION(BlueprintCallable) bool TryCollect(AEndlessPlayerCharacter* Player);
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FEndlessWeaponDropEntry Contents;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool bCollected = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<USphereComponent> PickupArea;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> WeaponMesh;
private:
    bool bInitialized = false;
    float AvailableTime = 0.f;
};
