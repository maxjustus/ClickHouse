-- Results of SELECT ... FINAL must not depend on `merge_tree_final_layers_per_stream`.
-- Larger values bucket more PK-range layers per parallel stream (cutting peak memory),
-- but row output is identical. Each query prints "<mode> 1" iff all three settings
-- values produce byte-identical row sets.

-- ReplacingMergeTree with version column
DROP TABLE IF EXISTS t_final_replacing;
CREATE TABLE t_final_replacing (k UInt32, v UInt32, ver UInt32) ENGINE = ReplacingMergeTree(ver) ORDER BY k;
SYSTEM STOP MERGES t_final_replacing;
INSERT INTO t_final_replacing SELECT number, number + 10, 1 FROM numbers(1000);
INSERT INTO t_final_replacing SELECT number, number + 20, 2 FROM numbers(500, 1000);
INSERT INTO t_final_replacing SELECT number, number + 30, 3 FROM numbers(1500, 500);
INSERT INTO t_final_replacing SELECT number, number + 40, 4 FROM numbers(1800, 800);
INSERT INTO t_final_replacing SELECT number, number + 50, 5 FROM numbers(1200, 1200);
INSERT INTO t_final_replacing SELECT number, number + 60, 6 FROM numbers(0, 500);
WITH
    (SELECT cityHash64(groupArray(tuple(k, v, ver))) FROM (SELECT k, v, ver FROM t_final_replacing FINAL ORDER BY k SETTINGS merge_tree_final_layers_per_stream = 1, max_final_threads = 4)) AS h1,
    (SELECT cityHash64(groupArray(tuple(k, v, ver))) FROM (SELECT k, v, ver FROM t_final_replacing FINAL ORDER BY k SETTINGS merge_tree_final_layers_per_stream = 2, max_final_threads = 4)) AS h2,
    (SELECT cityHash64(groupArray(tuple(k, v, ver))) FROM (SELECT k, v, ver FROM t_final_replacing FINAL ORDER BY k SETTINGS merge_tree_final_layers_per_stream = 4, max_final_threads = 4)) AS h4
SELECT 'Replacing', (h1 = h2) AND (h2 = h4);
DROP TABLE t_final_replacing;

-- CollapsingMergeTree
DROP TABLE IF EXISTS t_final_collapsing;
CREATE TABLE t_final_collapsing (k UInt32, v UInt32, sign Int8) ENGINE = CollapsingMergeTree(sign) ORDER BY k;
SYSTEM STOP MERGES t_final_collapsing;
INSERT INTO t_final_collapsing SELECT number, number + 10, 1 FROM numbers(1000);
INSERT INTO t_final_collapsing SELECT number, number + 10, -1 FROM numbers(500, 400);
INSERT INTO t_final_collapsing SELECT number, number + 20, 1 FROM numbers(500, 400);
INSERT INTO t_final_collapsing SELECT number, number + 30, 1 FROM numbers(1500, 800);
INSERT INTO t_final_collapsing SELECT number, number + 40, 1 FROM numbers(1800, 600);
INSERT INTO t_final_collapsing SELECT number, number + 50, 1 FROM numbers(1200, 700);
WITH
    (SELECT cityHash64(groupArray(tuple(k, v))) FROM (SELECT k, v FROM t_final_collapsing FINAL ORDER BY k, v SETTINGS merge_tree_final_layers_per_stream = 1, max_final_threads = 4)) AS h1,
    (SELECT cityHash64(groupArray(tuple(k, v))) FROM (SELECT k, v FROM t_final_collapsing FINAL ORDER BY k, v SETTINGS merge_tree_final_layers_per_stream = 2, max_final_threads = 4)) AS h2,
    (SELECT cityHash64(groupArray(tuple(k, v))) FROM (SELECT k, v FROM t_final_collapsing FINAL ORDER BY k, v SETTINGS merge_tree_final_layers_per_stream = 4, max_final_threads = 4)) AS h4
SELECT 'Collapsing', (h1 = h2) AND (h2 = h4);
DROP TABLE t_final_collapsing;

-- SummingMergeTree
DROP TABLE IF EXISTS t_final_summing;
CREATE TABLE t_final_summing (k UInt32, v UInt64) ENGINE = SummingMergeTree(v) ORDER BY k;
SYSTEM STOP MERGES t_final_summing;
INSERT INTO t_final_summing SELECT number, 1 FROM numbers(1000);
INSERT INTO t_final_summing SELECT number, 2 FROM numbers(500, 1000);
INSERT INTO t_final_summing SELECT number, 3 FROM numbers(1500, 500);
INSERT INTO t_final_summing SELECT number, 4 FROM numbers(1800, 800);
INSERT INTO t_final_summing SELECT number, 5 FROM numbers(1200, 1200);
INSERT INTO t_final_summing SELECT number, 6 FROM numbers(0, 500);
WITH
    (SELECT cityHash64(groupArray(tuple(k, v))) FROM (SELECT k, v FROM t_final_summing FINAL ORDER BY k SETTINGS merge_tree_final_layers_per_stream = 1, max_final_threads = 4)) AS h1,
    (SELECT cityHash64(groupArray(tuple(k, v))) FROM (SELECT k, v FROM t_final_summing FINAL ORDER BY k SETTINGS merge_tree_final_layers_per_stream = 2, max_final_threads = 4)) AS h2,
    (SELECT cityHash64(groupArray(tuple(k, v))) FROM (SELECT k, v FROM t_final_summing FINAL ORDER BY k SETTINGS merge_tree_final_layers_per_stream = 4, max_final_threads = 4)) AS h4
