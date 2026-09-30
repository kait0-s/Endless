#include "RoomManager.h"

#include "Components/PrimitiveComponent.h"
#include "Endless.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "TimerManager.h"

AEndlessRoomManager::AEndlessRoomManager()
{
    PrimaryActorTick.bCanEverTick = false;
}

void AEndlessRoomManager::BeginPlay()
{
    Super::BeginPlay();

    if (!bNativeRoomManagerEnabled)
    {
        return;
    }

    if (bUseFixedNativeRandomSeed)
    {
        NativeRandomStream.Initialize(NativeRandomSeed);
    }
    else
    {
        NativeRandomStream.GenerateNewSeed();
    }

    if (!NativeInitialDoor)
    {
        UE_LOG(
            LogEndlessRoomSystem,
            Error,
            TEXT("%s: NativeInitialDoor is not set; native room progression cannot start."),
            *GetPathName());
        return;
    }

    InitializePersistentRoom();
    PrepareCandidatesForRoom(CurrentRoom);

    GetWorldTimerManager().SetTimer(
        NativePreloadPollHandle,
        this,
        &AEndlessRoomManager::CheckNativePreloadDistance,
        FMath::Max(NativePreloadPollInterval, 0.02f),
        true,
        0.0f);
}

void AEndlessRoomManager::InitializePersistentRoom()
{
    CurrentRoom.Reset();
    CurrentRoom.bPersistentRoom = true;
    CurrentRoom.bReady = true;
    CurrentRoom.bDoorsResolved = true;
    CurrentRoom.LoadedLevel = NativeInitialDoor->GetLevel();

    RegisterNativeDoor(NativeInitialDoor);

    FEndlessExitCandidate InitialExit;
    InitialExit.ExitId = NativeInitialDoor->NativeDoorId.IsNone()
        ? FName(TEXT("StartExit"))
        : NativeInitialDoor->NativeDoorId;
    InitialExit.Door = NativeInitialDoor;
    InitialExit.bActiveForInstance = true;
    InitialExit.ConnectionTransform = NativeInitialDoor->GetNativeRoomSpawnTransform();
    CurrentRoom.ExitCandidates.Add(MoveTemp(InitialExit));

    FString OccupancyError;
    if (!BuildWorldOccupancy(
            CurrentRoom.LoadedLevel,
            FTransform::Identity,
            CurrentRoom.WorldOccupancy,
            OccupancyError))
    {
        UE_LOG(
            LogEndlessRoomSystem,
            Error,
            TEXT("%s: start-room occupancy is invalid: %s"),
            *GetPathName(),
            *OccupancyError);
    }
}

void AEndlessRoomManager::RegisterNativeDoor(AEndlessRoomDoor* Door)
{
    if (!IsValid(Door))
    {
        return;
    }

    Door->SetNativeRoomManager(this);

    if (!bNativeRoomManagerEnabled)
    {
        return;
    }

    Door->OnNativePreloadRequested.AddUniqueDynamic(
        this, &AEndlessRoomManager::HandleNativePreloadRequested);
    Door->OnNativeDoorOpened.AddUniqueDynamic(
        this, &AEndlessRoomManager::HandleNativeDoorOpened);
    Door->OnNativePassageCompleted.AddUniqueDynamic(
        this, &AEndlessRoomManager::HandleNativePassageCompleted);
}

bool AEndlessRoomManager::RequestNativeDoorOpen(AEndlessRoomDoor* Door, AActor* Interactor)
{
    if (!bNativeRoomManagerEnabled || !IsValid(Door))
    {
        return false;
    }

    if (FEndlessExitCandidate* Exit = FindExitCandidate(CurrentRoom, Door))
    {
        if (!Exit->bActiveForInstance || Exit->bLocked || !Exit->HasCandidate())
        {
            return false;
        }

        if (NextRoom.HasRoom() && NextRoom.BackDoor == Door && NextRoom.bReady)
        {
            CommitExitChoice(Door);
            return true;
        }

        QueuedOpenDoor = Door;
        QueuedInteractor = Interactor;
        BeginPreloadForDoor(Door);
        return false;
    }

    if (IsCurrentBackDoor(Door))
    {
        return PreviousRoom.HasRoom() && PreviousRoom.bReady;
    }

    if (Door->GetNativeDoorRole() == EEndlessRoomDoorRole::Auxiliary)
    {
        return true;
    }

    UE_LOG(
        LogEndlessRoomSystem,
        Verbose,
        TEXT("%s: ignored open request from unrelated or disabled door %s."),
        *GetPathName(),
        *Door->GetPathName());
    return false;
}

