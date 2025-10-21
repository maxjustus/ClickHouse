-- Tags: no-fasttest

SET allow_experimental_object_type = 1;

-- Basic aggregation with String input
SELECT '=== Basic String aggregation ===';
SELECT groupJSONMergePatch(json_str) FROM
(
    SELECT '{"a":1}' AS json_str
    UNION ALL SELECT '{"b":2}'
    UNION ALL SELECT '{"c":3}'
);

-- Basic aggregation with JSON type input
SELECT '=== Basic JSON type aggregation ===';
SELECT groupJSONMergePatch(json_obj) FROM
(
    SELECT '{"a":1}'::JSON AS json_obj
    UNION ALL SELECT '{"b":2}'::JSON
    UNION ALL SELECT '{"c":3}'::JSON
);

-- Mixed String and JSON inputs (both accepted, returns JSON)
SELECT '=== Mixed String/JSON inputs ===';
SELECT groupJSONMergePatch(val) FROM
(
    SELECT '{"a":1}'::JSON AS val
    UNION ALL SELECT '{"b":2}' AS val  -- String coerced to JSON
    UNION ALL SELECT '{"c":3}'::JSON AS val
);

-- Null deletion during aggregation
SELECT '=== Null deletion ===';
SELECT groupJSONMergePatch(json_str) FROM
(
    SELECT '{"a":1,"b":2}' AS json_str
    UNION ALL SELECT '{"b":null}'
    UNION ALL SELECT '{"c":3}'
);

-- Nested object merging
SELECT '=== Nested objects ===';
SELECT groupJSONMergePatch(json_obj) FROM
(
    SELECT '{"a":{"b":1}}'::JSON AS json_obj
    UNION ALL SELECT '{"a":{"c":2}}'::JSON
    UNION ALL SELECT '{"a":{"d":3}}'::JSON
);

-- Value replacement (last wins)
SELECT '=== Value replacement ===';
SELECT groupJSONMergePatch(json_str) FROM
(
    SELECT '{"a":1}' AS json_str
    UNION ALL SELECT '{"a":2}'
    UNION ALL SELECT '{"a":3}'
);

-- Arrays
SELECT '=== Arrays ===';
SELECT groupJSONMergePatch(json_obj) FROM
(
    SELECT '{"arr":[1,2,3]}'::JSON AS json_obj
    UNION ALL SELECT '{"arr":[4,5,6]}'::JSON
);

-- WITH GROUP BY
SELECT '=== GROUP BY ===';
SELECT
    key,
    groupJSONMergePatch(json_str) AS merged
FROM
(
    SELECT 'group1' AS key, '{"a":1}' AS json_str
    UNION ALL SELECT 'group1', '{"b":2}'
    UNION ALL SELECT 'group2', '{"x":10}'
    UNION ALL SELECT 'group2', '{"y":20}'
)
GROUP BY key
ORDER BY key;

-- Empty object handling
SELECT '=== Empty objects ===';
SELECT groupJSONMergePatch(json_obj) FROM
(
    SELECT '{}'::JSON AS json_obj
    UNION ALL SELECT '{"a":1}'::JSON
    UNION ALL SELECT '{}'::JSON
);

-- SimpleAggregateFunction in AggregatingMergeTree
SELECT '=== SimpleAggregateFunction ===';

DROP TABLE IF EXISTS test_json_agg;

CREATE TABLE test_json_agg
(
    key UInt64,
    attributes SimpleAggregateFunction(groupJSONMergePatch, JSON)
)
ENGINE = AggregatingMergeTree()
ORDER BY key;

INSERT INTO test_json_agg VALUES
    (1, '{"name":"Alice"}'::JSON),
    (1, '{"age":30}'::JSON),
    (2, '{"name":"Bob"}'::JSON),
    (2, '{"age":25}'::JSON);

-- Trigger merge
OPTIMIZE TABLE test_json_agg FINAL;

SELECT key, attributes FROM test_json_agg FINAL ORDER BY key;

DROP TABLE test_json_agg;

-- SimpleAggregateFunction with String input (coerced to JSON)
SELECT '=== SimpleAggregateFunction with String input ===';

DROP TABLE IF EXISTS test_json_agg_string;

CREATE TABLE test_json_agg_string
(
    key UInt64,
    attributes SimpleAggregateFunction(groupJSONMergePatch, String)
)
ENGINE = AggregatingMergeTree()
ORDER BY key;

INSERT INTO test_json_agg_string VALUES
    (1, '{"city":"NYC"}'),
    (1, '{"country":"USA"}'),
    (2, '{"city":"London"}'),
    (2, '{"country":"UK"}');

OPTIMIZE TABLE test_json_agg_string FINAL;

SELECT key, attributes FROM test_json_agg_string FINAL ORDER BY key;

DROP TABLE test_json_agg_string;

-- Null deletion with SimpleAggregateFunction
SELECT '=== SimpleAggregateFunction with null deletion ===';

DROP TABLE IF EXISTS test_json_null;

CREATE TABLE test_json_null
(
    key UInt64,
    data SimpleAggregateFunction(groupJSONMergePatch, JSON)
)
ENGINE = AggregatingMergeTree()
ORDER BY key;

INSERT INTO test_json_null VALUES
    (1, '{"a":1,"b":2}'::JSON),
    (1, '{"b":null}'::JSON),
    (1, '{"c":3}'::JSON);

OPTIMIZE TABLE test_json_null FINAL;

SELECT key, data FROM test_json_null FINAL;

DROP TABLE test_json_null;

-- Casting result to String if needed
SELECT '=== Result casting ===';
SELECT groupJSONMergePatch(json_obj)::String FROM
(
    SELECT '{"a":1}'::JSON AS json_obj
    UNION ALL SELECT '{"b":2}'::JSON
);
