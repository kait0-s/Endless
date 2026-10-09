#include "EndlessEnemyController.h"
#include "EndlessWeaponLoot.h"
#include "EndlessPlayerWeapons.h"
#include "EndlessRoomPathFollowing.h"
#include "EndlessEnemyBehavior.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Composites/BTComposite_Selector.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISense_Hearing.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Components/SkeletalMeshComponent.h"
CSV_DEFINE_CATEGORY(EndlessAI, true);

#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogEndlessEnemyAI, Log, All);

AEndlessEnemyController::AEndlessEnemyController(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer.SetDefaultSubobjectClass<UEndlessRoomPathFollowing>(TEXT("PathFollowingComponent")))
{
    // 同じモジュールのTaskを参照するBTはCDO初期化から切り離す。
    // OnPossessでロードし、Cook対象は設定で明示する。
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    EnemyPerception=CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("EnemyPerception"));
    SetPerceptionComponent(*EnemyPerception);
    SightConfig=CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("EnemySight"));
    SightConfig->SightRadius=2500;SightConfig->LoseSightRadius=3500;SightConfig->PeripheralVisionAngleDegrees=70;
    SightConfig->DetectionByAffiliation.bDetectEnemies=true;SightConfig->DetectionByAffiliation.bDetectNeutrals=false;SightConfig->DetectionByAffiliation.bDetectFriendlies=false;
    SightConfig->SetMaxAge(5.f);
    HearingConfig=CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("EnemyHearing"));
    HearingConfig->HearingRange=6000;HearingConfig->SetMaxAge(5.f);
    HearingConfig->DetectionByAffiliation=SightConfig->DetectionByAffiliation;
    // HearingはイベントのTeamIdで判定する。既存PawnはNoTeamなので全属性を許可し、通知側でプレイヤーに限定する。
    HearingConfig->DetectionByAffiliation.bDetectNeutrals=true;
    HearingConfig->DetectionByAffiliation.bDetectFriendlies=true;
    EnemyPerception->ConfigureSense(*SightConfig);EnemyPerception->ConfigureSense(*HearingConfig);
    EnemyPerception->SetDominantSense(UAISense_Sight::StaticClass());
}

void AEndlessEnemyController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);
    CacheRoom();

    State = EEndlessEnemyState::Patrol;
    bHasPatrolDestination = false;
    StuckTime = 0.0f;
    LastThinkLocation = InPawn->GetActorLocation();
    HomeLocation=LastThinkLocation;

    const UWorld* World = GetWorld();
    LastThinkTime = World ? World->GetTimeSeconds() : 0.0f;
    PatrolWaitUntil = 0.0f;
    NextFireTime = 0.0f;
    SetFacingMode(false);

    EnemyPerception->OnTargetPerceptionUpdated.AddUniqueDynamic(this,&AEndlessEnemyController::PerceptionUpdated);
    SightConfig->SightRadius=SightRadius;SightConfig->LoseSightRadius=LoseSightRadius;SightConfig->PeripheralVisionAngleDegrees=180.f;
    EnemyPerception->ConfigureSense(*SightConfig);
    StartEnemyBehavior();
}

void AEndlessEnemyController::OnUnPossess()
{
    SetActorTickEnabled(false);
    GetWorldTimerManager().ClearTimer(ThinkTimer);
    HomeRoom = nullptr;
    RoomFloorBounds.Reset();
    Super::OnUnPossess();
}

void AEndlessEnemyController::SetBrainActive(const bool bActive)
{
    if (bBrainActive==bActive || State==EEndlessEnemyState::Dead) return;
    bBrainActive=bActive;
    const float Now=GetWorld()->GetTimeSeconds();
    SetActorTickEnabled(bActive);
    EnemyPerception->SetSenseEnabled(UAISense_Sight::StaticClass(),bActive);
    EnemyPerception->SetSenseEnabled(UAISense_Hearing::StaticClass(),bActive);
    if (bActive)
    {
        const float Pause=FMath::Max(0.f,Now-SuspendedAt);
        if (auto* Enemy=Cast<AEndlessLootEnemy>(GetPawn())) Enemy->ResumeWeaponTimers(Pause);
        LastSeenTime+=Pause;InvestigationExpires+=Pause;ReturnExpires+=Pause;PatrolWaitUntil+=Pause;NextFireTime+=Pause;
        LastThinkTime=Now;NextRepathTime=0;FailedMoveRetryTime=0;
        if (BrainComponent) BrainComponent->ResumeLogic(TEXT("Room resumed"));
        EnemyPerception->RequestStimuliListenerUpdate();
    }
    else
    {
        SuspendedAt=Now;AttackTokenUntil=0;bPerceivedPlayer=false;EnemyPerception->ForgetAll();
        if (BrainComponent) BrainComponent->PauseLogic(TEXT("Room suspended"));
        StopMovement();ClearFocus(EAIFocusPriority::Gameplay);bHasPatrolDestination=false;
    }
}

