#!/usr/bin/env bash

CURDIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../shell_config.sh
. "$CURDIR"/../shell_config.sh

# Test profile events are produced with long query
$CLICKHOUSE_CURL -sS "$CLICKHOUSE_URL" -d "SELECT count(*) FROM numbers(100000000) SETTINGS output_format_json_include_profile_events=true, output_format_json_include_logs=false FORMAT JSONEachRowWithProgress" | grep -c '"profile_events":' | awk '{if($1>0) print "OK"; else print "FAIL"}'

# Test profile events are NOT produced when disabled
$CLICKHOUSE_CURL -sS "$CLICKHOUSE_URL" -d "SELECT count(*) FROM numbers(100000000) SETTINGS output_format_json_include_profile_events=false FORMAT JSONEachRowWithProgress" | grep -c '"profile_events":' | awk '{if($1==0) print "OK"; else print "FAIL"}'

# Test profile events structure contains expected fields
$CLICKHOUSE_CURL -sS "$CLICKHOUSE_URL" -d "SELECT count(*) FROM numbers(100000000) SETTINGS output_format_json_include_profile_events=true FORMAT JSONEachRowWithProgress" | grep '"profile_events":' | head -1 | jq -e '.profile_events | has("thread_id") and has("events")' > /dev/null && echo "OK"