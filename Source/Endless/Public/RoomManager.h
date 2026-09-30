#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoomDoor.h"
#include "RoomManager.generated.h"

class ULevel;
class ULevelStreamingDynamic;
class UWorld;

USTRUCT(BlueprintType)
struct ENDLESS_API FEndlessExitCandidate
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Room Exit")
    FName ExitId = NAME_None;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Room Exit")
    TObjectPtr<AEndlessRoomDoor> Door;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Room Exit")
    TObjectPtr<AActor> ClosureActor;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Room Exit")
    TArray<TSoftObjectPtr<UWorld>> CandidateLevels;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Room Exit")
    int32 CandidateIndex = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Room Exit")
    bool bActiveForInstance = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Room Exit")
    bool bLocked = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Room Exit")
    FTransform ConnectionTransform = FTransform::Identity;

    bool HasCandidate() const
    {
        return CandidateLevels.IsValidIndex(CandidateIndex)
            && !CandidateLevels[CandidateIndex].IsNull();
    }

    TSoftObjectPtr<UWorld> GetCandidate() const
    {
        return HasCandidate() ? CandidateLevels[CandidateIndex] : TSoftObjectPtr<UWorld>();
    }
};

USTRUCT(BlueprintType)
struct ENDLESS_API FEndlessRoomInstance
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Room")
    TSoftObjectPtr<UWorld> LevelAsset;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Room")
    TObjectPtr<ULevelStreamingDynamic> StreamingLevel;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Room")
    TObjectPtr<ULevel> LoadedLevel;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Room")
    TObjectPtr<AEndlessRoomDoor> EntryDoor;

    /** Physical shared door in the previous room. The streamed Entry actor remains suppressed. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Room")
    TObjectPtr<AEndlessRoomDoor> BackDoor;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Room")
    TArray<FEndlessExitCandidate> ExitCandidates;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Room")
    FName CommittedExitId = NAME_None;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Room")
    bool bPersistentRoom = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Room")
    bool bReady = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Room")
    bool bDoorsResolved = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Room")
    FTransform InstanceTransform = FTransform::Identity;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Room")
    FTransform BackConnectionTransform = FTransform::Identity;

    /** Tagged floor, wall, stair, and guard-rail boxes in world space. */
    TArray<FBox> WorldOccupancy;

    bool HasRoom() const
    {
        return bPersistentRoom || StreamingLevel != nullptr;
    }

    void Reset()
    {
        LevelAsset.Reset();
        StreamingLevel = nullptr;
        LoadedLevel = nullptr;
        EntryDoor = nullptr;
        BackDoor = nullptr;
        ExitCandidates.Reset();
        CommittedExitId = NAME_None;
        bPersistentRoom = false;
        bReady = false;
        bDoorsResolved = false;
        InstanceTransform = FTransform::Identity;
        BackConnectionTransform = FTransform::Identity;
        WorldOccupancy.Reset();
    }
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FEndlessRoomInstanceEvent,
    ULevelStreamingDynamic*, StreamingLevel);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FEndlessRoomLoadFailureEvent,
    AEndlessRoomDoor*, Door,
    FString, Reason);

/**
 * Native three-room streaming state machine with one Entry and one-to-three Exit candidates.
 *
 * Previous, Current, and Next are explicit slots. Every streamed actor lookup is constrained to
 * the owning ULevel, and multi-floor occupancy is evaluated as tagged 3D geometry boxes.
 */
UCLASS(Blueprintable)
class ENDLESS_API AEndlessRoomManager : public AActor
{
    GENERATED_BODY()

public:
    AEndlessRoomManager();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Manager|Native Migration")
    bool bNativeRoomManagerEnabled = false;

    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Room Manager|Rooms")
    TObjectPtr<AEndlessRoomDoor> NativeInitialDoor;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Manager|Rooms")
    TArray<TSoftObjectPtr<UWorld>> NativeRoomLevels;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Manager|Loading", meta = (ClampMin = "0.0", Units = "s"))
    float NativeLoadTimeoutSeconds = 15.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Manager|Loading", meta = (ClampMin = "0.02", Units = "s"))
    float NativePreloadPollInterval = 0.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Manager|Placement", meta = (Units = "deg"))
    float NativeEntryFacingYawOffset = 0.0f;

    /** Tagged floor actors are the authoritative occupancy pieces for rectangles and compound rooms. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Manager|Placement")
    FName NativeFloorActorTag = TEXT("EndlessRoomFloor");

    /** Tagged floor, wall, stair, landing, and guard-rail actors form the authoritative 3D occupancy. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Manager|Placement")
    FName NativeOccupancyActorTag = TEXT("EndlessRoomOccupancy");

    /** Edge contact is allowed; only interior overlap greater than this tolerance is rejected. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Manager|Placement", meta = (ClampMin = "0.0", Units = "cm"))
    float NativeFootprintOverlapTolerance = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Manager|Random")
    bool bUseFixedNativeRandomSeed = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Manager|Random", meta = (EditCondition = "bUseFixedNativeRandomSeed"))
    int32 NativeRandomSeed = 12345;

    UPROPERTY(BlueprintAssignable, Category = "Room Manager|Native Events")
    FEndlessRoomInstanceEvent OnNativeNextRoomReady;

    UPROPERTY(BlueprintAssignable, Category = "Room Manager|Native Events")
    FEndlessRoomInstanceEvent OnNativeRoomBecameCurrent;

    UPROPERTY(BlueprintAssignable, Category = "Room Manager|Native Events")
    FEndlessRoomLoadFailureEvent OnNativeRoomLoadFailed;

    UFUNCTION(BlueprintCallable, Category = "Room Manager|Native")
    void RegisterNativeDoor(AEndlessRoomDoor* Door);

    /** Returns true only when the door may animate open immediately. False means closed/queued. */
    bool RequestNativeDoorOpen(AEndlessRoomDoor* Door, AActor* Interactor);

