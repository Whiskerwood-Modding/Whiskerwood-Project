#pragma once

#include "CoreMinimal.h"

class UScriptStruct;
class FArchive;

// Custom serializers for dynamically-generated game structs whose in-game type is
// STRUCT_SerializeNative / STRUCT_Immutable. Those structs store their contents in the game's own
// binary format inside cooked assets, bypassing tagged/unversioned property serialization entirely,
// so Suzie's reflected reconstruction of them would misread the bytes and corrupt the asset (and
// everything after it in the export). Registering a serializer here makes Suzie install it as the
// struct's CppStructOps, which sets STRUCT_SerializeNative on the editor struct and routes the
// struct's contents through the matching binary (de)serializer instead
namespace SuzieCustomSerializers
{
    // Serializes a single instance of a dynamic struct in the game's native binary format.
    // Ar may be loading or saving; Data points to the struct's reflected memory; Struct is the
    // dynamic UScriptStruct. Return true when the struct was fully handled (false falls back to
    // default serialization).
    using FStructSerializeFn = bool (*)(FArchive& Ar, void* Data, UScriptStruct* Struct);

    // Returns the serializer registered for the given /Script/ struct path, or nullptr.
    FStructSerializeFn FindStructSerializer(const FString& StructPath);
}
