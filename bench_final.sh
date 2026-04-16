#!/usr/bin/env bash
# Benchmark the impact of merge_tree_final_layers_per_stream on FINAL.
#
# Spins up a local clickhouse-server on a random port, builds a
# ReplacingMergeTree with a sliding-overlap pattern (so PartsSplitter produces
# enough layers to exercise bucketing), and runs the same FINAL query under a
# matrix of (K, max_final_threads). Reports peak memory_usage and
# query_duration_ms from system.query_log.
#
# Env overrides (all optional):
#   CH               clickhouse binary path (default: ./macbuild/programs/clickhouse)
#   DATA_DIR         scratch data dir       (default: tmp/final_bench)
#   PORT             server TCP port        (default: random 40000-50000)
#   PARTS            number of parts        (default: 32)
#   ROWS_PER_PART    rows per part          (default: 200000)
#   V_SIZE           bytes per `v` value    (default: 256)
#   OVERLAP_NUM/_DEN overlap fraction       (default: 2/3 — each part shifts
#                                            by ROWS_PER_PART/3)
#   REPS             reps per cell          (default: 4; first is warmup)
#   K_LIST           K values               (default: "1 2 4 8 16")
#   T_LIST           max_final_threads      (default: "2 4 8")

set -euo pipefail

CH=${CH:-./macbuild/programs/clickhouse}
DATA_DIR=${DATA_DIR:-tmp/final_bench}
PORT=${PORT:-$((40000 + RANDOM % 10000))}
PARTS=${PARTS:-32}
ROWS_PER_PART=${ROWS_PER_PART:-200000}
V_SIZE=${V_SIZE:-256}
OVERLAP_NUM=${OVERLAP_NUM:-2}
OVERLAP_DEN=${OVERLAP_DEN:-3}
REPS=${REPS:-4}
K_LIST=${K_LIST:-"1 2 4 8 16"}
T_LIST=${T_LIST:-"2 4 8"}

rm -rf "$DATA_DIR"
mkdir -p "$DATA_DIR"

CONFIG="$DATA_DIR/config.xml"
cat > "$CONFIG" <<EOF
<clickhouse>
    <path>$(pwd)/$DATA_DIR/</path>
    <tmp_path>$(pwd)/$DATA_DIR/tmp/</tmp_path>
    <user_files_path>$(pwd)/$DATA_DIR/user_files/</user_files_path>
    <format_schema_path>$(pwd)/$DATA_DIR/format_schemas/</format_schema_path>

    <tcp_port>$PORT</tcp_port>
    <listen_host>127.0.0.1</listen_host>
    <max_connections>16</max_connections>

    <logger>
        <level>warning</level>
        <log>$(pwd)/$DATA_DIR/clickhouse.log</log>
        <errorlog>$(pwd)/$DATA_DIR/clickhouse.err.log</errorlog>
        <size>100M</size>
        <count>1</count>
    </logger>

    <users>
        <default>
            <password/>
            <networks><ip>::/0</ip></networks>
            <profile>default</profile>
            <quota>default</quota>
            <access_management>1</access_management>
        </default>
    </users>
    <profiles><default/></profiles>
    <quotas><default/></quotas>

    <remote_servers>
        <default>
            <shard>
                <replica>
                    <host>127.0.0.1</host>
                    <port>$PORT</port>
                </replica>
            </shard>
        </default>
    </remote_servers>

    <query_log>
        <database>system</database>
        <table>query_log</table>
        <flush_interval_milliseconds>500</flush_interval_milliseconds>
    </query_log>
</clickhouse>
EOF

echo "Starting clickhouse-server on port $PORT..." >&2
"$CH" server --config-file="$CONFIG" >"$DATA_DIR/stdout.log" 2>"$DATA_DIR/stderr.log" &
SERVER_PID=$!

