#!/usr/bin/env bash
# Tags: no-fasttest

CURDIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../shell_config.sh
. "$CURDIR"/../shell_config.sh

# Create test table for INSERT SELECT tests
${CLICKHOUSE_CLIENT} -q "DROP TABLE IF EXISTS test_insert_select_progress"
${CLICKHOUSE_CLIENT} -q "CREATE TABLE test_insert_select_progress (n UInt64) ENGINE = MergeTree ORDER BY n"

# Test 1: INSERT SELECT produces profile events when setting enabled
${CLICKHOUSE_CURL} -sS "${CLICKHOUSE_URL}" -d "INSERT INTO test_insert_select_progress SELECT number FROM numbers(100000) SETTINGS output_format_json_include_profile_events=true" | grep -c '"profile_events":' | awk '{if($1>0) print "INSERT SELECT profile_events: OK"; else print "INSERT SELECT profile_events: FAIL"}'

# Test 2: INSERT SELECT produces progress when setting enabled
${CLICKHOUSE_CURL} -sS "${CLICKHOUSE_URL}" -d "INSERT INTO test_insert_select_progress SELECT number FROM numbers(100000) SETTINGS output_format_json_include_profile_events=true" | grep -c '"progress":' | awk '{if($1>0) print "INSERT SELECT progress: OK"; else print "INSERT SELECT progress: FAIL"}'

# Test 3: Verify data was actually inserted
${CLICKHOUSE_CLIENT} -q "SELECT count() > 0 FROM test_insert_select_progress" | awk '{if($1==1) print "INSERT SELECT data inserted: OK"; else print "INSERT SELECT data inserted: FAIL"}'

# Test 4: CREATE TABLE AS SELECT produces profile events
${CLICKHOUSE_CLIENT} -q "DROP TABLE IF EXISTS test_ctas_progress"
${CLICKHOUSE_CURL} -sS "${CLICKHOUSE_URL}" -d "CREATE TABLE test_ctas_progress ENGINE = MergeTree ORDER BY n AS SELECT number as n FROM numbers(100000) SETTINGS output_format_json_include_profile_events=true" | grep -c '"profile_events":' | awk '{if($1>0) print "CTAS profile_events: OK"; else print "CTAS profile_events: FAIL"}'

# Test 5: Verify CTAS table was created with data
${CLICKHOUSE_CLIENT} -q "SELECT count() > 0 FROM test_ctas_progress" | awk '{if($1==1) print "CTAS data inserted: OK"; else print "CTAS data inserted: FAIL"}'

# Cleanup
${CLICKHOUSE_CLIENT} -q "DROP TABLE test_insert_select_progress"
${CLICKHOUSE_CLIENT} -q "DROP TABLE test_ctas_progress"
