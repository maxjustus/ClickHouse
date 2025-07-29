#!/usr/bin/env bash

CURDIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../shell_config.sh
. "$CURDIR"/../shell_config.sh

# Test that logs are included in JSONEachRowWithProgress output when enabled
echo "Test 1: Logs disabled"
${CLICKHOUSE_CURL} -sS ${CLICKHOUSE_URL} -d "SELECT 1 FORMAT JSONEachRowWithProgress SETTINGS send_logs_level='trace', output_format_json_include_logs=false" | grep -c '"log":'

echo "Test 2: Basic logs with trace level"
${CLICKHOUSE_CURL} -sS "${CLICKHOUSE_URL}?default_format=JSONEachRowWithProgress" -d "SELECT count(*) FROM numbers(100000000) SETTINGS send_logs_level='trace', output_format_json_include_logs=true, max_threads=1, interactive_delay=100" | grep -c '"log":' | awk '{if($1>0) print "1"; else print "0"}'

echo "Test 3: Log structure validation"
output=$(${CLICKHOUSE_CURL} -sS "${CLICKHOUSE_URL}?default_format=JSONEachRowWithProgress" -d "SELECT count(*) FROM numbers(100000000) SETTINGS send_logs_level='trace', output_format_json_include_logs=true, max_threads=1, interactive_delay=100")
log_line=$(echo "$output" | grep '"log":' | head -1)
if [ -n "$log_line" ]; then
    echo "$log_line" | jq -e '.log | has("event_time") and has("event_time_microseconds") and has("host_name") and has("query_id") and has("thread_id") and has("level") and has("source") and has("text")' > /dev/null && echo "1"
else
    echo "0"
fi

echo "Test 4: Different log levels filter correctly"
# Only trace level typically produces logs for simple queries
for level in trace debug information warning error; do
    output=$(${CLICKHOUSE_CURL} -sS ${CLICKHOUSE_URL} -d "SELECT count(*) FROM numbers(10000000) FORMAT JSONEachRowWithProgress SETTINGS send_logs_level='$level', output_format_json_include_logs=true, max_threads=1, interactive_delay=100")
    count=$(echo "$output" | grep -c '"log":')
    if [ "$count" -gt 0 ]; then
        echo "Log level $level: found logs"
    else
        echo "Log level $level: no logs found"
    fi
done