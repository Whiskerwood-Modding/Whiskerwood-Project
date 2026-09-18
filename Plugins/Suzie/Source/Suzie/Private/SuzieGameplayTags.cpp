#include "SuzieGameplayTags.h"
#include "SuziePlugin.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "GameplayTagsManager.h"
#include "GameplayTagContainer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FSuzieGameplayTags& FSuzieGameplayTags::Get()
{
    static FSuzieGameplayTags Instance;
    return Instance;
}

void FSuzieGameplayTags::CollectFromJmap(const TSharedPtr<FJsonObject>& GlobalObjectMap)
{
    for (auto It = GlobalObjectMap->Values.CreateConstIterator(); It; ++It)
    {
        const TSharedPtr<FJsonObject> Definition = It.Value()->AsObject();
        if (!Definition || Definition->GetStringField(TEXT("type")) != TEXT("Object"))
        {
            continue;
        }
        // GameplayTagsSettings derives from UGameplayTagsList and is the backing list of the
        // DefaultGameplayTags.ini tag source (its CDO carries the ini-loaded GameplayTagList)
        FString ClassPath;
        if (!Definition->TryGetStringField(TEXT("class"), ClassPath) ||
            (ClassPath != TEXT("/Script/GameplayTags.GameplayTagsList") && ClassPath != TEXT("/Script/GameplayTags.GameplayTagsSettings")))
        {
            continue;
        }
        const TSharedPtr<FJsonObject>* PropertyValues = nullptr;
        if (!Definition->TryGetObjectField(TEXT("property_values"), PropertyValues))
        {
            continue;
        }
        const TArray<TSharedPtr<FJsonValue>>* TagRows = nullptr;
        if (!(*PropertyValues)->TryGetArrayField(TEXT("GameplayTagList"), TagRows))
        {
            continue;
        }

        const int32 NumCollectedBefore = CollectedTagNames.Num();
        for (const TSharedPtr<FJsonValue>& TagRowValue : *TagRows)
        {
            const TSharedPtr<FJsonObject> TagRow = TagRowValue->AsObject();
            if (!TagRow)
            {
                continue;
            }
            FString TagString;
            if (!TagRow->TryGetStringField(TEXT("Tag"), TagString) || TagString.IsEmpty())
            {
                continue;
            }
            const FName TagName(*TagString);
            bool bAlreadyCollected = false;
            CollectedTagNameSet.Add(TagName, &bAlreadyCollected);
            if (!bAlreadyCollected)
            {
                CollectedTagNames.Add(TagName);
            }
        }
        UE_LOG(LogSuzie, Display, TEXT("Collected %d gameplay tags from %s"), CollectedTagNames.Num() - NumCollectedBefore, *It.Key());
    }
}

void FSuzieGameplayTags::RegisterCollectedTags()
{
    if (CollectedTagNames.IsEmpty())
    {
        return;
    }

    // Native registration was sealed during the OnPostEngineInit broadcast (before this editor
    // module loads), so the legacy FName path and FNativeGameplayTag both fail here. The editor-only
    // injection path stores the tags so they survive every later tag tree reconstruction.
    UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
    Manager.AddEditorOnlyNativeGameplayTags(CollectedTagNames);

    UE_LOG(LogSuzie, Display, TEXT("Registered %d native gameplay tags from jmap"), CollectedTagNames.Num());

    WriteRegisteredTagsReport(Manager);
}

void FSuzieGameplayTags::WriteRegisteredTagsReport(const UGameplayTagsManager& Manager) const
{
    const FString OutputDir = FPaths::ProjectDir() / TEXT("Automation");

    // {
    //     TArray<FString> Lines;
    //     Lines.Reserve(CollectedTagNames.Num());
    //     for (const FName& TagName : CollectedTagNames)
    //     {
    //         Lines.Add(TagName.ToString());
    //     }
    //     Lines.Sort();
    //     Lines.Insert(FString::Printf(TEXT("# %d gameplay tags harvested from jmap by Suzie"), Lines.Num()), 0);
    //     const FString Path = OutputDir / TEXT("GameplayTags.txt");
    //     if (FFileHelper::SaveStringArrayToFile(Lines, *Path))
    //     {
    //         UE_LOG(LogSuzie, Display, TEXT("Wrote harvested gameplay tag list to %s"), *Path);
    //     }
    //     else
    //     {
    //         UE_LOG(LogSuzie, Warning, TEXT("Failed to write harvested gameplay tag list to %s"), *Path);
    //     }
    // }

    {
        FGameplayTagContainer AllTags;
        Manager.RequestAllGameplayTags(AllTags, /*OnlyIncludeDictionaryTags*/ false);

        TArray<FString> Lines;
        Lines.Reserve(AllTags.Num());
        for (const FGameplayTag& Tag : AllTags)
        {
            Lines.Add(Tag.GetTagName().ToString());
        }
        Lines.Sort();
        const FString Path = OutputDir / TEXT("GameplayTags.txt");
        if (FFileHelper::SaveStringArrayToFile(Lines, *Path))
        {
            UE_LOG(LogSuzie, Display, TEXT("Wrote full registered gameplay tag list (%d tags) to %s"), AllTags.Num(), *Path);
        }
        else
        {
            UE_LOG(LogSuzie, Warning, TEXT("Failed to write registered gameplay tag list to %s"), *Path);
        }
    }
}
