#include <Functions/JSONMergePatchHelpers.h>
#include <config.h>

#if USE_RAPIDJSON

#include <Columns/ColumnString.h>
#include <Columns/ColumnObject.h>
#include <DataTypes/DataTypeString.h>
#include <DataTypes/DataTypeObject.h>
#include <Common/FieldVisitorToString.h>
#include <IO/ReadBufferFromString.h>

#define RAPIDJSON_PARSE_DEFAULT_FLAGS (kParseIterativeFlag)

#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/error/en.h>
#include <string_view>

namespace DB
{

namespace ErrorCodes
{
    extern const int BAD_ARGUMENTS;
}

namespace JSONMergePatchHelpers
{
    /// Convert RapidJSON scalar value to Field
    static Field rapidjsonScalarToField(const rapidjson::Value & value)
    {
        if (value.IsBool())
            return Field(value.GetBool());
        else if (value.IsInt64())
            return Field(value.GetInt64());
        else if (value.IsUint64())
            return Field(value.GetUint64());
        else if (value.IsDouble())
            return Field(value.GetDouble());
        else if (value.IsString())
            return Field(String(value.GetString(), value.GetStringLength()));
        else
            throw Exception(ErrorCodes::BAD_ARGUMENTS, "Unsupported JSON value type");
    }

    /// Forward declaration for recursive use
    static Field rapidjsonArrayToField(const rapidjson::Value & value);
    static void flattenJSONValue(Object & result, const String & path_prefix, const rapidjson::Value & value);

    /// Convert RapidJSON array to Field
    static Field rapidjsonArrayToField(const rapidjson::Value & value)
    {
        Array result;
        result.reserve(value.Size());

        for (const auto & elem : value.GetArray())
        {
            if (elem.IsNull())
                result.push_back(Field(Null()));
            else if (elem.IsObject())
            {
                /// Recursively convert nested object to Object (flattened map)
                Object nested_obj;
                flattenJSONValue(nested_obj, "", elem);
                result.push_back(Field(nested_obj));
            }
            else if (elem.IsArray())
            {
                /// Recursively convert nested array
                result.push_back(rapidjsonArrayToField(elem));
            }
            else
                result.push_back(rapidjsonScalarToField(elem));
        }

        return Field(result);
    }

    /// Recursively flatten JSON object into path -> value map
    static void flattenJSONValue(Object & result, const String & path_prefix, const rapidjson::Value & value)
    {
        if (value.IsObject())
        {
            /// Recursively flatten nested objects
            for (auto it = value.MemberBegin(); it != value.MemberEnd(); ++it)
            {
                String new_path = path_prefix.empty()
                    ? String(it->name.GetString(), it->name.GetStringLength())
                    : path_prefix + "." + String(it->name.GetString(), it->name.GetStringLength());

                flattenJSONValue(result, new_path, it->value);
            }
        }
        else if (value.IsNull())
        {
            /// Preserve null values - this allows us to delete paths from JSON objects during merge
            result[path_prefix] = Field(Null());
        }
        else if (value.IsArray())
        {
            /// Store array as Field
            result[path_prefix] = rapidjsonArrayToField(value);
        }
        else
        {
            /// Scalar value
            result[path_prefix] = rapidjsonScalarToField(value);
        }
    }

    /// Parse JSON string to flattened Object using RapidJSON
    static Object parseJSONStringToObject(StringRef json_str)
    {
        rapidjson::Document doc;
        doc.Parse(json_str.data, json_str.size);

        if (doc.HasParseError())
            throw Exception(ErrorCodes::BAD_ARGUMENTS,
                           "Wrong JSON string to merge: {}", rapidjson::GetParseError_En(doc.GetParseError()));

        if (!doc.IsObject())
            throw Exception(ErrorCodes::BAD_ARGUMENTS, "Wrong JSON string to merge. Expected JSON object");

        Object result;
        flattenJSONValue(result, "", doc);
        return result;
    }

    /// Remove parents of the provided path to avoid scalar/object conflicts
    static void eraseParentPaths(Object & object, std::string_view path)
    {
        String prefix;
        prefix.reserve(path.size());
        for (const char c : path)
        {
            if (c == '.')
                object.erase(prefix);
            prefix.push_back(c);
        }
    }

    /// Remove the provided path and all its descendants from the flattened map
    static void erasePathAndDescendants(Object & object, std::string_view path)
    {
        if (auto it = object.find(path); it != object.end())
            object.erase(it);

        String prefix(path);
        prefix.push_back('.');
        auto it = object.lower_bound(prefix);
        if (it == object.end())
            return;

        String upper_bound(path);
        upper_bound.push_back('/');
        auto end = object.lower_bound(upper_bound);
        object.erase(it, end);
    }

    /// Extract Object from argument - handles both String and JSON types
    Object extractObject(const ColumnWithTypeAndName & arg, size_t row)
    {
        if (arg.type->getTypeId() == TypeIndex::Object)
        {
            /// Extract from ColumnObject and track explicit nulls stored in dynamic paths
            const auto & col_object = assert_cast<const ColumnObject &>(*arg.column);
            Object result = col_object[row].safeGet<Object>();

            const auto & dynamic_paths_ptrs = col_object.getDynamicPathsPtrs();
            for (const auto & [path, dynamic_column] : dynamic_paths_ptrs)
            {
                if (row >= dynamic_column->size())
                    continue;

                if (dynamic_column->isNullAt(row) && !result.contains(path))
                    result[path] = Field(Null());
            }

            return result;
        }
        else
        {
            /// Parse String argument with RapidJSON
            return parseJSONStringToObject(arg.column->getDataAt(row));
        }
    }

    void applyPatchEntry(Object & dest, std::string_view path, Field && value)
    {
        if (value.isNull())
        {
            erasePathAndDescendants(dest, path);
            return;
        }

        eraseParentPaths(dest, path);
        erasePathAndDescendants(dest, path);
        auto [it, inserted] = dest.emplace(std::piecewise_construct, std::forward_as_tuple(path), std::forward_as_tuple());
        if (!inserted)
            it->second = std::move(value);
        else
            it->second = std::move(value);
    }

    /// Merge two objects according to RFC 7386 JSONMergePatch
    void mergeObjects(Object & dest, const Object & src)
    {
        std::vector<std::pair<const String *, const Field *>> pending_inserts;
        pending_inserts.reserve(src.size());

        for (const auto & [path, value] : src)
        {
            if (value.isNull())
            {
                /// RFC 7386: null deletes path and all children
                erasePathAndDescendants(dest, path);
                continue;
            }

            /// Remove parent paths to avoid mixing scalars and objects
            eraseParentPaths(dest, path);
            /// Remove existing value and descendants before inserting replacement
            erasePathAndDescendants(dest, path);
            pending_inserts.emplace_back(&path, &value);
        }

        for (const auto & [path_ptr, value_ptr] : pending_inserts)
            dest.insert_or_assign(*path_ptr, *value_ptr);
    }
}

}

#endif
