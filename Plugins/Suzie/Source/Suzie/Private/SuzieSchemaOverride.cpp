#include "SuzieSchemaOverride.h"
#include "SuziePlugin.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeLock.h"
#include "Misc/ScopeRWLock.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3
#include "UObject/PropertyOptional.h"
#endif

FSuzieSchemaOverrides& FSuzieSchemaOverrides::Get()
{
    static FSuzieSchemaOverrides Instance;
    return Instance;
}

// jmap property type discriminators are FField class names, with one historical quirk
static FString NormalizeJmapPropertyTypeName(const FString& TypeName)
{
    if (TypeName == TEXT("FUtf8StrProperty"))
    {
        return TEXT("Utf8StrProperty");
    }
    return TypeName;
}

// Serialized size of the underlying integer of a jmap numeric property type, or 0 when not numeric
static int32 GetJmapNumericTypeSize(const FString& TypeName)
{
    if (TypeName == TEXT("Int8Property") || TypeName == TEXT("ByteProperty") || TypeName == TEXT("BoolProperty"))
    {
        return 1;
    }
    if (TypeName == TEXT("Int16Property") || TypeName == TEXT("UInt16Property"))
    {
        return 2;
    }
    if (TypeName == TEXT("IntProperty") || TypeName == TEXT("UInt32Property") || TypeName == TEXT("FloatProperty"))
    {
        return 4;
    }
    if (TypeName == TEXT("Int64Property") || TypeName == TEXT("UInt64Property") || TypeName == TEXT("DoubleProperty"))
    {
        return 8;
    }
    return 0;
}

bool FSuzieSchemaOverrides::IsShapeCompatible(const TSharedPtr<FJsonObject>& PropertyJson, const FProperty* EditorProperty)
{
    if (!PropertyJson || EditorProperty == nullptr)
    {
        return false;
    }
    const FString JmapType = NormalizeJmapPropertyTypeName(PropertyJson->GetStringField(TEXT("type")));
    const FString EditorType = EditorProperty->GetClass()->GetName();

    // Object-likes: the serialized stream depends only on the category (packed index, soft path,
    // weak/lazy guid, interface), never on the referenced class
    if (JmapType == TEXT("ObjectProperty") || JmapType == TEXT("ClassProperty"))
    {
        return EditorType == TEXT("ObjectProperty") || EditorType == TEXT("ClassProperty");
    }
    if (JmapType == TEXT("SoftObjectProperty") || JmapType == TEXT("SoftClassProperty"))
    {
        return EditorType == TEXT("SoftObjectProperty") || EditorType == TEXT("SoftClassProperty");
    }

    // Enums serialize as their underlying integer, so only the underlying size has to match;
    // byte-sized enum properties and (enum) byte properties are interchangeable in the stream
    if (JmapType == TEXT("EnumProperty"))
    {
        int32 GameUnderlyingSize = 1;
        const TSharedPtr<FJsonObject>* ContainerJson = nullptr;
        if (PropertyJson->TryGetObjectField(TEXT("container"), ContainerJson) && ContainerJson->IsValid())
        {
            GameUnderlyingSize = GetJmapNumericTypeSize((*ContainerJson)->GetStringField(TEXT("type")));
        }
        if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(EditorProperty))
        {
            const FNumericProperty* UnderlyingProperty = EnumProperty->GetUnderlyingProperty();
            return UnderlyingProperty && UnderlyingProperty->GetElementSize() == GameUnderlyingSize;
        }
        if (EditorProperty->IsA<FByteProperty>())
        {
            return GameUnderlyingSize == 1;
        }
        return false;
    }
    if (JmapType == TEXT("ByteProperty"))
    {
        if (EditorProperty->IsA<FByteProperty>())
        {
            return true;
        }
        if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(EditorProperty))
        {
            const FNumericProperty* UnderlyingProperty = EnumProperty->GetUnderlyingProperty();
            return UnderlyingProperty && UnderlyingProperty->GetElementSize() == 1;
        }
        return false;
    }

    // The nested stream of a struct is governed by that struct's own (override-corrected) schema
    // or native serializer, so the same struct type implies the same stream
    if (JmapType == TEXT("StructProperty"))
    {
        const FStructProperty* StructProperty = CastField<FStructProperty>(EditorProperty);
        return StructProperty && StructProperty->Struct &&
            StructProperty->Struct->GetPathName() == PropertyJson->GetStringField(TEXT("struct"));
    }

    // Containers: same container type and recursively compatible element types
    if (JmapType == TEXT("ArrayProperty"))
    {
        const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(EditorProperty);
        const TSharedPtr<FJsonObject>* InnerJson = nullptr;
        return ArrayProperty && PropertyJson->TryGetObjectField(TEXT("inner"), InnerJson) &&
            IsShapeCompatible(*InnerJson, ArrayProperty->Inner);
    }
    if (JmapType == TEXT("SetProperty"))
    {
        const FSetProperty* SetProperty = CastField<FSetProperty>(EditorProperty);
        const TSharedPtr<FJsonObject>* ElementJson = nullptr;
        return SetProperty && PropertyJson->TryGetObjectField(TEXT("key_prop"), ElementJson) &&
            IsShapeCompatible(*ElementJson, SetProperty->ElementProp);
    }
    if (JmapType == TEXT("MapProperty"))
    {
        const FMapProperty* MapProperty = CastField<FMapProperty>(EditorProperty);
        const TSharedPtr<FJsonObject>* KeyJson = nullptr;
        const TSharedPtr<FJsonObject>* ValueJson = nullptr;
        return MapProperty &&
            PropertyJson->TryGetObjectField(TEXT("key_prop"), KeyJson) && IsShapeCompatible(*KeyJson, MapProperty->KeyProp) &&
            PropertyJson->TryGetObjectField(TEXT("value_prop"), ValueJson) && IsShapeCompatible(*ValueJson, MapProperty->ValueProp);
    }
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3
    if (JmapType == TEXT("OptionalProperty"))
    {
        const FOptionalProperty* OptionalProperty = CastField<FOptionalProperty>(EditorProperty);
        const TSharedPtr<FJsonObject>* InnerJson = nullptr;
        return OptionalProperty && PropertyJson->TryGetObjectField(TEXT("inner"), InnerJson) &&
            IsShapeCompatible(*InnerJson, OptionalProperty->GetValueProperty());
    }