void AEndlessEnemyController::AlertToPlayer()
{
    const UWorld* World = GetWorld();
    APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    if (!World || !IsValid(Player) || !GetPawn())
    {
        return;
    }

    const float Now = World->GetTimeSeconds();
    if (CanSeePlayer(GetPawn(),Player))
    {
        LastKnownPlayerLocation=Player->GetActorLocation();LastSeenTime=Now;EnterChase(Now,State);
    }
    else
    {
        // 視認していない攻撃者の現在位置を、被弾通知から取得しない。
        // 銃声の発生地点はSensoryから届く。ここでは自分の被弾地点だけを調べる。
        HearNoise(Player,GetPawn()->GetActorLocation(),EEndlessNoiseKind::Impact,5000.f);
    }
}

void AEndlessEnemyController::HearNoise(AActor* Source,FVector Location,EEndlessNoiseKind Kind,float Radius)
{
    APawn* Self=GetPawn();
    if (!bBrainActive || !IsValid(Self) || !IsValid(Source) || Self->IsActorBeingDestroyed() || (State==EEndlessEnemyState::Chase || State==EEndlessEnemyState::Combat)) return;
    FHitResult Block;FCollisionQueryParams Q(SCENE_QUERY_STAT(EnemyHearing),false,Self);Q.AddIgnoredActor(Source);
    const bool Occluded=GetWorld()->LineTraceSingleByChannel(Block,Self->GetActorLocation()+FVector(0,0,45),Location+FVector(0,0,20),ECC_Visibility,Q);
    if (Occluded) Radius*=OccludedHearingScale;
    if (FMath::Abs(Self->GetActorLocation().Z-Location.Z)>250.f) Radius*=DifferentFloorHearingScale;
    if (FVector::DistSquared(Self->GetActorLocation(),Location)>FMath::Square(FMath::Max(0.f,Radius))) return;
    // 音源Actorを追跡せず、この瞬間の位置だけを保持する。
    LastHeardSourceLocation=Location;LastNoiseKind=Kind;++HeardEvents;
    if (!ResolveRoomDestination(Location,InvestigationLocation))
    {
        // 部屋内の音をNavMeshの一時的な失敗で忘れない。移動側で間隔を空けて再試行する。
        if (!IsInsideHomeRoom(Location)) { SyncBlackboard();return; }
        InvestigationLocation=Location;
    }
    if (State==EEndlessEnemyState::Investigate && IsMoveActive())
    { InvestigationExpires=GetWorld()->GetTimeSeconds()+InvestigationSeconds;return; }
    State=EEndlessEnemyState::Investigate;InvestigationExpires=GetWorld()->GetTimeSeconds()+InvestigationSeconds;
    NextRepathTime=0.f;StopMovement();ClearFocus(EAIFocusPriority::Gameplay);SetFacingMode(false);
    ShareAlert(InvestigationLocation);
}

void AEndlessEnemyController::Think() { UpdateBehaviorDecision(); }

