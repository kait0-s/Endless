#include "EndlessCombatEffects.h"
#include "Engine/World.h"
#include "Components/LineBatchComponent.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarEndlessCombatEffects(TEXT("Endless.CombatEffects"),1,
    TEXT("Combat trail/flash rendering. 0 disables visuals for profiling; damage is unchanged."),ECVF_Default);

void EndlessCombatEffects::Trail(UWorld* World,FVector Start,FVector End,FLinearColor Color,float Width,float Seconds)
{
    if (!CVarEndlessCombatEffects.GetValueOnGameThread() || !World || !World->GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent) || Start.Equals(End,.01f)) return;
    World->GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent)->DrawLine(Start,End,Color,SDPG_World,FMath::Clamp(Width,.5f,4.f),FMath::Clamp(Seconds,.02f,.3f));
}

void EndlessCombatEffects::Flash(UWorld* World,FVector Location,FVector Direction,FLinearColor Color,float Size,float Seconds)
{
    if (!CVarEndlessCombatEffects.GetValueOnGameThread() || !World) return;
    if (auto* Batch=World->GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent))
        Batch->DrawPoint(Location+Direction.GetSafeNormal()*Size*.15f,Color.ToFColor(true),FMath::Clamp(Size*.35f,3.f,9.f),SDPG_World,Seconds);
    Direction=Direction.GetSafeNormal();FVector Right,Up;Direction.FindBestAxisVectors(Right,Up);
    Trail(World,Location,Location+Direction*Size,Color,4.f,Seconds);
    for (int32 I=0;I<4;++I)
    {
        const float Angle=I*PI*.5f;
        Trail(World,Location,Location+Direction*Size*.6f+(Right*FMath::Cos(Angle)+Up*FMath::Sin(Angle))*Size*.35f,Color,2.f,Seconds);
    }
}