bool AEndlessRoomManager::ConfigureLoadedRoom(FEndlessRoomInstance& Room, FString& OutError)
{
    if (!Room.LoadedLevel)
    {
        OutError = TEXT("Loaded room has no ULevel.");
        return false;
    }

    AEndlessRoomDoor* EntryDoor = nullptr;
    TArray<AEndlessRoomDoor*> ExitDoors;
    int32 EntryCount = 0;

    for (AActor* Actor : Room.LoadedLevel->Actors)
    {
        AEndlessRoomDoor* Door = Cast<AEndlessRoomDoor>(Actor);
        if (!Door)
        {
            continue;
        }

        RegisterNativeDoor(Door);
        if (Door->GetNativeDoorRole() == EEndlessRoomDoorRole::Entry)
        {
            EntryDoor = Door;
            ++EntryCount;
        }
        else if (Door->GetNativeDoorRole() == EEndlessRoomDoorRole::Exit)
        {
            ExitDoors.Add(Door);
        }
    }

    if (EntryCount != 1 || !EntryDoor || ExitDoors.IsEmpty() || ExitDoors.Num() > 3)
    {
        OutError = FString::Printf(
            TEXT("Loaded room requires exactly one Entry and one-to-three Exit doors (Entry=%d, Exit=%d)."),
            EntryCount,
            ExitDoors.Num());
        return false;
    }

    ExitDoors.Sort([](const AEndlessRoomDoor& A, const AEndlessRoomDoor& B)
    {
        return A.NativeDoorId.ToString() < B.NativeDoorId.ToString();
    });

    TSet<FName> UsedIds;
    for (const AEndlessRoomDoor* ExitDoor : ExitDoors)
    {
        if (!ExitDoor || ExitDoor->NativeDoorId.IsNone() || UsedIds.Contains(ExitDoor->NativeDoorId))
        {
            OutError = TEXT("Every Exit door must have a non-empty ID unique inside its level instance.");
            return false;
        }
        if (ExitDoor->NativeExitClosureTag.IsNone())
        {
            OutError = FString::Printf(
                TEXT("Exit %s has no closure actor tag."),
                *ExitDoor->NativeDoorId.ToString());
            return false;
        }
        UsedIds.Add(ExitDoor->NativeDoorId);
    }

    const int32 MinimumActive = FMath::Clamp(
        EntryDoor->NativeMinimumActiveExits, 1, ExitDoors.Num());
    const int32 MaximumActive = FMath::Clamp(
        EntryDoor->NativeMaximumActiveExits, MinimumActive, ExitDoors.Num());
    const int32 ActiveCount = NativeRandomStream.RandRange(MinimumActive, MaximumActive);

    TArray<int32> ShuffledIndices;
    for (int32 Index = 0; Index < ExitDoors.Num(); ++Index)
    {
        ShuffledIndices.Add(Index);
    }
    for (int32 Index = ShuffledIndices.Num() - 1; Index > 0; --Index)
    {
        ShuffledIndices.Swap(Index, NativeRandomStream.RandRange(0, Index));
    }

    TSet<int32> ActiveIndices;
    for (int32 Index = 0; Index < ActiveCount; ++Index)
    {
        ActiveIndices.Add(ShuffledIndices[Index]);
    }

    Room.EntryDoor = EntryDoor;
    Room.ExitCandidates.Reset();
    for (int32 Index = 0; Index < ExitDoors.Num(); ++Index)
    {
        AEndlessRoomDoor* ExitDoor = ExitDoors[Index];
        AActor* ClosureActor = FindTaggedActorInLevel(
            Room.LoadedLevel,
            ExitDoor->NativeExitClosureTag);
        if (!ClosureActor)
        {
            OutError = FString::Printf(
                TEXT("Exit %s closure actor tagged '%s' was not found in its owning level."),
                *ExitDoor->NativeDoorId.ToString(),
                *ExitDoor->NativeExitClosureTag.ToString());
            return false;
        }

        FEndlessExitCandidate Exit;
        Exit.ExitId = ExitDoor->NativeDoorId;
        Exit.Door = ExitDoor;
        Exit.ClosureActor = ClosureActor;
        Exit.bActiveForInstance = ActiveIndices.Contains(Index);
        Exit.ConnectionTransform = ExitDoor->GetNativeRoomSpawnTransform();
        Room.ExitCandidates.Add(MoveTemp(Exit));
    }

    for (FEndlessExitCandidate& Exit : Room.ExitCandidates)
    {
        ApplyExitState(Exit, Exit.bActiveForInstance);
    }

    Room.bDoorsResolved = true;
    UE_LOG(
        LogEndlessRoomSystem,
        Log,
        TEXT("%s: configured %d active Exit(s) from %d candidate(s) in %s."),
        *GetPathName(),
        ActiveCount,
        ExitDoors.Num(),
        *Room.LevelAsset.ToSoftObjectPath().ToString());
    return true;
}