void AEndlessEnemyController::UpdateBehaviorDecision()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(EndlessAI_Decision);
    CSV_SCOPED_TIMING_STAT(EndlessAI, Decision);
    APawn* Self=GetPawn();
    if (!bBrainActive || !Self || !GetWorld() || State==EEndlessEnemyState::Dead) return;
    ++Decisions;
    const float Now=GetWorld()->GetTimeSeconds();
    UpdateStuck(Self,FMath::Max(.01f,Now-LastThinkTime),Now);LastThinkTime=Now;
    APawn* Player=UGameplayStatics::GetPlayerPawn(this,0);
    if (Player && CanSeePlayer(Self,Player))
    {
        LastKnownPlayerLocation=Player->GetActorLocation();LastSeenTime=Now;ShareAlert(LastKnownPlayerLocation);
        if (State!=EEndlessEnemyState::Chase && State!=EEndlessEnemyState::Combat) EnterChase(Now,State);
        const auto* Enemy=Cast<AEndlessLootEnemy>(Self);const auto* Weapon=Enemy ? Enemy->GetEnemyWeapon() : nullptr;
        const float Range=Weapon ? FMath::Min(PreferredDistance,Weapon->EnemyRange*.65f) : PreferredDistance;
        State=FVector::Dist(Self->GetActorLocation(),Player->GetActorLocation())<=Range ? EEndlessEnemyState::Combat : EEndlessEnemyState::Chase;
    }
    else if (State==EEndlessEnemyState::Chase || State==EEndlessEnemyState::Combat)
    {
        State=EEndlessEnemyState::Search;NextRepathTime=0;StopMovement();ClearFocus(EAIFocusPriority::Gameplay);SetFacingMode(false);AttackTokenUntil=0;
    }
    else if (State==EEndlessEnemyState::Search && Now-LastSeenTime>MemorySeconds)
    { State=EEndlessEnemyState::Return;ReturnExpires=Now+12.f;NextRepathTime=0;StopMovement(); }
    else if (State==EEndlessEnemyState::Investigate && (Now>=InvestigationExpires || FVector::Dist(Self->GetNavAgentLocation(),InvestigationLocation)<110.f))
    { State=EEndlessEnemyState::Search;LastKnownPlayerLocation=InvestigationLocation;LastSeenTime=Now;NextSearchPoint=0;StopMovement(); }
    else if (State==EEndlessEnemyState::Return && (Now>=ReturnExpires || FVector::Dist(Self->GetActorLocation(),HomeLocation)<140.f)) EnterPatrol(Now);
    else if (State==EEndlessEnemyState::Idle && Now>=PatrolWaitUntil) State=EEndlessEnemyState::Patrol;
    else if (State==EEndlessEnemyState::Patrol && !IsMoveActive() && Now<PatrolWaitUntil) State=EEndlessEnemyState::Idle;
    SyncBlackboard();
}

void AEndlessEnemyController::ExecuteBehaviorState()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(EndlessAI_Action);
    CSV_SCOPED_TIMING_STAT(EndlessAI, Action);
    APawn* Self=GetPawn();if (!Self || !bBrainActive) return;
    ++Actions;
    const float Now=GetWorld()->GetTimeSeconds();
    switch (State)
    {
    case EEndlessEnemyState::Patrol: TickPatrol(Self,Now);break;
    case EEndlessEnemyState::Chase:
    case EEndlessEnemyState::Combat:
        if (auto* Player=UGameplayStatics::GetPlayerPawn(this,0)) TickChase(Self,Player,Now);break;
    case EEndlessEnemyState::Search: TickSearch(Self,Now);break;
    case EEndlessEnemyState::Investigate:
        if (Now>=NextRepathTime) { MoveWithinRoom(InvestigationLocation,65.f);NextRepathTime=Now+1.f; } break;
    case EEndlessEnemyState::Return:
        if (Now>=NextRepathTime) { MoveWithinRoom(HomeLocation,80.f);NextRepathTime=Now+1.f; } break;
    default: break;
    }
}

void AEndlessEnemyController::UpdateStuck(APawn* Self, const float DeltaSeconds, const float Now)
{
    const FVector Location = Self->GetActorLocation();
    const bool bMoving = GetMoveStatus() == EPathFollowingStatus::Moving;
    const float MinDistance = StuckSpeed * DeltaSeconds;

    if (bMoving && FVector::DistSquared2D(Location, LastThinkLocation) < FMath::Square(MinDistance))
    {
        StuckTime += DeltaSeconds;
    }
    else
    {
        StuckTime = 0.0f;
    }
    LastThinkLocation = Location;

    if (StuckTime >= StuckSeconds)
    {
        if (bLogDebug)
        {
            UE_LOG(LogEndlessEnemyAI, Log, TEXT("%s: stuck, re-planning."), *Self->GetName());
        }
        StopMovement();
        bHasPatrolDestination = false;
        PatrolWaitUntil = Now + 0.2f;
        NextRepathTime = 0.0f;
        StuckTime = 0.0f;
    }
}

