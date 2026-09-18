#include "SuzieCustomSerializers.h"

#include "UObject/Class.h"
#include "UObject/UnrealType.h"

namespace SuzieCustomSerializers
{
    FStructSerializeFn FindStructSerializer(const FString& StructPath)
    {
        return nullptr;
    }
}
