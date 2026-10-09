#include "EndlessEnemyBehavior.h"
#include "EndlessEnemyController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"

UEndlessEnemyDecisionService::UEndlessEnemyDecisionService()
{
    NodeName=TEXT("Update perception memory and decision");
    Interval=.2f;RandomDeviation=.025f;bNotifyTick=true;
}
void UEndlessEnemyDecisionService::TickNode(UBehaviorTreeComponent& OwnerComp,uint8* Memory,float DeltaSeconds)
{
    Super::TickNode(OwnerComp,Memory,DeltaSeconds);
    if (auto* AI=Cast<AEndlessEnemyController>(OwnerComp.GetAIOwner())) AI->UpdateBehaviorDecision();
}
UEndlessEnemyStateTask::UEndlessEnemyStateTask()
{
    NodeName=TEXT("Execute room enemy state");bNotifyTick=true;bTickIntervals=true;
}
EBTNodeResult::Type UEndlessEnemyStateTask::ExecuteTask(UBehaviorTreeComponent& OwnerComp,uint8* Memory)
{
    auto* AI=Cast<AEndlessEnemyController>(OwnerComp.GetAIOwner());
    if (AI) { ++AI->TaskExecutions;AI->LastTaskState=ExpectedState; }
    if (!AI || !OwnerComp.GetBlackboardComponent() || OwnerComp.GetBlackboardComponent()->GetValueAsInt(TEXT("State"))!=ExpectedState) return EBTNodeResult::Failed;
    AI->ExecuteBehaviorState();SetNextTickTime(Memory,.2f);return EBTNodeResult::InProgress;
}
void UEndlessEnemyStateTask::TickTask(UBehaviorTreeComponent& OwnerComp,uint8* Memory,float DeltaSeconds)
{
    auto* AI=Cast<AEndlessEnemyController>(OwnerComp.GetAIOwner());
    if (AI) { ++AI->TaskTicks;AI->LastTaskState=ExpectedState; }
    if (!AI || OwnerComp.GetBlackboardComponent()->GetValueAsInt(TEXT("State"))!=ExpectedState)
    { FinishLatentTask(OwnerComp,EBTNodeResult::Succeeded);return; }
    AI->ExecuteBehaviorState();SetNextTickTime(Memory,.2f);
}