bool AEndlessEnemyController::CanSeePlayer(const APawn* Self, APawn* Player)
{
    if (!bPerceivedPlayer || !IsInsideHomeRoom(Player->GetNavAgentLocation())) return false;
    const FVector ToPlayer = Player->GetActorLocation() - Self->GetActorLocation();
    const bool bAlerted = State != EEndlessEnemyState::Patrol && State != EEndlessEnemyState::Idle;
    const float MaxDistance = bAlerted ? LoseSightRadius : SightRadius;
    if (ToPlayer.SizeSquared() > FMath::Square(MaxDistance))
    {
        return false;
    }

    if (!bAlerted)
    {
        const FVector Forward = Self->GetActorForwardVector().GetSafeNormal2D();
        const FVector Direction = ToPlayer.GetSafeNormal2D();
        const float MinDot = FMath::Cos(FMath::DegreesToRadians(SightHalfAngle));
        if (FVector::DotProduct(Forward, Direction) < MinDot)
        {
            return false;
        }
    }

    return true; // Perception performs visibility tests; the attack checks muzzle LOS again.
}

void AEndlessEnemyController::EnterPatrol(const float Now)
{
    if (bLogDebug && GetPawn())
    {
        UE_LOG(LogEndlessEnemyAI, Log, TEXT("%s: -> Patrol"), *GetPawn()->GetName());
    }
    State = EEndlessEnemyState::Patrol;
    ClearFocus(EAIFocusPriority::Gameplay);
    SetFacingMode(false);
    StopMovement();
    bHasPatrolDestination = false;
    PatrolWaitUntil = Now + 0.5f;
}

void AEndlessEnemyController::EnterChase(const float Now, const EEndlessEnemyState PreviousState)
{
    if (bLogDebug && GetPawn())
    {
        UE_LOG(LogEndlessEnemyAI, Log, TEXT("%s: -> Chase"), *GetPawn()->GetName());
    }
    State = EEndlessEnemyState::Chase;
    bHasPatrolDestination = false;
    NextRepathTime = 0.0f;
    SetFacingMode(true);
    if (PreviousState == EEndlessEnemyState::Patrol || PreviousState == EEndlessEnemyState::Idle)
    {
        NextFireTime = FMath::Max(NextFireTime, Now + ReactionTime);
    }
}

void AEndlessEnemyController::TickPatrol(APawn* Self, const float Now)
{
    // A trip that finished (or failed) leaves the move status idle: pause, then pick a new point.
    if (bHasPatrolDestination && !IsMoveActive())
    {
        bHasPatrolDestination = false;
        PatrolWaitUntil = Now + FMath::FRandRange(
            FMath::Min(PatrolWaitMin, PatrolWaitMax), FMath::Max(PatrolWaitMin, PatrolWaitMax));
        return;
    }
    if (IsMoveActive() || Now < PatrolWaitUntil)
    {
        return;
    }

    UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
    if (!NavSys)
    {
        PatrolWaitUntil = Now + 1.0f;
        return;
    }

    const FVector Location = Self->GetActorLocation();
    FNavLocation Best;
    bool bFound = false;
    for (int32 Attempt = 0; Attempt < 6; ++Attempt)
    {
        FNavLocation Candidate;
        if (!NavSys->GetRandomReachablePointInRadius(Location, PatrolRadius, Candidate))
        {
            continue;
        }
        FNavPathSharedPtr Path;
        if (!BuildRoomPath(Candidate.Location,Path)) continue;
        Best = Candidate;
        bFound = true;
        if (FVector::DistSquared2D(Candidate.Location, Location) >= FMath::Square(MinPatrolDistance))
        {
            break;
        }
    }

    if (!bFound)
    {
        if (bLogDebug)
        {
            UE_LOG(LogEndlessEnemyAI, Warning,
                TEXT("%s: no reachable patrol point (NavMesh missing or not built yet?)."),
                *Self->GetName());
        }
        PatrolWaitUntil = Now + 1.0f;
        return;
    }

    const EPathFollowingRequestResult::Type Result = MoveWithinRoom(Best.Location,PatrolAcceptRadius);

    if (Result == EPathFollowingRequestResult::RequestSuccessful)
    {
        bHasPatrolDestination = true;
    }
    else
    {
        PatrolWaitUntil = Now + 0.5f;
    }
}

