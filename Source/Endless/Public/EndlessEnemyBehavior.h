#pragma once
#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTTaskNode.h"
#include "EndlessEnemyBehavior.generated.h"

/** 既存の部屋内移動と武器処理をBTから再利用する。共有ノードに個体状態を置かない。 */
UCLASS()
class ENDLESS_API UEndlessEnemyDecisionService : public UBTService
{
    GENERATED_BODY()
public:
    UEndlessEnemyDecisionService();
    virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* Memory, float DeltaSeconds) override;
};

UCLASS()
class ENDLESS_API UEndlessEnemyStateTask : public UBTTaskNode
{
    GENERATED_BODY()
public:
    UEndlessEnemyStateTask();
    UPROPERTY(EditAnywhere, Category="Enemy") int32 ExpectedState = 0;
    virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* Memory) override;
    virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* Memory, float DeltaSeconds) override;
};