#endif

    // Everything else (numerics, bool, strings, names, text, delegates, field paths, weak/lazy
    // object and interface properties) requires the exact property class. Bools always serialize
    // one byte, so bitfield masks can be ignored. Inline and sparse multicast delegates have
    // different stream formats and never cross-match.
    return JmapType == EditorType;
}

bool FSuzieSchemaOverrides::CanBuildShadowSafely(const TSharedPtr<FJsonObject>& PropertyJson, const TSharedPtr<FJsonObject>& GlobalObjectMap)
{
    if (!PropertyJson)
    {
        return false;
    }

    // FindOrCreateScriptStruct/FindOrCreateEnum checkf-crash when the referenced type exists
    // neither in the editor nor in the jmap, so verify resolvability before building
    auto CanResolveReference = [&GlobalObjectMap](const FString& ObjectPath, const TCHAR* ExpectedJmapType, UClass* ObjectClass) -> bool
    {
        if (ObjectPath.IsEmpty())
        {
            return false;
        }
        if (StaticFindObject(ObjectClass, nullptr, *ObjectPath))
        {
            return true;
        }
        const TSharedPtr<FJsonObject>* Definition = nullptr;
        return GlobalObjectMap->TryGetObjectField(ObjectPath, Definition) &&
            (*Definition)->GetStringField(TEXT("type")) == ExpectedJmapType;
    };
    auto RecurseField = [&PropertyJson, &GlobalObjectMap](const TCHAR* FieldName) -> bool
    {
        const TSharedPtr<FJsonObject>* InnerJson = nullptr;
        return PropertyJson->TryGetObjectField(FieldName, InnerJson) && CanBuildShadowSafely(*InnerJson, GlobalObjectMap);
    };

    const FString JmapType = NormalizeJmapPropertyTypeName(PropertyJson->GetStringField(TEXT("type")));
    if (JmapType == TEXT("StructProperty"))
    {
        return CanResolveReference(PropertyJson->GetStringField(TEXT("struct")), TEXT("ScriptStruct"), UScriptStruct::StaticClass());
    }
    if (JmapType == TEXT("EnumProperty"))
    {
        return CanResolveReference(PropertyJson->GetStringField(TEXT("enum")), TEXT("Enum"), UEnum::StaticClass()) && RecurseField(TEXT("container"));
    }
    if (JmapType == TEXT("ByteProperty"))
    {
        FString EnumPath;
        if (PropertyJson->TryGetStringField(TEXT("enum"), EnumPath) && !EnumPath.IsEmpty())
        {
            return CanResolveReference(EnumPath, TEXT("Enum"), UEnum::StaticClass());
        }
        return true;
    }
    if (JmapType == TEXT("ArrayProperty") || JmapType == TEXT("OptionalProperty"))
    {
        return RecurseField(TEXT("inner"));
    }
    if (JmapType == TEXT("SetProperty"))
    {
        return RecurseField(TEXT("key_prop"));
    }
    if (JmapType == TEXT("MapProperty"))
    {
        return RecurseField(TEXT("key_prop")) && RecurseField(TEXT("value_prop"));
    }
    return true;
}

