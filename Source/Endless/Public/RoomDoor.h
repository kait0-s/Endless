#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoomDoor.generated.h"

class AEndlessRoomManager;
class AEndlessRoomDoor;
class UCurveFloat;
class UPrimitiveComponent;
class USceneComponent;

UENUM(BlueprintType)
enum class EEndlessRoomDoorRole : uint8
{
    Unassigned UMETA(DisplayName = "Unassigned (migration not complete)"),
    Entry UMETA(DisplayName = "Room Entry"),
    Exit UMETA(DisplayName = "Room Exit / Progression"),
    Auxiliary UMETA(DisplayName = "Auxiliary (no room generation)")
};

UENUM(BlueprintType)
enum class EEndlessDoorSide : uint8
{
    Unknown,
    SideA,
    SideB
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FEndlessDoorOpenedSignature,
    AEndlessRoomDoor*, Door,
    FTransform, RoomSpawnTransform);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FEndlessPreloadRequestedSignature,
    AEndlessRoomDoor*, Door);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FEndlessDoorPassageSignature,
    AEndlessRoomDoor*, Door,
    EEndlessDoorSide, FromSide,
    EEndlessDoorSide, ToSide);

/**
 * Native implementation for the existing BP_RoomDoor.
 *
 * The class intentionally does not create visual or collision components. After BP_RoomDoor
 * is reparented, the existing Blueprint components are resolved by name so their transforms,
 * meshes, and instance overrides remain intact.
 */
UCLASS(Blueprintable)
class ENDLESS_API AEndlessRoomDoor : public AActor
{
    GENERATED_BODY()

public:
    AEndlessRoomDoor();

    virtual void Tick(float DeltaSeconds) override;
    virtual void ProcessEvent(UFunction* Function, void* Parameters) override;

    /** Master switch used during the staged Blueprint-to-C++ migration. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Door|Native Migration")
    bool bNativeDoorLogicEnabled = false;

    /** Explicit per-instance role. Loaded-room mapping rejects ambiguous Entry/Exit roles. */
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Room Door|Room Mapping")
    EEndlessRoomDoorRole NativeDoorRole = EEndlessRoomDoorRole::Unassigned;

    /** Stable identifier unique inside one room level (Entry, Exit_A, Exit_B, ...). */
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Room Door|Room Mapping")
    FName NativeDoorId = NAME_None;

    /** Actor tag of the solid wall piece used when this Exit candidate is disabled. */
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Room Door|Room Mapping")
    FName NativeExitClosureTag = NAME_None;

    /** Read from the Entry door to choose how many Exit candidates are active for this instance. */
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Room Door|Room Mapping", meta = (ClampMin = "1", ClampMax = "3"))
    int32 NativeMinimumActiveExits = 1;

    /** Read from the Entry door to choose how many Exit candidates are active for this instance. */
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Room Door|Room Mapping", meta = (ClampMin = "1", ClampMax = "3"))
    int32 NativeMaximumActiveExits = 1;

    /** Use a map-local socket when the retained management Entry actor is not placed at the wall opening. */
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Room Door|Room Mapping")
    bool bNativeUseEntryAlignmentOverride = false;

    /** Map-local transform whose forward axis points from the previous room into this room. */
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Room Door|Room Mapping", meta = (EditCondition = "bNativeUseEntryAlignmentOverride"))
    FTransform NativeEntryAlignmentOverride = FTransform::Identity;

