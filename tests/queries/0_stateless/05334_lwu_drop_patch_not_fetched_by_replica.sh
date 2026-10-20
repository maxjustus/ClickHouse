#!/usr/bin/env bash
# Tags: no-replicated-database, no-shared-catalog, no-shared-merge-tree

CUR_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../shell_config.sh
. "$CUR_DIR"/../shell_config.sh

set -e

$CLICKHOUSE_CLIENT --query "
    DROP TABLE IF EXISTS t_lwu_drop_patch_1 SYNC;
    DROP TABLE IF EXISTS t_lwu_drop_patch_2 SYNC;

    CREATE TABLE t_lwu_drop_patch_1 (k UInt64, v String)
    ENGINE = ReplicatedMergeTree('/clickhouse/tables/{database}/t_lwu_drop_patch', '1')
    ORDER BY k
    SETTINGS
        enable_block_number_column = 1,
        enable_block_offset_column = 1,
        cleanup_delay_period = 1,
        max_cleanup_delay_period = 1,
        cleanup_delay_period_random_add = 0;

    CREATE TABLE t_lwu_drop_patch_2 (k UInt64, v String)
    ENGINE = ReplicatedMergeTree('/clickhouse/tables/{database}/t_lwu_drop_patch', '2')
    ORDER BY k
    SETTINGS
        enable_block_number_column = 1,
        enable_block_offset_column = 1,
        cleanup_delay_period = 1,
        max_cleanup_delay_period = 1,
        cleanup_delay_period_random_add = 0;

    SET enable_lightweight_update = 1;
    SET insert_keeper_fault_injection_probability = 0.0;

    INSERT INTO t_lwu_drop_patch_1 VALUES (1, 'v1') (2, 'v2') (3, 'v3');
    SYSTEM SYNC REPLICA t_lwu_drop_patch_2;
    SYSTEM STOP FETCHES t_lwu_drop_patch_2;

    UPDATE t_lwu_drop_patch_1 SET v = 'u2' WHERE k = 2;
    ALTER TABLE t_lwu_drop_patch_1 UPDATE v = v || '_m' WHERE 1;
"

# Replica 1 applies the patch part in the mutation, and its cleanup thread then tries to drop the patch part.
# Replica 2 has not fetched the patch part, so replica 1 must keep it and retry the drop on the next cleanup.
for _ in {0..60}; do
    res=$($CLICKHOUSE_CLIENT --query "
        SYSTEM FLUSH LOGS text_log;
        SELECT count() FROM system.text_log
        WHERE logger_name LIKE '${CLICKHOUSE_DATABASE}.t_lwu_drop_patch_1 %' AND message LIKE 'Will drop unused patch part%'")
    if [[ $res -ge 2 ]]; then
        break
    fi
    sleep 1.0
done

$CLICKHOUSE_CLIENT --query "
    SYSTEM SYNC REPLICA t_lwu_drop_patch_2 PULL;
    SYSTEM START FETCHES t_lwu_drop_patch_2;
    SYSTEM SYNC REPLICA t_lwu_drop_patch_2;

    SELECT _part, * FROM t_lwu_drop_patch_1 ORDER BY k SETTINGS apply_patch_parts = 0;
    SELECT _part, * FROM t_lwu_drop_patch_2 ORDER BY k SETTINGS apply_patch_parts = 0;

    DROP TABLE t_lwu_drop_patch_1 SYNC;
    DROP TABLE t_lwu_drop_patch_2 SYNC;
"
