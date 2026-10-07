#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "EndlessSensory.generated.h"

class USoundBase;
class USoundAttenuation;
class USoundConcurrency;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EEndlessNoiseKind : uint8 { Walk, Run, Crouch, Gunshot, Impact };

UCLASS(BlueprintType)
class ENDLESS_API UEndlessSensorySettings : public UDataAsset
{
    GENERATED_BODY()
public:
    // コンクリート、木、金属、タイルの順。各床材に3バリエーション。
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<TObjectPtr<USoundBase>> Footsteps;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<USoundBase> WallImpact;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<USoundBase> HitConfirm;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<USoundAttenuation> Attenuation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<USoundAttenuation> ShotAttenuation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<USoundConcurrency> Concurrency;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UMaterialInterface> BloodMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float WalkRadius = 650.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float RunRadius = 1100.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float CrouchRadius = 180.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float ImpactRadius = 900.f;
};

/** 音の再生と聴覚イベントを同じ発生位置から通知する。 */
UCLASS(ClassGroup=(Endless), meta=(BlueprintSpawnableComponent))
class ENDLESS_API UEndlessSensoryComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UEndlessSensoryComponent();
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UEndlessSensorySettings> Settings;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 StepsPlayed = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 ShotsPlayed = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 ConfirmedHits = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FName LastSurface;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FVector LastStepLocation;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float LastConfirmedHitTime = -100.f;
    UFUNCTION(BlueprintCallable) void PlayShot(USoundBase* Sound, FVector Location, float Radius);
    UFUNCTION(BlueprintCallable) void ConfirmHit(FVector Location, FVector Normal, ULevel* Level);
    UFUNCTION(BlueprintCallable) void PlayWallImpact(FVector Location);
    UFUNCTION(BlueprintCallable) static void ReportNoise(AActor* Source, FVector Location, EEndlessNoiseKind Kind, float Radius);
protected:
    virtual void BeginPlay() override;
private:
    bool Contact[2] = {true,true};
    float LastStepTime = -100.f;
    void PlayAt(USoundBase* Sound, FVector Location, float Volume=1.f, float Pitch=1.f);
};

/** 0.25秒だけ表示する小さな血滴。衝突・ダメージ・ナビへの影響なし。 */
UCLASS()
class ENDLESS_API AEndlessImpactBurst : public AActor
{
    GENERATED_BODY()
public:
    AEndlessImpactBurst();
    void Initialize(UMaterialInterface* Material, FVector Normal);
    virtual void Tick(float DeltaSeconds) override;
private:
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Drops;
    TArray<FVector> Velocities;
    float Age=0.f;
};
