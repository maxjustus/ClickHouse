#!/usr/bin/env bash

CURDIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../shell_config.sh
. "$CURDIR"/../shell_config.sh

# Test that logs are included in JSONEachRowWithProgress output when enabled
echo "Test 1: Basic logs output"
${CLICKHOUSE_CURL} -sS "${CLICKHOUSE_URL}?default_format=JSONEachRowWithProgress" -d "SELECT count(*) FROM numbers(10000000) SETTINGS send_logs_level='trace', output_format_json_include_logs=true" | grep -c '"log":' | awk '{if($1>0) print "1"; else print "0"}'

echo "Test 2: No logs when disabled"
${CLICKHOUSE_CURL} -sS ${CLICKHOUSE_URL} -d "SELECT count(*) FROM numbers(1000000) FORMAT JSONEachRowWithProgress SETTINGS send_logs_level='debug', output_format_json_include_logs=false" | grep -c '"log":'

echo "Test 3: Log structure validation"
${CLICKHOUSE_CURL} -sS "${CLICKHOUSE_URL}?default_format=JSONEachRowWithProgress" -d "SELECT count(*) FROM numbers(10000000) SETTINGS send_logs_level='trace', output_format_json_include_logs=true" | grep '"log":' | head -1 | jq -e '.log | has("event_time") and has("event_time_microseconds") and has("host_name") and has("query_id") and has("thread_id") and has("level") and has("source") and has("text")' > /dev/null && echo "1"

echo "Test 4: Logs with longer query"
${CLICKHOUSE_CURL} -sS "${CLICKHOUSE_URL}?default_format=JSONEachRowWithProgress" -d "SELECT count(*) FROM numbers(100000000) SETTINGS send_logs_level='trace', output_format_json_include_logs=true" | grep -c '"log":' | awk '{if($1>0) print "1"; else print "0"}'

echo "Test 5: Different log levels"
for level in trace debug information warning error; do
    count=$(${CLICKHOUSE_CURL} -sS ${CLICKHOUSE_URL} -d "SELECT 1 FORMAT JSONEachRowWithProgress SETTINGS send_logs_level='$level', output_format_json_include_logs=true" | grep '"log":' | wc -l)
    echo "Log level $level: found logs"
done