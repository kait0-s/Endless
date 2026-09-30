#include "RoomDoor.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Curves/CurveFloat.h"
#include "Endless.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "RoomManager.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

AEndlessRoomDoor::AEndlessRoomDoor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
}

void AEndlessRoomDoor::ProcessEvent(UFunction* Function, void* Parameters)
{
    // BPI_Interactable is a Blueprint-only interface. Intercept its existing Interact event after
    // migration so the TacticalSurvive input assets do not need to be rewritten and the legacy
    // Blueprint graph cannot execute a second room-generation path.
    if (bNativeDoorLogicEnabled && Function && Function->GetFName() == TEXT("Interact"))
    {
        AActor* Interactor = nullptr;
        for (TFieldIterator<FObjectPropertyBase> It(Function); Parameters && It; ++It)
        {
            FObjectPropertyBase* ObjectProperty = *It;
            if (ObjectProperty->HasAnyPropertyFlags(CPF_Parm)
                && !ObjectProperty->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                UObject* ParameterObject = ObjectProperty->GetObjectPropertyValue(
                    ObjectProperty->ContainerPtrToValuePtr<void>(Parameters));
                Interactor = Cast<AActor>(ParameterObject);
                if (Interactor)
                {
                    break;
                }
            }
        }

        NativeInteract(Interactor);
        return;
    }

    Super::ProcessEvent(Function, Parameters);
}

void AEndlessRoomDoor::BeginPlay()
{
    Super::BeginPlay();

    ResolveExistingComponents();

    if (!bNativeDoorLogicEnabled)
    {
        SetActorTickEnabled(false);
        return;
    }

    if (!DoorPivotComponent)
    {
        UE_LOG(
            LogEndlessRoomSystem,
            Error,
            TEXT("%s: component '%s' was not found; native door animation is disabled."),
            *GetPathName(),
            *DoorPivotComponentName.ToString());
        return;
    }

    NativeClosedRotation = DoorPivotComponent->GetRelativeRotation();
    BindNativeCollisionEvents();
    FindAndRegisterWithManager();
}

void AEndlessRoomDoor::Tick(const float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (!bNativeIsAnimating || !DoorPivotComponent)
    {
        SetActorTickEnabled(false);
        return;
    }

    NativeAnimationElapsed += DeltaSeconds;
    const float LinearAlpha = NativeOpenDuration <= KINDA_SMALL_NUMBER
        ? 1.0f
        : FMath::Clamp(NativeAnimationElapsed / NativeOpenDuration, 0.0f, 1.0f);
    const float AnimationAlpha = NativeOpenCurve
        ? FMath::Clamp(NativeOpenCurve->GetFloatValue(LinearAlpha), 0.0f, 1.0f)
        : LinearAlpha;

    const FQuat Rotation = FQuat::Slerp(
        NativeAnimationStartRotation.Quaternion(),
        NativeAnimationTargetRotation.Quaternion(),
        AnimationAlpha);
    DoorPivotComponent->SetRelativeRotation(Rotation);

    if (LinearAlpha >= 1.0f)
    {
        FinishNativeDoorAnimation();
    }
}

void AEndlessRoomDoor::NativeInteract(AActor* Interactor)
{
    if (!bNativeDoorLogicEnabled
        || bNativeConnectionSuppressed
        || (NativeDoorRole == EEndlessRoomDoorRole::Exit && !bNativeProgressionEnabled)
        || !IsValid(Interactor))
    {
        return;
    }

    const FVector InteractionTarget = GetActorLocation()
        + GetActorUpVector() * NativeInteractionTargetHeight;
    if (NativeInteractionDistance > 0.0f
        && FVector::Dist(Interactor->GetActorLocation(), InteractionTarget) > NativeInteractionDistance)
    {
        return;
    }

    const FVector ToDoor = InteractionTarget - Interactor->GetActorLocation();
    const FVector InteractorForward = Interactor->GetActorForwardVector();
    if (!ToDoor.IsNearlyZero()
        && !InteractorForward.IsNearlyZero()
        && FVector::DotProduct(ToDoor.GetSafeNormal(), InteractorForward.GetSafeNormal())
            < NativeMinFacingDot)
    {
        return;
    }

    NativeToggleDoor(Interactor);
}

