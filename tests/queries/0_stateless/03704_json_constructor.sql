-- Test json() constructor function for direct JSON object creation

-- Basic construction
SELECT json('a', 1, 'b', 2) AS result FORMAT JSONEachRow;

-- With strings
SELECT json('name', 'Alice', 'city', 'NYC') AS result FORMAT JSONEachRow;

-- Mixed types
SELECT json('id', 42, 'price', 19.99, 'active', true) AS result FORMAT JSONEachRow;

-- Empty object
SELECT json() AS result FORMAT JSONEachRow;

-- Nested with json()
SELECT json('outer', json('inner', 123)) AS result FORMAT JSONEachRow;

-- Nested with named tuple
SELECT json('user', tuple('Bob' AS name, 25 AS age)) AS result
SETTINGS enable_named_columns_in_function_tuple=1
FORMAT JSONEachRow;

-- Arrays
SELECT json('tags', ['a', 'b', 'c'], 'ids', [1, 2, 3]) AS result FORMAT JSONEachRow;

-- With map (flattened to object)
SELECT json('meta', map('version', 2, 'enabled', true)) AS result FORMAT JSONEachRow;

-- Complex nesting with named tuples
SELECT json(
    'user', tuple(
        'Alice' AS name,
        tuple('NYC' AS city, 10001 AS zip) AS address
    ),
    'tags', ['admin', 'premium']
) AS result
SETTINGS enable_named_columns_in_function_tuple=1
FORMAT JSONEachRow;

-- Aggregation use case (main motivation)
SELECT
    number % 3 AS key,
    json('count', count(), 'sum', sum(number), 'avg', avg(number)) AS stats
FROM numbers(10)
GROUP BY key
ORDER BY key
FORMAT JSONEachRow;

-- Comparison with map cast (should produce identical results)
SELECT
    json('a', 1, 'b', 2) AS via_json,
    CAST(map('a', 1, 'b', 2) AS JSON) AS via_map_cast
FORMAT JSONEachRow;

-- Unnamed tuple becomes array
SELECT json('data', tuple(1, 2, 3)) AS result FORMAT JSONEachRow;

-- Multiple rows
SELECT
    number,
    json('id', number, 'value', number * 2) AS obj
FROM numbers(3)
FORMAT JSONEachRow;

-- NULL values
SELECT json('a', NULL, 'b', 2, 'c', NULL) AS result FORMAT JSONEachRow;

-- Nested arrays
SELECT json('matrix', [[1, 2], [3, 4]]) AS result FORMAT JSONEachRow;

-- Mix of nested json() and tuples
SELECT json(
    'explicit', json('x', 1, 'y', 2),
    'implicit', tuple(3 AS x, 4 AS y)
) AS result
SETTINGS enable_named_columns_in_function_tuple=1
FORMAT JSONEachRow;

-- Single key-value pair
SELECT json('key', 'value') AS result FORMAT JSONEachRow;

-- Using with groupJSONMergePatch aggregate function
SELECT groupJSONMergePatch(json('value', number, 'flag', number % 2 = 0)) AS merged
FROM numbers(5)
FORMAT JSONEachRow;
