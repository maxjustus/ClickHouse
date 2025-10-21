#include <Columns/ColumnString.h>
#include <Columns/ColumnObject.h>
#include <DataTypes/DataTypeString.h>
#include <DataTypes/DataTypeObject.h>
#include <Functions/FunctionFactory.h>
#include <Functions/FunctionHelpers.h>
#include <Functions/IFunction.h>
#include <Interpreters/Context.h>
#include <IO/ReadBufferFromString.h>
#include <Common/FieldVisitorToString.h>
#include "config.h"

#if USE_RAPIDJSON

/// Prevent stack overflow:
#define RAPIDJSON_PARSE_DEFAULT_FLAGS (kParseIterativeFlag)

#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/error/en.h>


namespace DB
{

namespace ErrorCodes
{
    extern const int BAD_ARGUMENTS;
    extern const int ILLEGAL_COLUMN;
    extern const int TOO_FEW_ARGUMENTS_FOR_FUNCTION;
    extern const int ILLEGAL_TYPE_OF_ARGUMENT;
}

namespace
{
    // select JSONMergePatch('{"a":1}','{"name": "joey"}','{"name": "tom"}','{"name": "zoey"}');
    //           ||
    //           \/
    // ┌───────────────────────┐
    // │ {"a":1,"name":"zoey"} │
    // └───────────────────────┘
    class FunctionJSONMergePatch : public IFunction
    {
    public:
        static constexpr auto name = "JSONMergePatch";
        static FunctionPtr create(ContextPtr) { return std::make_shared<FunctionJSONMergePatch>(); }

        String getName() const override { return name; }
        bool isVariadic() const override { return true; }
        bool isSuitableForShortCircuitArgumentsExecution(const DataTypesWithConstInfo & /*arguments*/) const override { return true; }

        size_t getNumberOfArguments() const override { return 0; }
        bool useDefaultImplementationForConstants() const override { return true; }

        DataTypePtr getReturnTypeImpl(const ColumnsWithTypeAndName & arguments) const override
        {
            if (arguments.empty())
                throw Exception(ErrorCodes::TOO_FEW_ARGUMENTS_FOR_FUNCTION, "Function {} requires at least one argument.", getName());

            /// Check if any argument is JSON type
            bool has_json = false;
            for (const auto & arg : arguments)
            {
                if (arg.type->getTypeId() == TypeIndex::Object)
                    has_json = true;
                else if (!isString(arg.type))
                    throw Exception(ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT,
                                   "Function {} requires String or JSON arguments", getName());
            }

            /// Return JSON type if any input is JSON, otherwise String
            /// Use first JSON argument's type to preserve type parameters
            if (has_json)
            {
                for (const auto & arg : arguments)
                    if (arg.type->getTypeId() == TypeIndex::Object)
                        return arg.type;
            }

            return std::make_shared<DataTypeString>();
        }

        ColumnPtr executeImpl(const ColumnsWithTypeAndName & arguments, const DataTypePtr & result_type, size_t input_rows_count) const override
        {
            chassert(!arguments.empty());

            /// Check if any argument is JSON type
            bool has_json = false;
            for (const auto & arg : arguments)
            {
                if (arg.type->getTypeId() == TypeIndex::Object)
                {
                    has_json = true;
                    break;
                }
            }

            /// All String → use RapidJSON path
            if (!has_json)
                return executeWithStrings(arguments, input_rows_count);

            /// Has JSON or mixed → use Object-based merge with String parsing
            return executeWithMixed(arguments, result_type, input_rows_count);
        }

    private:
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
                /// Preserve null values - this allows us to delete paths from JSON objects during merge by passing json strings with nulls
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
            doc.Parse(json_str.toString().c_str());

            if (doc.HasParseError())
                throw Exception(ErrorCodes::BAD_ARGUMENTS,
                               "Wrong JSON string to merge: {}", rapidjson::GetParseError_En(doc.GetParseError()));

            if (!doc.IsObject())
                throw Exception(ErrorCodes::BAD_ARGUMENTS, "Wrong JSON string to merge. Expected JSON object");

            Object result;
            flattenJSONValue(result, "", doc);
            return result;
        }

