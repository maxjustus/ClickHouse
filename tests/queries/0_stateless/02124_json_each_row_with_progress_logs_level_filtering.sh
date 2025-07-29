#!/usr/bin/env bash

CURDIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../shell_config.sh
. "$CURDIR"/../shell_config.sh

# Test that send_logs_level correctly filters logs in JSONEachRowWithProgress format

echo "Test 1: send_logs_level='trace' should show all log levels"
trace_logs=$($CLICKHOUSE_CURL -sS "$CLICKHOUSE_URL" -d "SELECT count(*) FROM numbers(10000000) SETTINGS send_logs_level='trace', output_format_json_include_logs=true, max_threads=1, interactive_delay=100 FORMAT JSONEachRowWithProgress" | grep '"log":' | jq -r '.log.level' | sort | uniq)
if echo "$trace_logs" | grep -q "Trace\|Debug"; then echo "OK"; else echo "FAIL - expected Trace/Debug logs"; fi

echo "Test 2: send_logs_level='debug' should NOT show trace logs" 
debug_logs=$($CLICKHOUSE_CURL -sS "$CLICKHOUSE_URL" -d "SELECT count(*) FROM numbers(10000000) SETTINGS send_logs_level='debug', output_format_json_include_logs=true, max_threads=1, interactive_delay=100 FORMAT JSONEachRowWithProgress" | grep '"log":' | jq -r '.log.level' | sort | uniq)
if echo "$debug_logs" | grep -q "Trace"; then echo "FAIL - trace logs should not appear"; else echo "OK"; fi

echo "Test 3: send_logs_level='information' should NOT show debug or trace logs"
info_logs=$($CLICKHOUSE_CURL -sS "$CLICKHOUSE_URL" -d "SELECT count(*) FROM numbers(10000000) SETTINGS send_logs_level='information', output_format_json_include_logs=true, max_threads=1, interactive_delay=100 FORMAT JSONEachRowWithProgress" | grep '"log":' | jq -r '.log.level' | sort | uniq)
if echo "$info_logs" | grep -q "Debug\|Trace"; then echo "FAIL - debug/trace logs should not appear"; else echo "OK"; fi

echo "Test 4: send_logs_level='warning' should only show warning and above"
warning_logs=$($CLICKHOUSE_CURL -sS "$CLICKHOUSE_URL" -d "SELECT count(*) FROM numbers(10000000) SETTINGS send_logs_level='warning', output_format_json_include_logs=true, max_threads=1, interactive_delay=100 FORMAT JSONEachRowWithProgress" | grep '"log":' | jq -r '.log.level' | sort | uniq)
if echo "$warning_logs" | grep -q "Information\|Debug\|Trace"; then echo "FAIL - lower level logs should not appear"; else echo "OK"; fi

echo "Test 5: send_logs_level='error' should only show error and above"
error_logs=$($CLICKHOUSE_CURL -sS "$CLICKHOUSE_URL" -d "SELECT count(*) FROM numbers(10000000) SETTINGS send_logs_level='error', output_format_json_include_logs=true, max_threads=1, interactive_delay=100 FORMAT JSONEachRowWithProgress" | grep '"log":' | jq -r '.log.level' | sort | uniq)
if echo "$error_logs" | grep -q "Warning\|Information\|Debug\|Trace"; then echo "FAIL - lower level logs should not appear"; else echo "OK"; fi

echo "Test 6: send_logs_level='none' should show no logs"
none_count=$($CLICKHOUSE_CURL -sS "$CLICKHOUSE_URL" -d "SELECT count(*) FROM numbers(10000000) SETTINGS send_logs_level='none', output_format_json_include_logs=true, max_threads=1, interactive_delay=100 FORMAT JSONEachRowWithProgress" | grep -c '"log":')
if [ "$none_count" -eq 0 ]; then echo "OK"; else echo "FAIL - no logs should appear with level 'none'"; fi