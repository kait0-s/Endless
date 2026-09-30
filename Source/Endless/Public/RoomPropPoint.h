#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoomPropPoint.generated.h"

class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * A validated candidate point for room furniture or props.
 *
 * The generated mesh component belongs to this actor and therefore to the streamed ULevel. It is
 * automatically destroyed with the room instance and cannot leak into another instance.
 */
UCLASS(Blueprintable)
class ENDLESS_API AEndlessRoomPropPoint : public AActor
{
    GENERATED_BODY()

public:
    AEndlessRoomPropPoint();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Content|Random")
    bool bNativeRandomPlacementEnabled = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Content|Random", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float NativeSpawnChance = 1.0f;

    /** Uniformly selected mesh candidates. Empty/null entries safely result in no generated prop. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Content|Candidates")
    TArray<TSoftObjectPtr<UStaticMesh>> NativeMeshOptions;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Content|Placement")
    FTransform NativeMeshRelativeTransform = FTransform::Identity;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Content|Placement", meta = (ClampMin = "1", ClampMax = "36"))
    int32 NativeRandomYawSteps = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Content|Random")
    int32 NativeSeedOffset = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room Content|Components")
    TObjectPtr<UStaticMeshComponent> GeneratedMesh;

    UFUNCTION(BlueprintCallable, Category = "Room Content|Native")
    bool GenerateNativeProp(int32 Seed);

    UFUNCTION(BlueprintCallable, Category = "Room Content|Native")
    void ClearNativeProp();

protected:
    virtual void BeginPlay() override;

private:
    UPROPERTY()
    TObjectPtr<USceneComponent> SceneRoot;
};