UStruct* FSuzieSchemaOverrides::GetShadowPropertyOwner()
{
    if (ShadowPropertyOwner == nullptr)
    {
        // Never bound, linked or registered: only owns the shadow properties so GC keeps their
        // referenced types alive (UStruct::AddReferencedObjects visits ChildProperties) and the
        // property linked list is destroyed in an orderly fashion on shutdown
        ShadowPropertyOwner = NewObject<UScriptStruct>(GetTransientPackage(), TEXT("SuzieShadowPropertyOwner"), RF_Public | RF_Transient | RF_MarkAsRootSet);
    }
    return ShadowPropertyOwner;
}

FProperty* FSuzieSchemaOverrides::BuildShadowProperty(const TSharedPtr<FJsonObject>& PropertyJson, const FShadowPropertyBuilder& ShadowPropertyBuilder)
{
    // Patch the type name quirk on a shallow copy so BuildProperty can resolve the field class
    TSharedPtr<FJsonObject> EffectiveJson = PropertyJson;
    const FString RawTypeName = PropertyJson->GetStringField(TEXT("type"));
    const FString NormalizedTypeName = NormalizeJmapPropertyTypeName(RawTypeName);
    if (NormalizedTypeName != RawTypeName)
    {
        EffectiveJson = MakeShared<FJsonObject>();
        EffectiveJson->Values = PropertyJson->Values;
        EffectiveJson->SetStringField(TEXT("type"), NormalizedTypeName);
    }

    FProperty* ShadowProperty = ShadowPropertyBuilder(FFieldVariant(GetShadowPropertyOwner()), EffectiveJson);
    if (ShadowProperty == nullptr)
    {
        return nullptr;
    }

    // One schema entry is emitted per game array index, each consuming a single element
    ShadowProperty->ArrayDim = 1;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
    // Shadow properties do not belong to the struct being loaded and must never participate in
    // initialized-property-value tracking
    ShadowProperty->PropertyFlags &= ~CPF_RequiredParm;
#endif

    // Link standalone to compute element size and inner layouts (same as dynamic class properties)
    FArchive EmptyLinkArchive;
    ShadowProperty->Link(EmptyLinkArchive);

    // Append to the rooted owner's property list
    ShadowProperty->Next = nullptr;
    if (ShadowPropertyOwnerTail != nullptr)
    {
        ShadowPropertyOwnerTail->Next = ShadowProperty;
    }
    else
    {
        GetShadowPropertyOwner()->ChildProperties = ShadowProperty;
    }
    ShadowPropertyOwnerTail = ShadowProperty;

    return ShadowProperty;
}

