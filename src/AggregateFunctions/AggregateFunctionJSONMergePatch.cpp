#include <AggregateFunctions/AggregateFunctionFactory.h>
#include <AggregateFunctions/FactoryHelpers.h>
#include <AggregateFunctions/IAggregateFunction.h>
#include <Columns/ColumnDynamic.h>
#include <Columns/ColumnObject.h>
#include <DataTypes/DataTypeObject.h>
#include <Functions/JSONMergePatchHelpers.h>
#include <DataTypes/Serializations/SerializationDynamic.h>
#include <Common/FieldBinaryEncoding.h>
#include <Formats/FormatSettings.h>
#include <IO/ReadBufferFromMemory.h>
#include <IO/ReadHelpers.h>
#include <IO/WriteHelpers.h>
#include <Common/assert_cast.h>
#include <string_view>

#if USE_RAPIDJSON

namespace DB
{

namespace ErrorCodes
{
    extern const int ILLEGAL_TYPE_OF_ARGUMENT;
}

namespace
{
    const FormatSettings & getFormatSettingsForJSONMergePatch()
    {
        static thread_local const FormatSettings settings;
        return settings;
    }

    const std::shared_ptr<SerializationDynamic> & getDynamicSerializationForJSONMergePatch()
    {
        static thread_local const std::shared_ptr<SerializationDynamic> serialization = std::make_shared<SerializationDynamic>();
        return serialization;
    }

    struct AggregateFunctionJSONMergePatchData
    {
        Object merged;
        bool initialized = false;
    };