void AEndlessRoomManager::PrepareCandidatesForRoom(FEndlessRoomInstance& Room)
{
    for (FEndlessExitCandidate& Exit : Room.ExitCandidates)
    {
        if (!Exit.bActiveForInstance || !Exit.CandidateLevels.IsEmpty())
        {
            continue;
        }

        for (const TSoftObjectPtr<UWorld>& Candidate : NativeRoomLevels)
        {
            if (Candidate.IsNull())
            {
                continue;
            }
            if (!Room.LevelAsset.IsNull()
                && Candidate.ToSoftObjectPath() == Room.LevelAsset.ToSoftObjectPath())
            {
                continue;
            }
            Exit.CandidateLevels.Add(Candidate);
        }
        ShuffleLevels(Exit.CandidateLevels);
        Exit.CandidateIndex = 0;

        if (!Exit.HasCandidate())
        {
            Exit.bLocked = true;
            ApplyExitState(Exit, false);
            UE_LOG(
                LogEndlessRoomSystem,
                Error,
                TEXT("%s: Exit %s has no usable next-room candidates and was sealed."),
                *GetPathName(),
                *Exit.ExitId.ToString());
        }
        else
        {
            Exit.ConnectionTransform = Exit.Door->GetNativeRoomSpawnTransform();
            UE_LOG(
                LogEndlessRoomSystem,
                Log,
                TEXT("%s: Exit %s prepared %d ordered room candidate(s); first is %s."),
                *GetPathName(),
                *Exit.ExitId.ToString(),
                Exit.CandidateLevels.Num(),
                *Exit.GetCandidate().ToSoftObjectPath().ToString());
        }
    }
}

void AEndlessRoomManager::ShuffleLevels(TArray<TSoftObjectPtr<UWorld>>& Levels)
{
    for (int32 Index = Levels.Num() - 1; Index > 0; --Index)
    {
        Levels.Swap(Index, NativeRandomStream.RandRange(0, Index));
    }
}

bool AEndlessRoomManager::BeginPreloadForDoor(AEndlessRoomDoor* Door)
{
    FEndlessExitCandidate* Exit = FindExitCandidate(CurrentRoom, Door);
    if (!Exit || !Exit->bActiveForInstance || Exit->bLocked)
    {
        return false;
    }

    if (NextRoom.HasRoom())
    {
        if (NextRoom.BackDoor == Door)
        {
            return true;
        }
        ReleasePreloadedNextRoomForSwitch();
    }

    if (!Exit->HasCandidate())
    {
        Exit->bLocked = true;
        ApplyExitState(*Exit, false);
        return false;
    }

    if (!Door->HasNativeRoomSpawnPoint())
    {
        FailNextRoomLoad(Door, TEXT("RoomSpawnPoint component was not resolved."));
        return false;
    }

    Exit->ConnectionTransform = Door->GetNativeRoomSpawnTransform();
    const TSoftObjectPtr<UWorld> CandidateLevel = Exit->GetCandidate();
    bool bLoadStarted = false;
    const FString InstanceName = FString::Printf(
        TEXT("EndlessRoomInstance_%d_%s"),
        ++NativeInstanceSerial,
        *Exit->ExitId.ToString());
    const FString LevelPackageName = FPackageName::ObjectPathToPackageName(
        CandidateLevel.ToSoftObjectPath().ToString());
    ULevelStreamingDynamic::FLoadLevelInstanceParams LoadParams(
        GetWorld(), LevelPackageName, FTransform::Identity);
    LoadParams.OptionalLevelNameOverride = &InstanceName;
    LoadParams.bInitiallyVisible = false;
    ULevelStreamingDynamic* StreamingLevel = ULevelStreamingDynamic::LoadLevelInstance(
        LoadParams, bLoadStarted);

    if (!bLoadStarted || !StreamingLevel)
    {
        FailNextRoomLoad(Door, TEXT("LoadLevelInstance failed to start."));
        return false;
    }

    NextRoom.Reset();
    NextRoom.LevelAsset = CandidateLevel;
    NextRoom.StreamingLevel = StreamingLevel;
    NextRoom.BackDoor = Door;
    NextRoom.BackConnectionTransform = Exit->ConnectionTransform;

    StreamingLevel->OnLevelLoaded.AddUniqueDynamic(
        this, &AEndlessRoomManager::HandleNextLevelLoaded);
    StreamingLevel->OnLevelShown.AddUniqueDynamic(
        this, &AEndlessRoomManager::HandleNextLevelShown);

    if (NativeLoadTimeoutSeconds > 0.0f)
    {
        GetWorldTimerManager().SetTimer(
            NativeLoadTimeoutHandle,
            this,
            &AEndlessRoomManager::HandleNextLevelLoadTimeout,
            NativeLoadTimeoutSeconds,
            false);
    }

    UE_LOG(
        LogEndlessRoomSystem,
        Log,
        TEXT("%s: preloading %s for Exit %s from owning door %s."),
        *GetPathName(),
        *CandidateLevel.ToSoftObjectPath().ToString(),
        *Exit->ExitId.ToString(),
        *Door->GetPathName());
    return true;
}