    /** Project-wide playable interaction distance. Existing placed actors are migrated to this value. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Door|Interaction", meta = (ClampMin = "0.0", Units = "cm"))
    float NativeInteractionDistance = 150.0f;

    /** Height of the interaction target above the door actor origin. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Door|Interaction", meta = (ClampMin = "0.0", Units = "cm"))
    float NativeInteractionTargetHeight = 110.0f;

    /** Minimum horizontal facing dot product required to interact (0.5 = within 60 degrees). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Door|Interaction", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
    float NativeMinFacingDot = 0.5f;

    /** Existing PreloadRange sphere radius confirmed from the asset: 600 cm. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Door|Preload", meta = (ClampMin = "0.0", Units = "cm"))
    float NativePreloadDistance = 600.0f;

    /** Prevents a door on another floor from preloading through the ceiling or floor. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Door|Preload", meta = (ClampMin = "0.0", Units = "cm"))
    float NativePreloadVerticalTolerance = 250.0f;

    /** Maximum vertical separation accepted by the two safe-zone passage sensors. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Door|Passage", meta = (ClampMin = "0.0", Units = "cm"))
    float NativePassageVerticalTolerance = 200.0f;

    /** Existing BP_RoomDoor value confirmed from the asset: 90 degrees. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Door|Animation", meta = (ClampMin = "0.0", Units = "deg"))
    float NativeOpenAngle = 90.0f;

    /** Existing BP_RoomDoor value confirmed from the asset: 0.5 seconds. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Door|Animation", meta = (ClampMin = "0.0", Units = "s"))
    float NativeOpenDuration = 0.5f;

    /** Editable because the legacy graph's SideA/SideB branch wiring must be verified in the Editor. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Door|Animation", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
    float NativeSideAOpenDirection = -1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Door|Animation", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
    float NativeSideBOpenDirection = 1.0f;

    /** Optional normalized 0..1 curve. Linear interpolation is used when this is unset. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Door|Animation")
    TObjectPtr<UCurveFloat> NativeOpenCurve;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Door|Component Binding")
    FName DoorPivotComponentName = TEXT("DoorPivot");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Door|Component Binding")
    FName DoorVisualComponentName = TEXT("DoorMesh");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Door|Component Binding")
    FName RoomSpawnPointComponentName = TEXT("RoomSpawnPoint");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Door|Component Binding")
    FName PreloadRangeComponentName = TEXT("PreloadRange");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Door|Component Binding")
    FName SafeZoneSideAComponentName = TEXT("SafeZone_SideA");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Door|Component Binding")
    FName SafeZoneSideBComponentName = TEXT("SafeZone_SideB");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Door|Component Binding")
    FName InteractionPointSideAComponentName = TEXT("InteractionPoint_SideA");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Door|Component Binding")
    FName InteractionPointSideBComponentName = TEXT("InteractionPoint_SideB");

    /** Optional explicit manager. Streaming-room doors are assigned by the manager. */
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Room Door|Room Mapping")
    TObjectPtr<AEndlessRoomManager> NativeManagerOverride;

    UPROPERTY(BlueprintAssignable, Category = "Room Door|Native Events")
    FEndlessDoorOpenedSignature OnNativeDoorOpened;

    UPROPERTY(BlueprintAssignable, Category = "Room Door|Native Events")
    FEndlessPreloadRequestedSignature OnNativePreloadRequested;

    UPROPERTY(BlueprintAssignable, Category = "Room Door|Native Events")
    FEndlessDoorPassageSignature OnNativePassageCompleted;

    /** Adapter target for BPI_Interactable.Interact. All interaction logic runs in C++. */
    UFUNCTION(BlueprintCallable, Category = "Room Door|Native")
    void NativeInteract(AActor* Interactor);

    /** Shared validation used by Blueprint and native input adapters. */
    UFUNCTION(BlueprintPure, Category = "Room Door|Native")
    bool CanNativeInteract(const AActor* Interactor) const;

    UFUNCTION(BlueprintCallable, Category = "Room Door|Native")
    void NativeToggleDoor(AActor* Interactor);

    UFUNCTION(BlueprintPure, Category = "Room Door|Native")
    bool IsNativeOpen() const { return bNativeIsOpen; }

    UFUNCTION(BlueprintPure, Category = "Room Door|Native")
    bool IsNativeAnimating() const { return bNativeIsAnimating; }

    UFUNCTION(BlueprintPure, Category = "Room Door|Native")
    FTransform GetNativeRoomSpawnTransform() const;

    UFUNCTION(BlueprintPure, Category = "Room Door|Native")
    bool HasNativeRoomSpawnPoint() const { return IsValid(RoomSpawnPointComponent); }

    UFUNCTION(BlueprintPure, Category = "Room Door|Native")
    EEndlessRoomDoorRole GetNativeDoorRole() const { return NativeDoorRole; }

    UFUNCTION(BlueprintPure, Category = "Room Door|Native")
    FName GetNativeDoorId() const { return NativeDoorId; }

    UFUNCTION(BlueprintPure, Category = "Room Door|Native")
    float GetNativePreloadDistance() const { return NativePreloadDistance; }

    UFUNCTION(BlueprintPure, Category = "Room Door|Native")
    float GetNativePreloadVerticalTolerance() const { return NativePreloadVerticalTolerance; }

    /** Valid while a streamed room is loaded but still hidden. */
    FTransform GetNativeEntryAlignmentLocalTransform() const;

