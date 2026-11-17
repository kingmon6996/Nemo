#include "nemo/storage/record_deserializer.hpp"
#include "nemo/storage/record_header.hpp"
#include "nemo/storage/record_serializer.hpp"
#include "nemo/storage/tuple_layout.hpp"
#include "nemo/types/array.hpp"
#include "nemo/types/dict.hpp"
#include "nemo/types/value.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

TEST(RecordSerializationTest, RoundTripsSupportedTypesAndNulls) {
    using nemo::types::TypeID;
    std::vector<nemo::types::Value> values;
    values.emplace_back(true);
    values.emplace_back(static_cast<std::int16_t>(-12));
    values.emplace_back(static_cast<std::int32_t>(42));
    values.emplace_back(static_cast<std::int64_t>(1'234'567'890));
    values.emplace_back(1.25F);
    values.emplace_back(9.5);
    values.emplace_back(nemo::types::Decimal(12345, 2));
    values.emplace_back(std::string("c"), TypeID::Char);
    values.emplace_back(std::string("varchar"), TypeID::VarChar);
    values.emplace_back(TypeID::VarChar);
    values.emplace_back(std::string("text value"), TypeID::Text);
    values.emplace_back(std::vector<std::uint8_t>{0, 1, 255});
    values.emplace_back(nemo::types::Date(23));
    values.emplace_back(nemo::types::Time(123456));
    values.emplace_back(nemo::types::Timestamp(654321));
    values.emplace_back(nemo::types::Timestamp(987654), true);
    values.emplace_back(nemo::types::Interval(2, 3, 4));
    const auto uuid = nemo::types::UUID::from_string(
        "550e8400-e29b-41d4-a716-446655440000");
    ASSERT_TRUE(uuid);
    values.emplace_back(*uuid);
    values.emplace_back(nemo::types::Json::parse("{\"key\":1}"));
    values.emplace_back(std::string("{\"jsonb\":true}"), TypeID::JSONB);

    nemo::types::Array array(TypeID::Integer);
    array.push_back(nemo::types::Value(static_cast<std::int32_t>(7)));
    values.emplace_back(std::move(array));

    nemo::types::Dict dict;
    dict.set("key", nemo::types::Value(std::string("value")));
    values.emplace_back(std::move(dict));
    values.emplace_back(7, "seven");
    values.emplace_back(std::string("101"), TypeID::Bit);
    values.emplace_back(std::string("10101"), TypeID::VarBit);
    values.emplace_back(std::string("127.0.0.1"), TypeID::Inet);
    values.emplace_back(std::string("127.0.0.0/8"), TypeID::Cidr);
    values.emplace_back(std::string("00:11:22:33:44:55"), TypeID::MacAddr);
    values.emplace_back(std::string("[1,5]"), TypeID::Range);
    values.emplace_back(TypeID::Text);

    nemo::storage::TupleLayout layout;
    for (const auto& value : values) layout.add_column(value.type());
    const nemo::storage::Record expected(std::move(values));
    const auto encoded = nemo::storage::RecordSerializer::serialize(expected, layout);
    const auto decoded = nemo::storage::RecordDeserializer::deserialize(encoded, layout);

    ASSERT_EQ(decoded.field_count(), expected.field_count());
    const auto& decoded_dict = decoded.field(21).get<nemo::types::Dict>();
    const auto& expected_dict = expected.field(21).get<nemo::types::Dict>();
    ASSERT_NE(decoded_dict.get("key"), nullptr);
    ASSERT_NE(expected_dict.get("key"), nullptr);
    EXPECT_EQ(decoded_dict.get("key")->type(), expected_dict.get("key")->type());
    EXPECT_EQ(decoded_dict.get("key")->to_string(), expected_dict.get("key")->to_string());
    EXPECT_EQ(*decoded_dict.get("key"), *expected_dict.get("key"));
    for (std::size_t i = 0; i < expected.field_count(); ++i) {
        SCOPED_TRACE(i);
        EXPECT_EQ(decoded.field(i).type(), expected.field(i).type());
        EXPECT_EQ(decoded.field(i).to_string(), expected.field(i).to_string());
        EXPECT_EQ(decoded.field(i), expected.field(i));
    }
}

TEST(RecordSerializationTest, ReadsLegacyVariableOffsetsWithNullColumns) {
    using nemo::types::TypeID;
    nemo::storage::TupleLayout layout;
    layout.add_column(TypeID::VarChar);
    layout.add_column(TypeID::Text);

    constexpr char payload[] = "legacy";
    std::vector<std::byte> bytes(sizeof(nemo::storage::RecordHeader) + 1 + 4 + 6,
                                 std::byte{0});
    nemo::storage::RecordHeader header;
    header.size = static_cast<std::uint32_t>(bytes.size());
    header.set_has_nulls(true);
    header.set_has_varlen(true);
    std::memcpy(bytes.data(), &header, sizeof(header));
    bytes[sizeof(header)] = std::byte{1};
    std::memcpy(bytes.data() + sizeof(header) + 1 + 4, payload, 6);

    const auto decoded = nemo::storage::RecordDeserializer::deserialize(bytes, layout);
    EXPECT_TRUE(decoded.field(0).is_null());
    ASSERT_FALSE(decoded.field(1).is_null());
    EXPECT_EQ(decoded.field(1).get<std::string>(), "legacy");
}

}