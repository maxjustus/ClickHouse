#pragma once

#include <Core/ColumnWithTypeAndName.h>
#include <Core/Field.h>

/// Shared helpers for JSONMergePatch
/// Implementation in JSONMergePatchHelpers.cpp

namespace DB
{

/// All implementations require RapidJSON and are in the .cpp file
namespace JSONMergePatchHelpers
{
    /// Extract Object from column (handles both String and JSON types)
    Object extractObject(const ColumnWithTypeAndName & arg, size_t row);

    /// Merge two objects according to RFC 7386 JSONMergePatch
    void mergeObjects(Object & dest, const Object & src);
}

}
