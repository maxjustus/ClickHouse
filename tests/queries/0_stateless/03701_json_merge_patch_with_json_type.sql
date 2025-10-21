-- Tags: no-fasttest

-- Test JSONMergePatch with new JSON data type support

-- Basic all-String arguments (existing behavior)
SELECT '=== All-String arguments ===';
SELECT JSONMergePatch('{"a":1}', '{"b":2}');
SELECT JSONMergePatch('{"a":1}', '{"b":2}', '{"c":3}');
SELECT JSONMergePatch('{"a":{"b":1}}', '{"a":{"c":2}}');

-- All-JSON type arguments
SELECT '=== All-JSON type arguments ===';
SELECT JSONMergePatch('{"a":1}'::JSON, '{"b":2}'::JSON);
SELECT JSONMergePatch('{"a":1}'::JSON, '{"b":2}'::JSON, '{"c":3}'::JSON);
SELECT JSONMergePatch('{"a":{"b":1}}'::JSON, '{"a":{"c":2}}'::JSON);

-- Mixed String/JSON arguments (new feature)
SELECT '=== Mixed String/JSON arguments ===';
SELECT JSONMergePatch('{"a":1}'::JSON, '{"b":2}');
SELECT JSONMergePatch('{"a":1}', '{"b":2}'::JSON);
SELECT JSONMergePatch('{"a":1}'::JSON, '{"b":2}', '{"c":3}'::JSON);

-- Null deletion with String arguments (preserves null)
SELECT '=== Null deletion with String arguments ===';
SELECT JSONMergePatch('{"a":1,"b":2}', '{"b":null}');
SELECT JSONMergePatch('{"a":{"b":1},"c":2}', '{"a":null}');
SELECT JSONMergePatch('{"a":{"b":{"c":1}}}', '{"a":{"b":null}}');

-- Null deletion with mixed String/JSON (String provides null)
SELECT '=== Null deletion with mixed String/JSON ===';
SELECT JSONMergePatch('{"a":1,"b":2}'::JSON, '{"b":null}');
SELECT JSONMergePatch('{"a":{"b":1},"c":2}'::JSON, '{"a":null}');
SELECT JSONMergePatch('{"1":2,"z":[1,2,3]}'::JSON, '{"a":{"b":1}}'::JSON, '{"a":null}');
SELECT JSONMergePatch('{"1":2,"z":[1,2,3]}'::JSON, '{"a":{"b":{"c":1}}}'::JSON, '{"a":{"b":null}}', '{"z":null}');

-- Child path removal
SELECT '=== Child path removal ===';
SELECT JSONMergePatch('{"a":{"b":{"c":1,"d":2},"e":3}}'::JSON, '{"a":{"b":null}}');
SELECT JSONMergePatch('{"x":1,"a":{"b":1,"c":2}}'::JSON, '{"a":null}');

-- Nested arrays
SELECT '=== Nested arrays ===';
SELECT JSONMergePatch('{"a":[1,2,3]}', '{"b":[4,5,6]}');
SELECT JSONMergePatch('{"a":[1,2,3]}'::JSON, '{"b":[4,5,6]}'::JSON);
SELECT JSONMergePatch('{"a":[1,2]}'::JSON, '{"a":[3,4,5]}');

-- Arrays containing objects
SELECT '=== Arrays containing objects ===';
SELECT JSONMergePatch('{"arr":[{"x":1}]}'::JSON, '{"arr":[{"y":2}]}');
SELECT JSONMergePatch('{"data":[{"nested":true}]}'::JSON, '{"extra":"value"}'::JSON);

-- Arrays containing arrays
SELECT '=== Arrays containing arrays ===';
SELECT JSONMergePatch('{"matrix":[[1,2],[3,4]]}'::JSON, '{"vector":[5,6]}'::JSON);

-- Complex nested structures
SELECT '=== Complex nested structures ===';
SELECT JSONMergePatch('{"a":{"b":{"c":1}}}'::JSON, '{"a":{"b":{"d":2}}}'::JSON);
SELECT JSONMergePatch('{"obj":{"arr":[{"deep":1}]}}'::JSON, '{"obj":{"arr":[{"deep":2}]}}'::JSON);

-- Multiple operations
SELECT '=== Multiple merge operations ===';
SELECT JSONMergePatch('{"a":1}'::JSON, '{"b":2}', '{"c":3}'::JSON, '{"d":4}');
SELECT JSONMergePatch('{"x":1,"y":2}'::JSON, '{"y":null}', '{"z":3}'::JSON);

-- Empty objects
SELECT '=== Empty objects ===';
SELECT JSONMergePatch('{}'::JSON, '{"a":1}');
SELECT JSONMergePatch('{"a":1}'::JSON, '{}');
SELECT JSONMergePatch('{}'::JSON, '{}'::JSON);

-- Value replacement
SELECT '=== Value replacement ===';
SELECT JSONMergePatch('{"a":1}'::JSON, '{"a":2}');
SELECT JSONMergePatch('{"a":"old"}'::JSON, '{"a":"new"}');
SELECT JSONMergePatch('{"x":[1,2]}'::JSON, '{"x":{"b":3}}');

-- Type changes
SELECT '=== Type changes ===';
SELECT JSONMergePatch('{"a":1}'::JSON, '{"a":"string"}');
SELECT JSONMergePatch('{"a":"str"}'::JSON, '{"a":123}');
SELECT JSONMergePatch('{"a":{"b":1}}'::JSON, '{"a":[1,2,3]}');
