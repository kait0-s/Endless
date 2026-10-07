#include "EndlessScoreSubsystem.h"

#include "Kismet/GameplayStatics.h"

namespace
{
    const TCHAR* const ScoreSlotName = TEXT("EndlessScores");
    constexpr int32 ScoreUserIndex = 0;
}

void UEndlessScoreSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    if (UGameplayStatics::DoesSaveGameExist(ScoreSlotName, ScoreUserIndex))
    {
        SaveData = Cast<UEndlessScoreSaveGame>(
            UGameplayStatics::LoadGameFromSlot(ScoreSlotName, ScoreUserIndex));
    }
    if (!SaveData)
    {
        SaveData = NewObject<UEndlessScoreSaveGame>(this);
    }
}

FEndlessScoreResult UEndlessScoreSubsystem::SubmitScore(const int32 Kills)
{
    if (!SaveData)
    {
        SaveData = NewObject<UEndlessScoreSaveGame>(this);
    }

    const int32 SafeKills = FMath::Max(0, Kills);

    FEndlessScoreResult Result;
    Result.PreviousBest = GetBestScore();

    int32 HigherCount = 0;
    for (const FEndlessScoreEntry& Existing : SaveData->Entries)
    {
        if (Existing.Kills > SafeKills)
        {
            ++HigherCount;
        }
    }

    FEndlessScoreEntry Entry;
    Entry.RunNumber = SaveData->NextRunNumber++;
    Entry.Kills = SafeKills;
    Entry.PlayedAt = FDateTime::Now();
    Entry.PlayedAtText = Entry.PlayedAt.ToString(TEXT("%Y/%m/%d %H:%M"));
    SaveData->Entries.Add(Entry);

    const int32 MaxEntries = FMath::Max(1, MaxStoredEntries);
    if (SaveData->Entries.Num() > MaxEntries)
    {
        SaveData->Entries.RemoveAt(0, SaveData->Entries.Num() - MaxEntries);
    }

    Result.Kills = SafeKills;
    Result.RunNumber = Entry.RunNumber;
    Result.Rank = HigherCount + 1;
    Result.TotalRuns = SaveData->Entries.Num();
    Result.bNewBest = SafeKills > Result.PreviousBest;
    Result.BestScore = FMath::Max(Result.PreviousBest, SafeKills);

    Save();
    return Result;
}

TArray<FEndlessScoreEntry> UEndlessScoreSubsystem::GetTopScores(const int32 Count) const
{
    TArray<FEndlessScoreEntry> Sorted;
    if (SaveData)
    {
        Sorted = SaveData->Entries;
    }

    Sorted.Sort([](const FEndlessScoreEntry& A, const FEndlessScoreEntry& B)
    {
        return A.Kills != B.Kills ? A.Kills > B.Kills : A.RunNumber > B.RunNumber;
    });

    if (Count > 0 && Sorted.Num() > Count)
    {
        Sorted.SetNum(Count);
    }
    return Sorted;
}

TArray<FEndlessScoreEntry> UEndlessScoreSubsystem::GetRecentScores(const int32 Count) const
{
    TArray<FEndlessScoreEntry> Recent;
    if (!SaveData)
    {
        return Recent;
    }

    for (int32 Index = SaveData->Entries.Num() - 1; Index >= 0; --Index)
    {
        if (Count > 0 && Recent.Num() >= Count)
        {
            break;
        }
        Recent.Add(SaveData->Entries[Index]);
    }
    return Recent;
}

int32 UEndlessScoreSubsystem::GetBestScore() const
{
    int32 Best = 0;
    if (SaveData)
    {
        for (const FEndlessScoreEntry& Entry : SaveData->Entries)
        {
            Best = FMath::Max(Best, Entry.Kills);
        }
    }
    return Best;
}

int32 UEndlessScoreSubsystem::GetTotalRuns() const
{
    return SaveData ? SaveData->Entries.Num() : 0;
}

void UEndlessScoreSubsystem::ClearScores()
{
    SaveData = NewObject<UEndlessScoreSaveGame>(this);
    Save();
}

void UEndlessScoreSubsystem::Save() const
{
    if (SaveData)
    {
        UGameplayStatics::SaveGameToSlot(SaveData, ScoreSlotName, ScoreUserIndex);
    }
}