        /// Extract Object from argument - handles both String and JSON types
        static Object extractObject(const ColumnWithTypeAndName & arg, size_t row)
        {
            if (arg.type->getTypeId() == TypeIndex::Object)
            {
                /// Extract from ColumnObject - null values are not stored
                const auto & col_object = assert_cast<const ColumnObject &>(*arg.column);
                return col_object[row].safeGet<Object>();
            }
            else
            {
                /// Parse String argument with RapidJSON - preserves null values
                return parseJSONStringToObject(arg.column->getDataAt(row));
            }
        }

        /// Merge logic for JSON type arguments or mixed String/JSON arguments
        ColumnPtr executeWithMixed(
            const ColumnsWithTypeAndName & arguments,
            const DataTypePtr & result_type,
            size_t input_rows_count) const
        {
            auto result_col = result_type->createColumn();
            auto & result_object = assert_cast<ColumnObject &>(*result_col);

            for (size_t row = 0; row < input_rows_count; ++row)
            {
                /// Start with first argument
                Object merged = extractObject(arguments[0], row);

                /// Merge each subsequent argument
                for (size_t arg_idx = 1; arg_idx < arguments.size(); ++arg_idx)
                {
                    Object current = extractObject(arguments[arg_idx], row);

                    /// Phase 1: Collect what needs to be deleted and what to insert
                    std::set<String> exact_deletions;  // Paths to delete exactly (parents of new values)
                    std::set<String> subtree_deletions;  // Paths whose children should also be deleted
                    Object values_to_insert;

                    for (auto & [path, value] : current)
                    {
                        if (value.isNull())
                        {
                            /// RFC 7386: null deletes path and all children
                            exact_deletions.insert(path);
                            subtree_deletions.insert(path);
                        }
                        else
                        {
                            /// Value replaces: need to delete all parents and children
                            subtree_deletions.insert(path);

                            /// Mark all parent paths for deletion
                            String prefix;
                            prefix.reserve(path.size());
                            for (size_t i = 0; i < path.size(); ++i)
                            {
                                if (path[i] == '.')
                                    exact_deletions.insert(prefix);
                                prefix.push_back(path[i]);
                            }

                            values_to_insert[path] = value;
                        }
                    }

                    /// Phase 2: Execute deletions using range-based operations
                    /// First delete subtrees (path + all children)
                    for (const auto & subtree_root : subtree_deletions)
                    {
                        /// Delete the path itself
                        merged.erase(subtree_root);

                        /// Delete all children using range erase
                        String prefix = subtree_root + ".";
                        auto it = merged.lower_bound(prefix);
                        // Find all keys between "subtree_root." and the next key that doesn't start with that prefix
                        auto end = merged.lower_bound(subtree_root + "/");
                        merged.erase(it, end);
                    }

                    /// Then delete exact paths (parents that aren't already deleted)
                    for (const auto & exact_path : exact_deletions)
                    {
                        merged.erase(exact_path);
                    }

                    /// Phase 3: Insert new values
                    for (auto & [path, value] : values_to_insert)
                    {
                        merged[path] = std::move(value);
                    }
                }

                /// Insert merged object - ColumnObject handles typed/dynamic/shared logic
                result_object.insert(Field(merged));
            }

            return result_col;
        }

