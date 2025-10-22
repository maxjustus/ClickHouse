#pragma once

#include <Core/ColumnWithTypeAndName.h>
#include <Core/Field.h>
#include <string_view>

/// Shared helpers for JSONMergePatch
/// Implementation in JSONMergePatchHelpers.cpp

namespace DB
{

/// All implementations require RapidJSON and are in the .cpp file
namespace JSONMergePatchHelpers
{
    /// Extract Object from column (handles both String and JSON types)
    Object extractObject(const ColumnWithTypeAndName & arg, size_t row);

    /// Apply single JSON patch entry to destination object
    void applyPatchEntry(Object & dest, std::string_view path, Field && value);

    /// Merge two objects according to RFC 7386 JSONMergePatch
    void mergeObjects(Object & dest, const Object & src);
}

}