    /** Keeps the loaded Entry actor for ownership/mapping while the shared Exit door represents the connection. */
    UFUNCTION(BlueprintCallable, Category = "Room Door|Native")
    void SetNativeConnectionSuppressed(bool bSuppressed);

    UFUNCTION(BlueprintPure, Category = "Room Door|Native")
    bool IsNativeConnectionSuppressed() const { return bNativeConnectionSuppressed; }

    /** Enables a progression candidate or turns it into a sealed, non-interactive wall opening. */
    void SetNativeProgressionEnabled(bool bEnabled);

    bool IsNativeProgressionEnabled() const { return bNativeProgressionEnabled; }

    /** Immediately returns the door to its closed state when an unchosen preload is discarded. */
    void ResetNativeDoorToClosed();

    void SetNativeRoomManager(AEndlessRoomManager* InManager);
    void CompleteNativeQueuedOpen(AActor* Interactor);

protected:
    virtual void BeginPlay() override;

private:
    UPROPERTY(Transient)
    TObjectPtr<AEndlessRoomManager> NativeRoomManager;

    UPROPERTY(Transient)
    TObjectPtr<USceneComponent> DoorPivotComponent;

    UPROPERTY(Transient)
    TObjectPtr<UPrimitiveComponent> DoorVisualComponent;

    UPROPERTY(Transient)
    TObjectPtr<USceneComponent> RoomSpawnPointComponent;

    UPROPERTY(Transient)
    TObjectPtr<UPrimitiveComponent> PreloadRangeComponent;

    UPROPERTY(Transient)
    TObjectPtr<UPrimitiveComponent> SafeZoneSideAComponent;

    UPROPERTY(Transient)
    TObjectPtr<UPrimitiveComponent> SafeZoneSideBComponent;

    UPROPERTY(Transient)
    TObjectPtr<USceneComponent> InteractionPointSideAComponent;

    UPROPERTY(Transient)
    TObjectPtr<USceneComponent> InteractionPointSideBComponent;

    FRotator NativeClosedRotation = FRotator::ZeroRotator;
    FRotator NativeAnimationStartRotation = FRotator::ZeroRotator;
    FRotator NativeAnimationTargetRotation = FRotator::ZeroRotator;
    float NativeAnimationElapsed = 0.0f;
    float NativeOpenDirection = 1.0f;
    bool bNativeIsOpen = false;
    bool bNativeIsAnimating = false;
    bool bNativeAnimationOpening = false;
    bool bNativeOpenedEventSent = false;
    bool bPassageDebounce = false;
    bool bNativeConnectionSuppressed = false;
    bool bNativeProgressionEnabled = true;
    EEndlessDoorSide LastSafeZoneSide = EEndlessDoorSide::Unknown;

    void ResolveExistingComponents();
    void BindNativeCollisionEvents();
    void FindAndRegisterWithManager();
    void StartNativeDoorAnimation(bool bOpen, AActor* Interactor);
    void FinishNativeDoorAnimation();
    float DetermineOpenDirection(const AActor* Interactor) const;
    bool IsPlayerControlledCharacter(const AActor* Actor) const;
    USceneComponent* FindSceneComponentByName(FName ComponentName) const;
    UPrimitiveComponent* FindPrimitiveComponentByName(FName ComponentName) const;
    void HandleSafeZoneEntered(EEndlessDoorSide EnteredSide, AActor* OtherActor);
    void ClearPassageDebounce();

    UFUNCTION()
    void HandlePreloadRangeBeginOverlap(
        UPrimitiveComponent* OverlappedComponent,
        AActor* OtherActor,
        UPrimitiveComponent* OtherComponent,
        int32 OtherBodyIndex,
        bool bFromSweep,
        const FHitResult& SweepResult);

    UFUNCTION()
    void HandleSafeZoneSideABeginOverlap(
        UPrimitiveComponent* OverlappedComponent,
        AActor* OtherActor,
        UPrimitiveComponent* OtherComponent,
        int32 OtherBodyIndex,
        bool bFromSweep,
        const FHitResult& SweepResult);

    UFUNCTION()
    void HandleSafeZoneSideBBeginOverlap(
        UPrimitiveComponent* OverlappedComponent,
        AActor* OtherActor,
        UPrimitiveComponent* OtherComponent,
        int32 OtherBodyIndex,
        bool bFromSweep,
        const FHitResult& SweepResult);
};