    class AggregateFunctionJSONMergePatch final
        : public IAggregateFunctionDataHelper<AggregateFunctionJSONMergePatchData, AggregateFunctionJSONMergePatch>
    {
    private:
        DataTypePtr input_type;

    public:
        explicit AggregateFunctionJSONMergePatch(const DataTypePtr & input_type_)
            : IAggregateFunctionDataHelper<AggregateFunctionJSONMergePatchData, AggregateFunctionJSONMergePatch>(
                {input_type_}, {}, std::make_shared<DataTypeObject>(DataTypeObject::SchemaFormat::JSON))
            , input_type(input_type_)
        {
            if (!isString(input_type) && input_type->getTypeId() != TypeIndex::Object)
                throw Exception(ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT,
                               "Argument for aggregate function {} must be String or JSON type", getName());
        }

        String getName() const override { return "groupJSONMergePatch"; }

        void add(AggregateDataPtr __restrict place, const IColumn ** columns, size_t row_num, Arena *) const override
        {
            auto & data = this->data(place);

            if (input_type->getTypeId() == TypeIndex::Object)
            {
                const auto & column_object = assert_cast<const ColumnObject &>(*columns[0]);
                const auto & typed_paths = column_object.getTypedPaths();
                const auto & dynamic_paths_ptrs = column_object.getDynamicPathsPtrs();
                const auto & shared_offsets = column_object.getSharedDataOffsets();
                const auto [shared_paths, shared_values] = column_object.getSharedDataPathsAndValues();
                auto & shared_serialization = getDynamicSerializationForJSONMergePatch();

                if (!data.initialized)
                {
                    auto & dest = data.merged;
                    dest.clear();
                    Field value;
                    for (const auto & [path, typed_column] : typed_paths)
                    {
                        typed_column->get(row_num, value);
                        auto [it, inserted] = dest.emplace(std::piecewise_construct, std::forward_as_tuple(path), std::forward_as_tuple());
                        it->second = std::move(value);
                    }
                    for (const auto & [path, dynamic_column] : dynamic_paths_ptrs)
                    {
                        dynamic_column->get(row_num, value);
                        auto [it, inserted] = dest.emplace(std::piecewise_construct, std::forward_as_tuple(path), std::forward_as_tuple());
                        it->second = std::move(value);
                    }
                    if (row_num < shared_offsets.size())
                    {
                        size_t start = row_num == 0 ? 0 : shared_offsets[row_num - 1];
                        size_t end = shared_offsets[row_num];
                        for (size_t i = start; i < end; ++i)
                        {
                            const auto path_ref = shared_paths->getDataAt(i);
                            auto value_data = shared_values->getDataAt(i);
                            ReadBufferFromMemory buf(value_data.data, value_data.size);
                            shared_serialization->deserializeBinary(value, buf, getFormatSettingsForJSONMergePatch());
                            auto [it, inserted] = dest.emplace(std::piecewise_construct, std::forward_as_tuple(path_ref.data, path_ref.size), std::forward_as_tuple());
                            it->second = std::move(value);
                        }
                    }
                    data.initialized = true;
                }
                else
                {
                    Field value;
                    auto & dest = data.merged;
                    for (const auto & [path, typed_column] : typed_paths)
                    {
                        typed_column->get(row_num, value);
                        JSONMergePatchHelpers::applyPatchEntry(dest, path, std::move(value));
                    }
                    for (const auto & [path, dynamic_column] : dynamic_paths_ptrs)
                    {
                        dynamic_column->get(row_num, value);
                        JSONMergePatchHelpers::applyPatchEntry(dest, path, std::move(value));
                    }
                    if (row_num < shared_offsets.size())
                    {
                        size_t start = row_num == 0 ? 0 : shared_offsets[row_num - 1];
                        size_t end = shared_offsets[row_num];
                        for (size_t i = start; i < end; ++i)
                        {
                            const auto path_ref = shared_paths->getDataAt(i);
                            auto value_data = shared_values->getDataAt(i);
                            ReadBufferFromMemory buf(value_data.data, value_data.size);
                            shared_serialization->deserializeBinary(value, buf, getFormatSettingsForJSONMergePatch());
                            JSONMergePatchHelpers::applyPatchEntry(dest, std::string_view(path_ref.data, path_ref.size), std::move(value));
                        }
                    }
                }
                return;
            }

            /// Extract Object from String input using RapidJSON
            Object current = JSONMergePatchHelpers::extractObject(ColumnWithTypeAndName{columns[0]->getPtr(), input_type, ""}, row_num);

            if (!data.initialized)
            {
                data.merged = std::move(current);
                data.initialized = true;
            }
            else
            {
                JSONMergePatchHelpers::mergeObjects(data.merged, current);
            }
        }

        void merge(AggregateDataPtr __restrict place, ConstAggregateDataPtr rhs, Arena *) const override
        {
            auto & data = this->data(place);
            const auto & rhs_data = this->data(rhs);

            if (!rhs_data.initialized)
                return;

            if (!data.initialized)
            {
                data.merged = rhs_data.merged;
                data.initialized = true;
            }
            else
            {
                JSONMergePatchHelpers::mergeObjects(data.merged, rhs_data.merged);
            }
        }

        void serialize(ConstAggregateDataPtr __restrict place, WriteBuffer & buf, std::optional<size_t>) const override
        {
            const auto & data = this->data(place);
            writeBinaryLittleEndian(data.initialized, buf);

            if (!data.initialized)
                return;

            writeVarUInt(data.merged.size(), buf);
            for (const auto & [key, value] : data.merged)
            {
                writeStringBinary(key, buf);
                encodeField(value, buf);
            }
        }

        void deserialize(AggregateDataPtr __restrict place, ReadBuffer & buf, std::optional<size_t>, Arena *) const override
        {
            auto & data = this->data(place);
            readBinaryLittleEndian(data.initialized, buf);

            if (!data.initialized)
                return;

            size_t size;
            readVarUInt(size, buf);
            data.merged.clear();

            for (size_t i = 0; i < size; ++i)
            {
                String key;
                readStringBinary(key, buf);
                Field value = decodeField(buf);
                data.merged.insert_or_assign(std::move(key), std::move(value));
            }
        }

        void insertResultInto(AggregateDataPtr __restrict place, IColumn & to, Arena *) const override
        {
            const auto & data = this->data(place);

            /// Always insert to ColumnObject (JSON type)
            auto & col_object = assert_cast<ColumnObject &>(to);
            col_object.insert(Field(data.merged));
        }

        bool allocatesMemoryInArena() const override { return false; }
    };

    AggregateFunctionPtr createAggregateFunctionJSONMergePatch(
        const std::string & name, const DataTypes & argument_types, const Array &, const Settings *)
    {
        assertUnary(name, argument_types);
        return std::make_shared<AggregateFunctionJSONMergePatch>(argument_types[0]);
    }
}

void registerAggregateFunctionJSONMergePatch(AggregateFunctionFactory & factory)
{
    factory.registerFunction("groupJSONMergePatch", createAggregateFunctionJSONMergePatch);
}

}

#endif
