#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UGameplayTagsManager;

// Imports the game's gameplay tags from a jmap dump into the editor's gameplay tags manager.
//
// Cooked blueprints reference tags the game registered from C++ (EGameplayTagSourceType::Native)
// or from its own ini tag sources; none of those exist in the editor project, so the engine's
// cooked-load tag filter drops them. A full jmap dump contains the game's GameplayTagsManager
// instance whose UGameplayTagsList subobjects (one per tag source, e.g.
// /Engine/Transient.GameplayTagsManager_X:Native) list every registered tag.
class FSuzieGameplayTags
{
public:
    static FSuzieGameplayTags& Get();

    // Collects tag names from every GameplayTagsList instance in the jmap object map (all tag
    // sources, deduplicated across files)
    void CollectFromJmap(const TSharedPtr<FJsonObject>& GlobalObjectMap);

    // Registers all collected tags with the gameplay tags manager as native tags. Called from
    // StartupModule; native tag registration is already sealed by then (DoneAddingNativeTags fires
    // on the OnPostEngineInit broadcast, before PostEngineInit plugin modules load), so this uses
    // the editor-only injection path that survives tag tree reconstruction.
    void RegisterCollectedTags();

private:
    // Writes the harvested tag list and the full project-registered tag list to
    // {ProjectSaved}/Suzie/*.txt for cross-checking (works even if the editor later fails to load)
    void WriteRegisteredTagsReport(const UGameplayTagsManager& Manager) const;

    TArray<FName> CollectedTagNames;
    TSet<FName> CollectedTagNameSet;
};
