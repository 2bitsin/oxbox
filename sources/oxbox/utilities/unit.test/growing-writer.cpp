#include "oxbox/utilities/serdes.hpp"
#include "octets.hpp"

#include <gtest/gtest.h>

#include <limits>

namespace
{
  using namespace oxbox::utilities;
  using test::Octets;
  enum class Kind : U16 { VALUE = 0x5678 };
}

TEST(GrowingWriter, ScalarWidthsAndLittleEndianAreByteExact)
{
  GrowingWriter<> writer;
  writer.Put<U08>(0x12);
  writer.Put<U16>(0x3456);
  writer.Put<U32>(0x789ABCDE);
  writer.Put<U64>(0x0102030405060708);
  writer.Put<S16>(-2);
  writer.Put(Kind::VALUE);
  EXPECT_TRUE(std::ranges::equal(writer.Bytes(), Octets(
    0x12, 0x56, 0x34, 0xDE, 0xBC, 0x9A, 0x78, 8, 7, 6, 5, 4, 3, 2, 1, 0xFE, 0xFF, 0x78, 0x56)));
  EXPECT_EQ(writer.Size(), 19u);
}

TEST(GrowingWriter, BigEndianAndReaderRoundTrip)
{
  GrowingWriter<std::endian::big> writer;
  writer.Put<U32>(0x12345678);
  writer.Put(Kind::VALUE);
  writer.Text("ox");
  EXPECT_TRUE(std::ranges::equal(writer.Bytes(), Octets(0x12, 0x34, 0x56, 0x78, 0x56, 0x78, 'o', 'x')));
  BoundedReader reader{ writer.Bytes() };
  EXPECT_EQ((reader.Fetch<U32, std::endian::big>()), 0x12345678u);
  EXPECT_EQ((reader.Fetch<U16, std::endian::big>()), std::to_underlying(Kind::VALUE));
  EXPECT_EQ(reader.Text(2), "ox");
  EXPECT_TRUE(reader.Sound());
  EXPECT_EQ(reader.Remaining(), 0u);
}

TEST(GrowingWriter, ZeroAppendAndNarrowTextPreserveEveryByte)
{
  GrowingWriter<> writer;
  EXPECT_EQ(writer.Size(), 0u);
  EXPECT_TRUE(writer.Bytes().empty());
  writer.Zero(2);
  writer.Append(Octets(0x80, 0xFF));
  writer.Text(std::string_view{ "a\0b", 3 });
  writer.Zero(0);
  writer.Append({ });
  writer.Text(std::string_view{ });
  EXPECT_TRUE(std::ranges::equal(writer.Bytes(), Octets(0, 0, 0x80, 0xFF, 'a', 0, 'b')));
}

TEST(GrowingWriter, Utf16TextUsesWriterByteOrder)
{
  GrowingWriter<>                little;
  GrowingWriter<std::endian::big> big;
  little.Text(std::u16string_view{ u"\u1234\0", 2 });
  big.Text(std::u16string_view{ u"\u1234\0", 2 });
  little.Text(std::u16string_view{ });
  big.Text(std::u16string_view{ });
  EXPECT_TRUE(std::ranges::equal(little.Bytes(), Octets(0x34, 0x12, 0, 0)));
  EXPECT_TRUE(std::ranges::equal(big.Bytes(), Octets(0x12, 0x34, 0, 0)));
}

TEST(GrowingWriter, NonAliasingAppendPreservesReservedCapacity)
{
  std::vector<std::byte> storage;
  storage.reserve(32);
  auto const capacity{ storage.capacity() };
  GrowingWriter<> writer{ storage };
  writer.Append(Octets(1, 2, 3));
  writer.Append(Octets(4, 5));
  EXPECT_EQ(storage.capacity(), capacity);
  EXPECT_TRUE(std::ranges::equal(storage, Octets(1, 2, 3, 4, 5)));
}

TEST(GrowingWriter, ExternalStorageAndReleaseTransferTheWholeVector)
{
  std::vector<std::byte> storage{ std::byte{ 1 } };
  GrowingWriter<> writer{ storage };
  writer.Put<U08>(2);
  EXPECT_EQ(writer.Bytes().data(), storage.data());
  auto const released{ writer.Release() };
  EXPECT_TRUE(std::ranges::equal(released, Octets(1, 2)));
  EXPECT_TRUE(storage.empty());
  EXPECT_EQ(writer.Size(), 0u);
  writer.Zero(1);
  EXPECT_TRUE(std::ranges::equal(storage, Octets(0)));
}

TEST(GrowingWriter, OwnedCopiesAreIndependentAndSelfAppendSurvivesGrowth)
{
  GrowingWriter<> writer;
  writer.Append(Octets(1, 2, 3));
  auto copy{ writer };
  writer.Append(writer.Bytes());
  EXPECT_TRUE(std::ranges::equal(writer.Bytes(), Octets(1, 2, 3, 1, 2, 3)));
  EXPECT_TRUE(std::ranges::equal(copy.Release(), Octets(1, 2, 3)));
  EXPECT_TRUE(copy.Bytes().empty());
}

TEST(GrowingWriter, ImpossiblePaddingThrowsWithoutChangingStorage)
{
  GrowingWriter<> writer;
  writer.Put<U08>(1);
  EXPECT_THROW(writer.Zero(std::numeric_limits<std::size_t>::max()), std::length_error);
  EXPECT_TRUE(std::ranges::equal(writer.Bytes(), Octets(1)));
}

static_assert([] {
  GrowingWriter<> writer;
  writer.Put<U16>(0x1234);
  writer.Put(Kind::VALUE);
  writer.Zero(1);
  writer.Append(Octets(0xFF));
  auto copy{ writer };
  auto const released{ copy.Release() };
  BoundedReader reader{ writer.Bytes() };
  return reader.Fetch<U16, std::endian::little>() == 0x1234
      && writer.Size() == 6 && copy.Bytes().empty()
      && std::ranges::equal(released, Octets(0x34, 0x12, 0x78, 0x56, 0, 0xFF));
}());

static_assert([] {
  GrowingWriter<>                little;
  GrowingWriter<std::endian::big> big;
  little.Text(u"\u1234");
  big.Text(u"\u1234");
  little.Append(little.Bytes());
  return std::ranges::equal(little.Bytes(), Octets(0x34, 0x12, 0x34, 0x12))
      && std::ranges::equal(big.Bytes(), Octets(0x12, 0x34));
}());
