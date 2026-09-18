#pragma once

#include "CoreMinimal.h"
#include "Serialization/UnversionedSchemaOverride.h"

class FJsonObject;
class FJsonValue;
class FAutoConsoleCommand;

// Builds game-accurate unversioned property schemas from jmap reflection data and serves them to
// CoreUObject through the schema override provider hook.
//
// Cooked assets index properties by their position in the PropertyLink chain of the class that the
// game compiled, so any divergence between the game's classes and the editor's shifts every
// subsequent schema index. The jmap dump contains the game's exact per-class property lists in
// ChildProperties order (the order UStruct::Link uses), which lets us rebuild the game's schema:
// each game schema index is mapped to the editor property of the same name and stream shape, and
// game-only properties become "discard" entries backed by shadow FProperties that consume the
// value from the stream without storing it.
class FSuzieSchemaOverrides
{
public:
    static FSuzieSchemaOverrides& Get();

    // Builds a shadow FProperty from a jmap property descriptor (wraps FSuziePluginModule::BuildProperty
    // with a live class generation context)
    using FShadowPropertyBuilder = TFunction<FProperty*(FFieldVariant Owner, const TSharedPtr<FJsonObject>& PropertyJson)>;

    // Harvests /Script/ Class and ScriptStruct definitions from a parsed jmap object map and
    // prebuilds per-level schema override entries. Game thread only, before RegisterProvider.
    void BuildFromJmap(const TSharedPtr<FJsonObject>& GlobalObjectMap, const FShadowPropertyBuilder& ShadowPropertyBuilder);

    // Registers the provider with CoreUObject and flushes already-cached schemas. No-op when no
    // harvested level differs from the editor's runtime schema.
    void RegisterProvider();
    void UnregisterProvider();

private:
    // Schema override data for one level of a struct hierarchy (one struct's own properties,
    // excluding supers). Full schemas are composed by concatenating levels derived-first, matching
    // PropertyLink order.
    struct FLevelOverride
    {
        TArray<UE::UnversionedSchema::FOverrideEntry> Entries;
        // True when Entries do not exactly match the editor's own-level schema segment; overrides
        // only activate for chains containing at least one differing level
        bool bDiffers = false;
        // True when a shadow property could not be built; chains containing this level keep the
        // editor schema since we cannot represent the game's stream
        bool bPoisoned = false;
        // True when the game parented this struct differently than the editor; per-level
        // composition cannot repair that, so affected chains keep the editor schema
        bool bReparented = false;
    };

    bool ProvideSchema(const UStruct* Struct, TArray<UE::UnversionedSchema::FOverrideEntry>& OutEntries);
    void BuildLevelOverride(UStruct* Level, const TSharedPtr<FJsonObject>& StructDefinition,
        const TSharedPtr<FJsonObject>& GlobalObjectMap, const FShadowPropertyBuilder& ShadowPropertyBuilder);

    // Records game structs the game serialized natively/binary (STRUCT_SerializeNative / Immutable)
    // but which Suzie generated as plain tagged-property structs. These cannot be fixed by schema
    // overrides - any cooked asset containing one corrupts from that point.
    // The scan lists them so a handler can be written.
    void CheckNativeSerializedStruct(const UStruct* Level, const TSharedPtr<FJsonObject>& StructDefinition);

    // True when the jmap property descriptor and the editor property produce identical serialized
    // streams for unversioned values (applied recursively for containers)
    static bool IsShapeCompatible(const TSharedPtr<FJsonObject>& PropertyJson, const FProperty* EditorProperty);
    // Guards against the checkf crashes in FindOrCreateScriptStruct/FindOrCreateEnum when a
    // descriptor references a type that exists neither in the editor nor in the jmap
    static bool CanBuildShadowSafely(const TSharedPtr<FJsonObject>& PropertyJson, const TSharedPtr<FJsonObject>& GlobalObjectMap);

    FProperty* BuildShadowProperty(const TSharedPtr<FJsonObject>& PropertyJson, const FShadowPropertyBuilder& ShadowPropertyBuilder);
    UStruct* GetShadowPropertyOwner();
    void DumpSchema(const TArray<FString>& Args);

    mutable FRWLock OverridesLock;
    TMap<const UStruct*, FLevelOverride> LevelOverrides;

    // Rooted transient owner of all shadow properties; never linked or registered, only keeps the
    // properties and their referenced types alive and destroys them on shutdown
    UStruct* ShadowPropertyOwner = nullptr;
    FField* ShadowPropertyOwnerTail = nullptr;

    // De-duplicates "declined due to poisoned/reparented level" logs from loading threads
    FCriticalSection DeclinedStructsLock;
    TSet<FName> LoggedDeclinedStructs;

    TUniquePtr<FAutoConsoleCommand> DumpSchemaCommand;

    int32 NumDifferingLevels = 0;
    int32 NumDiscardEntries = 0;
    int32 NumPoisonedLevels = 0;
    int32 NumReparentedLevels = 0;

    // Game structs serialized natively in-game but generated as reflected structs in the editor
    TArray<FString> NativeSerializedGameStructs;
};
