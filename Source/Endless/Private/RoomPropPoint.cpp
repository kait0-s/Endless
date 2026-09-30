#include "RoomPropPoint.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Endless.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"

AEndlessRoomPropPoint::AEndlessRoomPropPoint()
{
    PrimaryActorTick.bCanEverTick = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);

    GeneratedMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GeneratedMesh"));
    GeneratedMesh->SetupAttachment(SceneRoot);
    GeneratedMesh->SetMobility(EComponentMobility::Movable);
    GeneratedMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    GeneratedMesh->SetVisibility(false);
}

void AEndlessRoomPropPoint::BeginPlay()
{
    Super::BeginPlay();

    if (!bNativeRandomPlacementEnabled)
    {
        return;
    }

    const FString LevelPackage = GetLevel() && GetLevel()->GetOutermost()
        ? GetLevel()->GetOutermost()->GetName()
        : FString();
    const int32 InstanceSeed = HashCombine(
        GetTypeHash(LevelPackage),
        HashCombine(GetTypeHash(GetFName()), GetTypeHash(NativeSeedOffset)));
    GenerateNativeProp(InstanceSeed);
}

bool AEndlessRoomPropPoint::GenerateNativeProp(const int32 Seed)
{
    ClearNativeProp();

    FRandomStream RandomStream(Seed);
    if (NativeMeshOptions.IsEmpty()
        || RandomStream.FRand() > FMath::Clamp(NativeSpawnChance, 0.0f, 1.0f))
    {
        return false;
    }

    TArray<int32> ValidIndices;
    for (int32 Index = 0; Index < NativeMeshOptions.Num(); ++Index)
    {
        if (!NativeMeshOptions[Index].IsNull())
        {
            ValidIndices.Add(Index);
        }
    }
    if (ValidIndices.IsEmpty())
    {
        return false;
    }

    const int32 SelectedIndex = ValidIndices[RandomStream.RandRange(0, ValidIndices.Num() - 1)];
    UStaticMesh* Mesh = NativeMeshOptions[SelectedIndex].LoadSynchronous();
    if (!Mesh)
    {
        UE_LOG(
            LogEndlessRoomSystem,
            Warning,
            TEXT("%s: failed to load room prop candidate %s."),
            *GetPathName(),
            *NativeMeshOptions[SelectedIndex].ToSoftObjectPath().ToString());
        return false;
    }

    FTransform MeshTransform = NativeMeshRelativeTransform;
    if (NativeRandomYawSteps > 1)
    {
        FRotator Rotation = MeshTransform.Rotator();
        Rotation.Yaw += RandomStream.RandRange(0, NativeRandomYawSteps - 1)
            * (360.0f / static_cast<float>(NativeRandomYawSteps));
        MeshTransform.SetRotation(Rotation.Quaternion());
    }

    GeneratedMesh->SetStaticMesh(Mesh);
    GeneratedMesh->SetRelativeTransform(MeshTransform);
    GeneratedMesh->SetVisibility(true);
    GeneratedMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    return true;
}

void AEndlessRoomPropPoint::ClearNativeProp()
{
    if (!GeneratedMesh)
    {
        return;
    }

    GeneratedMesh->SetVisibility(false);
    GeneratedMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GeneratedMesh->SetStaticMesh(nullptr);
}
