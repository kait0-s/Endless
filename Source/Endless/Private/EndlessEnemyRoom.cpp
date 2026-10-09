#include "EndlessEnemyController.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "Navigation/PathFollowingComponent.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Perception/AISense_Hearing.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace { TArray<TWeakObjectPtr<AEndlessEnemyController>> RoomBrains; }

void AEndlessEnemyController::CacheRoom()
{
    RoomFloorBounds.Reset();
    HomeRoom = GetPawn() ? GetPawn()->GetLevel() : nullptr;
    if (HomeRoom)
        for (AActor* Actor : HomeRoom->Actors)
            if (IsValid(Actor) && Actor->ActorHasTag(TEXT("EndlessRoomFloor")))
            {
                const FBox Bounds = Actor->GetComponentsBoundingBox(true);
                if (Bounds.IsValid) RoomFloorBounds.Add(Bounds);
            }
    RoomBrains.RemoveAll([](const auto& Brain) { return !Brain.IsValid(); });
    const bool bFirstBrainInRoom=!RoomBrains.ContainsByPredicate([this](const auto& Brain)
    { return Brain.IsValid() && Brain->HomeRoom==HomeRoom; });
    RoomBrains.AddUnique(this);
    if (bFirstBrainInRoom && HomeRoom)
    {
        // 単体PIEでも保存済みの古い上階タイルを使い続けない。部屋ごとに一度だけ範囲更新する。
        GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,[this]()
        {
            if (auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
            {
                FBox Bounds(ForceInit);
                for (const FBox& Floor:RoomFloorBounds) Bounds+=Floor;
                if (Bounds.IsValid) Nav->AddDirtyArea(Bounds.ExpandBy(FVector(100,100,600)),ENavigationDirtyFlag::All,FName(TEXT("EndlessEnemyRoomReady")));
            }
        }));
    }
}

void AEndlessEnemyController::EndPlay(const EEndPlayReason::Type Reason)
{
    RoomBrains.Remove(this);
    GetWorldTimerManager().ClearTimer(ThinkTimer);
    StopMovement();
    HomeRoom = nullptr;
    Super::EndPlay(Reason);
}

bool AEndlessEnemyController::IsInsideHomeRoom(FVector Location, float Margin) const
{
    if (!IsValid(HomeRoom)) return false;
    // 床領域の和集合を使い、同じ部屋の床の継ぎ目や階段を境界と誤認しない。
    auto Contains = [this](FVector P)
    {
        for (const FBox& Floor : RoomFloorBounds)
            if (P.X >= Floor.Min.X && P.X <= Floor.Max.X && P.Y >= Floor.Min.Y && P.Y <= Floor.Max.Y
                && P.Z >= Floor.Max.Z - 60.f && P.Z <= Floor.Max.Z + 500.f) return true;
        return false;
    };
    if (!Contains(Location)) return false;
    for (int32 I = 0; Margin > 0.f && I < 8; ++I)
    {
        const float Angle = I * PI / 4.f;
        if (!Contains(Location + FVector(FMath::Cos(Angle)*Margin,FMath::Sin(Angle)*Margin,0))) return false;
    }
    return true;
}