void AEndlessRoomManager::ReleasePreloadedNextRoomForSwitch()
{
    AEndlessRoomDoor* PreviousPreloadDoor = NextRoom.BackDoor;
    GetWorldTimerManager().ClearTimer(NativeLoadTimeoutHandle);
    ReleaseRoom(NextRoom);
    if (IsValid(PreviousPreloadDoor))
    {
        PreviousPreloadDoor->ResetNativeDoorToClosed();
    }
    QueuedOpenDoor.Reset();
    QueuedInteractor.Reset();
}

bool AEndlessRoomManager::ResolveAndAlignDoorsForNextRoom(FString& OutError)
{
    if (!NextRoom.StreamingLevel)
    {
        OutError = TEXT("Next room has no streaming level object.");
        return false;
    }

    NextRoom.LoadedLevel = NextRoom.StreamingLevel->GetLoadedLevel();
    if (!NextRoom.LoadedLevel)
    {
        OutError = TEXT("Streaming level reported loaded without a ULevel.");
        return false;
    }

    if (!ConfigureLoadedRoom(NextRoom, OutError))
    {
        return false;
    }

    if (!IsValid(NextRoom.BackDoor) || !NextRoom.BackDoor->HasNativeRoomSpawnPoint())
    {
        OutError = TEXT("The connecting door or its RoomSpawnPoint is invalid.");
        return false;
    }

    FTransform DesiredEntryTransform = NextRoom.BackConnectionTransform;
    if (!FMath::IsNearlyZero(NativeEntryFacingYawOffset))
    {
        DesiredEntryTransform.ConcatenateRotation(
            FRotator(0.0f, NativeEntryFacingYawOffset, 0.0f).Quaternion());
        DesiredEntryTransform.NormalizeRotation();
    }

    const FTransform EntryLocalTransform = NextRoom.EntryDoor->GetNativeEntryAlignmentLocalTransform();
    FTransform InstanceTransform = EntryLocalTransform.Inverse() * DesiredEntryTransform;
    InstanceTransform.SetScale3D(FVector::OneVector);
    NextRoom.InstanceTransform = InstanceTransform;

    if (!BuildWorldOccupancy(
            NextRoom.LoadedLevel,
            InstanceTransform,
            NextRoom.WorldOccupancy,
            OutError))
    {
        return false;
    }
    if (OccupancyOverlapsRetainedRooms(NextRoom.WorldOccupancy))
    {
        OutError = TEXT("Candidate 3D occupancy overlaps the retained Current or Previous room.");
        return false;
    }

    NextRoom.StreamingLevel->LevelTransform = InstanceTransform;
    NextRoom.EntryDoor->SetNativeConnectionSuppressed(true);
    NextRoom.StreamingLevel->SetShouldBeVisible(true);

    UE_LOG(
        LogEndlessRoomSystem,
        Log,
        TEXT("%s: aligned %s Entry %s to Exit %s (occupancy boxes=%d, transform=%s)."),
        *GetPathName(),
        *NextRoom.LevelAsset.ToSoftObjectPath().ToString(),
        *NextRoom.EntryDoor->GetPathName(),
        *NextRoom.BackDoor->GetNativeDoorId().ToString(),
        NextRoom.WorldOccupancy.Num(),
        *InstanceTransform.ToHumanReadableString());
    return true;
}