SELECT 'Summing', (h1 = h2) AND (h2 = h4);
DROP TABLE t_final_summing;

-- ReplacingMergeTree with a data layout that forces multiple layers and low stream count,
-- so the chain path actually activates pending layers during execution.
DROP TABLE IF EXISTS t_final_chain;
CREATE TABLE t_final_chain (k UInt32, v UInt32, ver UInt32) ENGINE = ReplacingMergeTree(ver) ORDER BY k;
SYSTEM STOP MERGES t_final_chain;
INSERT INTO t_final_chain SELECT number, number, 1 FROM numbers(0, 2000);
INSERT INTO t_final_chain SELECT number, number, 2 FROM numbers(500, 2000);
INSERT INTO t_final_chain SELECT number, number, 3 FROM numbers(1500, 2000);
INSERT INTO t_final_chain SELECT number, number, 4 FROM numbers(2500, 2000);
INSERT INTO t_final_chain SELECT number, number, 5 FROM numbers(3500, 2000);
INSERT INTO t_final_chain SELECT number, number, 6 FROM numbers(4500, 2000);
INSERT INTO t_final_chain SELECT number, number, 7 FROM numbers(5500, 2000);
INSERT INTO t_final_chain SELECT number, number, 8 FROM numbers(6500, 2000);
WITH
    (SELECT cityHash64(groupArray(tuple(k, v, ver))) FROM (SELECT k, v, ver FROM t_final_chain FINAL ORDER BY k SETTINGS merge_tree_final_layers_per_stream = 1, max_final_threads = 2)) AS h1,
    (SELECT cityHash64(groupArray(tuple(k, v, ver))) FROM (SELECT k, v, ver FROM t_final_chain FINAL ORDER BY k SETTINGS merge_tree_final_layers_per_stream = 2, max_final_threads = 2)) AS h2,
    (SELECT cityHash64(groupArray(tuple(k, v, ver))) FROM (SELECT k, v, ver FROM t_final_chain FINAL ORDER BY k SETTINGS merge_tree_final_layers_per_stream = 4, max_final_threads = 2)) AS h4,
    (SELECT cityHash64(groupArray(tuple(k, v, ver))) FROM (SELECT k, v, ver FROM t_final_chain FINAL ORDER BY k SETTINGS merge_tree_final_layers_per_stream = 8, max_final_threads = 2)) AS h8
SELECT 'Chain', (h1 = h2) AND (h2 = h4) AND (h4 = h8);
DROP TABLE t_final_chain;

-- AggregatingMergeTree with a SimpleAggregateFunction
DROP TABLE IF EXISTS t_final_aggregating;
CREATE TABLE t_final_aggregating (k UInt32, v SimpleAggregateFunction(sum, UInt64)) ENGINE = AggregatingMergeTree ORDER BY k;
SYSTEM STOP MERGES t_final_aggregating;
INSERT INTO t_final_aggregating SELECT number, 1 FROM numbers(1000);
INSERT INTO t_final_aggregating SELECT number, 2 FROM numbers(500, 1000);
INSERT INTO t_final_aggregating SELECT number, 3 FROM numbers(1500, 500);
INSERT INTO t_final_aggregating SELECT number, 4 FROM numbers(1800, 800);
INSERT INTO t_final_aggregating SELECT number, 5 FROM numbers(1200, 1200);
WITH
    (SELECT cityHash64(groupArray(tuple(k, v))) FROM (SELECT k, v FROM t_final_aggregating FINAL ORDER BY k SETTINGS merge_tree_final_layers_per_stream = 1, max_final_threads = 4)) AS h1,
    (SELECT cityHash64(groupArray(tuple(k, v))) FROM (SELECT k, v FROM t_final_aggregating FINAL ORDER BY k SETTINGS merge_tree_final_layers_per_stream = 2, max_final_threads = 4)) AS h2,
    (SELECT cityHash64(groupArray(tuple(k, v))) FROM (SELECT k, v FROM t_final_aggregating FINAL ORDER BY k SETTINGS merge_tree_final_layers_per_stream = 4, max_final_threads = 4)) AS h4
SELECT 'Aggregating', (h1 = h2) AND (h2 = h4);
DROP TABLE t_final_aggregating;