bool AEndlessEnemyController::BuildRoomPath(FVector Destination, FNavPathSharedPtr& Path) const
{
    const APawn* RoomPawn = GetPawn();
    auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if (!RoomPawn || !Nav) return false;
    const auto* RoomCharacter = Cast<ACharacter>(RoomPawn);
    const float Margin = RoomCharacter ? RoomCharacter->GetCapsuleComponent()->GetScaledCapsuleRadius() + 4.f : 38.f;
    if (!IsInsideHomeRoom(Destination,Margin)) return false;
    const ANavigationData* Data = Nav->GetNavDataForProps(GetNavAgentPropertiesRef(),RoomPawn->GetNavAgentLocation());
    if (!Data) return false;
    FPathFindingQuery Query(this,*Data,RoomPawn->GetNavAgentLocation(),Destination);
    Query.SetAllowPartialPaths(false);
    const FPathFindingResult Result = Nav->FindPathSync(GetNavAgentPropertiesRef(),Query);
    if (!Result.IsSuccessful() || !Result.Path.IsValid() || Result.Path->IsPartial()) return false;
    FVector Previous = RoomPawn->GetNavAgentLocation();
    for (const FNavPathPoint& Point : Result.Path->GetPathPoints())
    {
        const int32 Steps = FMath::Max(1,FMath::CeilToInt(FVector::Dist(Previous,Point.Location)/30.f));
        for (int32 I=1;I<=Steps;++I)
            if (!IsInsideHomeRoom(FMath::Lerp(Previous,Point.Location,float(I)/Steps),Margin)) return false;
        Previous=Point.Location;
    }
    Path=Result.Path;
    // 自動再計算が隣室経由の経路に置き換えることを防ぎ、次のThinkで再検証する。
    Path->EnableRecalculationOnInvalidation(false);
    return true;
}

bool AEndlessEnemyController::ResolveRoomDestination(FVector Requested, FVector& Resolved) const
{
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if (!Nav || !GetPawn()) return false;
    TArray<FVector> Candidates;Candidates.Add(Requested);
    for (const FBox& Floor:RoomFloorBounds)
    {
        const FVector Min=Floor.Min+FVector(65,65,0),Max=Floor.Max-FVector(65,65,0);
        Candidates.Add(FVector(FMath::Clamp(Requested.X,Min.X,Max.X),FMath::Clamp(Requested.Y,Min.Y,Max.Y),Floor.Max.Z));
    }
    Candidates.Sort([Requested](const FVector& A,const FVector& B){return FVector::DistSquared(A,Requested)<FVector::DistSquared(B,Requested);});
    for (const FVector& Candidate:Candidates)
    {
        FNavLocation Projected;FNavPathSharedPtr Path;
        if (Nav->ProjectPointToNavigation(Candidate,Projected,FVector(120,120,180))
            // 上階タイルが未生成のとき、同じXYの下階を「到達先」にしない。
            && (!IsInsideHomeRoom(Requested) || FMath::Abs(Projected.Location.Z-Requested.Z)<=200.f)
            && BuildRoomPath(Projected.Location,Path))
        { Resolved=Projected.Location;return true; }
    }
    Resolved=GetPawn()->GetNavAgentLocation();
    return false; // 現在地を成功と偽らず、NavMesh準備後に再試行する。
}

EPathFollowingRequestResult::Type AEndlessEnemyController::MoveWithinRoom(FVector Destination,float AcceptanceRadius)
{
    const float Now=GetWorld()->GetTimeSeconds();
    TRACE_CPUPROFILER_EVENT_SCOPE(EndlessAI_RoomPath);
    // 動的NavMeshの更新後も再試行する。最大8秒のバックオフに留める。
    if (Now<FailedMoveRetryTime && Destination.Equals(FailedDestination,100.f)) return EPathFollowingRequestResult::Failed;
    FNavPathSharedPtr Path;
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());FNavLocation Projected;
    if (!Nav || !Nav->ProjectPointToNavigation(Destination,Projected,FVector(90,90,150)) || !BuildRoomPath(Projected.Location,Path))
    {
        StopMovement();
        ++MoveFailures;
        FailedMoveCount=Destination.Equals(FailedDestination,100.f) ? FMath::Min(FailedMoveCount+1,4) : 1;
        FailedDestination=Destination;FailedMoveRetryTime=Now+FMath::Pow(2.f,FailedMoveCount-1);
        return EPathFollowingRequestResult::Failed;
    }
    FailedMoveCount=0;FailedMoveRetryTime=0.f;
    FAIMoveRequest Request(Projected.Location);
    Request.SetAcceptanceRadius(AcceptanceRadius);Request.SetAllowPartialPath(false);Request.SetReachTestIncludesAgentRadius(false);
    return RequestMove(Request,Path).IsValid() ? EPathFollowingRequestResult::RequestSuccessful : EPathFollowingRequestResult::Failed;
}