bool AEndlessRoomManager::BuildWorldOccupancy(
    ULevel* Level,
    const FTransform& InstanceTransform,
    TArray<FBox>& OutOccupancy,
    FString& OutError) const
{
    OutOccupancy.Reset();
    if (!Level)
    {
        OutError = TEXT("Cannot extract occupancy from a null level.");
        return false;
    }

    for (AActor* Actor : Level->Actors)
    {
        if (!IsValid(Actor) || !Actor->ActorHasTag(NativeOccupancyActorTag))
        {
            continue;
        }

        // A level instance is deliberately kept hidden until placement is accepted. Its
        // primitive components are therefore not registered yet and their cached Bounds are
        // invalid. CalcBounds derives bounds from the asset using the component transform and
        // is safe before registration.
        FBox Bounds(ForceInit);
        TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
        Actor->GetComponents(PrimitiveComponents);
        for (const UPrimitiveComponent* Component : PrimitiveComponents)
        {
            if (IsValid(Component))
            {
                Bounds += Component->CalcBounds(Component->GetComponentTransform()).GetBox();
            }
        }
        if (!Bounds.IsValid)
        {
            continue;
        }

        const FVector Corners[] = {
            FVector(Bounds.Min.X, Bounds.Min.Y, Bounds.Min.Z),
            FVector(Bounds.Min.X, Bounds.Min.Y, Bounds.Max.Z),
            FVector(Bounds.Min.X, Bounds.Max.Y, Bounds.Min.Z),
            FVector(Bounds.Min.X, Bounds.Max.Y, Bounds.Max.Z),
            FVector(Bounds.Max.X, Bounds.Min.Y, Bounds.Min.Z),
            FVector(Bounds.Max.X, Bounds.Min.Y, Bounds.Max.Z),
            FVector(Bounds.Max.X, Bounds.Max.Y, Bounds.Min.Z),
            FVector(Bounds.Max.X, Bounds.Max.Y, Bounds.Max.Z),
        };
        FVector Minimum(FLT_MAX, FLT_MAX, FLT_MAX);
        FVector Maximum(-FLT_MAX, -FLT_MAX, -FLT_MAX);
        for (const FVector& Corner : Corners)
        {
            const FVector WorldCorner = InstanceTransform.TransformPosition(Corner);
            Minimum.X = FMath::Min(Minimum.X, WorldCorner.X);
            Minimum.Y = FMath::Min(Minimum.Y, WorldCorner.Y);
            Minimum.Z = FMath::Min(Minimum.Z, WorldCorner.Z);
            Maximum.X = FMath::Max(Maximum.X, WorldCorner.X);
            Maximum.Y = FMath::Max(Maximum.Y, WorldCorner.Y);
            Maximum.Z = FMath::Max(Maximum.Z, WorldCorner.Z);
        }
        if (Maximum.X - Minimum.X > KINDA_SMALL_NUMBER
            && Maximum.Y - Minimum.Y > KINDA_SMALL_NUMBER
            && Maximum.Z - Minimum.Z > KINDA_SMALL_NUMBER)
        {
            OutOccupancy.Emplace(Minimum, Maximum);
        }
    }

    if (OutOccupancy.IsEmpty())
    {
        OutError = FString::Printf(
            TEXT("No occupancy actor tagged '%s' was found."),
            *NativeOccupancyActorTag.ToString());
        return false;
    }
    return true;
}

bool AEndlessRoomManager::OccupancyOverlapsRetainedRooms(
    const TArray<FBox>& CandidateOccupancy) const
{
    const FEndlessRoomInstance* RetainedRooms[] = {&CurrentRoom, &PreviousRoom};
    for (const FEndlessRoomInstance* Room : RetainedRooms)
    {
        if (!Room->HasRoom())
        {
            continue;
        }
        for (const FBox& CandidatePiece : CandidateOccupancy)
        {
            for (const FBox& RetainedPiece : Room->WorldOccupancy)
            {
                if (BoxesOverlapInterior3D(
                        CandidatePiece,
                        RetainedPiece,
                        NativeFootprintOverlapTolerance))
                {
                    return true;
                }
            }
        }
    }
    return false;
}

bool AEndlessRoomManager::BoxesOverlapInterior3D(
    const FBox& A,
    const FBox& B,
    const float Tolerance)
{
    return A.Min.X < B.Max.X - Tolerance
        && A.Max.X > B.Min.X + Tolerance
        && A.Min.Y < B.Max.Y - Tolerance
        && A.Max.Y > B.Min.Y + Tolerance
        && A.Min.Z < B.Max.Z - Tolerance
        && A.Max.Z > B.Min.Z + Tolerance;
}

AActor* AEndlessRoomManager::FindTaggedActorInLevel(ULevel* Level, const FName Tag) const
{
    if (!Level || Tag.IsNone())
    {
        return nullptr;
    }
    for (AActor* Actor : Level->Actors)
    {
        if (IsValid(Actor) && Actor->ActorHasTag(Tag))
        {
            return Actor;
        }
    }
    return nullptr;
}

