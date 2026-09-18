// NOLINTBEGIN(misc-non-private-member-variables-in-classes): test scaffolding -- public members
// are the project's own allowance in tests
#include "oxbox/serialization/errors.hpp"
#include "oxbox/serialization/format-binary.hpp"
#include "oxbox/serialization/io.hpp"
#include "oxbox/utilities/span.hpp"

#include <_buildutil/reflect.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
  using oxbox::serialization::BinaryFormat;
  using oxbox::serialization::Deserialize;
  using oxbox::serialization::Serialize;

#include "format-point.inc"

  struct Poly
  {
    friend constexpr auto reflect_scheme(Poly*);
    std::string        name;
    std::vector<Point> points;
    auto operator==(Poly const&) const -> bool = default;
  };

  struct Scalars
  {
    friend constexpr auto reflect_scheme(Scalars*);
    bool          flag{};
    std::uint64_t big{};
    double        ratio{};
    std::int64_t  n{};
    std::string   text;
    auto operator==(Scalars const&) const -> bool = default;
  };

  struct Maybe
  {
    friend constexpr auto reflect_scheme(Maybe*);
    std::optional<std::int64_t> value;
    auto operator==(Maybe const&) const -> bool = default;
  };

  struct JustA { friend constexpr auto reflect_scheme(JustA*); std::int64_t a{}; };
  struct AAndB { friend constexpr auto reflect_scheme(AAndB*); std::int64_t a{}; std::int64_t b{}; };
  struct StrV  { friend constexpr auto reflect_scheme(StrV*);  std::string  v; };
  struct IntV  { friend constexpr auto reflect_scheme(IntV*);  std::int64_t v{}; };

  enum class Color : std::uint8_t { RED, GREEN = 7, BLUE = 200 };
  struct Tagged
  {
    friend constexpr auto reflect_scheme(Tagged*);
    Color        hue{};
    std::int64_t n{};
    auto operator==(Tagged const&) const -> bool = default;
  };

  constexpr auto reflect_scheme(Poly*)
  {
    using T = Poly;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"name",   &T::name>,
      ::reflect::member_scheme<"points", &T::points>>{ };
  }

  constexpr auto reflect_scheme(Scalars*)
  {
    using T = Scalars;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"flag",  &T::flag>,
      ::reflect::member_scheme<"big",   &T::big>,
      ::reflect::member_scheme<"ratio", &T::ratio>,
      ::reflect::member_scheme<"n",     &T::n>,
      ::reflect::member_scheme<"text",  &T::text>>{ };
  }

  constexpr auto reflect_scheme(Maybe*)
  {
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"value", &Maybe::value>>{ };
  }

  constexpr auto reflect_scheme(JustA*)
  { return ::reflect::class_scheme<::reflect::member_scheme<"a", &JustA::a>>{ }; }

  constexpr auto reflect_scheme(AAndB*)
  {
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"a", &AAndB::a>,
      ::reflect::member_scheme<"b", &AAndB::b>>{ };
  }

  constexpr auto reflect_scheme(StrV*)
  { return ::reflect::class_scheme<::reflect::member_scheme<"v", &StrV::v>>{ }; }

  constexpr auto reflect_scheme(IntV*)
  { return ::reflect::class_scheme<::reflect::member_scheme<"v", &IntV::v>>{ }; }

  constexpr auto reflect_scheme(Tagged*)
  {
    using T = Tagged;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"hue", &T::hue>,
      ::reflect::member_scheme<"n",   &T::n>>{ };
  }
}

TEST(BinaryFormat, RoundTripsScalarsAndStrings)
{
  Point const point{ -5, 42, "origin" };
  auto const bytes{ Serialize<BinaryFormat>(point) };
  EXPECT_EQ((Deserialize<BinaryFormat, Point>(bytes)), point);
}

TEST(BinaryFormat, RoundTripsNestedArrays)
{
  Poly const poly{ "tri", { { 0, 0, "a" }, { 1, 0, "b" }, { 0, 1, "c" } } };
  auto const bytes{ Serialize<BinaryFormat>(poly) };
  EXPECT_EQ((Deserialize<BinaryFormat, Poly>(bytes)), poly);
}

