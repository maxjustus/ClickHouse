#!/usr/bin/env bash

CURDIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../shell_config.sh
. "$CURDIR"/../shell_config.sh

# Test logs are produced with trace level and long query
$CLICKHOUSE_CURL -sS "$CLICKHOUSE_URL" -d "SELECT count(*) FROM numbers(100000000) SETTINGS send_logs_level='trace', output_format_json_include_logs=true, output_format_json_include_profile_events=false FORMAT JSONEachRowWithProgress" | grep -c '"log":' | awk '{if($1>0) print "OK"; else print "FAIL"}'

# Test logs are NOT produced when disabled
$CLICKHOUSE_CURL -sS "$CLICKHOUSE_URL" -d "SELECT count(*) FROM numbers(100000000) SETTINGS send_logs_level='trace', output_format_json_include_logs=false FORMAT JSONEachRowWithProgress" | grep -c '"log":' | awk '{if($1==0) print "OK"; else print "FAIL"}'

# Test log structure contains expected fields
$CLICKHOUSE_CURL -sS "$CLICKHOUSE_URL" -d "SELECT count(*) FROM numbers(100000000) SETTINGS send_logs_level='trace', output_format_json_include_logs=true FORMAT JSONEachRowWithProgress" | grep '"log":' | head -1 | jq -e '.log | has("event_time") and has("level") and has("text")' > /dev/null && echo "OK"