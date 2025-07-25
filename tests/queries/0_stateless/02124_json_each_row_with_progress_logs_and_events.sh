#!/usr/bin/env bash

CURDIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../shell_config.sh
. "$CURDIR"/../shell_config.sh

# Test that both logs and profile events work together
echo "Test 1: Both logs and profile events enabled"
output=$(${CLICKHOUSE_CURL} -sS ${CLICKHOUSE_URL} -d "SELECT number FROM numbers(10000) LIMIT 5 FORMAT JSONEachRowWithProgress SETTINGS send_logs_level='debug', output_format_json_include_logs=true, output_format_json_include_profile_events=true, max_block_size=1000")
echo "$output" | grep -c '"log":'
echo "$output" | grep -c '"profile_events":'
echo "$output" | grep -c '"row":'
echo "$output" | grep -c '"progress":'

echo "Test 2: Validate JSON structure with all types"
${CLICKHOUSE_CURL} -sS ${CLICKHOUSE_URL} -d "SELECT number FROM numbers(1000) LIMIT 2 FORMAT JSONEachRowWithProgress SETTINGS send_logs_level='debug', output_format_json_include_logs=true, output_format_json_include_profile_events=true" | jq -s 'map(keys[0]) | unique | sort | join(",")' | grep -E "log|meta|profile_events|row" > /dev/null && echo "1"

echo "Test 3: Progress updates trigger logs and events output"
${CLICKHOUSE_CURL} -sS ${CLICKHOUSE_URL} -d "SELECT number FROM numbers(1000000) LIMIT 10 FORMAT JSONEachRowWithProgress SETTINGS send_logs_level='debug', output_format_json_include_logs=true, output_format_json_include_profile_events=true, max_block_size=10000, interactive_delay=100000" | grep -E '"progress":|"log":|"profile_events":' | wc -l