FEndlessExitCandidate* AEndlessRoomManager::FindExitCandidate(
    FEndlessRoomInstance& Room,
    const AEndlessRoomDoor* Door)
{
    return Room.ExitCandidates.FindByPredicate(
        [Door](const FEndlessExitCandidate& Exit)
        {
            return IsValid(Door) && Exit.Door == Door;
        });
}

const FEndlessExitCandidate* AEndlessRoomManager::FindExitCandidate(
    const FEndlessRoomInstance& Room,
    const AEndlessRoomDoor* Door) const
{
    return Room.ExitCandidates.FindByPredicate(
        [Door](const FEndlessExitCandidate& Exit)
        {
            return IsValid(Door) && Exit.Door == Door;
        });
}

void AEndlessRoomManager::ApplyExitState(FEndlessExitCandidate& Exit, const bool bEnabled)
{
    if (IsValid(Exit.Door))
    {
        Exit.Door->SetNativeProgressionEnabled(bEnabled);
    }
    if (IsValid(Exit.ClosureActor))
    {
        Exit.ClosureActor->SetActorHiddenInGame(bEnabled);
        Exit.ClosureActor->SetActorEnableCollision(!bEnabled);
    }
}

void AEndlessRoomManager::CommitExitChoice(AEndlessRoomDoor* Door)
{
    FEndlessExitCandidate* SelectedExit = FindExitCandidate(CurrentRoom, Door);
    if (!SelectedExit || !SelectedExit->bActiveForInstance || SelectedExit->bLocked)
    {
        return;
    }

    CurrentRoom.CommittedExitId = SelectedExit->ExitId;
    for (FEndlessExitCandidate& Exit : CurrentRoom.ExitCandidates)
    {
        if (!Exit.bActiveForInstance || Exit.Door == Door)
        {
            continue;
        }
        Exit.bLocked = true;
        ApplyExitState(Exit, false);
    }

    UE_LOG(
        LogEndlessRoomSystem,
        Log,
        TEXT("%s: committed Exit %s; all other progression exits are sealed."),
        *GetPathName(),
        *SelectedExit->ExitId.ToString());
}

void AEndlessRoomManager::RestoreExitChoices(FEndlessRoomInstance& Room)
{
    Room.CommittedExitId = NAME_None;
    for (FEndlessExitCandidate& Exit : Room.ExitCandidates)
    {
        if (!Exit.bActiveForInstance || !Exit.HasCandidate())
        {
            continue;
        }
        Exit.bLocked = false;
        ApplyExitState(Exit, true);
    }
}

void AEndlessRoomManager::CompleteQueuedDoorOpen()
{
    if (!QueuedOpenDoor.IsValid())
    {
        return;
    }

    AEndlessRoomDoor* Door = QueuedOpenDoor.Get();
    AActor* Interactor = QueuedInteractor.Get();
    QueuedOpenDoor.Reset();
    QueuedInteractor.Reset();

    if (!NextRoom.bReady || NextRoom.BackDoor != Door)
    {
        return;
    }
    CommitExitChoice(Door);
    Door->CompleteNativeQueuedOpen(Interactor);
}

void AEndlessRoomManager::FailNextRoomLoad(AEndlessRoomDoor* Door, const FString& Reason)
{
    GetWorldTimerManager().ClearTimer(NativeLoadTimeoutHandle);
    ReleaseRoom(NextRoom);

    FEndlessExitCandidate* Exit = FindExitCandidate(CurrentRoom, Door);
    if (!Exit)
    {
        QueuedOpenDoor.Reset();
        QueuedInteractor.Reset();
        UE_LOG(
            LogEndlessRoomSystem,
            Error,
            TEXT("%s: room load failed for an exit no longer owned by Current: %s"),
            *GetPathName(),
            *Reason);
        return;
    }

    ++Exit->CandidateIndex;
    if (Exit->HasCandidate())
    {
        UE_LOG(
            LogEndlessRoomSystem,
            Warning,
            TEXT("%s: Exit %s rejected candidate (%s); retrying with %s."),
            *GetPathName(),
            *Exit->ExitId.ToString(),
            *Reason,
            *Exit->GetCandidate().ToSoftObjectPath().ToString());
        BeginPreloadForDoor(Door);
        return;
    }

    Exit->bLocked = true;
    ApplyExitState(*Exit, false);
    if (QueuedOpenDoor.Get() == Door)
    {
        QueuedOpenDoor.Reset();
        QueuedInteractor.Reset();
    }
    UE_LOG(
        LogEndlessRoomSystem,
        Error,
        TEXT("%s: Exit %s exhausted all candidates and was sealed: %s"),
        *GetPathName(),
        *Exit->ExitId.ToString(),
        *Reason);
    OnNativeRoomLoadFailed.Broadcast(Door, Reason);
}