void FSuzieSchemaOverrides::BuildFromJmap(const TSharedPtr<FJsonObject>& GlobalObjectMap, const FShadowPropertyBuilder& ShadowPropertyBuilder)
{
    check(IsInGameThread());
    FWriteScopeLock Lock(OverridesLock);

    for (auto It = GlobalObjectMap->Values.CreateConstIterator(); It; ++It)
    {
        const FString& ObjectPath = It.Key();
        if (!ObjectPath.StartsWith(TEXT("/Script/")))
        {
            continue;
        }
        const TSharedPtr<FJsonObject> Definition = It.Value()->AsObject();
        if (!Definition)
        {
            continue;
        }
        const FString ObjectType = Definition->GetStringField(TEXT("type"));
        if (ObjectType != TEXT("Class") && ObjectType != TEXT("ScriptStruct"))
        {
            continue;
        }

        // Meatloaf bug (commit d8179e8): CDOs of UClass-derived native classes are labeled with
        // their class type instead of "Object", skip them like the class generation pass does
        int32 LastDelimiterIndex = INDEX_NONE;
        if (!ObjectPath.FindLastChar(TEXT(':'), LastDelimiterIndex))
        {
            ObjectPath.FindLastChar(TEXT('.'), LastDelimiterIndex);
        }
        const FString ObjectName = LastDelimiterIndex != INDEX_NONE ? ObjectPath.Mid(LastDelimiterIndex + 1) : ObjectPath;
        if (ObjectName.StartsWith(TEXT("Default__")))
        {
            continue;
        }

        UStruct* Level = FindObject<UStruct>(nullptr, *ObjectPath);
        if (Level == nullptr)
        {
            UE_LOG(LogSuzie, Verbose, TEXT("Schema override: no editor struct for jmap entry %s, skipping"), *ObjectPath);
            continue;
        }
        if (LevelOverrides.Contains(Level))
        {
            UE_LOG(LogSuzie, Verbose, TEXT("Schema override: %s already harvested from an earlier jmap file, skipping"), *ObjectPath);
            continue;
        }

        if (ObjectType == TEXT("ScriptStruct"))
        {
            CheckNativeSerializedStruct(Level, Definition);
        }
        BuildLevelOverride(Level, Definition, GlobalObjectMap, ShadowPropertyBuilder);
    }
}

