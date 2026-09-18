#include "oxbox/http/delivery.hpp"

#include "oxbox/http/error.hpp"
#include "oxbox/utilities/span.hpp"
#include "oxbox/utilities/unit.test/octets.hpp"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// The seam's own contract, asked directly rather than through a socket; what
// a transfer does with the answer is fetch's test.

using namespace oxbox;

namespace
{
  using http::CharsetName;
  using http::CharsetOf;
  using http::Delivery;
  using http::TransportError;
  using utilities::Encoding;
  using utilities::test::Octets;

  constexpr auto PEER{ "https://example.org/x" };

  auto Through(Delivery delivery, std::span<std::byte const> bytes)
      -> std::vector<std::byte>
  {
    auto const carried{ delivery.Deliver(bytes) };
    std::vector<std::byte> out{ carried.begin(), carried.end() };
    auto const tail{ delivery.Flush() };
    out.insert(out.end(), tail.begin(), tail.end());
    return out;
  }

  auto Wanting(std::optional<std::string_view> declared) -> Delivery
  {
    return Delivery::For(Encoding::UTF8, declared, PEER);
  }
}

TEST(Delivery, ABodyWithNoDeclaredCharsetIsCarriedUnchanged)
{
  auto const wire{ Octets(0x7Bu, 0x22u, 0x61u, 0x22u, 0x7Du) };
  EXPECT_EQ(Through(Wanting(std::nullopt), wire),
            std::vector<std::byte>(wire.begin(), wire.end()));
}

TEST(Delivery, TheByteCompatibleFamilyIsCarriedUnchangedIncludingItsTopHalf)
{
  // 0xE9 under any of these names is one byte in and one byte out; a
  // transcode would have made it two
  auto const wire{ Octets(0x63u, 0x61u, 0x66u, 0xE9u, 0xFFu, 0x00u, 0x80u) };
  auto const same{ std::vector<std::byte>(wire.begin(), wire.end()) };

  for (auto const name : { "utf-8", "UTF-8", "us-ascii", "iso-8859-1", "latin1" })
    EXPECT_EQ(Through(Wanting(name), wire), same) << name;
}

TEST(Delivery, ANameThisClientDoesNotRecogniseIsCarriedRatherThanRefused)
{
  auto const wire{ Octets(0x62u, 0x79u, 0x74u, 0x65u, 0x73u) };
  auto const same{ std::vector<std::byte>(wire.begin(), wire.end()) };

  for (auto const name : { "shift_jis", "windows-1252", "", "utf-9", "koi8-r" })
    EXPECT_EQ(Through(Wanting(name), wire), same) << name;
}

TEST(Delivery, TheWideFamilyIsRefusedAsNotImplemented)
{
  // the one family whose code unit is not an octet, so no run of its bytes
  // is the same text read one byte at a time
  for (auto const name : { "utf-16", "utf-16le", "utf-16be",
                           "utf-32", "utf-32le", "utf-32be",
                           "ucs-2", "iso-10646-ucs-2",
                           "ucs-4", "iso-10646-ucs-4" })
    EXPECT_THROW(Wanting(name), TransportError) << name;
}

TEST(Delivery, TheRefusalNamesTheCharsetAndWhatIsMissing)
{
  try {
    Wanting("UTF-16LE");
    FAIL() << "a wide charset was accepted";
  } catch (TransportError const& refused) {
    std::string_view const said{ refused.what() };
    EXPECT_NE(said.find("UTF-16LE"), std::string_view::npos) << said;
    EXPECT_NE(said.find("not implemented"), std::string_view::npos) << said;
    EXPECT_NE(said.find(PEER), std::string_view::npos) << said;
  }
}

TEST(Delivery, AWideREQUIREMENTIsRefusedWhateverTheServerDeclared)
{
  // nothing that arrives is already UTF-16, so every one of these would
  // otherwise take the pass-through and hand the caller octets this door
  // promised were something else
  for (auto const declared : { std::optional<std::string_view>{ },
                               std::optional<std::string_view>{ "utf-8" },
                               std::optional<std::string_view>{ "us-ascii" },
                               std::optional<std::string_view>{ "shift_jis" },
                               std::optional<std::string_view>{ "utf-16le" } })
    for (auto const wanted : { Encoding::UTF16, Encoding::UCS2, Encoding::UCS4 })
      EXPECT_THROW(Delivery::For(wanted, declared, PEER), TransportError)
          << declared.value_or("(none)");
}

TEST(Delivery, AWideRequirementIsRefusedByTheEncodingItNamesNotTheDeclaration)
{
  // with nothing declared there is no other token the message could point at
  try {
    Delivery::For(Encoding::UTF16, std::nullopt, PEER);
    FAIL() << "a wide requirement was accepted";
  } catch (TransportError const& refused) {
    std::string_view const said{ refused.what() };
    EXPECT_NE(said.find("utf-16"), std::string_view::npos) << said;
    EXPECT_NE(said.find("not implemented"), std::string_view::npos) << said;
  }
}

