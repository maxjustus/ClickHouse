#include <algorithm>
#include <Columns/IColumn.h>
#include <Formats/JSONUtils.h>
#include <Processors/Formats/Impl/JSONColumnsBlockOutputFormatBase.h>
#include <Processors/Formats/OutputFormatWithUTF8ValidationAdaptor.h>
#include <Processors/Port.h>


namespace DB
{

JSONColumnsBlockOutputFormatBase::JSONColumnsBlockOutputFormatBase(
    WriteBuffer & out_, SharedHeader header_, const FormatSettings & format_settings_, bool validate_utf8)
    : OutputFormatWithUTF8ValidationAdaptor(header_, out_, validate_utf8)
    , format_settings(format_settings_)
    , serializations(header_->getSerializations())
{
    ostr = OutputFormatWithUTF8ValidationAdaptor::getWriteBufferPtr();
}

void JSONColumnsBlockOutputFormatBase::resetFormatterImpl()
{
    OutputFormatWithUTF8ValidationAdaptor::resetFormatterImpl();
    ostr = OutputFormatWithUTF8ValidationAdaptor::getWriteBufferPtr();
}

void JSONColumnsBlockOutputFormatBase::consume(Chunk chunk)
{
    if (!mono_chunk)
    {
        mono_chunk = std::move(chunk);
        return;
    }

    mono_chunk.append(chunk);
}

void JSONColumnsBlockOutputFormatBase::writeSuffix()
{
    if (format_settings.json_columns.output_block_size == 0 || mono_chunk.getNumRows() == 0)
    {
        // Single block mode or empty data
        writeChunk(mono_chunk);
    }
    else
    {
        // Multi-block mode: split chunk into blocks of specified size
        size_t block_size = format_settings.json_columns.output_block_size;
        size_t total_rows = mono_chunk.getNumRows();
        const auto & columns = mono_chunk.getColumns();

        for (size_t offset = 0; offset < total_rows; offset += block_size)
        {
            size_t rows_in_block = std::min(block_size, total_rows - offset);

            // Slice each column manually
            Columns sliced_columns;
            for (const auto & col : columns)
            {
                sliced_columns.push_back(col->cut(offset, rows_in_block));
            }

            Chunk block_chunk(std::move(sliced_columns), rows_in_block);
            writeChunk(block_chunk);
            // writeChunk() calls writeChunkEnd() which writes the newline
        }
    }
    mono_chunk.clear();
}

void JSONColumnsBlockOutputFormatBase::writeChunk(Chunk & chunk)
{
    writeChunkStart();
    const auto & columns = chunk.getColumns();
    for (size_t i = 0; i != columns.size(); ++i)
    {
        writeColumnStart(i);
        writeColumn(*columns[i], *serializations[i]);
        writeColumnEnd(i == columns.size() - 1);
    }
    written_rows += chunk.getNumRows();
    writeChunkEnd();
}

void JSONColumnsBlockOutputFormatBase::writeColumnEnd(bool is_last)
{
    JSONUtils::writeCompactArrayEnd(*ostr);
    if (!is_last)
        JSONUtils::writeFieldDelimiter(*ostr);
}

void JSONColumnsBlockOutputFormatBase::writeColumn(const IColumn & column, const ISerialization & serialization)
{
    for (size_t i = 0; i != column.size(); ++i)
    {
        if (i != 0)
            JSONUtils::writeFieldCompactDelimiter(*ostr);
        serialization.serializeTextJSON(column, i, *ostr, format_settings);
    }
}

}
