#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoomRuntimeContent.generated.h"

class APawn;
class USceneComponent;

UENUM(BlueprintType)
enum class EEndlessRoomSpawnKind : uint8
{
    Enemy,
    HealingItem
};

/**
 * An editor-authored, collision-free candidate location owned by a room level instance.
 */
UCLASS(Blueprintable)
class ENDLESS_API AEndlessRoomSpawnPoint : public AActor
{
    GENERATED_BODY()

public:
    AEndlessRoomSpawnPoint();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Content|Spawn")
    EEndlessRoomSpawnKind NativeSpawnKind = EEndlessRoomSpawnKind::Enemy;

    /** Added to the authored floor-surface location before spawning. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Content|Spawn", meta = (Units = "cm"))
    float NativeVerticalOffset = 96.0f;

    /** Runtime clearance check around this point. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Content|Safety", meta = (ClampMin = "0.0", Units = "cm"))
    float NativeClearanceRadius = 45.0f;

private:
    UPROPERTY(VisibleAnywhere, Category = "Room Content|Components")
    TObjectPtr<USceneComponent> SceneRoot;
};

/**
 * Per-template limits and class choices. Exactly one is expected in every managed room level.
 */
UCLASS(Blueprintable)
class ENDLESS_API AEndlessRoomContentConfig : public AActor
{
    GENERATED_BODY()

public:
    AEndlessRoomContentConfig();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Content|Runtime")
    bool bNativeRuntimeContentEnabled = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Content|Enemies", meta = (ClampMin = "0", ClampMax = "20"))
    int32 NativeMaxEnemies = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Content|Enemies")
    TArray<TSubclassOf<APawn>> NativeEnemyClasses;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Content|Healing", meta = (ClampMin = "0", ClampMax = "10"))
    int32 NativeMaxHealingItems = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Content|Healing")
    TArray<TSubclassOf<AActor>> NativeHealingItemClasses;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Content|Safety", meta = (ClampMin = "0.0", Units = "cm"))
    float NativeMinimumDoorDistance = 350.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Content|Safety", meta = (ClampMin = "0.0", Units = "cm"))
    float NativeMinimumPlayerDistance = 450.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Content|Safety", meta = (ClampMin = "0.0", Units = "cm"))
    float NativeMinimumSpawnSeparation = 180.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Content|Navigation")
    bool bNativeRequireNavigationForEnemies = true;

private:
    UPROPERTY(VisibleAnywhere, Category = "Room Content|Components")
    TObjectPtr<USceneComponent> SceneRoot;
};
