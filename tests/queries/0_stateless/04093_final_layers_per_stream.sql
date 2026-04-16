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

-- Cross-partition chain: ReplacingMergeTree with a partition key producing several partitions,
-- do_not_merge_across_partitions_select_final keeps them independent so merging_pipes has
-- one entry per partition that the chain can bucket.
DROP TABLE IF EXISTS t_final_cross;
CREATE TABLE t_final_cross (p UInt32, k UInt32, v UInt32, ver UInt32)
ENGINE = ReplacingMergeTree(ver) PARTITION BY p ORDER BY k;
SYSTEM STOP MERGES t_final_cross;
INSERT INTO t_final_cross SELECT 0, number, number + 10, 1 FROM numbers(1000);
INSERT INTO t_final_cross SELECT 0, number, number + 11, 2 FROM numbers(500, 1000);
INSERT INTO t_final_cross SELECT 1, number, number + 20, 1 FROM numbers(1000);
INSERT INTO t_final_cross SELECT 1, number, number + 21, 2 FROM numbers(500, 1000);
INSERT INTO t_final_cross SELECT 2, number, number + 30, 1 FROM numbers(1000);
INSERT INTO t_final_cross SELECT 2, number, number + 31, 2 FROM numbers(500, 1000);
INSERT INTO t_final_cross SELECT 3, number, number + 40, 1 FROM numbers(1000);
INSERT INTO t_final_cross SELECT 3, number, number + 41, 2 FROM numbers(500, 1000);
INSERT INTO t_final_cross SELECT 4, number, number + 50, 1 FROM numbers(1000);
INSERT INTO t_final_cross SELECT 4, number, number + 51, 2 FROM numbers(500, 1000);
INSERT INTO t_final_cross SELECT 5, number, number + 60, 1 FROM numbers(1000);
INSERT INTO t_final_cross SELECT 5, number, number + 61, 2 FROM numbers(500, 1000);
INSERT INTO t_final_cross SELECT 6, number, number + 70, 1 FROM numbers(1000);
INSERT INTO t_final_cross SELECT 6, number, number + 71, 2 FROM numbers(500, 1000);
INSERT INTO t_final_cross SELECT 7, number, number + 80, 1 FROM numbers(1000);
INSERT INTO t_final_cross SELECT 7, number, number + 81, 2 FROM numbers(500, 1000);
WITH
    (SELECT cityHash64(groupArray(tuple(p, k, v, ver))) FROM (SELECT p, k, v, ver FROM t_final_cross FINAL ORDER BY p, k SETTINGS merge_tree_final_partitions_per_stream = 1, merge_tree_final_layers_per_stream = 1, do_not_merge_across_partitions_select_final = 1, max_final_threads = 4)) AS h_1_1,
    (SELECT cityHash64(groupArray(tuple(p, k, v, ver))) FROM (SELECT p, k, v, ver FROM t_final_cross FINAL ORDER BY p, k SETTINGS merge_tree_final_partitions_per_stream = 2, merge_tree_final_layers_per_stream = 1, do_not_merge_across_partitions_select_final = 1, max_final_threads = 4)) AS h_2_1,
    (SELECT cityHash64(groupArray(tuple(p, k, v, ver))) FROM (SELECT p, k, v, ver FROM t_final_cross FINAL ORDER BY p, k SETTINGS merge_tree_final_partitions_per_stream = 4, merge_tree_final_layers_per_stream = 1, do_not_merge_across_partitions_select_final = 1, max_final_threads = 4)) AS h_4_1,
    (SELECT cityHash64(groupArray(tuple(p, k, v, ver))) FROM (SELECT p, k, v, ver FROM t_final_cross FINAL ORDER BY p, k SETTINGS merge_tree_final_partitions_per_stream = 8, merge_tree_final_layers_per_stream = 1, do_not_merge_across_partitions_select_final = 1, max_final_threads = 4)) AS h_8_1,
    (SELECT cityHash64(groupArray(tuple(p, k, v, ver))) FROM (SELECT p, k, v, ver FROM t_final_cross FINAL ORDER BY p, k SETTINGS merge_tree_final_partitions_per_stream = 4, merge_tree_final_layers_per_stream = 2, do_not_merge_across_partitions_select_final = 1, max_final_threads = 2)) AS h_4_2
SELECT 'Cross', (h_1_1 = h_2_1) AND (h_2_1 = h_4_1) AND (h_4_1 = h_8_1) AND (h_8_1 = h_4_2);
DROP TABLE t_final_cross;

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