void AEndlessRoomManager::ReleaseRoom(FEndlessRoomInstance& Room)
{
    if (Room.bPersistentRoom)
    {
        for (FEndlessExitCandidate& Exit : Room.ExitCandidates)
        {
            ApplyExitState(Exit, false);
        }
    }

    if (Room.StreamingLevel)
    {
        Room.StreamingLevel->OnLevelLoaded.RemoveDynamic(
            this, &AEndlessRoomManager::HandleNextLevelLoaded);
        Room.StreamingLevel->OnLevelShown.RemoveDynamic(
            this, &AEndlessRoomManager::HandleNextLevelShown);
        Room.StreamingLevel->SetShouldBeVisible(false);
        Room.StreamingLevel->SetShouldBeLoaded(false);
        Room.StreamingLevel->SetIsRequestingUnloadAndRemoval(true);
    }
    Room.Reset();
}

void AEndlessRoomManager::AdvanceForward()
{
    if (!NextRoom.HasRoom() || !NextRoom.bReady)
    {
        return;
    }

    CommitExitChoice(NextRoom.BackDoor);
    ReleaseRoom(PreviousRoom);
    PreviousRoom = CurrentRoom;

    if (IsValid(PreviousRoom.EntryDoor))
    {
        PreviousRoom.EntryDoor->SetNativeConnectionSuppressed(false);
    }

    CurrentRoom = NextRoom;
    NextRoom.Reset();
    QueuedOpenDoor.Reset();
    QueuedInteractor.Reset();
    PrepareCandidatesForRoom(CurrentRoom);

    OnNativeRoomBecameCurrent.Broadcast(CurrentRoom.StreamingLevel);
}

void AEndlessRoomManager::MoveBackward()
{
    if (!PreviousRoom.HasRoom() || !PreviousRoom.bReady)
    {
        return;
    }

    ReleaseRoom(NextRoom);
    FEndlessRoomInstance ForwardRoom = CurrentRoom;
    CurrentRoom = PreviousRoom;
    PreviousRoom.Reset();
    NextRoom = ForwardRoom;
    QueuedOpenDoor.Reset();
    QueuedInteractor.Reset();
    RestoreExitChoices(CurrentRoom);

    OnNativeRoomBecameCurrent.Broadcast(CurrentRoom.StreamingLevel);
}

bool AEndlessRoomManager::IsCurrentExitDoor(const AEndlessRoomDoor* Door) const
{
    const FEndlessExitCandidate* Exit = FindExitCandidate(CurrentRoom, Door);
    return Exit && Exit->bActiveForInstance && !Exit->bLocked;
}

bool AEndlessRoomManager::IsCurrentBackDoor(const AEndlessRoomDoor* Door) const
{
    return IsValid(Door) && CurrentRoom.BackDoor == Door;
}

void AEndlessRoomManager::CheckNativePreloadDistance()
{
    if (!bNativeRoomManagerEnabled)
    {
        return;
    }

    APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
    if (!IsValid(PlayerPawn))
    {
        return;
    }

    FEndlessExitCandidate* NearestExit = nullptr;
    float NearestDistanceSquared = FLT_MAX;
    for (FEndlessExitCandidate& Exit : CurrentRoom.ExitCandidates)
    {
        if (!Exit.bActiveForInstance || Exit.bLocked || !Exit.HasCandidate() || !IsValid(Exit.Door))
        {
            continue;
        }

        const float DistanceSquared = FVector::DistSquared(
            PlayerPawn->GetActorLocation(),
            Exit.Door->GetNativeRoomSpawnTransform().GetLocation());
        const float PreloadDistance = Exit.Door->GetNativePreloadDistance();
        const float VerticalDistance = FMath::Abs(
            PlayerPawn->GetActorLocation().Z
            - Exit.Door->GetNativeRoomSpawnTransform().GetLocation().Z);
        if (PreloadDistance > 0.0f
            && VerticalDistance <= Exit.Door->GetNativePreloadVerticalTolerance()
            && DistanceSquared <= FMath::Square(PreloadDistance)
            && DistanceSquared < NearestDistanceSquared)
        {
            NearestExit = &Exit;
            NearestDistanceSquared = DistanceSquared;
        }
    }

    if (NearestExit && (!NextRoom.HasRoom() || NextRoom.BackDoor != NearestExit->Door))
    {
        BeginPreloadForDoor(NearestExit->Door);
    }
}

void AEndlessRoomManager::HandleNativePreloadRequested(AEndlessRoomDoor* Door)
{
    if (bNativeRoomManagerEnabled && IsCurrentExitDoor(Door))
    {
        BeginPreloadForDoor(Door);
    }
}

