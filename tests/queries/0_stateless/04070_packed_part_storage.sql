-- Tags: no-random-merge-tree-settings

DROP TABLE IF EXISTS t_packed;

-- Test 1: Basic INSERT + SELECT with packed storage
CREATE TABLE t_packed (x UInt64, s String)
ENGINE = MergeTree ORDER BY x
SETTINGS enable_packed_part_storage = 1;

INSERT INTO t_packed SELECT number, toString(number) FROM numbers(1000);

SELECT 'test1_count', count(), sum(x) FROM t_packed;
SELECT 'test1_type', part_type FROM system.parts WHERE database = currentDatabase() AND table = 't_packed' AND active ORDER BY name;

-- Test 2: OPTIMIZE (merge) preserves data
INSERT INTO t_packed SELECT number + 1000, toString(number + 1000) FROM numbers(1000);
OPTIMIZE TABLE t_packed FINAL;

SELECT 'test2_count', count(), sum(x) FROM t_packed;

-- Test 3: ALTER DELETE (mutation)
ALTER TABLE t_packed DELETE WHERE x < 100 SETTINGS mutations_sync = 1;
SELECT 'test3_count', count() FROM t_packed;

-- Test 4: Projection with packed storage
DROP TABLE t_packed;

CREATE TABLE t_packed (x UInt64, s String, PROJECTION p_sum (SELECT sum(x)))
ENGINE = MergeTree ORDER BY x
SETTINGS enable_packed_part_storage = 1;

INSERT INTO t_packed SELECT number, toString(number) FROM numbers(1000);
SELECT 'test4_sum', sum(x) FROM t_packed;

-- Test 5: DETACH / ATTACH round-trip
DETACH TABLE t_packed;
ATTACH TABLE t_packed;
SELECT 'test5_count', count(), sum(x) FROM t_packed;

-- Test 6: Mixed Full + Packed parts coexist
DROP TABLE t_packed;

CREATE TABLE t_packed (x UInt64, s String)
ENGINE = MergeTree ORDER BY x;

-- Insert with Full storage (default)
INSERT INTO t_packed SELECT number, toString(number) FROM numbers(500);

-- Enable packed, insert more
ALTER TABLE t_packed MODIFY SETTING enable_packed_part_storage = 1;
INSERT INTO t_packed SELECT number + 500, toString(number + 500) FROM numbers(500);

-- Both part types coexist, all data accessible
SELECT 'test6_count', count(), sum(x) FROM t_packed;

DROP TABLE t_packed;