void FSuzieSchemaOverrides::BuildLevelOverride(UStruct* Level, const TSharedPtr<FJsonObject>& StructDefinition,
    const TSharedPtr<FJsonObject>& GlobalObjectMap, const FShadowPropertyBuilder& ShadowPropertyBuilder)
{
    FLevelOverride LevelOverride;

    // Per-level composition assumes the game and editor agree on the parent chain
    FString JmapSuperPath;
    StructDefinition->TryGetStringField(TEXT("super_struct"), JmapSuperPath);
    const UStruct* EditorSuper = Level->GetSuperStruct();
    const FString EditorSuperPath = EditorSuper ? EditorSuper->GetPathName() : FString();
    // Meatloaf bug (commit d8179e8): UClass-derived native classes dump a null super_struct
    const bool bSuperMatches = JmapSuperPath == EditorSuperPath ||
        (JmapSuperPath.IsEmpty() && EditorSuperPath == TEXT("/Script/CoreUObject.Class"));
    if (!bSuperMatches)
    {
        // An empty game super_struct means the dump has no parent for this level
        if (JmapSuperPath.IsEmpty())
        {
            UE_LOG(LogSuzie, Verbose, TEXT("Schema override: %s has no resolved game parent (editor parent '%s'); keeping editor schema"),
                *Level->GetPathName(), *EditorSuperPath);
        }
        else
        {
            UE_LOG(LogSuzie, Warning, TEXT("Schema override: %s is parented differently in the game ('%s') than in the editor ('%s'); structs in this chain keep the editor schema"),
                *Level->GetPathName(), *JmapSuperPath, *EditorSuperPath);
        }
        LevelOverride.bReparented = true;
        ++NumReparentedLevels;
        LevelOverrides.Add(Level, MoveTemp(LevelOverride));
        return;
    }

    // Editor properties of this level only (supers contribute their own levels)
    TMap<FName, FProperty*> EditorProperties;
    for (FField* Field = Level->ChildProperties; Field; Field = Field->Next)
    {
        if (FProperty* Property = CastField<FProperty>(Field))
        {
            EditorProperties.Add(Property->GetFName(), Property);
        }
    }

    // The game is a shipping build: editor-only properties were compiled out entirely, so every
    // jmap property is part of the game's schema and must be emitted - never filter here. A
    // CPF_EditorOnly flag on a jmap property just means the flag survived in a shipped property.
    int32 NumLevelDiscards = 0;
    const TArray<TSharedPtr<FJsonValue>>* PropertyDescriptors = nullptr;
    StructDefinition->TryGetArrayField(TEXT("properties"), PropertyDescriptors);
    if (PropertyDescriptors != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& DescriptorValue : *PropertyDescriptors)
        {
            const TSharedPtr<FJsonObject> Descriptor = DescriptorValue->AsObject();
            if (!Descriptor)
            {
                continue;
            }
            const FString PropertyNameString = Descriptor->GetStringField(TEXT("name"));
            int32 GameArrayDim = 1;
            Descriptor->TryGetNumberField(TEXT("array_dim"), GameArrayDim);
            GameArrayDim = FMath::Max(GameArrayDim, 1);

            FProperty* EditorProperty = EditorProperties.FindRef(FName(*PropertyNameString));
            const bool bCompatible = EditorProperty != nullptr && IsShapeCompatible(Descriptor, EditorProperty);
            if (EditorProperty != nullptr && !bCompatible)
            {
                UE_LOG(LogSuzie, Verbose, TEXT("Schema override: %s.%s changed type in the game (game %s, editor %s), its value will be discarded"),
                    *Level->GetPathName(), *PropertyNameString,
                    *Descriptor->GetStringField(TEXT("type")), *EditorProperty->GetClass()->GetName());
            }
            else if (EditorProperty == nullptr)
            {
                UE_LOG(LogSuzie, Verbose, TEXT("Schema override: %s.%s (%s) does not exist in the editor, its value will be discarded"),
                    *Level->GetPathName(), *PropertyNameString, *Descriptor->GetStringField(TEXT("type")));
            }

            FProperty* ShadowProperty = nullptr;
            for (int32 ArrayIndex = 0; ArrayIndex < GameArrayDim; ++ArrayIndex)
            {
                if (bCompatible && ArrayIndex < EditorProperty->ArrayDim)
                {
                    LevelOverride.Entries.Add({EditorProperty, ArrayIndex, false});
                    continue;
                }
                if (ShadowProperty == nullptr)
                {
                    if (CanBuildShadowSafely(Descriptor, GlobalObjectMap))
                    {
                        ShadowProperty = BuildShadowProperty(Descriptor, ShadowPropertyBuilder);
                    }
                    if (ShadowProperty == nullptr)
                    {
                        UE_LOG(LogSuzie, Error, TEXT("Schema override: cannot build a shadow property for %s.%s (%s); structs containing %s keep the editor schema and may fail to load"),
                            *Level->GetPathName(), *PropertyNameString, *Descriptor->GetStringField(TEXT("type")), *Level->GetName());
                        LevelOverride.bPoisoned = true;
                        break;
                    }
                }
                LevelOverride.Entries.Add({ShadowProperty, 0, true});
                ++NumLevelDiscards;
            }
            if (LevelOverride.bPoisoned)
            {
                break;
            }
        }
    }

    if (LevelOverride.bPoisoned)
    {
        ++NumPoisonedLevels;
    }
    else
    {
        // Overrides only activate for chains where some level actually diverges from what the
        // editor's own PropertyLink walk would produce. Without this every class would be
        // overridden, since even /Script/CoreUObject.Object is a jmap level.
        TArray<UE::UnversionedSchema::FOverrideEntry> BaselineEntries;
        for (FField* Field = Level->ChildProperties; Field; Field = Field->Next)
        {
            FProperty* Property = CastField<FProperty>(Field);
            if (Property == nullptr || Property->IsEditorOnlyProperty())
            {
                continue;
            }
            for (int32 ArrayIndex = 0; ArrayIndex < Property->ArrayDim; ++ArrayIndex)
            {
                BaselineEntries.Add({Property, ArrayIndex, false});
            }
        }
        LevelOverride.bDiffers = LevelOverride.Entries.Num() != BaselineEntries.Num();
        if (!LevelOverride.bDiffers)
        {
            for (int32 EntryIndex = 0; EntryIndex < BaselineEntries.Num(); ++EntryIndex)
            {
                const UE::UnversionedSchema::FOverrideEntry& Entry = LevelOverride.Entries[EntryIndex];
                const UE::UnversionedSchema::FOverrideEntry& BaselineEntry = BaselineEntries[EntryIndex];
                if (Entry.bDiscard || Entry.Property != BaselineEntry.Property || Entry.ArrayIndex != BaselineEntry.ArrayIndex)
                {
                    LevelOverride.bDiffers = true;
                    break;
                }
            }
        }
        if (LevelOverride.bDiffers)
        {
            ++NumDifferingLevels;
            NumDiscardEntries += NumLevelDiscards;
            UE_LOG(LogSuzie, Display, TEXT("Schema override: game schema differs for %s (game %d entries, editor %d entries, %d discards)"),
                *Level->GetPathName(), LevelOverride.Entries.Num(), BaselineEntries.Num(), NumLevelDiscards);
        }
    }

    LevelOverrides.Add(Level, MoveTemp(LevelOverride));
}

