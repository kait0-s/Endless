#pragma once
#include "CoreMinimal.h"

// 実弾の掃引結果だけを描画する。命中判定やカメラへ影響しない。
namespace EndlessCombatEffects
{
    void Trail(UWorld* World,FVector Start,FVector End,FLinearColor Color,float Width,float Seconds);
    void Flash(UWorld* World,FVector Location,FVector Direction,FLinearColor Color,float Size,float Seconds);
}