void AEndlessEnemyController::DispatchNoise(AActor* Source,FVector Location,EEndlessNoiseKind Kind,float Radius)
{
    if (!IsValid(Source) || Radius<=0) return;
    // 半径を強度へ保存し、Perception通知後に遮蔽・階差減衰を適用する。
    UAISense_Hearing::ReportNoiseEvent(Source,Location,Radius/6000.f,Source,Radius,FName(*FString::FromInt(int32(Kind))));
}

void AEndlessEnemyController::ShareAlert(FVector Location)
{
    const float Now=GetWorld()->GetTimeSeconds();
    if (Now<NextAlertShareTime || !GetPawn()) return;
    NextAlertShareTime=Now+1.f;
    for (const auto& Weak:RoomBrains)
    {
        auto* Other=Weak.Get();
        if (!Other || Other==this || Other->HomeRoom!=HomeRoom || !Other->bBrainActive || !Other->GetPawn()
            || Other->State==EEndlessEnemyState::Chase || Other->State==EEndlessEnemyState::Combat
            || FVector::DistSquared(GetPawn()->GetActorLocation(),Other->GetPawn()->GetActorLocation())>FMath::Square(AlertShareRadius)) continue;
        FVector Point;
        if (!Other->ResolveRoomDestination(Location,Point)) continue;
        if (Other->State==EEndlessEnemyState::Investigate && Point.Equals(Other->InvestigationLocation,150.f)) continue;
        Other->InvestigationLocation=Point;Other->State=EEndlessEnemyState::Investigate;
        Other->InvestigationExpires=Now+Other->InvestigationSeconds;Other->NextRepathTime=0;
        ++Other->SharedAlertsReceived;Other->StopMovement();Other->ClearFocus(EAIFocusPriority::Gameplay);Other->SetFacingMode(false);
    }
}

bool AEndlessEnemyController::AcquireAttackToken(float Now)
{
    if (Now<AttackTokenUntil) return true;
    if (AttackTokenUntil>0) { AttackTokenUntil=0;AttackTokenRestUntil=Now+.6f; }
    if (Now<AttackTokenRestUntil) return false;
    int32 Active=0;
    for (const auto& Weak:RoomBrains)
        if (const auto* Other=Weak.Get();Other && Other!=this && Other->HomeRoom==HomeRoom && Other->bBrainActive && Other->AttackTokenUntil>Now) ++Active;
    if (Active>=2) { ++AttackTokensDenied;return false; }
    AttackTokenUntil=Now+.8f;return true;
}

bool AEndlessEnemyController::FindCombatPosition(FVector Target,float Distance,FVector& Position) const
{
    if (!GetPawn()) return false;
    const FVector Away=(GetPawn()->GetActorLocation()-Target).GetSafeNormal2D();
    const float Base=FMath::Atan2(Away.Y,Away.X);
    for (int32 I=0;I<6;++I)
    {
        const float Angle=Base+((I+GetUniqueID()%3)%6-2.5f)*.3f;
        const FVector Candidate=Target+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*Distance;
        FVector Resolved;if (!ResolveRoomDestination(Candidate,Resolved) || FVector::Dist2D(Resolved,Target)<180.f) continue;
        bool Crowded=false;
        for (const auto& Weak:RoomBrains)
            if (const auto* Other=Weak.Get();Other && Other!=this && Other->HomeRoom==HomeRoom && Other->GetPawn()
                && FVector::DistSquared2D(Other->GetPawn()->GetActorLocation(),Resolved)<FMath::Square(180.f)) { Crowded=true;break; }
        if (Crowded) continue;
        FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(CombatPosition),false,GetPawn());
        if (GetWorld()->LineTraceSingleByChannel(Hit,Resolved+FVector(0,0,120),Target+FVector(0,0,25),ECC_Visibility,Q)
            && !Cast<ACharacter>(Hit.GetActor())) continue;
        Position=Resolved;return true;
    }
    return false;
}
