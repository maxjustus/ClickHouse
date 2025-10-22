#include <Columns/ColumnConst.h>
#include <Columns/ColumnLowCardinality.h>
#include <Columns/ColumnObject.h>
#include <Columns/ColumnString.h>
#include <Core/Field.h>
#include <DataTypes/DataTypeObject.h>
#include <DataTypes/DataTypeString.h>
#include <DataTypes/DataTypeTuple.h>
#include <Functions/FunctionFactory.h>
#include <Functions/FunctionHelpers.h>
#include <Functions/IFunction.h>


namespace DB
{

namespace ErrorCodes
{
extern const int NUMBER_OF_ARGUMENTS_DOESNT_MATCH;
extern const int ILLEGAL_TYPE_OF_ARGUMENT;
extern const int ILLEGAL_COLUMN;
}

namespace
{

/// json(key1, value1, key2, value2, ...) - constructs a JSON object from key-value pairs
class JSONConstructor : public IFunction
{
public:
    static constexpr auto name = "json";

    static FunctionPtr create(ContextPtr) { return std::make_shared<JSONConstructor>(); }

    String getName() const override { return name; }

    bool isVariadic() const override { return true; }

    size_t getNumberOfArguments() const override { return 0; }

    bool isInjective(const ColumnsWithTypeAndName &) const override { return true; }

    bool isSuitableForShortCircuitArgumentsExecution(const DataTypesWithConstInfo & /*arguments*/) const override { return true; }

    bool useDefaultImplementationForNulls() const override { return false; }
    bool useDefaultImplementationForNothing() const override { return false; }
    bool useDefaultImplementationForConstants() const override { return false; } // Keep custom handling for constant keys
    bool useDefaultImplementationForLowCardinalityColumns() const override { return false; }

private:
    /// Helper function to convert named tuples to Objects recursively
    static Field convertTupleToObjectIfNeeded(const Field & value, const DataTypePtr & type)
    {
        if (value.getType() != Field::Types::Tuple)
            return value;

        const auto * tuple_type = typeid_cast<const DataTypeTuple *>(type.get());
        if (!tuple_type || !tuple_type->hasExplicitNames())
            return value; // Unnamed tuple stays as array

        // Convert named tuple to object
        const auto & tuple = value.safeGet<Tuple>();
        const auto & names = tuple_type->getElementNames();
        Object nested_obj;

        for (size_t i = 0; i < tuple.size(); ++i)
        {
            // Recursively convert nested tuples
            Field element_value = convertTupleToObjectIfNeeded(tuple[i], tuple_type->getElement(i));
            nested_obj[names[i]] = std::move(element_value);
        }

        return Field(nested_obj);
    }

public:
    DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override
    {
        if (arguments.size() % 2 != 0)
            throw Exception(
                ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH,
                "Function {} requires even number of arguments (key-value pairs), but {} given",
                getName(),
                arguments.size());

        /// Validate that all keys (at even indices 0, 2, 4, ...) are String type
        for (size_t i = 0; i < arguments.size(); i += 2)
        {
            if (!isString(arguments[i]))
                throw Exception(
                    ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT,
                    "Function {} requires String type for keys (argument {}), but {} given",
                    getName(),
                    i,
                    arguments[i]->getName());
        }

        /// Values (at odd indices 1, 3, 5, ...) can be any type
        /// They will be converted automatically by ColumnObject::insert()

        return std::make_shared<DataTypeObject>(DataTypeObject::SchemaFormat::JSON);
    }