void AEndlessRoomManager::HandleNativeDoorOpened(
    AEndlessRoomDoor* Door,
    FTransform RoomSpawnTransform)
{
    if (IsCurrentExitDoor(Door)
        && (!NextRoom.HasRoom() || NextRoom.BackDoor != Door || !NextRoom.bReady))
    {
        UE_LOG(
            LogEndlessRoomSystem,
            Error,
            TEXT("%s: progression Exit %s opened before its own next room was ready."),
            *GetPathName(),
            *Door->GetNativeDoorId().ToString());
    }
}

void AEndlessRoomManager::HandleNativePassageCompleted(
    AEndlessRoomDoor* Door,
    EEndlessDoorSide FromSide,
    EEndlessDoorSide ToSide)
{
    if (!bNativeRoomManagerEnabled)
    {
        return;
    }

    if (IsCurrentExitDoor(Door))
    {
        AdvanceForward();
    }
    else if (IsCurrentBackDoor(Door))
    {
        MoveBackward();
    }
}

void AEndlessRoomManager::HandleNextLevelLoaded()
{
    AEndlessRoomDoor* LoadingDoor = NextRoom.BackDoor;
    FString Error;
    if (!ResolveAndAlignDoorsForNextRoom(Error))
    {
        FailNextRoomLoad(LoadingDoor, Error);
        return;
    }

    UE_LOG(
        LogEndlessRoomSystem,
        Verbose,
        TEXT("%s: next room package loaded, mapped, footprint-checked, and queued for visibility."),
        *GetPathName());
}

void AEndlessRoomManager::HandleNextLevelShown()
{
    GetWorldTimerManager().ClearTimer(NativeLoadTimeoutHandle);

    if (!NextRoom.bDoorsResolved
        || !NextRoom.EntryDoor
        || NextRoom.ExitCandidates.IsEmpty())
    {
        FailNextRoomLoad(
            NextRoom.BackDoor,
            TEXT("Next room became visible before Entry/Exit mapping completed."));
        return;
    }

    const FTransform ExpectedEntryTransform = NextRoom.BackConnectionTransform;
    const FTransform ActualEntryTransform = NextRoom.EntryDoor->bNativeUseEntryAlignmentOverride
        ? NextRoom.EntryDoor->NativeEntryAlignmentOverride * NextRoom.InstanceTransform
        : NextRoom.EntryDoor->GetActorTransform();
    const float LocationError = FVector::Dist(
        ActualEntryTransform.GetLocation(), ExpectedEntryTransform.GetLocation());
    const float RotationErrorDegrees = FMath::RadiansToDegrees(
        ActualEntryTransform.GetRotation().AngularDistance(ExpectedEntryTransform.GetRotation()));
    if (!NextRoom.BackDoor || LocationError > 1.0f || RotationErrorDegrees > 0.5f)
    {
        FailNextRoomLoad(
            NextRoom.BackDoor,
            FString::Printf(
                TEXT("Entry alignment validation failed (location %.3f cm, rotation %.3f deg)."),
                LocationError,
                RotationErrorDegrees));
        return;
    }

    NextRoom.bReady = true;
    UE_LOG(
        LogEndlessRoomSystem,
        Log,
        TEXT("%s: next room is visible and ready (alignment %.3f cm / %.3f deg, occupancy boxes=%d)."),
        *GetPathName(),
        LocationError,
        RotationErrorDegrees,
        NextRoom.WorldOccupancy.Num());
    OnNativeNextRoomReady.Broadcast(NextRoom.StreamingLevel);
    CompleteQueuedDoorOpen();
}

void AEndlessRoomManager::HandleNextLevelLoadTimeout()
{
    if (NextRoom.HasRoom() && !NextRoom.bReady)
    {
        FailNextRoomLoad(
            NextRoom.BackDoor,
            TEXT("Timed out while waiting for the next room to become visible."));
    }
}

int32 AEndlessRoomManager::GetNativeActiveExitCount() const
{
    int32 Count = 0;
    for (const FEndlessExitCandidate& Exit : CurrentRoom.ExitCandidates)
    {
        if (Exit.bActiveForInstance && !Exit.bLocked)
        {
            ++Count;
        }
    }
    return Count;
}

int32 AEndlessRoomManager::GetNativeRealizedRoomCount() const
{
    return (PreviousRoom.HasRoom() ? 1 : 0)
        + (CurrentRoom.HasRoom() ? 1 : 0)
        + (NextRoom.HasRoom() ? 1 : 0);
}