void AEndlessRoomDoor::NativeToggleDoor(AActor* Interactor)
{
    if (!bNativeDoorLogicEnabled
        || bNativeConnectionSuppressed
        || (NativeDoorRole == EEndlessRoomDoorRole::Exit && !bNativeProgressionEnabled)
        || bNativeIsAnimating
        || !DoorPivotComponent)
    {
        return;
    }

    if (bNativeIsOpen)
    {
        StartNativeDoorAnimation(false, Interactor);
        return;
    }

    NativeOpenDirection = DetermineOpenDirection(Interactor);

    if (NativeRoomManager)
    {
        if (!NativeRoomManager->RequestNativeDoorOpen(this, Interactor))
        {
            return;
        }
    }
    else if (NativeDoorRole != EEndlessRoomDoorRole::Auxiliary)
    {
        UE_LOG(
            LogEndlessRoomSystem,
            Warning,
            TEXT("%s: opening was blocked because no active native room manager is assigned."),
            *GetPathName());
        return;
    }

    StartNativeDoorAnimation(true, Interactor);
}

void AEndlessRoomDoor::CompleteNativeQueuedOpen(AActor* Interactor)
{
    if (!bNativeDoorLogicEnabled || bNativeIsOpen || bNativeIsAnimating || !DoorPivotComponent)
    {
        return;
    }

    if (IsValid(Interactor))
    {
        NativeOpenDirection = DetermineOpenDirection(Interactor);
    }
    StartNativeDoorAnimation(true, Interactor);
}

FTransform AEndlessRoomDoor::GetNativeRoomSpawnTransform() const
{
    return RoomSpawnPointComponent
        ? RoomSpawnPointComponent->GetComponentTransform()
        : GetActorTransform();
}

FTransform AEndlessRoomDoor::GetNativeEntryAlignmentLocalTransform() const
{
    if (bNativeUseEntryAlignmentOverride)
    {
        return NativeEntryAlignmentOverride;
    }

    return GetRootComponent()
        ? GetRootComponent()->GetRelativeTransform()
        : GetActorTransform();
}

void AEndlessRoomDoor::SetNativeRoomManager(AEndlessRoomManager* InManager)
{
    NativeRoomManager = InManager;
}

