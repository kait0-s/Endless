#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "TimerManager.h"
#include "EndlessSensory.h"
#include "Perception/AIPerceptionTypes.h"
#include "EndlessEnemyController.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FEndlessEnemyShootRequested);

UENUM(BlueprintType)
enum class EEndlessEnemyState : uint8
{
    /** Wandering the level until the player is spotted. */
    Patrol,
    /** Player in sight: run at him and shoot. */
    Chase,
    /** Lost sight: go to the last known position, then give up after MemorySeconds. */
    Search,
    Investigate,
    Return,
    Idle,
    Combat,
    Dead
};

/**
 * One self-contained brain for enemies: patrol -> chase + shoot -> search -> patrol.
 * Set as the AI Controller Class of BP_Enemy. Movement/decisions live here; the actual shot is
 * delegated to the pawn's existing parameterless function/event (default name "EnemyShoot").
 * Perception updates memory; a throttled Behavior Tree service selects the room-local state.
 */
UCLASS(BlueprintType, Blueprintable)
class ENDLESS_API AEndlessEnemyController : public AAIController
{
    GENERATED_BODY()

public:
    AEndlessEnemyController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<class UAIPerceptionComponent> EnemyPerception;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) TObjectPtr<class UBehaviorTree> EnemyBehaviorTree;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool bRoomActive = true;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 MoveFailures = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 Decisions = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 Actions = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 TaskExecutions = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 TaskTicks = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 LastTaskState = -1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 AttackTokensDenied = 0;
    UFUNCTION(BlueprintCallable) static void ConfigureBehaviorAssets(class UBehaviorTree* Tree, class UBlackboardData* Board);
    UFUNCTION(BlueprintCallable) void UpdateBehaviorDecision();
    UFUNCTION(BlueprintCallable) void ExecuteBehaviorState();
    UFUNCTION(BlueprintCallable) void SetRoomActive(bool bActive);
    UFUNCTION(BlueprintCallable) void MarkDead();
    UFUNCTION(BlueprintPure) bool HasAttackToken() const;
    UFUNCTION(BlueprintPure) FString GetBehaviorDebug() const;
    virtual ETeamAttitude::Type GetTeamAttitudeTowards(const AActor& Other) const override;
    static void DispatchNoise(AActor* Source, FVector Location, EEndlessNoiseKind Kind, float Radius);
    UFUNCTION(BlueprintPure, Category="Enemy AI|Room") bool IsInsideHomeRoom(FVector Location, float Margin = 0.f) const;
    UFUNCTION(BlueprintCallable, Category="Enemy AI|Room") bool ResolveRoomDestination(FVector Requested, FVector& Resolved) const;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Enemy AI|Room") TObjectPtr<ULevel> HomeRoom;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Enemy AI|Room") int32 SharedAlertsReceived = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy AI|Room") float AlertShareRadius = 900.f;
    UFUNCTION(BlueprintCallable, Category="Enemy AI|Hearing") void HearNoise(AActor* Source, FVector Location, EEndlessNoiseKind Kind, float Radius);
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Enemy AI|Hearing") FVector InvestigationLocation;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Enemy AI|Hearing") FVector LastHeardSourceLocation;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Enemy AI|Hearing") int32 HeardEvents = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Enemy AI|Hearing") EEndlessNoiseKind LastNoiseKind = EEndlessNoiseKind::Walk;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy AI|Hearing") float InvestigationSeconds = 15.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy AI|Hearing") float OccludedHearingScale = .4f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy AI|Hearing") float DifferentFloorHearingScale = .15f;

    /** Make the enemy aware of the player right now (call this when the enemy is damaged). */
    UFUNCTION(BlueprintCallable, Category = "Enemy AI")
    void AlertToPlayer();

    /** Stop/resume all behaviour (e.g. on death or game over). */
    UFUNCTION(BlueprintCallable, Category = "Enemy AI")
    void SetBrainActive(bool bActive);

    UFUNCTION(BlueprintPure, Category = "Enemy AI")
    EEndlessEnemyState GetBrainState() const { return State; }

    /** Fired only when the pawn has no function named ShootFunctionName (bind it in Blueprint instead). */
    UPROPERTY(BlueprintAssignable, Category = "Enemy AI")
    FEndlessEnemyShootRequested OnShootRequested;

    // ---- Vision ----
    /** How far an unaware enemy can see (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Sight", meta = (ClampMin = "100"))
    float SightRadius = 2500.0f;

    /** How far an alerted enemy keeps seeing (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Sight", meta = (ClampMin = "100"))
    float LoseSightRadius = 3500.0f;

    /** Half of the vision cone for an unaware enemy (degrees). 180 = sees all around. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Sight", meta = (ClampMin = "10", ClampMax = "180"))
    float SightHalfAngle = 70.0f;

    /** Seconds the enemy keeps hunting after losing sight. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Sight", meta = (ClampMin = "0"))
    float MemorySeconds = 8.0f;

    // ---- Patrol ----
    /** Wander radius around the enemy's current position (cm). Bigger = roams more of the level. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Patrol", meta = (ClampMin = "200"))
    float PatrolRadius = 3000.0f;

    /** Avoid tiny trips: try to pick points at least this far away (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Patrol", meta = (ClampMin = "0"))
    float MinPatrolDistance = 500.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Patrol", meta = (ClampMin = "0"))
    float PatrolWaitMin = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Patrol", meta = (ClampMin = "0"))
    float PatrolWaitMax = 2.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Patrol", meta = (ClampMin = "10"))
    float PatrolAcceptRadius = 80.0f;

    // ---- Combat ----
    /** Stop approaching at about this distance (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Combat", meta = (ClampMin = "100"))
    float PreferredDistance = 800.0f;

    /** Never shoot from further than this (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Combat", meta = (ClampMin = "100"))
    float FireRange = 2200.0f;

    /** Seconds between shots, plus a random 0..FireIntervalVariance. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Combat", meta = (ClampMin = "0.1"))
    float FireInterval = 1.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Combat", meta = (ClampMin = "0"))
    float FireIntervalVariance = 0.6f;

    /** Delay between first spotting the player and the first shot (seconds). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Combat", meta = (ClampMin = "0"))
    float ReactionTime = 0.7f;

    /** Name of the parameterless function/event on the pawn that fires one shot. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Combat")
    FName ShootFunctionName = TEXT("EnemyShoot");

    // ---- Tuning ----
    /** How often the brain thinks (seconds). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Tuning", meta = (ClampMin = "0.05"))
    float ThinkInterval = 0.2f;

    /** Wait this long (+random) after spawning before thinking: lets NavMesh/level finish loading. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Tuning", meta = (ClampMin = "0"))
    float InitialDelay = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Tuning", meta = (ClampMin = "0"))
    float InitialDelayVariance = 1.0f;

    /** Re-issue the chase move at most this often (seconds). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Tuning", meta = (ClampMin = "0.1"))
    float RepathInterval = 0.5f;

    /** Moving slower than this (cm/s) for StuckSeconds counts as stuck. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Tuning", meta = (ClampMin = "0"))
    float StuckSpeed = 20.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Tuning", meta = (ClampMin = "0.2"))
    float StuckSeconds = 1.5f;

    /** Log state changes and failures to the Output Log. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Tuning")
    bool bLogDebug = false;

protected:
    virtual void OnPossess(APawn* InPawn) override;
    virtual void OnUnPossess() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    UFUNCTION() void PerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);
    void StartEnemyBehavior();
    void SyncBlackboard();
    bool AcquireAttackToken(float Now);
    bool FindCombatPosition(FVector Target, float Distance, FVector& Position) const;
    float AttackTokenUntil = 0.f;
    float AttackTokenRestUntil = 0.f;
    float SuspendedAt = 0.f;
    bool bPerceivedPlayer = false;
    UPROPERTY() TObjectPtr<class UAISenseConfig_Sight> SightConfig;
    UPROPERTY() TObjectPtr<class UAISenseConfig_Hearing> HearingConfig;
    void CacheRoom();
    void ShareAlert(FVector Location);
    EPathFollowingRequestResult::Type MoveWithinRoom(FVector Destination, float AcceptanceRadius);
    bool BuildRoomPath(FVector Destination, FNavPathSharedPtr& Path) const;
    TArray<FBox> RoomFloorBounds;
    float NextAlertShareTime = 0.f;
    void Think();
    void UpdateStuck(APawn* Self, float DeltaSeconds, float Now);
    bool CanSeePlayer(const APawn* Self, APawn* Player);

    void EnterPatrol(float Now);
    void EnterChase(float Now, EEndlessEnemyState PreviousState);
    void TickPatrol(APawn* Self, float Now);
    void TickChase(APawn* Self, APawn* Player, float Now);
    void TickSearch(APawn* Self, float Now);

    void TryShoot(APawn* Self, const APawn* Player, float Distance, float Now);
    void FireShot(APawn* Self);
    void SetFacingMode(bool bFaceTarget);
    bool IsMoveActive() const;

    FTimerHandle ThinkTimer;
    FVector FailedDestination=FVector::ZeroVector;
    float FailedMoveRetryTime=0.f;
    int32 FailedMoveCount=0;
    EEndlessEnemyState State = EEndlessEnemyState::Patrol;
    bool bBrainActive = true;

    FVector LastKnownPlayerLocation = FVector::ZeroVector;
    FVector LastThinkLocation = FVector::ZeroVector;
    float LastThinkTime = 0.0f;
    float LastSeenTime = -1000.0f;
    float NextFireTime = 0.0f;
    float NextRepathTime = 0.0f;
    float PatrolWaitUntil = 0.0f;
    float StuckTime = 0.0f;
    bool bHasPatrolDestination = false;
    bool bWarnedMissingShoot = false;
    FVector HomeLocation = FVector::ZeroVector;
    FVector SearchDestination = FVector::ZeroVector;
    float InvestigationExpires = 0.f;
    float ReturnExpires = 0.f;
    float NextSearchPoint = 0.f;
};
