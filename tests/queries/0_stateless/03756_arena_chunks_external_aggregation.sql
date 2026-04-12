-- Tags: long
-- Regression test for Arena chunk allocation in spill-heavy external aggregation.
--
-- Aggregator::mergeBlocks() used to allocate fresh Arena objects on every
-- per-bucket call, driving ArenaAllocChunks into the thousands per query
-- before any merging actually happened. The fix reuses a per-thread key arena
-- in MergingAggregatedBucketTransform, and writeToTemporaryFile() reuses the
-- aggregates_pool across spills via Arena::clear().
--
-- This test pins ArenaAllocChunks for a workload that spills many times. If
-- this regresses upward by orders of magnitude, somebody has reintroduced a
-- per-bucket or per-spill Arena allocation in the merge path.

SELECT count() FROM (
    SELECT
        concat('user_', toString(cityHash64(number) % 5000000)) AS k1,
        concat('evt_',  toString(cityHash64(number + 17) % 5000000)) AS k2,
        argMax(concat('payload_', toString(number * 31)), number) AS v
    FROM numbers(2000000)
    GROUP BY k1, k2
)
SETTINGS
    max_threads = 4,
    max_bytes_before_external_group_by = 16000000,
    max_bytes_ratio_before_external_group_by = 0,
    group_by_two_level_threshold = 1000,
    group_by_two_level_threshold_bytes = 1000000,
    max_memory_usage = 0,
    log_comment = '03756_arena_chunks_external_aggregation';

SYSTEM FLUSH LOGS query_log;

-- Pre-fix this query allocates ~1500+ chunks; post-fix it allocates ~280.
-- 500 leaves headroom for thread-count variation, page size differences,
-- and growth-curve jitter while still catching reintroduction of per-bucket
-- key-arena allocation in the merge path.
SELECT
    ProfileEvents['ArenaAllocChunks'] < 500           AS chunks_bounded,
    ProfileEvents['ExternalAggregationWritePart'] > 0 AS spilled
FROM system.query_log
WHERE type = 'QueryFinish'
  AND current_database = currentDatabase()
  AND log_comment = '03756_arena_chunks_external_aggregation'
ORDER BY event_time DESC
LIMIT 1;