TEST(BinaryFormat, ProducesBinaryNotText)
{
  auto const bytes{ Serialize<BinaryFormat>(Point{ 1, 2, "x" }) };
  EXPECT_EQ(static_cast<unsigned char>(bytes.front()), 5u);
  EXPECT_GT(bytes.size(), 0u);
}

TEST(BinaryFormat, RoundTripsAllScalarTypes)
{
  Scalars const s{ true, 0xDEADBEEFCAFEull, 3.14159, -7, "hi" };
  EXPECT_EQ((Deserialize<BinaryFormat, Scalars>(Serialize<BinaryFormat>(s))), s);
}

TEST(BinaryFormat, RoundTripsNullAndSetOptional)
{
  Maybe const empty{};
  EXPECT_EQ((Deserialize<BinaryFormat, Maybe>(Serialize<BinaryFormat>(empty))), empty);
  Maybe const set{ 42 };
  EXPECT_EQ((Deserialize<BinaryFormat, Maybe>(Serialize<BinaryFormat>(set))), set);
}

TEST(BinaryFormat, ThrowsOnTypeMismatch)
{
  auto const bytes{ Serialize<BinaryFormat>(StrV{ "nope" }) };
  EXPECT_THROW((Deserialize<BinaryFormat, IntV>(bytes)), oxbox::serialization::TypeMismatch);
}

TEST(BinaryFormat, ThrowsOnMissingField)
{
  auto const bytes{ Serialize<BinaryFormat>(JustA{ 1 }) };
  EXPECT_THROW((Deserialize<BinaryFormat, AAndB>(bytes)), oxbox::serialization::MissingField);
}

TEST(BinaryFormat, RoundTripsEnumField)
{
  Tagged const t{ Color::BLUE, -3 };
  EXPECT_EQ((Deserialize<BinaryFormat, Tagged>(Serialize<BinaryFormat>(t))), t);
}

TEST(BinaryFormat, RoundTripsLongStringWithMultiByteLength)
{
  Point const p{ 1, 2, std::string(1000u, 'z') };
  EXPECT_EQ((Deserialize<BinaryFormat, Point>(Serialize<BinaryFormat>(p))), p);
}


TEST(BinaryFormat, VarIntRoundTripsBoundaryWidths)
{
  for (std::uint64_t const value :
       { 0ull, 1ull, 127ull, 128ull, 255ull, 16383ull, 16384ull, 2097151ull,
         2097152ull, 0xFFFFFFFFull, 0x100000000ull, 0x7FFFFFFFFFFFFFFFull,
         0xFFFFFFFFFFFFFFFFull }) {
    BinaryFormat::Blob buf;
    BinaryFormat::EncodeVarInt(buf, value);
    std::size_t pos{ 0u };
    EXPECT_EQ(BinaryFormat::DecodeVarInt(buf, pos), value);
  }
}

TEST(BinaryFormat, VarIntUsesExpectedByteCounts)
{
  auto const size{ [](std::uint64_t v) {
    BinaryFormat::Blob b; BinaryFormat::EncodeVarInt(b, v); return b.size(); } };
  EXPECT_EQ(size(0u), 1u);                                     // 7 bits
  EXPECT_EQ(size(127u), 1u);
  EXPECT_EQ(size(128u), 2u);                                   // 14 bits
  EXPECT_EQ(size(16383u), 2u);
  EXPECT_EQ(size(16384u), 3u);                                 // 21 bits
  EXPECT_EQ(size(std::numeric_limits<std::uint64_t>::max()), 10u);
}

TEST(BinaryFormat, VarIntContinuationBitSetOnAllButLast)
{
  BinaryFormat::Blob buf;
  BinaryFormat::EncodeVarInt(buf, 300u);                       // spans two bytes
  ASSERT_EQ(buf.size(), 2u);
  EXPECT_NE(static_cast<unsigned char>(buf[0]) & 0x80u, 0u);   // continues
  EXPECT_EQ(static_cast<unsigned char>(buf[1]) & 0x80u, 0u);   // last byte
}