void AEndlessRoomDoor::SetNativeConnectionSuppressed(const bool bSuppressed)
{
    if (bNativeConnectionSuppressed == bSuppressed)
    {
        return;
    }

    bNativeConnectionSuppressed = bSuppressed;
    SetActorHiddenInGame(bSuppressed);
    SetActorEnableCollision(!bSuppressed);
    if (!DoorVisualComponent || !PreloadRangeComponent || !SafeZoneSideAComponent || !SafeZoneSideBComponent)
    {
        ResolveExistingComponents();
    }

    if (DoorVisualComponent)
    {
        DoorVisualComponent->SetVisibility(!bSuppressed, true);
        DoorVisualComponent->SetCollisionEnabled(
            bSuppressed ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
    }

    if (bSuppressed)
    {
        if (PreloadRangeComponent)
        {
            PreloadRangeComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
        if (SafeZoneSideAComponent)
        {
            SafeZoneSideAComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
        if (SafeZoneSideBComponent)
        {
            SafeZoneSideBComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
        SetActorTickEnabled(false);
    }
    else if (bNativeDoorLogicEnabled)
    {
        if (DoorVisualComponent)
        {
            DoorVisualComponent->SetVisibility(true, true);
            DoorVisualComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        }
        BindNativeCollisionEvents();
    }
}

void AEndlessRoomDoor::SetNativeProgressionEnabled(const bool bEnabled)
{
    bNativeProgressionEnabled = bEnabled;
    if (!bEnabled)
    {
        ResetNativeDoorToClosed();
    }
    SetNativeConnectionSuppressed(!bEnabled);
}

void AEndlessRoomDoor::ResetNativeDoorToClosed()
{
    if (!DoorPivotComponent)
    {
        ResolveExistingComponents();
    }

    if (DoorPivotComponent)
    {
        DoorPivotComponent->SetRelativeRotation(NativeClosedRotation);
    }

    NativeAnimationElapsed = 0.0f;
    NativeAnimationStartRotation = NativeClosedRotation;
    NativeAnimationTargetRotation = NativeClosedRotation;
    bNativeIsOpen = false;
    bNativeIsAnimating = false;
    bNativeAnimationOpening = false;
    bNativeOpenedEventSent = false;
    bPassageDebounce = false;
    LastSafeZoneSide = EEndlessDoorSide::Unknown;
    SetActorTickEnabled(false);
}

void AEndlessRoomDoor::ResolveExistingComponents()
{
    DoorPivotComponent = FindSceneComponentByName(DoorPivotComponentName);
    DoorVisualComponent = FindPrimitiveComponentByName(DoorVisualComponentName);
    RoomSpawnPointComponent = FindSceneComponentByName(RoomSpawnPointComponentName);
    PreloadRangeComponent = FindPrimitiveComponentByName(PreloadRangeComponentName);
    SafeZoneSideAComponent = FindPrimitiveComponentByName(SafeZoneSideAComponentName);
    SafeZoneSideBComponent = FindPrimitiveComponentByName(SafeZoneSideBComponentName);
    InteractionPointSideAComponent = FindSceneComponentByName(InteractionPointSideAComponentName);
    InteractionPointSideBComponent = FindSceneComponentByName(InteractionPointSideBComponentName);

    if (!RoomSpawnPointComponent)
    {
        UE_LOG(
            LogEndlessRoomSystem,
            Error,
            TEXT("%s: component '%s' was not found; level-instance placement will be blocked."),
            *GetPathName(),
            *RoomSpawnPointComponentName.ToString());
    }
}

void AEndlessRoomDoor::BindNativeCollisionEvents()
{
    if (USphereComponent* PreloadSphere = Cast<USphereComponent>(PreloadRangeComponent))
    {
        PreloadSphere->SetSphereRadius(NativePreloadDistance);
    }

    if (PreloadRangeComponent)
    {
        PreloadRangeComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        PreloadRangeComponent->SetGenerateOverlapEvents(true);
        PreloadRangeComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
        PreloadRangeComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
        PreloadRangeComponent->OnComponentBeginOverlap.AddUniqueDynamic(
            this, &AEndlessRoomDoor::HandlePreloadRangeBeginOverlap);
    }

    auto ConfigureSafeZone = [](UPrimitiveComponent* Component)
    {
        if (!Component)
        {
            return;
        }
        Component->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Component->SetGenerateOverlapEvents(true);
        Component->SetCollisionResponseToAllChannels(ECR_Ignore);
        Component->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    };

    ConfigureSafeZone(SafeZoneSideAComponent);
    ConfigureSafeZone(SafeZoneSideBComponent);

    if (SafeZoneSideAComponent)
    {
        SafeZoneSideAComponent->OnComponentBeginOverlap.AddUniqueDynamic(
            this, &AEndlessRoomDoor::HandleSafeZoneSideABeginOverlap);
    }
    if (SafeZoneSideBComponent)
    {
        SafeZoneSideBComponent->OnComponentBeginOverlap.AddUniqueDynamic(
            this, &AEndlessRoomDoor::HandleSafeZoneSideBBeginOverlap);
    }
}

void AEndlessRoomDoor::FindAndRegisterWithManager()
{
    NativeRoomManager = NativeManagerOverride;

    if (!NativeRoomManager)
    {
        for (TActorIterator<AEndlessRoomManager> It(GetWorld()); It; ++It)
        {
            NativeRoomManager = *It;
            break;
        }
    }

    if (NativeRoomManager)
    {
        NativeRoomManager->RegisterNativeDoor(this);
    }
}

void AEndlessRoomDoor::StartNativeDoorAnimation(const bool bOpen, AActor* Interactor)
{
    if (!DoorPivotComponent)
    {
        return;
    }

    NativeAnimationStartRotation = DoorPivotComponent->GetRelativeRotation();
    NativeAnimationTargetRotation = bOpen
        ? NativeClosedRotation + FRotator(0.0f, NativeOpenAngle * NativeOpenDirection, 0.0f)
        : NativeClosedRotation;
    NativeAnimationElapsed = 0.0f;
    bNativeAnimationOpening = bOpen;
    bNativeIsAnimating = true;
    SetActorTickEnabled(true);

    if (bOpen && !bNativeOpenedEventSent)
    {
        bNativeOpenedEventSent = true;
        OnNativeDoorOpened.Broadcast(this, GetNativeRoomSpawnTransform());
    }

    if (NativeOpenDuration <= KINDA_SMALL_NUMBER)
    {
        DoorPivotComponent->SetRelativeRotation(NativeAnimationTargetRotation);
        FinishNativeDoorAnimation();
    }
}

void AEndlessRoomDoor::FinishNativeDoorAnimation()
{
    if (DoorPivotComponent)
    {
        DoorPivotComponent->SetRelativeRotation(NativeAnimationTargetRotation);
    }
    bNativeIsOpen = bNativeAnimationOpening;
    if (!bNativeIsOpen)
    {
        bNativeOpenedEventSent = false;
        LastSafeZoneSide = EEndlessDoorSide::Unknown;
    }
    bNativeIsAnimating = false;
    SetActorTickEnabled(false);
}

float AEndlessRoomDoor::DetermineOpenDirection(const AActor* Interactor) const
{
    if (!IsValid(Interactor))
    {
        return 1.0f;
    }

    if (InteractionPointSideAComponent && InteractionPointSideBComponent)
    {
        const float DistanceToA = FVector::DistSquared(
            Interactor->GetActorLocation(), InteractionPointSideAComponent->GetComponentLocation());
        const float DistanceToB = FVector::DistSquared(
            Interactor->GetActorLocation(), InteractionPointSideBComponent->GetComponentLocation());
        return DistanceToA <= DistanceToB
            ? NativeSideAOpenDirection
            : NativeSideBOpenDirection;
    }

    const FVector LocalPosition = GetActorTransform().InverseTransformPosition(Interactor->GetActorLocation());
    return LocalPosition.X <= 0.0f
        ? NativeSideAOpenDirection
        : NativeSideBOpenDirection;
}

bool AEndlessRoomDoor::IsPlayerControlledCharacter(const AActor* Actor) const
{
    const APawn* Pawn = Cast<APawn>(Actor);
    return Pawn && Pawn->IsPlayerControlled();
}

USceneComponent* AEndlessRoomDoor::FindSceneComponentByName(const FName ComponentName) const
{
    TArray<UActorComponent*> Components;
    GetComponents(Components);
    for (UActorComponent* Component : Components)
    {
        if (Component && Component->GetFName() == ComponentName)
        {
            return Cast<USceneComponent>(Component);
        }
    }
    return nullptr;
}

UPrimitiveComponent* AEndlessRoomDoor::FindPrimitiveComponentByName(const FName ComponentName) const
{
    return Cast<UPrimitiveComponent>(FindSceneComponentByName(ComponentName));
}

void AEndlessRoomDoor::HandlePreloadRangeBeginOverlap(
    UPrimitiveComponent* OverlappedComponent,
    AActor* OtherActor,
    UPrimitiveComponent* OtherComponent,
    int32 OtherBodyIndex,
    bool bFromSweep,
    const FHitResult& SweepResult)
{
    if (bNativeDoorLogicEnabled
        && !bNativeConnectionSuppressed
        && (NativeDoorRole != EEndlessRoomDoorRole::Exit || bNativeProgressionEnabled)
        && IsPlayerControlledCharacter(OtherActor)
        && FMath::Abs(OtherActor->GetActorLocation().Z - GetActorLocation().Z)
            <= NativePreloadVerticalTolerance
        && FVector::DistSquared(OtherActor->GetActorLocation(), GetActorLocation())
            <= FMath::Square(NativePreloadDistance))
    {
        OnNativePreloadRequested.Broadcast(this);
    }
}

void AEndlessRoomDoor::HandleSafeZoneSideABeginOverlap(
    UPrimitiveComponent* OverlappedComponent,
    AActor* OtherActor,
    UPrimitiveComponent* OtherComponent,
    int32 OtherBodyIndex,
    bool bFromSweep,
    const FHitResult& SweepResult)
{
    HandleSafeZoneEntered(EEndlessDoorSide::SideA, OtherActor);
}

void AEndlessRoomDoor::HandleSafeZoneSideBBeginOverlap(
    UPrimitiveComponent* OverlappedComponent,
    AActor* OtherActor,
    UPrimitiveComponent* OtherComponent,
    int32 OtherBodyIndex,
    bool bFromSweep,
    const FHitResult& SweepResult)
{
    HandleSafeZoneEntered(EEndlessDoorSide::SideB, OtherActor);
}

void AEndlessRoomDoor::HandleSafeZoneEntered(const EEndlessDoorSide EnteredSide, AActor* OtherActor)
{
    if (!bNativeDoorLogicEnabled
        || bNativeConnectionSuppressed
        || !IsPlayerControlledCharacter(OtherActor)
        || FMath::Abs(OtherActor->GetActorLocation().Z - GetActorLocation().Z)
            > NativePassageVerticalTolerance)
    {
        return;
    }

    const EEndlessDoorSide PreviousSide = LastSafeZoneSide;
    LastSafeZoneSide = EnteredSide;

    if ((!bNativeIsOpen && !(bNativeIsAnimating && bNativeAnimationOpening))
        || PreviousSide == EEndlessDoorSide::Unknown
        || PreviousSide == EnteredSide
        || bPassageDebounce)
    {
        return;
    }

    bPassageDebounce = true;
    OnNativePassageCompleted.Broadcast(this, PreviousSide, EnteredSide);

    FTimerHandle TemporaryHandle;
    GetWorldTimerManager().SetTimer(
        TemporaryHandle,
        this,
        &AEndlessRoomDoor::ClearPassageDebounce,
        0.25f,
        false);
}

void AEndlessRoomDoor::ClearPassageDebounce()
{
    bPassageDebounce = false;
}