    ColumnPtr executeImpl(const ColumnsWithTypeAndName & arguments, const DataTypePtr & result_type, size_t input_rows_count) const override
    {
        /// Handle empty case - json() with no arguments creates empty objects
        if (arguments.empty())
        {
            auto result = result_type->createColumn();
            auto & result_object = assert_cast<ColumnObject &>(*result);
            result_object.reserve(input_rows_count);

            Object empty_obj;
            for (size_t row = 0; row < input_rows_count; ++row)
                result_object.insert(Field(empty_obj));

            return result;
        }

        /// Create result column
        auto result = result_type->createColumn();
        auto & result_object = assert_cast<ColumnObject &>(*result);
        result_object.reserve(input_rows_count);

        const size_t num_pairs = arguments.size() / 2;

        struct KeyInfo
        {
            ColumnPtr column;
            String constant_key;
            bool is_const = false;
        };

        std::vector<KeyInfo> keys(num_pairs);
        std::vector<const ColumnString *> key_columns(num_pairs, nullptr);
        std::vector<ColumnPtr> value_columns;
        std::vector<DataTypePtr> value_types;
        value_columns.reserve(num_pairs);
        value_types.reserve(num_pairs);

        for (size_t pair_index = 0, arg_index = 0; pair_index < num_pairs; ++pair_index, arg_index += 2)
        {
            const auto & key_argument = arguments[arg_index];
            const auto & value_argument = arguments[arg_index + 1];

            if (!key_argument.column)
                throw Exception(
                    ErrorCodes::ILLEGAL_COLUMN,
                    "Function {} requires non-null columns for keys, but argument {} column is missing",
                    getName(),
                    arg_index);

            if (const auto * key_const = checkAndGetColumnConstStringOrFixedString(key_argument.column.get()))
            {
                keys[pair_index].is_const = true;
                keys[pair_index].constant_key = key_const->getValue<String>();
            }
            else
            {
                ColumnPtr key_column = key_argument.column;

                if (const auto * column_const = checkAndGetColumn<ColumnConst>(key_column.get()))
                    key_column = column_const->convertToFullColumn();

                if (const auto * low_cardinality = checkAndGetColumn<ColumnLowCardinality>(key_column.get()))
                    key_column = low_cardinality->convertToFullColumn();

                const auto * string_column = checkAndGetColumn<ColumnString>(key_column.get());
                if (!string_column)
                    throw Exception(
                        ErrorCodes::ILLEGAL_COLUMN,
                        "Function {} requires String type keys, but argument {} produced {}",
                        getName(),
                        arg_index,
                        key_column->getName());

                keys[pair_index].column = key_column;
                key_columns[pair_index] = string_column;
            }

            value_columns.push_back(value_argument.column);
            value_types.push_back(value_argument.type);
        }

        /// Build JSON objects for each row
        for (size_t row = 0; row < input_rows_count; ++row)
        {
            Object obj;

            for (size_t pair_index = 0; pair_index < num_pairs; ++pair_index)
            {
                Field value;
                value_columns[pair_index]->get(row, value);
                value = convertTupleToObjectIfNeeded(value, value_types[pair_index]);

                if (keys[pair_index].is_const)
                {
                    obj[keys[pair_index].constant_key] = std::move(value);
                }
                else
                {
                    const StringRef key_ref = key_columns[pair_index]->getDataAt(row);
                    obj[String(key_ref.data, key_ref.size)] = std::move(value);
                }
            }

            result_object.insert(Field(std::move(obj)));
        }

        return result;
    }
};

}

REGISTER_FUNCTION(JSONConstructor)
{
    FunctionDocumentation::Description description = R"(
Creates a JSON object from key-value pairs.

This is the most efficient way to construct JSON objects, avoiding intermediate Map or string conversions.
Keys can be constant strings or arbitrary string expressions evaluated per row. Values can be any type including nested json(), tuple(), map(), or arrays.

**Syntax**

```sql
json(key1, value1[, key2, value2, ...])
```

**Arguments**

- `key_n` — String expression for the JSON object field name
- `value_n` — Values of any type. Named tuples become nested objects, unnamed tuples become arrays

**Returned value**

- A JSON object containing the specified key-value pairs [JSON]

**Examples**

Simple object construction:

```sql
SELECT json('name', 'Alice', 'age', 30)
```

```text
{"name":"Alice","age":30}
```

Nested objects using json():

```sql
SELECT json('user', json('name', 'Alice', 'id', 1), 'active', true)
```

```text
{"user":{"name":"Alice","id":1},"active":true}
```

Nested objects using named tuples:

```sql
SELECT json('stats', tuple(5 AS count, 100 AS sum))
SETTINGS enable_named_columns_in_function_tuple=1
```

```text
{"stats":{"count":5,"sum":100}}
```

Arrays and complex nesting:

```sql
SELECT json(
    'user', tuple('Alice' AS name, tuple('NYC' AS city, 10001 AS zip) AS address),
    'tags', ['admin', 'premium']
) SETTINGS enable_named_columns_in_function_tuple=1
```

```text
{"user":{"name":"Alice","address":{"city":"NYC","zip":10001}},"tags":["admin","premium"]}
```

Using in aggregations:

```sql
SELECT
    number % 3 AS key,
    json('count', count(), 'sum', sum(number), 'avg', avg(number)) AS stats
FROM numbers(10)
GROUP BY key
ORDER BY key
```

```text
{"count":4,"sum":18,"avg":4.5}
{"count":3,"sum":12,"avg":4}
{"count":3,"sum":9,"avg":3}
```
)";

    FunctionDocumentation::Syntax syntax = "json(key1, value1[, key2, value2, ...])";

    FunctionDocumentation::Arguments arguments_doc
        = {{"key_n", "String expression used as the JSON object field name", {"String"}},
           {"value_n", "Values of any type (supports nested objects via tuple or json)", {"Any"}}};

    FunctionDocumentation::ReturnedValue returned_value = {"Returns a JSON object containing the key-value pairs", {"JSON"}};

    FunctionDocumentation::Examples examples
        = {{"Simple object", "SELECT json('name', 'Alice', 'age', 30)", "{\"name\":\"Alice\",\"age\":30}"},
           {"Nested with json()", "SELECT json('user', json('name', 'Alice', 'id', 1))", "{\"user\":{\"name\":\"Alice\",\"id\":1}}"},
           {"Named tuple",
            "SELECT json('stats', tuple(5 AS count, 100 AS sum)) SETTINGS enable_named_columns_in_function_tuple=1",
            "{\"stats\":{\"count\":5,\"sum\":100}}"}};

    FunctionDocumentation::IntroducedIn introduced_in = {25, 1};
    FunctionDocumentation::Category category = FunctionDocumentation::Category::JSON;

    FunctionDocumentation documentation = {description, syntax, arguments_doc, returned_value, examples, introduced_in, category};

    factory.registerFunction<JSONConstructor>(documentation);
}

}