TEST(BinaryFormat, VarIntDecodesSequentialValuesAdvancingPos)
{
  BinaryFormat::Blob buf;
  BinaryFormat::EncodeVarInt(buf, 1u);
  BinaryFormat::EncodeVarInt(buf, 1000u);
  BinaryFormat::EncodeVarInt(buf, 70000u);
  std::size_t pos{ 0u };
  EXPECT_EQ(BinaryFormat::DecodeVarInt(buf, pos), 1u);
  EXPECT_EQ(BinaryFormat::DecodeVarInt(buf, pos), 1000u);
  EXPECT_EQ(BinaryFormat::DecodeVarInt(buf, pos), 70000u);
  EXPECT_EQ(pos, buf.size());
}


namespace
{
  using oxbox::serialization::ParseError;
  using oxbox::serialization::StringSource;
  using Reader = BinaryFormat::Reader<StringSource>;

  auto Wire(std::initializer_list<unsigned> const bytes) -> std::string
  {
    std::string out;
    std::ranges::transform(bytes, std::back_inserter(out),
                           [](unsigned b) { return static_cast<char>(b); });
    return out;
  }

  // `{ "v": 1 }` has an object payload of a name string node followed by an integer value node.
  auto ValidObject() -> std::string
  {
    return Wire({ BinaryFormat::TAG_OBJECT, BinaryFormat::TAG_INT, 6u,
                  BinaryFormat::TAG_STRING, BinaryFormat::TAG_INT, 1u, 'v',
                  BinaryFormat::TAG_INT, 1u });
  }

  constexpr std::size_t OBJECT_LENGTH_AT{ 2u };   // the object's content length
  constexpr std::size_t NAME_LENGTH_AT{ 5u };     // the name string's length
  constexpr std::size_t VALUE_TAG_AT{ 7u };       // the value node's tag byte
  constexpr unsigned    BEYOND_BLOB{ 0x40u };     // a length no test blob reaches
  constexpr unsigned    CONTINUES{ 0x80u };       // varint continuation bit
}

TEST(BinaryFormatMalformed, HandBuiltReferenceObjectIsValid)
{
  EXPECT_EQ((Deserialize<BinaryFormat, IntV>(ValidObject()).v), 1);
}

TEST(BinaryFormatMalformed, EmptyBlobThrowsParseError)
{
  EXPECT_THROW((Deserialize<BinaryFormat, IntV>(std::string_view{})), ParseError);
}

TEST(BinaryFormatMalformed, ObjectLengthBeyondBlobThrowsParseError)
{
  auto wire{ ValidObject() };
  wire[OBJECT_LENGTH_AT] = static_cast<char>(BEYOND_BLOB);
  EXPECT_THROW((Deserialize<BinaryFormat, IntV>(wire)), ParseError);
}

TEST(BinaryFormatMalformed, StringLengthBeyondBlobThrowsParseError)
{
  auto wire{ ValidObject() };
  wire[NAME_LENGTH_AT] = static_cast<char>(BEYOND_BLOB);
  EXPECT_THROW((Deserialize<BinaryFormat, IntV>(wire)), ParseError);
}

TEST(BinaryFormatMalformed, UnknownTagMidStreamThrowsParseError)
{
  auto wire{ ValidObject() };
  wire[VALUE_TAG_AT] = static_cast<char>(0x63u);
  EXPECT_THROW((Deserialize<BinaryFormat, IntV>(wire)), ParseError);
}

TEST(BinaryFormatMalformed, TruncatedVarIntThrowsParseError)
{
  auto const wire{ Wire({ BinaryFormat::TAG_OBJECT, BinaryFormat::TAG_INT, CONTINUES }) };
  EXPECT_THROW((Deserialize<BinaryFormat, IntV>(wire)), ParseError);
}