void AEndlessEnemyController::TickChase(APawn* Self, APawn* Player, const float Now)
{
    SetFacingMode(true);
    SetFocus(Player);

    const float Distance = FVector::Dist(Self->GetActorLocation(), Player->GetActorLocation());
    const auto* Enemy=Cast<AEndlessLootEnemy>(Self);
    const auto* Weapon=Enemy ? Enemy->GetEnemyWeapon() : nullptr;
    const float DesiredDistance=Weapon ? FMath::Min(PreferredDistance,Weapon->EnemyRange*.65f) : PreferredDistance;
    if (Now>=NextRepathTime)
    {
        bool Crowded=false;
        TArray<FOverlapResult> Nearby;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(CombatSpacing),false,Self);
        GetWorld()->OverlapMultiByObjectType(Nearby,Self->GetActorLocation(),FQuat::Identity,FCollisionObjectQueryParams(ECC_Pawn),FCollisionShape::MakeSphere(150.f),Query);
        for (const auto& Hit:Nearby) if (Hit.GetActor()!=Player) Crowded=true;
        if (Distance>DesiredDistance || Distance<220.f || Crowded)
        {
            FVector Position;
            if (FindCombatPosition(LastKnownPlayerLocation,FMath::Clamp(DesiredDistance*.75f,250.f,650.f),Position)) MoveWithinRoom(Position,55.f);
            else if (Distance>DesiredDistance) MoveWithinRoom(LastKnownPlayerLocation,DesiredDistance*.7f);
        }
        NextRepathTime=Now+FMath::Max(.8f,RepathInterval);
    }

    TryShoot(Self, Player, Distance, Now);
}