void FSuzieSchemaOverrides::CheckNativeSerializedStruct(const UStruct* Level, const TSharedPtr<FJsonObject>& StructDefinition)
{
    FString StructFlags;
    if (!StructDefinition->TryGetStringField(TEXT("struct_flags"), StructFlags))
    {
        return;
    }
    // STRUCT_SerializeNative: struct has a C++ Serialize(FArchive&); STRUCT_Immutable: binary
    // serialized. Both bypass tagged/unversioned property serialization for the struct's contents.
    const bool bGameSerializesNatively = StructFlags.Contains(TEXT("STRUCT_SerializeNative")) || StructFlags.Contains(TEXT("STRUCT_Immutable"));
    if (!bGameSerializesNatively)
    {
        return;
    }

    // Stock engine structs (FBox2D, FSphere, FTransform, the LWC math structs, etc.) carry the same
    // native/binary serializer in the editor, so they serialize identically to the game and are
    // fine - FindObject returned the real engine struct, which still has the flags. Only game
    // structs Suzie generated as plain reflected structs (which never get these flags) are a
    // problem, so compare the editor struct's actual flags rather than trusting the jmap.
    const UScriptStruct* EditorStruct = Cast<UScriptStruct>(Level);
    const bool bEditorSerializesNatively = EditorStruct &&
        (EditorStruct->StructFlags & (STRUCT_SerializeNative | STRUCT_Immutable)) != 0;
    if (bEditorSerializesNatively)
    {
        return;
    }

    NativeSerializedGameStructs.Add(Level->GetPathName());
    UE_LOG(LogSuzie, Warning, TEXT("Schema override: %s is serialized natively in-game (%s) but generated as a reflected struct; cooked assets containing it need a hand-written serializer and will corrupt without one"),
        *Level->GetPathName(), *StructFlags);
}