TEST(BinaryFormatMalformed, UnterminatedVarIntThrowsParseError)
{
  // Ten continuation bytes exhaust 64 bits without terminating; stopping silently would accept a truncated value.
  auto const wire{ Wire({ BinaryFormat::TAG_INT,
                          CONTINUES, CONTINUES, CONTINUES, CONTINUES, CONTINUES,
                          CONTINUES, CONTINUES, CONTINUES, CONTINUES, CONTINUES,
                          1u }) };
  StringSource src{ wire };
  Reader reader{ src };
  EXPECT_THROW(reader.Read<std::int64_t>(), ParseError);
}

TEST(BinaryFormatMalformed, ArrayItemEscapingItsContainerIsRejectedBeforeItIsRead)
{
  // The eight-byte string fits the blob but not the array's three-byte payload: this is containment, not a blob bounds violation.
  auto const wire{ Wire({ BinaryFormat::TAG_ARRAY, BinaryFormat::TAG_INT, 3u,
                          BinaryFormat::TAG_STRING, BinaryFormat::TAG_INT, 5u,
                          'a', 'b', 'c', 'd', 'e' }) };
  StringSource src{ wire };
  Reader reader{ src };
  reader.EnterArray();
  ASSERT_TRUE(reader.HasNext());
  EXPECT_THROW(reader.EnterNext(), ParseError);
}

TEST(BinaryFormatMalformed, ChildNodeEscapingItsContainerThrowsParseError)
{
  auto const wire{ Wire({ BinaryFormat::TAG_OBJECT, BinaryFormat::TAG_INT, 4u,
                          BinaryFormat::TAG_STRING, BinaryFormat::TAG_INT, 3u,
                          'v', 'x', 'y', BinaryFormat::TAG_INT, 1u }) };
  EXPECT_THROW((Deserialize<BinaryFormat, IntV>(wire)), ParseError);
}

TEST(BinaryFormatMalformed, EveryTruncationOfAValidBlobIsRejected)
{
  auto const wire{ Serialize<BinaryFormat>(Poly{ "tri", { { 0, 0, "a" }, { 1, 2, "b" } } }) };
  for (std::size_t cut{ 0u }; cut < wire.size(); ++cut) {
    EXPECT_THROW((Deserialize<BinaryFormat, Poly>(std::string_view{ wire }.substr(0u, cut))),
                 ParseError) << "truncated to " << cut << " of " << wire.size() << " bytes";
  }
}

TEST(BinaryFormatMalformed, EverySingleByteCorruptionFailsAsASerializationError)
{
  // Corruption must parse or raise a serialization error, never read outside the blob or allocate against a bogus length.
  auto const wire{ Serialize<BinaryFormat>(Point{ -5, 42, "origin" }) };
  for (std::size_t at{ 0u }; at < wire.size(); ++at) {
    auto corrupt{ wire };
    corrupt[at] = static_cast<char>(0xFFu);
    try {
      static_cast<void>(Deserialize<BinaryFormat, Point>(corrupt));
    } catch (ParseError const&) {
    } catch (oxbox::serialization::TypeMismatch const&) {
    } catch (oxbox::serialization::MissingField const&) {
    } catch (std::exception const& e) {
      ADD_FAILURE() << "byte " << at << " corrupted: " << e.what();
    }
  }
}

TEST(BinaryFormatMalformed, ReaderRejectsTruncatedBool)
{
  auto const wire{ Wire({ BinaryFormat::TAG_BOOL }) };           // tag, no payload
  StringSource src{ wire };
  Reader reader{ src };
  EXPECT_THROW(reader.Read<bool>(), ParseError);
}

TEST(BinaryFormatMalformed, ReaderRejectsTruncatedDouble)
{
  auto const wire{ Wire({ BinaryFormat::TAG_DOUBLE, 0u, 0u, 0u }) };  // 3 of 8 bytes
  StringSource src{ wire };
  Reader reader{ src };
  EXPECT_THROW(reader.Read<double>(), ParseError);
}

