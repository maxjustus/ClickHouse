#!/usr/bin/env bash

CURDIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../shell_config.sh
. "$CURDIR"/../shell_config.sh

# Test that both logs and profile events work together
echo "Test 1: Both logs and profile events enabled"
output=$(${CLICKHOUSE_CURL} -sS ${CLICKHOUSE_URL} -d "SELECT count(*) FROM numbers(100000000) FORMAT JSONEachRowWithProgress SETTINGS send_logs_level='trace', output_format_json_include_logs=true, output_format_json_include_profile_events=true, max_threads=1, interactive_delay=100")
log_count=$(echo "$output" | grep -c '"log":')
events_count=$(echo "$output" | grep -c '"profile_events":')
row_count=$(echo "$output" | grep -c '"row":')

if [ "$log_count" -gt 0 ]; then echo "OK: Found logs"; else echo "FAIL: No logs found"; fi
if [ "$events_count" -gt 0 ]; then echo "OK: Found profile events"; else echo "FAIL: No profile events found"; fi
if [ "$row_count" -eq 1 ]; then echo "OK: Found 1 row"; else echo "FAIL: Expected 1 row, found $row_count"; fi

echo "Test 2: Validate JSON structure with all types"
${CLICKHOUSE_CURL} -sS ${CLICKHOUSE_URL} -d "SELECT number FROM numbers(100000) LIMIT 2 FORMAT JSONEachRowWithProgress SETTINGS send_logs_level='trace', output_format_json_include_logs=true, output_format_json_include_profile_events=true, max_threads=1, interactive_delay=100" | jq -s 'map(keys[0]) | unique | sort | contains(["meta", "profile_events", "row"])' | grep -q "true" && echo "OK"