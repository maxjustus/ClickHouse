#pragma once

#include <Interpreters/InternalTextLogsQueue.h>
#include <Interpreters/ProfileEventsExt.h>
#include <Processors/Formats/Impl/JSONEachRowRowOutputFormat.h>
#include <Common/ThreadStatus.h>

namespace DB
{

class JSONEachRowWithProgressRowOutputFormat final : public JSONEachRowRowOutputFormat
{
public:
    using JSONEachRowRowOutputFormat::JSONEachRowRowOutputFormat;

    /// Set the queue for receiving server text logs. The logs will be included in the output
    /// when settings.json.include_logs is true and send_logs_level is set.
    void setLogsQueue(InternalTextLogsQueuePtr logs_queue_) { logs_queue = logs_queue_; }

    /// Set the queue for receiving ProfileEvents counters. The events will be included in the output
    /// when settings.json.include_profile_events is true.
    void setProfileEventsQueue(InternalProfileEventsQueuePtr profile_events_queue_) { profile_events_queue = profile_events_queue_; }

private:
    bool supportTotals() const override { return true; }
    bool supportExtremes() const override { return true; }

    void writePrefix() override;
    void writeSuffix() override;
    bool writesProgressConcurrently() const override { return true; }
    void writeProgress(const Progress & value) override;
    void writeRowStartDelimiter() override;
    void writeRowEndDelimiter() override;
    void writeMinExtreme(const Columns & columns, size_t row_num) override;
    void writeMaxExtreme(const Columns & columns, size_t row_num) override;
    void writeTotals(const Columns & columns, size_t row_num) override;
    void finalizeImpl() override;

    void setRowsBeforeLimit(size_t rows_before_limit_) override
    {
        statistics.applied_limit = true;
        statistics.rows_before_limit = rows_before_limit_;
    }
    void setRowsBeforeAggregation(size_t rows_before_aggregation_) override
    {
        statistics.applied_aggregation = true;
        statistics.rows_before_aggregation = rows_before_aggregation_;
    }

    void writeSpecialRow(const char * kind, const Columns & columns, size_t row_num);

    /// Write pending log entries from the logs queue as JSON objects
    void writeLogs();

    /// Write accumulated ProfileEvents counters as JSON objects
    void writeProfileEvents();

    InternalTextLogsQueuePtr logs_queue;
    InternalProfileEventsQueuePtr profile_events_queue;
    ProfileEvents::ThreadIdToCountersSnapshot last_sent_snapshots;
    String host_name;
};

}
