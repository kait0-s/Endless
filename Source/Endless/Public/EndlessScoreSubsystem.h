#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "EndlessScoreSubsystem.generated.h"

/** One finished run. */
USTRUCT(BlueprintType)
struct ENDLESS_API FEndlessScoreEntry
{
    GENERATED_BODY()

    /** 1, 2, 3... counted over the lifetime of the save file. */
    UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Score")
    int32 RunNumber = 0;

    UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Score")
    int32 Kills = 0;

    UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Score")
    FDateTime PlayedAt;

    /** Ready-to-display "YYYY/MM/DD HH:MM". */
    UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Score")
    FString PlayedAtText;
};

/** What SubmitScore tells the score screen about the run that was just saved. */
USTRUCT(BlueprintType)
struct ENDLESS_API FEndlessScoreResult
{
    GENERATED_BODY()

    /** True when this run beat every earlier run. */
    UPROPERTY(BlueprintReadOnly, Category = "Score")
    bool bNewBest = false;

    /** 1 = best of all runs (ties share the higher rank). */
    UPROPERTY(BlueprintReadOnly, Category = "Score")
    int32 Rank = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Score")
    int32 TotalRuns = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Score")
    int32 RunNumber = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Score")
    int32 Kills = 0;

    /** Best score including this run. */
    UPROPERTY(BlueprintReadOnly, Category = "Score")
    int32 BestScore = 0;

    /** Best score before this run (0 on the first run). */
    UPROPERTY(BlueprintReadOnly, Category = "Score")
    int32 PreviousBest = 0;
};

UCLASS()
class ENDLESS_API UEndlessScoreSaveGame : public USaveGame
{
    GENERATED_BODY()

public:
    UPROPERTY(SaveGame)
    TArray<FEndlessScoreEntry> Entries;

    UPROPERTY(SaveGame)
    int32 NextRunNumber = 1;
};

/**
 * Local kill-count ranking. Lives as long as the GameInstance, saves to
 * Saved/SaveGames/EndlessScores.sav immediately on every SubmitScore.
 */
UCLASS()
class ENDLESS_API UEndlessScoreSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    /** Record a finished run (call ONCE per run) and save to disk. */
    UFUNCTION(BlueprintCallable, Category = "Endless|Score")
    FEndlessScoreResult SubmitScore(int32 Kills);

    /** Best runs first (ties: newer first). Count <= 0 returns everything. */
    UFUNCTION(BlueprintCallable, Category = "Endless|Score")
    TArray<FEndlessScoreEntry> GetTopScores(int32 Count) const;

    /** Newest runs first. Count <= 0 returns everything. */
    UFUNCTION(BlueprintCallable, Category = "Endless|Score")
    TArray<FEndlessScoreEntry> GetRecentScores(int32 Count) const;

    UFUNCTION(BlueprintPure, Category = "Endless|Score")
    int32 GetBestScore() const;

    UFUNCTION(BlueprintPure, Category = "Endless|Score")
    int32 GetTotalRuns() const;

    /** Wipe all saved runs. */
    UFUNCTION(BlueprintCallable, Category = "Endless|Score")
    void ClearScores();

    /** Oldest runs are dropped beyond this many. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Endless|Score")
    int32 MaxStoredEntries = 500;

private:
    void Save() const;

    UPROPERTY()
    TObjectPtr<UEndlessScoreSaveGame> SaveData;
};
