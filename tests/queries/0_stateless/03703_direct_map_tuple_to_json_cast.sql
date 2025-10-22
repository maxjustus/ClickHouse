-- Test direct Map to JSON conversion (bypass string serialization)

-- Simple map with integers
SELECT CAST(map('a', 1, 'b', 2, 'c', 3) AS JSON) AS result FORMAT JSONEachRow;

-- Map with string values
SELECT CAST(map('name', 'Alice', 'city', 'NYC') AS JSON) AS result FORMAT JSONEachRow;

-- Map with mixed value types
SELECT CAST(map('id', 42, 'price', 19.99) AS JSON) AS result FORMAT JSONEachRow;

-- Empty map
SELECT CAST(map() AS JSON) AS result FORMAT JSONEachRow;

-- Map with NULL values
SELECT CAST(map('a', NULL, 'b', 2) AS JSON) AS result FORMAT JSONEachRow;

-- Nested maps
SELECT CAST(map('outer', map('inner', 123)) AS JSON) AS result FORMAT JSONEachRow;

-- Map in array context
SELECT CAST([map('x', 1), map('y', 2)] AS Array(JSON)) AS result FORMAT JSONEachRow;

-- Named tuple to JSON
SELECT CAST(tuple(1 AS a, 'hello' AS b, 3.14 AS c) AS JSON) AS result
SETTINGS enable_named_columns_in_function_tuple=1
FORMAT JSONEachRow;

-- Named tuple with nested structure
SELECT CAST(tuple(1 AS id, tuple(10 AS x, 20 AS y) AS data) AS JSON) AS result
SETTINGS enable_named_columns_in_function_tuple=1
FORMAT JSONEachRow;

-- Named tuple with array
SELECT CAST(tuple([1,2,3] AS nums, 'test' AS text) AS JSON) AS result
SETTINGS enable_named_columns_in_function_tuple=1
FORMAT JSONEachRow;

-- Empty tuple becomes empty array
SELECT CAST(tuple() AS JSON) AS result FORMAT JSONEachRow;

-- Named tuple with NULL
SELECT CAST(tuple(1 AS a, NULL AS b, 3 AS c) AS JSON) AS result
SETTINGS enable_named_columns_in_function_tuple=1
FORMAT JSONEachRow;

-- Aggregation using map cast (performance test scenario)
SELECT
    number % 10 AS key,
    CAST(map('sum', sum(number), 'count', count()) AS JSON) AS agg
FROM numbers(100)
GROUP BY key
ORDER BY key
LIMIT 3
FORMAT JSONEachRow;

-- Verify type preservation through aggregation
SELECT
    groupArray(CAST(map('id', number, 'value', number * 2) AS JSON)) AS result
FROM numbers(5)
FORMAT JSONEachRow;