bool FSuzieSchemaOverrides::ProvideSchema(const UStruct* Struct, TArray<UE::UnversionedSchema::FOverrideEntry>& OutEntries)
{
    FReadScopeLock Lock(OverridesLock);

    bool bAnyLevelDiffers = false;
    bool bAnyLevelUnusable = false;
    for (const UStruct* Level = Struct; Level; Level = Level->GetSuperStruct())
    {
        if (const FLevelOverride* LevelOverride = LevelOverrides.Find(Level))
        {
            bAnyLevelDiffers |= LevelOverride->bDiffers;
            bAnyLevelUnusable |= LevelOverride->bPoisoned || LevelOverride->bReparented;
        }
    }
    if (!bAnyLevelDiffers)
    {
        // Editor schema already matches the game (or we know nothing about this chain)
        return false;
    }
    if (bAnyLevelUnusable)
    {
        FScopeLock LogLock(&DeclinedStructsLock);
        bool bAlreadyLogged = false;
        LoggedDeclinedStructs.Add(Struct->GetFName(), &bAlreadyLogged);
        if (!bAlreadyLogged)
        {
            UE_LOG(LogSuzie, Warning, TEXT("Schema override: %s diverges from the game but its chain contains an unusable level, keeping the editor schema (loads may corrupt)"),
                *Struct->GetPathName());
        }
        return false;
    }

    // Compose the chain derived-first, matching PropertyLink order. Levels without harvested jmap
    // data (cooked BPGCs, whose own properties were deserialized from the cooked package itself
    // and therefore already match the game) contribute their runtime properties.
    for (const UStruct* Level = Struct; Level; Level = Level->GetSuperStruct())
    {
        if (const FLevelOverride* LevelOverride = LevelOverrides.Find(Level))
        {
            OutEntries.Append(LevelOverride->Entries);
        }
        else
        {
            for (FField* Field = Level->ChildProperties; Field; Field = Field->Next)
            {
                FProperty* Property = CastField<FProperty>(Field);
                if (Property == nullptr || Property->IsEditorOnlyProperty())
                {
                    continue;
                }
                for (int32 ArrayIndex = 0; ArrayIndex < Property->ArrayDim; ++ArrayIndex)
                {
                    OutEntries.Add({Property, ArrayIndex, false});
                }
            }
        }
    }
    return true;
}

void FSuzieSchemaOverrides::RegisterProvider()
{
    check(IsInGameThread());

    // Register the diagnostic console command unconditionally - it is useful even when no schema
    // overrides are active (e.g. to compare the editor schema against the jmap by eye).
    if (!DumpSchemaCommand)
    {
        DumpSchemaCommand = MakeUnique<FAutoConsoleCommand>(
            TEXT("Suzie.DumpSchema"),
            TEXT("Prints the unversioned game schema of a struct (its full PropertyLink walk). Usage: Suzie.DumpSchema /Script/UWEInventory.UWEItemType"),
            FConsoleCommandWithArgsDelegate::CreateRaw(this, &FSuzieSchemaOverrides::DumpSchema));
    }

    // Always surface natively-serialized game structs: these are the assets that corrupt despite a
    // matching schema, and the list tells the user exactly which structs need hand-written
    // serializers (see UWEWorldPopSpatialLayer / FUWEWorldPopSpatialCell etc.).
    if (NativeSerializedGameStructs.Num() > 0)
    {
        UE_LOG(LogSuzie, Warning, TEXT("Schema override: %d game structs are serialized natively/binary but were generated as reflected structs. Cooked assets containing these will corrupt unless a serializer is provided:"),
            NativeSerializedGameStructs.Num());
        for (const FString& StructPath : NativeSerializedGameStructs)
        {
            UE_LOG(LogSuzie, Warning, TEXT("    %s"), *StructPath);
        }
    }

    if (NumDifferingLevels > 0)
    {
        UE::UnversionedSchema::SetOverrideProvider(
            [this](const UStruct* Struct, TArray<UE::UnversionedSchema::FOverrideEntry>& OutEntries)
            {
                return ProvideSchema(Struct, OutEntries);
            });
    }

    // Drop every cached unversioned schema, unconditionally. Suzie mutates many UStructs while
    // generating classes (adding properties, relinking); any schema cached from an intermediate
    // class state - e.g. built mid-generation by class-digest hashing or a re-entrant load - would
    // otherwise be used at cooked-asset load time with a stale property layout. This also activates
    // the override provider for already-touched structs. Loading must be quiescent since
    // serialization holds raw schema pointers.
    FlushAsyncLoading();
    UE::UnversionedSchema::FlushAllUnversionedSchemas();

    if (NumDifferingLevels == 0)
    {
        UE_LOG(LogSuzie, Display, TEXT("Schema override: all %d harvested structs match the editor schema, no overrides needed (%d unbuildable, %d reparented)"),
            LevelOverrides.Num(), NumPoisonedLevels, NumReparentedLevels);
    }
    else
    {
        UE_LOG(LogSuzie, Display, TEXT("Schema override: active for %d differing structs out of %d harvested (%d discard entries, %d unbuildable, %d reparented)"),
            NumDifferingLevels, LevelOverrides.Num(), NumDiscardEntries, NumPoisonedLevels, NumReparentedLevels);
    }
}