void AEndlessEnemyController::TickSearch(APawn* Self, const float Now)
{
    // 真上・真下の別フロアを「最終確認位置に到着」と扱わない。
    const float DistanceToLastKnown = FVector::Dist(Self->GetNavAgentLocation(), LastKnownPlayerLocation);
    if (DistanceToLastKnown > 150.0f && (Now >= NextRepathTime))
    {
        MoveWithinRoom(LastKnownPlayerLocation,100.f);
        NextRepathTime = Now + RepathInterval * 2.0f;
    }
    else if (DistanceToLastKnown<=150.f && Now>=NextSearchPoint)
    {
        if (auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
        {
            FNavLocation Point;
            if (Nav->GetRandomReachablePointInRadius(LastKnownPlayerLocation,300.f,Point))
            {
                SearchDestination=Point.Location;MoveWithinRoom(SearchDestination,60.f);
            }
        }
        NextSearchPoint=Now+1.2f;
    }
}

void AEndlessEnemyController::TryShoot(APawn* Self, const APawn* Player, const float Distance, const float Now)
{
    auto* Enemy=Cast<AEndlessLootEnemy>(Self);const auto* Weapon=Enemy ? Enemy->GetEnemyWeapon() : nullptr;
    if (Distance > (Weapon ? Weapon->EnemyRange : FireRange) || Now < NextFireTime)
    {
        return;
    }

    const FVector Forward = Self->GetActorForwardVector().GetSafeNormal2D();
    const FVector Direction = (Player->GetActorLocation() - Self->GetActorLocation()).GetSafeNormal2D();
    if (FVector::DotProduct(Forward, Direction) < 0.85f)
    {
        return; // still turning towards the player
    }

    if (!LineOfSightTo(Player) || !AcquireAttackToken(Now)) return;
    NextFireTime = Now + (Weapon ? Weapon->EnemyFireInterval : FireInterval + FMath::FRandRange(0.0f,FireIntervalVariance));
    FireShot(Self);
}

void AEndlessEnemyController::FireShot(APawn* Self)
{
    if (auto* Enemy=Cast<AEndlessLootEnemy>(Self)) { Enemy->FireEquippedWeapon(UGameplayStatics::GetPlayerPawn(this,0));return; }
    if (UFunction* ShootFunction = Self->FindFunction(ShootFunctionName))
    {
        if (ShootFunction->NumParms == 0)
        {
            Self->ProcessEvent(ShootFunction, nullptr);
        }
        else if (!bWarnedMissingShoot)
        {
            bWarnedMissingShoot = true;
            UE_LOG(LogEndlessEnemyAI, Warning,
                TEXT("%s: '%s' has parameters; it must be a parameterless function/event."),
                *Self->GetName(), *ShootFunctionName.ToString());
        }
        return;
    }

    if (!bWarnedMissingShoot)
    {
        bWarnedMissingShoot = true;
        UE_LOG(LogEndlessEnemyAI, Warning,
            TEXT("%s: no function named '%s'; broadcasting OnShootRequested instead."),
            *Self->GetName(), *ShootFunctionName.ToString());
    }
    OnShootRequested.Broadcast();
}

void AEndlessEnemyController::SetFacingMode(const bool bFaceTarget)
{
    // AAIController::TickがFocusからControlRotationを更新する。戦闘中だけ有効。
    SetActorTickEnabled(bBrainActive);
    SetActorTickInterval(bFaceTarget ? 1.f/30.f : .1f);
    if (ACharacter* OwnerChar = Cast<ACharacter>(GetPawn()))
    {
        OwnerChar->bUseControllerRotationYaw = bFaceTarget;
        if (UCharacterMovementComponent* Movement = OwnerChar->GetCharacterMovement())
        {
            Movement->bOrientRotationToMovement = !bFaceTarget;
            Movement->MaxWalkSpeed=bFaceTarget ? 440.f : 220.f;
        }
    }
}

bool AEndlessEnemyController::IsMoveActive() const
{
    return GetMoveStatus() != EPathFollowingStatus::Idle;
}

void AEndlessEnemyController::ConfigureBehaviorAssets(UBehaviorTree* Tree,UBlackboardData* Board)
{
    if (!Tree || !Board) return;
    if (Tree->RootNode)
    {
        if (Tree->RootNode->Children.Num()==8 && Tree->RootNode->Children.ContainsByPredicate([](const FBTCompositeChild& Child){return !Child.ChildTask;})==false)
        {
            // Editorのグラフ補完中に孤立扱いされた既存Taskの一時フラグを解除して保存する。
            Tree->RootNode->ClearFlags(RF_Transient);
            for (auto& Child:Tree->RootNode->Children)
            {
                Child.ChildTask->Rename(nullptr,Tree,REN_DontCreateRedirectors|REN_DoNotDirty);
                Child.ChildTask->ClearFlags(RF_Transient);
                Child.ChildTask->SetFlags(RF_Transactional);
            }
            for (const auto& Service:Tree->RootNode->Services) if (Service) Service->ClearFlags(RF_Transient);
            Tree->MarkPackageDirty();
            return;
        }
        Tree->RootNode=nullptr;
#if WITH_EDITORONLY_DATA
        Tree->BTGraph=nullptr;
#endif
    }
    for (const FName Name : {FName(TEXT("State")),FName(TEXT("LastSeen")),FName(TEXT("LastHeard")),FName(TEXT("Target"))})
    {
        if (Board->Keys.ContainsByPredicate([Name](const FBlackboardEntry& E){return E.EntryName==Name;})) continue;
        FBlackboardEntry Entry;Entry.EntryName=Name;
        if (Name==TEXT("State")) Entry.KeyType=NewObject<UBlackboardKeyType_Int>(Board);
        else if (Name==TEXT("Target")) Entry.KeyType=NewObject<UBlackboardKeyType_Object>(Board);
        else Entry.KeyType=NewObject<UBlackboardKeyType_Vector>(Board);
        Board->Keys.Add(Entry);
    }
    Tree->BlackboardAsset=Board;
    auto* Root=NewObject<UBTComposite_Selector>(Tree);Tree->RootNode=Root;
    Root->Services.Add(NewObject<UEndlessEnemyDecisionService>(Tree));
    for (int32 I=0;I<=int32(EEndlessEnemyState::Dead);++I)
    {
        auto* Task=NewObject<UEndlessEnemyStateTask>(Tree);
        Task->ExpectedState=I;Task->NodeName=StaticEnum<EEndlessEnemyState>()->GetNameStringByValue(I);
        FBTCompositeChild Child;Child.ChildTask=Task;Root->Children.Add(Child);
    }
    Tree->MarkPackageDirty();Board->MarkPackageDirty();
}
void AEndlessEnemyController::StartEnemyBehavior()
{
    if (!EnemyBehaviorTree) EnemyBehaviorTree=LoadObject<UBehaviorTree>(nullptr,TEXT("/Game/Endless/AI/BT_RoomEnemy.BT_RoomEnemy"));
    if (!EnemyBehaviorTree)
    {
        EnemyBehaviorTree=NewObject<UBehaviorTree>(this);
        auto* Board=NewObject<UBlackboardData>(EnemyBehaviorTree);
        ConfigureBehaviorAssets(EnemyBehaviorTree,Board);
    }
    RunBehaviorTree(EnemyBehaviorTree);SyncBlackboard();
}
void AEndlessEnemyController::SyncBlackboard()
{
    if (auto* Board=GetBlackboardComponent())
    {
        Board->SetValueAsInt(TEXT("State"),int32(State));
        Board->SetValueAsVector(TEXT("LastSeen"),LastKnownPlayerLocation);
        Board->SetValueAsVector(TEXT("LastHeard"),LastHeardSourceLocation);
        Board->SetValueAsObject(TEXT("Target"),bPerceivedPlayer ? UGameplayStatics::GetPlayerPawn(this,0) : nullptr);
    }
}
void AEndlessEnemyController::PerceptionUpdated(AActor* Actor,FAIStimulus Stimulus)
{
    if (!bBrainActive || Actor!=UGameplayStatics::GetPlayerPawn(this,0)) return;
    if (Stimulus.Type==UAISense::GetSenseID<UAISense_Sight>())
    {
        bPerceivedPlayer=Stimulus.WasSuccessfullySensed();
        if (bPerceivedPlayer && GetPawn() && IsInsideHomeRoom(Cast<APawn>(Actor)->GetNavAgentLocation()))
        { LastKnownPlayerLocation=Stimulus.StimulusLocation;LastSeenTime=GetWorld()->GetTimeSeconds(); }
    }
    else if (Stimulus.Type==UAISense::GetSenseID<UAISense_Hearing>() && Stimulus.WasSuccessfullySensed())
    {
        const int32 Kind=FCString::Atoi(*Stimulus.Tag.ToString());
        HearNoise(Actor,Stimulus.StimulusLocation,EEndlessNoiseKind(FMath::Clamp(Kind,0,4)),Stimulus.Strength*6000.f);
    }
}
void AEndlessEnemyController::SetRoomActive(bool bActive)
{
    if (bRoomActive==bActive || State==EEndlessEnemyState::Dead) return;
    bRoomActive=bActive;SetBrainActive(bActive);
    if (auto* RoomCharacter=Cast<ACharacter>(GetPawn()))
    {
        RoomCharacter->SetActorTickEnabled(bActive);
        RoomCharacter->GetCharacterMovement()->SetComponentTickEnabled(bActive);
        RoomCharacter->GetMesh()->SetComponentTickEnabled(bActive);
        if (auto* Sensory=RoomCharacter->FindComponentByClass<UEndlessSensoryComponent>()) Sensory->SetComponentTickEnabled(bActive);
    }
}
void AEndlessEnemyController::MarkDead()
{
    SetBrainActive(false);State=EEndlessEnemyState::Dead;SyncBlackboard();
    if (BrainComponent) BrainComponent->StopLogic(TEXT("Dead"));
}

bool AEndlessEnemyController::HasAttackToken() const { return bBrainActive && GetWorld() && AttackTokenUntil>GetWorld()->GetTimeSeconds(); }

FString AEndlessEnemyController::GetBehaviorDebug() const
{
    return BrainComponent ? BrainComponent->GetDebugInfoString() : TEXT("No brain");
}

ETeamAttitude::Type AEndlessEnemyController::GetTeamAttitudeTowards(const AActor& Other) const
{
    // 敵同士の視覚クエリを作らず、プレイヤーだけを検知対象にする。
    return Other.IsA<AEndlessPlayerCharacter>() ? ETeamAttitude::Hostile : ETeamAttitude::Friendly;
}
