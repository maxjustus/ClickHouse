#include <AggregateFunctions/AggregateFunctionFactory.h>
#include <AggregateFunctions/FactoryHelpers.h>
#include <AggregateFunctions/IAggregateFunction.h>
#include <Columns/ColumnObject.h>
#include <DataTypes/DataTypeObject.h>
#include <Functions/JSONMergePatchHelpers.h>
#include <Common/FieldBinaryEncoding.h>
#include <IO/ReadHelpers.h>
#include <IO/WriteHelpers.h>
#include <Common/assert_cast.h>

#if USE_RAPIDJSON

namespace DB
{

namespace ErrorCodes
{
    extern const int ILLEGAL_TYPE_OF_ARGUMENT;
}

namespace
{
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

            /// Extract Object from input (handles both String and JSON types)
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