TEST(BinaryFormatMalformed, ReaderRejectsStringRunningPastTheBlob)
{
  auto const wire{ Wire({ BinaryFormat::TAG_STRING, BinaryFormat::TAG_INT, BEYOND_BLOB, 'a' }) };
  StringSource src{ wire };
  Reader reader{ src };
  EXPECT_THROW(reader.Read<std::string>(), ParseError);
}

TEST(BinaryFormatMalformed, ReaderRejectsArrayRunningPastTheBlob)
{
  auto const wire{ Wire({ BinaryFormat::TAG_ARRAY, BinaryFormat::TAG_INT, BEYOND_BLOB }) };
  StringSource src{ wire };
  Reader reader{ src };
  EXPECT_THROW(reader.EnterArray(), ParseError);
}

TEST(BinaryFormatMalformed, ReaderRejectsEmptyBlobOnEveryProbe)
{
  auto const wire{ std::string{} };
  StringSource src{ wire };
  Reader reader{ src };
  EXPECT_THROW(static_cast<void>(reader.IsNull()), ParseError);
  EXPECT_THROW(static_cast<void>(reader.Subtree()), ParseError);
  EXPECT_THROW(reader.EnterObject(), ParseError);
}

TEST(BinaryFormatMalformed, ReaderRejectsALengthNodeThatIsNotAnInteger)
{
  auto const wire{ Wire({ BinaryFormat::TAG_STRING, BinaryFormat::TAG_BOOL, 1u, 'a' }) };
  StringSource src{ wire };
  Reader reader{ src };
  EXPECT_THROW(static_cast<void>(reader.Read<std::string>()), ParseError);
}

TEST(BinaryFormat, DecodeVarIntRejectsReadingPastTheBuffer)
{
  auto const wire{ Wire({ CONTINUES }) };                        // continues, then nothing
  std::size_t pos{ 0u };
  EXPECT_THROW(static_cast<void>(BinaryFormat::DecodeVarInt(oxbox::utilities::AsBytes(wire), pos)),
               ParseError);
}

TEST(BinaryFormat, VarIntWiderThanSixtyFourBitsIsRejected)
{
  // A tenth byte of 0x7F encodes 127 * 2^63 = 1171368248680556527616, beyond U64; shifting alone truncates it to 2^63.
  auto const wire{ Wire({ CONTINUES, CONTINUES, CONTINUES, CONTINUES, CONTINUES,
                          CONTINUES, CONTINUES, CONTINUES, CONTINUES, 0x7Fu }) };
  std::size_t pos{ 0u };
  EXPECT_THROW(static_cast<void>(BinaryFormat::DecodeVarInt(oxbox::utilities::AsBytes(wire), pos)),
               ParseError);
}

TEST(BinaryFormat, VarIntAcceptsTheWidestTenthByteThatFits)
{
  // 0x01 in the tenth byte is exactly U64's top bit, so the widest legal varint must still decode.
  auto const wire{ Wire({ 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,
                          0xFFu, 0xFFu, 0xFFu, 0xFFu, 0x01u }) };
  std::size_t pos{ 0u };
  EXPECT_EQ(BinaryFormat::DecodeVarInt(oxbox::utilities::AsBytes(wire), pos),
            std::numeric_limits<std::uint64_t>::max());
  EXPECT_EQ(pos, wire.size());
}

TEST(BinaryFormat, NodeLengthRejectsNodesThatLeaveTheBuffer)
{
  auto const wire{ Wire({ BinaryFormat::TAG_STRING, BinaryFormat::TAG_INT, BEYOND_BLOB }) };
  auto const bytes{ oxbox::utilities::AsBytes(wire) };
  EXPECT_THROW(static_cast<void>(BinaryFormat::NodeLength(bytes, 0u)), ParseError);
  EXPECT_THROW(static_cast<void>(BinaryFormat::NodeLength(bytes, bytes.size())), ParseError);
}
// NOLINTEND(misc-non-private-member-variables-in-classes)