cleanup() {
    if [ -n "${SERVER_PID:-}" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

# Wait for server
for _ in $(seq 1 60); do
    if "$CH" client --port "$PORT" --query "SELECT 1" >/dev/null 2>&1; then
        break
    fi
    sleep 0.5
done
"$CH" client --port "$PORT" --query "SELECT 1" >/dev/null || { echo "server failed to start; see $DATA_DIR/clickhouse.err.log" >&2; exit 1; }

CLIENT=("$CH" client --port "$PORT")

echo "Building dataset..." >&2
"${CLIENT[@]}" --query "DROP TABLE IF EXISTS t SYNC"
"${CLIENT[@]}" --query "CREATE TABLE t (k UInt32, v String, ver UInt32) ENGINE = ReplacingMergeTree(ver) ORDER BY k"
"${CLIENT[@]}" --query "SYSTEM STOP MERGES t"

shift_per_part=$(( ROWS_PER_PART * (OVERLAP_DEN - OVERLAP_NUM) / OVERLAP_DEN ))
for i in $(seq 0 $((PARTS - 1))); do
    offset=$(( i * shift_per_part ))
    "${CLIENT[@]}" --query "INSERT INTO t SELECT (number + $offset)::UInt32, repeat('x', $V_SIZE), $((i + 1)) FROM numbers($ROWS_PER_PART)"
done

"${CLIENT[@]}" --query "SELECT 'dataset', count() AS rows, uniqExact(_part) AS parts, formatReadableSize(sum(length(v))) AS v_bytes FROM t FORMAT Vertical"

# Warmup per thread count.
for T in $T_LIST; do
    "${CLIENT[@]}" --query "SELECT sum(length(v)) FROM t FINAL SETTINGS merge_tree_final_layers_per_stream = 1, max_final_threads = $T" >/dev/null
done

echo "Benchmarking..." >&2
for K in $K_LIST; do
    for T in $T_LIST; do
        for rep in $(seq 1 $REPS); do
            tag="measure"
            [ "$rep" = "1" ] && tag="warmup"
            lc="bench_K${K}_T${T}_rep${rep}_${tag}"
            "${CLIENT[@]}" --query "
                SELECT sum(length(v)) FROM t FINAL
                SETTINGS merge_tree_final_layers_per_stream = $K,
                         max_final_threads = $T,
                         log_comment = '$lc'
            " >/dev/null
        done
    done
done

"${CLIENT[@]}" --query "SYSTEM FLUSH LOGS"

echo "" >&2
echo "Results (medians across $((REPS - 1)) measured reps per cell; warmup excluded):" >&2

"${CLIENT[@]}" --query "
WITH measured AS (
    SELECT
        toUInt32OrZero(extract(log_comment, 'K(\\d+)')) AS K,
        toUInt32OrZero(extract(log_comment, 'T(\\d+)')) AS threads,
        query_duration_ms,
        memory_usage,
        ProfileEvents['SelectedParts'] AS sel_parts,
        ProfileEvents['SelectedMarks'] AS sel_marks
    FROM system.query_log
    WHERE type = 'QueryFinish'
      AND match(log_comment, '^bench_K\\d+_T\\d+_rep\\d+_measure$')
)
SELECT
    threads,
    K,
    round(quantile(0.5)(query_duration_ms)) AS p50_ms,
    round(min(query_duration_ms))           AS min_ms,
    formatReadableSize(round(quantile(0.5)(memory_usage))) AS p50_mem,
    formatReadableSize(max(memory_usage))                  AS peak_mem,
    any(sel_parts) AS sel_parts,
    any(sel_marks) AS sel_marks,
    count()        AS reps
FROM measured
GROUP BY threads, K
ORDER BY threads, K
FORMAT PrettyCompact
"

echo "" >&2
echo "Ratios vs K=1 baseline (same threads):" >&2

"${CLIENT[@]}" --query "
WITH measured AS (
    SELECT
        toUInt32OrZero(extract(log_comment, 'K(\\d+)')) AS K,
        toUInt32OrZero(extract(log_comment, 'T(\\d+)')) AS threads,
        query_duration_ms,
        memory_usage
    FROM system.query_log
    WHERE type = 'QueryFinish'
      AND match(log_comment, '^bench_K\\d+_T\\d+_rep\\d+_measure$')
),
agg AS (
    SELECT
        threads,
        K,
        quantile(0.5)(query_duration_ms)::Float64 AS p50_ms,
        quantile(0.5)(memory_usage)::Float64      AS p50_mem
    FROM measured
    GROUP BY threads, K
),
baseline AS (
    SELECT threads, p50_ms AS base_ms, p50_mem AS base_mem
    FROM agg WHERE K = 1
)
SELECT
    a.threads AS threads,
    a.K       AS K,
    round(a.p50_ms / b.base_ms, 2) AS wall_ratio,
    round(a.p50_mem / b.base_mem, 2) AS mem_ratio
FROM agg a
INNER JOIN baseline b ON a.threads = b.threads
ORDER BY a.threads, a.K
FORMAT PrettyCompact
"
