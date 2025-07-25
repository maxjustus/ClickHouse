#!/usr/bin/env bash

CURDIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../shell_config.sh
. "$CURDIR"/../shell_config.sh

# Test that profile events are included in JSONEachRowWithProgress output when enabled
echo "Test 1: Basic profile events output"
${CLICKHOUSE_CURL} -sS ${CLICKHOUSE_URL} -d "SELECT number FROM numbers(100000) LIMIT 10 FORMAT JSONEachRowWithProgress SETTINGS output_format_json_include_profile_events=true" | grep -c '"profile_events":'

echo "Test 2: No profile events when disabled"
${CLICKHOUSE_CURL} -sS ${CLICKHOUSE_URL} -d "SELECT number FROM numbers(100000) LIMIT 10 FORMAT JSONEachRowWithProgress SETTINGS output_format_json_include_profile_events=false" | grep -c '"profile_events":'

echo "Test 3: Profile events structure validation"
${CLICKHOUSE_CURL} -sS ${CLICKHOUSE_URL} -d "SELECT number FROM numbers(100000) LIMIT 10 FORMAT JSONEachRowWithProgress SETTINGS output_format_json_include_profile_events=true, max_block_size=1000" | grep '"profile_events":' | head -1 | jq -e '.profile_events | has("thread_id") and has("events")' > /dev/null && echo "1"

echo "Test 4: Profile events contain counters"
${CLICKHOUSE_CURL} -sS ${CLICKHOUSE_URL} -d "SELECT number FROM numbers(100000) LIMIT 10 FORMAT JSONEachRowWithProgress SETTINGS output_format_json_include_profile_events=true, max_block_size=1000" | grep '"profile_events":' | head -1 | jq -e '.profile_events.events | length > 0' > /dev/null && echo "1"

echo "Test 5: Profile events with aggregation"
${CLICKHOUSE_CURL} -sS ${CLICKHOUSE_URL} -d "SELECT count(*) FROM numbers(100000) FORMAT JSONEachRowWithProgress SETTINGS output_format_json_include_profile_events=true" | grep -c '"profile_events":'