#include <Formats/FormatFactory.h>
#include <IO/WriteHelpers.h>
#include <Interpreters/InternalTextLogsQueue.h>
#include <Processors/Formats/Impl/JSONEachRowWithProgressRowOutputFormat.h>
#include <Processors/Port.h>
#include <Poco/Net/DNS.h>
#include <Common/CurrentThread.h>


namespace DB
{

namespace
{
constexpr size_t LOGS_COLUMNS_COUNT = 8;
constexpr const char * JSON_KEY_META = "meta";
constexpr const char * JSON_KEY_NAME = "name";
constexpr const char * JSON_KEY_TYPE = "type";
constexpr const char * JSON_KEY_ROW = "row";
constexpr const char * JSON_KEY_PROGRESS = "progress";
constexpr const char * JSON_KEY_LOG = "log";
constexpr const char * JSON_KEY_PROFILE_EVENTS = "profile_events";
constexpr const char * JSON_KEY_THREAD_ID = "thread_id";
constexpr const char * JSON_KEY_EVENTS = "events";
constexpr const char * JSON_KEY_EXCEPTION = "exception";
constexpr const char * JSON_KEY_TOTALS = "totals";
constexpr const char * JSON_KEY_MIN = "min";
constexpr const char * JSON_KEY_MAX = "max";
}

void JSONEachRowWithProgressRowOutputFormat::writePrefix()
{
    writeCString("{\"", *ostr);
    writeCString(JSON_KEY_META, *ostr);
    writeCString("\":[", *ostr);
    bool first = true;
    for (const auto & elem : getInputs().front().getHeader())
    {
        if (!first)
            writeChar(',', *ostr);
        first = false;
        writeCString("{\"", *ostr);
        writeCString(JSON_KEY_NAME, *ostr);
        writeCString("\":", *ostr);
        writeJSONString(elem.name, *ostr, settings);
        writeCString(",\"", *ostr);
        writeCString(JSON_KEY_TYPE, *ostr);
        writeCString("\":", *ostr);
        writeJSONString(elem.type->getName(), *ostr, settings);
        writeChar('}', *ostr);
    }
    writeCString("]}\n", *ostr);
}

void JSONEachRowWithProgressRowOutputFormat::writeSuffix()
{
    /// Do not write exception here like JSONEachRow does. See finalizeImpl.
}

void JSONEachRowWithProgressRowOutputFormat::writeRowStartDelimiter()
{
    writeCString("{\"", *ostr);
    writeCString(JSON_KEY_ROW, *ostr);
    writeCString("\":{", *ostr);
}

void JSONEachRowWithProgressRowOutputFormat::writeRowEndDelimiter()
{
    writeCString("}}\n", *ostr);
    field_number = 0;
}

void JSONEachRowWithProgressRowOutputFormat::writeSpecialRow(const char * kind, const Columns & columns, size_t row_num)
{
    writeCString("{\"", *ostr);
    writeCString(kind, *ostr);
    writeCString("\":{", *ostr);

    for (size_t i = 0; i < num_columns; ++i)
    {
        if (i != 0)
            writeFieldDelimiter();

        writeField(*columns[i], *serializations[i], row_num);
    }

    writeCString("}}\n", *ostr);
    field_number = 0;
}

void JSONEachRowWithProgressRowOutputFormat::writeTotals(const Columns & columns, size_t row_num)
{
    writeSpecialRow(JSON_KEY_TOTALS, columns, row_num);
}

void JSONEachRowWithProgressRowOutputFormat::writeMinExtreme(const Columns & columns, size_t row_num)
{
    writeSpecialRow(JSON_KEY_MIN, columns, row_num);
}

void JSONEachRowWithProgressRowOutputFormat::writeMaxExtreme(const Columns & columns, size_t row_num)
{
    writeSpecialRow(JSON_KEY_MAX, columns, row_num);
}

void JSONEachRowWithProgressRowOutputFormat::writeProgress(const Progress & value)
{
    if (value.empty())
        return;
    writeCString("{\"", *ostr);
    writeCString(JSON_KEY_PROGRESS, *ostr);
    writeCString("\":", *ostr);
    value.writeJSON(*ostr, Progress::DisplayMode::Minimal);
    writeCString("}\n", *ostr);

    /// Send logs and profile events when progress is written
    writeLogs();
    writeProfileEvents();
}

void JSONEachRowWithProgressRowOutputFormat::finalizeImpl()
{
    if (statistics.applied_limit)
    {
        writeCString("{\"rows_before_limit_at_least\":", *ostr);
        writeIntText(statistics.rows_before_limit, *ostr);
        writeCString("}\n", *ostr);
    }
    if (statistics.applied_aggregation)
    {
        writeCString("{\"rows_before_aggregation\":", *ostr);
        writeIntText(statistics.rows_before_aggregation, *ostr);
        writeCString("}\n", *ostr);
    }
    if (!exception_message.empty())
    {
        writeCString("{\"", *ostr);
        writeCString(JSON_KEY_EXCEPTION, *ostr);
        writeCString("\":", *ostr);
        writeJSONString(exception_message, *ostr, settings);
        writeCString("}\n", *ostr);
    }

    /// Final flush of logs and profile events before connection closes
    writeLogs();
    writeProfileEvents();
}

void JSONEachRowWithProgressRowOutputFormat::writeLogs()
{
    if (!logs_queue)
        return;

    if (!settings.json.include_logs)
        return;

    MutableColumns logs_columns;

    /// Pop logs from queue and write them directly as JSON
    while (logs_queue->tryPop(logs_columns))
    {
        /// Validate we have the expected number of columns
        if (logs_columns.size() < LOGS_COLUMNS_COUNT)
            continue;

        /// Validate all columns have the same size
        size_t num_rows = logs_columns[0]->size();
        bool valid = true;
        for (const auto & column : logs_columns)
        {
            if (column->size() != num_rows)
            {
                valid = false;
                break;
            }
        }
        if (!valid)
            continue;

        /// Write each log entry as a JSON object
        for (size_t i = 0; i < num_rows; ++i)
        {
            writeCString("{\"", *ostr);
            writeCString(JSON_KEY_LOG, *ostr);
            writeCString("\":{", *ostr);

            writeCString("\"event_time\":\"", *ostr);
            UInt32 timestamp = logs_columns[0]->getUInt(i);
            writeDateTimeText(timestamp, *ostr);
            writeCString("\",", *ostr);

            writeCString("\"event_time_microseconds\":", *ostr);
            writeIntText(logs_columns[1]->getUInt(i), *ostr);
            writeCString(",", *ostr);

            writeCString("\"host_name\":", *ostr);
            writeJSONString(logs_columns[2]->getDataAt(i).toString(), *ostr, settings);
            writeCString(",", *ostr);

            writeCString("\"query_id\":", *ostr);
            writeJSONString(logs_columns[3]->getDataAt(i).toString(), *ostr, settings);
            writeCString(",", *ostr);

            writeCString("\"thread_id\":", *ostr);
            writeIntText(logs_columns[4]->getUInt(i), *ostr);
            writeCString(",", *ostr);

            writeCString("\"level\":", *ostr);
            Int8 priority_num = logs_columns[5]->getInt(i);
            std::string_view priority_name = InternalTextLogsQueue::getPriorityName(priority_num);
            writeJSONString(priority_name, *ostr, settings);
            writeCString(",", *ostr);

            writeCString("\"source\":", *ostr);
            writeJSONString(logs_columns[6]->getDataAt(i).toString(), *ostr, settings);
            writeCString(",", *ostr);

            writeCString("\"text\":", *ostr);
            writeJSONString(logs_columns[7]->getDataAt(i).toString(), *ostr, settings);

            writeCString("}}\n", *ostr);
        }
    }
}

void JSONEachRowWithProgressRowOutputFormat::writeProfileEvents()
{
    if (!profile_events_queue)
        return;

    if (!settings.json.include_profile_events)
        return;

    if (CurrentThread::isInitialized())
        CurrentThread::updatePerformanceCounters();

    if (host_name.empty())
        host_name = Poco::Net::DNS::thisHost().name();

    Block block = getProfileEvents(host_name, profile_events_queue, last_sent_snapshots);

    if (block.rows() > 0)
    {
        /// Check if required columns exist
        if (!block.has("thread_id") || !block.has("name") || !block.has("value"))
            return;

        const auto & thread_id_column = block.getByName("thread_id").column;
        const auto & name_column = block.getByName("name").column;
        const auto & value_column = block.getByName("value").column;

        /// Validate columns are not null
        if (!thread_id_column || !name_column || !value_column)
            return;

        std::map<UInt64, std::vector<std::pair<StringRef, UInt64>>> events_by_thread;

        for (size_t i = 0; i < block.rows(); ++i)
        {
            UInt64 thread_id = thread_id_column->getUInt(i);
            StringRef name = name_column->getDataAt(i);
            UInt64 value = value_column->getUInt(i);

            events_by_thread[thread_id].emplace_back(name, value);
        }

        for (const auto & [thread_id, events] : events_by_thread)
        {
            writeCString("{\"", *ostr);
            writeCString(JSON_KEY_PROFILE_EVENTS, *ostr);
            writeCString("\":{\"", *ostr);
            writeCString(JSON_KEY_THREAD_ID, *ostr);
            writeCString("\":", *ostr);
            writeIntText(thread_id, *ostr);
            writeCString(",\"", *ostr);
            writeCString(JSON_KEY_EVENTS, *ostr);
            writeCString("\":{", *ostr);

            bool first = true;
            for (const auto & [name, value] : events)
            {
                if (!first)
                    writeCString(",", *ostr);
                first = false;

                writeJSONString(name.toView(), *ostr, settings);
                writeCString(":", *ostr);
                writeIntText(value, *ostr);
            }

            writeCString("}}}\n", *ostr);
        }
    }
}

void registerOutputFormatJSONEachRowWithProgress(FormatFactory & factory)
{
    factory.registerOutputFormat(
        "JSONEachRowWithProgress",
        [](WriteBuffer & buf, const Block & sample, const FormatSettings & _format_settings)
        {
            FormatSettings settings = _format_settings;
            settings.json.serialize_as_strings = false;
            return std::make_shared<JSONEachRowWithProgressRowOutputFormat>(buf, std::make_shared<const Block>(sample), settings);
        });
    factory.setContentType("JSONEachRowWithProgress", "application/json; charset=UTF-8");

    factory.registerOutputFormat(
        "JSONStringsEachRowWithProgress",
        [](WriteBuffer & buf, const Block & sample, const FormatSettings & _format_settings)
        {
            FormatSettings settings = _format_settings;
            settings.json.serialize_as_strings = true;
            return std::make_shared<JSONEachRowWithProgressRowOutputFormat>(buf, std::make_shared<const Block>(sample), settings);
        });
    factory.setContentType("JSONStringsEachRowWithProgress", "application/json; charset=UTF-8");
}

}