        /// Merge logic for String type arguments (existing RapidJSON implementation)
        ColumnPtr executeWithStrings(const ColumnsWithTypeAndName & arguments, size_t input_rows_count) const
        {
            rapidjson::Document::AllocatorType allocator;
            std::function<void(rapidjson::Value &, const rapidjson::Value &)> merge_objects;

            merge_objects = [&merge_objects, &allocator](rapidjson::Value & dest, const rapidjson::Value & src) -> void
            {
                if (!src.IsObject())
                    return;

                for (auto it = src.MemberBegin(); it != src.MemberEnd(); ++it)
                {
                    rapidjson::Value key(it->name, allocator);
                    rapidjson::Value value(it->value, allocator);
                    if (dest.HasMember(key))
                    {
                        if (dest[key].IsObject() && value.IsObject())
                            merge_objects(dest[key], value);
                        else
                            dest[key] = value;
                    }
                    else
                    {
                        dest.AddMember(key, value, allocator);
                    }
                }
            };

            auto parse_json_document = [](const ColumnString & column, rapidjson::Document & document, size_t i)
            {
                auto str_ref = column.getDataAt(i);
                document.Parse(str_ref.toString().c_str());

                if (document.HasParseError())
                    throw Exception(ErrorCodes::BAD_ARGUMENTS, "Wrong JSON string to merge: {}", rapidjson::GetParseError_En(document.GetParseError()));

                if (!document.IsObject())
                    throw Exception(ErrorCodes::BAD_ARGUMENTS, "Wrong JSON string to merge. Expected JSON object");
            };

            const bool is_first_const = isColumnConst(*arguments[0].column);
            const auto * first_column_arg_string = is_first_const
                        ? checkAndGetColumnConstData<ColumnString>(arguments[0].column.get())
                        : checkAndGetColumn<ColumnString>(arguments[0].column.get());

            if (!first_column_arg_string)
                throw Exception(ErrorCodes::ILLEGAL_COLUMN, "Arguments of function {} must be strings", getName());

            std::vector<rapidjson::Document> merged_jsons;
            merged_jsons.reserve(input_rows_count);

            for (size_t i = 0; i < input_rows_count; ++i)
            {
                auto & merged_json = merged_jsons.emplace_back(rapidjson::Type::kObjectType, &allocator);
                if (is_first_const)
                    parse_json_document(*first_column_arg_string, merged_json, 0);
                else
                    parse_json_document(*first_column_arg_string, merged_json, i);
            }

            for (size_t col_idx = 1; col_idx < arguments.size(); ++col_idx)
            {
                const bool is_const = isColumnConst(*arguments[col_idx].column);
                const auto * column_arg_string = is_const
                            ? checkAndGetColumnConstData<ColumnString>(arguments[col_idx].column.get())
                            : checkAndGetColumn<ColumnString>(arguments[col_idx].column.get());

                if (!column_arg_string)
                    throw Exception(ErrorCodes::ILLEGAL_COLUMN, "Arguments of function {} must be strings", getName());

                for (size_t i = 0; i < input_rows_count; ++i)
                {
                    rapidjson::Document document(&allocator);
                    if (is_const)
                        parse_json_document(*column_arg_string, document, 0);
                    else
                        parse_json_document(*column_arg_string, document, i);
                    merge_objects(merged_jsons[i], document);
                }
            }

            auto result = ColumnString::create();
            auto & result_string = assert_cast<ColumnString &>(*result);
            rapidjson::CrtAllocator buffer_allocator;

            for (size_t i = 0; i < input_rows_count; ++i)
            {
                rapidjson::StringBuffer buffer(&buffer_allocator);
                rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

                merged_jsons[i].Accept(writer);
                result_string.insertData(buffer.GetString(), buffer.GetSize());
            }

            return result;
        }
    };

}

REGISTER_FUNCTION(JSONMergePatch)
{
    /// jsonMergePatch documentation
    FunctionDocumentation::Description description_jsonMergePatch = R"(
Returns the merged JSON object string which is formed by merging multiple JSON objects.
    )";
    FunctionDocumentation::Syntax syntax_jsonMergePatch = "jsonMergePatch(json1[, json2, ...])";
    FunctionDocumentation::Arguments arguments_jsonMergePatch = {
        {"json1[, json2, ...]", "One or more strings with valid JSON.", {"String"}}
    };
    FunctionDocumentation::ReturnedValue returned_value_jsonMergePatch = {"Returns the merged JSON object string, if the JSON object strings are valid.", {"String"}};
    FunctionDocumentation::Examples examples_jsonMergePatch = {
    {
        "Usage example",
        R"(
SELECT jsonMergePatch('{"a":1}', '{"name": "joey"}', '{"name": "tom"}', '{"name": "zoey"}') AS res;
        )",
        R"(
┌─res───────────────────┐
│ {"a":1,"name":"zoey"} │
└───────────────────────┘
        )"
    }
    };
    FunctionDocumentation::IntroducedIn introduced_in_jsonMergePatch = {23, 10};
    FunctionDocumentation::Category category_jsonMergePatch = FunctionDocumentation::Category::JSON;
    FunctionDocumentation documentation_jsonMergePatch = {description_jsonMergePatch, syntax_jsonMergePatch, arguments_jsonMergePatch, returned_value_jsonMergePatch, examples_jsonMergePatch, introduced_in_jsonMergePatch, category_jsonMergePatch};

    factory.registerFunction<FunctionJSONMergePatch>(documentation_jsonMergePatch);

    factory.registerAlias("jsonMergePatch", "JSONMergePatch");
}

}

#endif