void FSuzieSchemaOverrides::UnregisterProvider()
{
    UE::UnversionedSchema::SetOverrideProvider(nullptr);
    DumpSchemaCommand.Reset();
}

void FSuzieSchemaOverrides::DumpSchema(const TArray<FString>& Args)
{
    if (Args.Num() < 1)
    {
        UE_LOG(LogSuzie, Display, TEXT("Usage: Suzie.DumpSchema <StructPath>"));
        return;
    }
    UStruct* Struct = FindObject<UStruct>(nullptr, *Args[0]);
    if (Struct == nullptr)
    {
        UE_LOG(LogSuzie, Display, TEXT("Suzie.DumpSchema: struct '%s' not found"), *Args[0]);
        return;
    }

    // What the stock engine schema walk would produce (PropertyLink, editor-only skipped)
    TArray<FString> BaselineLines;
    for (FProperty* Property = Struct->PropertyLink; Property; Property = Property->PropertyLinkNext)
    {
        if (Property->IsEditorOnlyProperty())
        {
            continue;
        }
        for (int32 ArrayIndex = 0; ArrayIndex < Property->ArrayDim; ++ArrayIndex)
        {
            BaselineLines.Add(FString::Printf(TEXT("%s %s[%d] (%s)"), *Property->GetClass()->GetName(), *Property->GetName(), ArrayIndex,
                *Property->GetOwnerStruct()->GetName()));
        }
    }

    TArray<UE::UnversionedSchema::FOverrideEntry> OverrideEntries;
    const bool bOverridden = ProvideSchema(Struct, OverrideEntries);

    UE_LOG(LogSuzie, Display, TEXT("=== Unversioned game schema for %s: %s ==="), *Struct->GetPathName(),
        bOverridden ? TEXT("OVERRIDDEN by Suzie") : TEXT("editor baseline (no override active)"));
    const int32 MaxLines = FMath::Max(BaselineLines.Num(), bOverridden ? OverrideEntries.Num() : 0);
    for (int32 LineIndex = 0; LineIndex < MaxLines; ++LineIndex)
    {
        FString OverrideLine;
        if (bOverridden && OverrideEntries.IsValidIndex(LineIndex))
        {
            const UE::UnversionedSchema::FOverrideEntry& Entry = OverrideEntries[LineIndex];
            OverrideLine = FString::Printf(TEXT("%s%s %s[%d]"), Entry.bDiscard ? TEXT("DISCARD ") : TEXT(""),
                *Entry.Property->GetClass()->GetName(), *Entry.Property->GetName(), Entry.ArrayIndex);
        }
        const FString BaselineLine = BaselineLines.IsValidIndex(LineIndex) ? BaselineLines[LineIndex] : FString();
        if (bOverridden)
        {
            const bool bMatches = OverrideEntries.IsValidIndex(LineIndex) && BaselineLines.IsValidIndex(LineIndex) &&
                !OverrideEntries[LineIndex].bDiscard &&
                BaselineLine.StartsWith(FString::Printf(TEXT("%s %s[%d]"), *OverrideEntries[LineIndex].Property->GetClass()->GetName(),
                    *OverrideEntries[LineIndex].Property->GetName(), OverrideEntries[LineIndex].ArrayIndex));
            UE_LOG(LogSuzie, Display, TEXT("[%4d]%s game: %-60s | editor: %s"), LineIndex, bMatches ? TEXT(" ") : TEXT("*"), *OverrideLine, *BaselineLine);
        }
        else
        {
            UE_LOG(LogSuzie, Display, TEXT("[%4d] %s"), LineIndex, *BaselineLine);
        }
    }
}