TEST(Delivery, TheNamesThisModulePutsOnTheWireAreNamesItAlsoReads)
{
  // a server that echoes the charset a request asked for sends back exactly
  // CharsetName's spelling, so a name this module can emit and not read is a
  // wide encoding slipping the refusal through this module's own vocabulary
  for (auto const wide : { Encoding::UTF16, Encoding::UCS2, Encoding::UCS4 })
    EXPECT_THROW(Delivery::For(Encoding::UTF8, CharsetName(wide), PEER),
                 TransportError)
        << CharsetName(wide);
}

TEST(Delivery, ACallerThatRequiredNothingIsOwedNothingWhateverWasDeclared)
{
  auto const wire{ Octets(0x68u, 0x00u, 0x69u, 0x00u) };
  auto delivery{ Delivery::For(std::nullopt, "utf-16le", PEER) };
  EXPECT_EQ(Through(std::move(delivery), wire),
            std::vector<std::byte>(wire.begin(), wire.end()));
}

TEST(Delivery, AnEmptyBodyDeliversNothingAndFlushesNothing)
{
  EXPECT_TRUE(Through(Wanting("utf-8"), { }).empty());
}

TEST(CharsetOf, EverySpellingParsesToItsEncodingAndTheOrderTheNameStates)
{
  // the suffix is not decoration: "utf-16le" says which end the code units
  // start at
  struct Row {
    std::string_view           name;
    Encoding                   encoding;
    std::optional<std::endian> order;
  };
  constexpr std::array ROWS{
      Row{ "utf-8",           Encoding::UTF8,  std::nullopt        },
      Row{ "us-ascii",        Encoding::UCS1,  std::nullopt        },
      Row{ "iso-8859-1",      Encoding::UCS1,  std::nullopt        },
      Row{ "latin1",          Encoding::UCS1,  std::nullopt        },
      // bare: the name states no order, and nullopt is that fact
      Row{ "utf-16",          Encoding::UTF16, std::nullopt        },
      Row{ "utf-16le",        Encoding::UTF16, std::endian::little },
      Row{ "utf-16be",        Encoding::UTF16, std::endian::big    },
      Row{ "utf-32",          Encoding::UCS4,  std::nullopt        },
      Row{ "utf-32le",        Encoding::UCS4,  std::endian::little },
      Row{ "utf-32be",        Encoding::UCS4,  std::endian::big    },
      Row{ "ucs-2",           Encoding::UCS2,  std::nullopt        },
      Row{ "iso-10646-ucs-2", Encoding::UCS2,  std::nullopt        },
      Row{ "ucs-4",           Encoding::UCS4,  std::nullopt        },
      Row{ "iso-10646-ucs-4", Encoding::UCS4,  std::nullopt        } };

  for (auto const& [name, encoding, order] : ROWS) {
    auto const parsed{ CharsetOf(name) };
    ASSERT_TRUE(parsed) << name;
    EXPECT_EQ(parsed->encoding, encoding) << name;
    EXPECT_EQ(parsed->order, order) << name;
  }
}

TEST(CharsetOf, TheNameIsReadCaseInsensitivelyAndPastItsSurroundingSpace)
{
  // RFC 9110 §8.3.2, and the wire really does vary
  for (auto const name : { "UTF-16BE", "  utf-16be", "Utf-16Be  " }) {
    auto const parsed{ CharsetOf(name) };
    ASSERT_TRUE(parsed) << name;
    EXPECT_EQ(parsed->encoding, Encoding::UTF16) << name;
    EXPECT_EQ(parsed->order, std::endian::big) << name;
  }
}

TEST(CharsetOf, ANameThisClientHasNoReadingOfIsNullopt)
{
  for (auto const name : { "shift_jis", "windows-1252", "", "utf-9" })
    EXPECT_FALSE(CharsetOf(name)) << name;
}

TEST(CharsetName, SpellsEveryRequirableEncodingTheWayIanaRegisteredIt)
{
  // it is what a request states in its Accept-Charset, so the spelling is
  // the wire's and not ours
  EXPECT_EQ(CharsetName(Encoding::UTF8),  "utf-8");
  EXPECT_EQ(CharsetName(Encoding::UCS1),  "us-ascii");
  EXPECT_EQ(CharsetName(Encoding::UTF16), "utf-16");
  EXPECT_EQ(CharsetName(Encoding::UCS4),  "utf-32");
  EXPECT_EQ(CharsetName(Encoding::UCS2),  "iso-10646-ucs-2");
}