    UFUNCTION(BlueprintPure, Category = "Room Manager|Native")
    bool IsNativeRoomManagerActive() const { return bNativeRoomManagerEnabled; }

    UFUNCTION(BlueprintPure, Category = "Room Manager|Native")
    FEndlessRoomInstance GetNativePreviousRoom() const { return PreviousRoom; }

    UFUNCTION(BlueprintPure, Category = "Room Manager|Native")
    FEndlessRoomInstance GetNativeCurrentRoom() const { return CurrentRoom; }

    UFUNCTION(BlueprintPure, Category = "Room Manager|Native")
    FEndlessRoomInstance GetNativeNextRoom() const { return NextRoom; }

    UFUNCTION(BlueprintPure, Category = "Room Manager|Native")
    int32 GetNativeActiveExitCount() const;

    UFUNCTION(BlueprintPure, Category = "Room Manager|Native")
    int32 GetNativeRealizedRoomCount() const;

protected:
    virtual void BeginPlay() override;

private:
    UPROPERTY(VisibleInstanceOnly, Transient, Category = "Room Manager|Runtime")
    FEndlessRoomInstance PreviousRoom;

    UPROPERTY(VisibleInstanceOnly, Transient, Category = "Room Manager|Runtime")
    FEndlessRoomInstance CurrentRoom;

    UPROPERTY(VisibleInstanceOnly, Transient, Category = "Room Manager|Runtime")
    FEndlessRoomInstance NextRoom;

    TWeakObjectPtr<AEndlessRoomDoor> QueuedOpenDoor;
    TWeakObjectPtr<AActor> QueuedInteractor;
    FRandomStream NativeRandomStream;
    FTimerHandle NativeLoadTimeoutHandle;
    FTimerHandle NativePreloadPollHandle;
    int32 NativeInstanceSerial = 0;

    void InitializePersistentRoom();
    bool ConfigureLoadedRoom(FEndlessRoomInstance& Room, FString& OutError);
    void PrepareCandidatesForRoom(FEndlessRoomInstance& Room);
    void ShuffleLevels(TArray<TSoftObjectPtr<UWorld>>& Levels);
    bool BeginPreloadForDoor(AEndlessRoomDoor* Door);
    void ReleasePreloadedNextRoomForSwitch();
    bool ResolveAndAlignDoorsForNextRoom(FString& OutError);
    bool BuildWorldOccupancy(
        ULevel* Level,
        const FTransform& InstanceTransform,
        TArray<FBox>& OutOccupancy,
        FString& OutError) const;
    bool OccupancyOverlapsRetainedRooms(const TArray<FBox>& CandidateOccupancy) const;
    static bool BoxesOverlapInterior3D(const FBox& A, const FBox& B, float Tolerance);
    AActor* FindTaggedActorInLevel(ULevel* Level, FName Tag) const;
    FEndlessExitCandidate* FindExitCandidate(FEndlessRoomInstance& Room, const AEndlessRoomDoor* Door);
    const FEndlessExitCandidate* FindExitCandidate(
        const FEndlessRoomInstance& Room,
        const AEndlessRoomDoor* Door) const;
    void ApplyExitState(FEndlessExitCandidate& Exit, bool bEnabled);
    void CommitExitChoice(AEndlessRoomDoor* Door);
    void RestoreExitChoices(FEndlessRoomInstance& Room);
    void CompleteQueuedDoorOpen();
    void FailNextRoomLoad(AEndlessRoomDoor* Door, const FString& Reason);
    void ReleaseRoom(FEndlessRoomInstance& Room);
    void AdvanceForward();
    void MoveBackward();
    bool IsCurrentExitDoor(const AEndlessRoomDoor* Door) const;
    bool IsCurrentBackDoor(const AEndlessRoomDoor* Door) const;
    void CheckNativePreloadDistance();

    UFUNCTION()
    void HandleNativePreloadRequested(AEndlessRoomDoor* Door);

    UFUNCTION()
    void HandleNativeDoorOpened(AEndlessRoomDoor* Door, FTransform RoomSpawnTransform);

    UFUNCTION()
    void HandleNativePassageCompleted(
        AEndlessRoomDoor* Door,
        EEndlessDoorSide FromSide,
        EEndlessDoorSide ToSide);

    UFUNCTION()
    void HandleNextLevelLoaded();

    UFUNCTION()
    void HandleNextLevelShown();

    UFUNCTION()
    void HandleNextLevelLoadTimeout();
};
