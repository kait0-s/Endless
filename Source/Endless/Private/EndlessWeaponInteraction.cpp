#include "EndlessPlayerWeapons.h"
#include "EndlessWeaponLoot.h"
#include "RoomDoor.h"
#include "EngineUtils.h"

AActor* AEndlessPlayerCharacter::FindInteractionTarget() const
{
    AActor* Best=nullptr;
    float Distance=TNumericLimits<float>::Max();
    auto Consider=[&](AActor* Actor)
    {
        const float D=FVector::DistSquared(GetActorLocation(),Actor->GetActorLocation());
        if (D<Distance) { Best=Actor; Distance=D; }
    };
    for (TActorIterator<AEndlessRoomDoor> It(GetWorld());It;++It)
        if (It->CanNativeInteract(const_cast<AEndlessPlayerCharacter*>(this))) Consider(*It);
    for (TActorIterator<AEndlessWeaponPickup> It(GetWorld());It;++It)
        if (It->CanCollect(const_cast<AEndlessPlayerCharacter*>(this))) Consider(*It);
    return Best;
}

void AEndlessPlayerCharacter::CancelWeaponExchange()
{
    ExchangePickup.Reset();ExchangeSlot=INDEX_NONE;
}

void AEndlessPlayerCharacter::InteractWithTarget()
{
    if (!CanUseWeapon()) { CancelWeaponExchange();return; }
    if (auto* Pickup=ExchangePickup.Get())
    {
        if (!Pickup->CanCollect(this) || !OwnedWeaponIndices.IsValidIndex(ExchangeSlot)) { CancelWeaponExchange();return; }
        // 選択した1枠だけを交換する。失敗時は元の弾薬と所持状態を戻す。
        ReadLegacyAmmo();CancelWeaponReload();StopWeaponFire();
        const auto SavedOwned=OwnedWeaponIndices;const auto SavedAmmo=WeaponAmmo;
        const int32 Removed=OwnedWeaponIndices[ExchangeSlot];
        OwnedWeaponIndices.RemoveAt(ExchangeSlot);
        if (Pickup->TryCollect(this))
        {
            WeaponAmmo[Removed]=FEndlessWeaponAmmo();
            const int32 Added=OwnedWeaponIndices.Pop();OwnedWeaponIndices.Insert(Added,ExchangeSlot);
            // 旧GameInstance弾薬を新しい装備へ読み戻さない。
            EquippedWeaponIndex=INDEX_NONE;EquipWeapon(Added);
        }
        else { OwnedWeaponIndices=SavedOwned;WeaponAmmo=SavedAmmo;PublishAmmo(); }
        CancelWeaponExchange();return;
    }
    AActor* Target=FindInteractionTarget();
    if (auto* Pickup=Cast<AEndlessWeaponPickup>(Target))
    {
        if (!Pickup->TryCollect(this) && OwnedWeaponIndices.Num()>=3)
        { ExchangePickup=Pickup;ExchangeSlot=INDEX_NONE; }
    }
    else if (auto* Door=Cast<AEndlessRoomDoor>(Target)) Door->NativeInteract(this);
}

FText AEndlessPlayerCharacter::GetInteractionPrompt() const
{
    if (!CanUseWeapon()) return FText::GetEmpty();
    FString Text=FString::Printf(TEXT("所持 %d/3  "),OwnedWeaponIndices.Num());
    if (ExchangePickup.IsValid())
    {
        if (OwnedWeaponIndices.IsValidIndex(ExchangeSlot))
            Text+=FString::Printf(TEXT("交換: %s → %s / インタラクトで確定・離れて取消"),
                *WeaponCatalog->Weapons[OwnedWeaponIndices[ExchangeSlot]].DisplayName.ToString(),*ExchangePickup->Contents.WeaponId.ToString());
        else Text+=TEXT("所持上限: 武器切替で交換する枠を選択 / 離れて取消");
    }
    else if (auto* Pickup=Cast<AEndlessWeaponPickup>(FindInteractionTarget()))
        Text+=FString::Printf(TEXT("インタラクト: %s を拾う%s"),*Pickup->Contents.WeaponId.ToString(),OwnedWeaponIndices.Num()>=3 ? TEXT(" / 未所持なら交換選択") : TEXT(""));
    else if (FindInteractionTarget()) Text+=TEXT("インタラクト: ドアを開く");
    return FText::FromString(Text);
}
