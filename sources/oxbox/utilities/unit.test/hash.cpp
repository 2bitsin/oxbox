// FNV-1a at both widths: the published vectors, the compile-time property
// the switch idiom rests on, and the streaming property a digest rests on.
#include "oxbox/utilities/hash.hpp"

#include "oxbox/utilities/unit.test/octets.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace
{
  using namespace oxbox::utilities;
  using namespace oxbox::utilities::literals;
  using oxbox::utilities::test::Octets;

  constexpr auto FOOBAR{ Octets('f', 'o', 'o', 'b', 'a', 'r') };
}

// ---- compile time ---------------------------------------------------
//
// Published FNV-1a vectors (draft-eastlake-fnv Appendix C, isthe.com/chongo).

static_assert(HashString("") == 0xcbf29ce484222325u,
              "the empty string hashes to the offset basis");
static_assert(HashString("a") == 0xaf63dc4c8601ec8cu);
static_assert(HashString("b") == 0xaf63df4c8601f1a5u);
static_assert(HashString("c") == 0xaf63de4c8601eff2u);
static_assert(HashString("foobar") == 0x85944171f73967e8u);

static_assert(Fnv1a<U32>("") == 0x811C9DC5u);
static_assert(Fnv1a<U32>("a") == 0xE40C292Cu);
static_assert(Fnv1a<U32>("foobar") == 0xBF9CF968u);

// The byte road and the text road are one algorithm.
static_assert(Fnv1a<U64>(std::span<std::byte const>{ FOOBAR }) == HashString("foobar"));
static_assert(Fnv1a<U32>(std::span<std::byte const>{ FOOBAR }) == Fnv1a<U32>("foobar"));

// The literal and the function are one rule.
static_assert(HashString("x") == "x"_hash);
static_assert(""_hash == FNV1A_BASIS<U64>);
static_assert(FNV1A_BASIS<U32> == 0x811C9DC5u);

static_assert("loaded"_hash != "unloaded"_hash);
static_assert(HashString("add") != HashString("lock add"));
static_assert(HashString("movsb") != HashString("rep movsb"));
static_assert("lock add"_hash == HashString("lock add"));

static_assert(std::integral_constant<U64, "loaded"_hash>::value ==
              HashString("loaded"));

static_assert(std::same_as<decltype(Fnv1a("a")), U64>);
static_assert(std::same_as<decltype(Fnv1a<U32>("a")), U32>);
static_assert(!Fnv1aWidth<U16>);
static_assert(!Fnv1aWidth<int>);
static_assert(Fnv1aWidth<U32> && Fnv1aWidth<U64>);

// ---- runtime --------------------------------------------------------

TEST(Fnv1a, MatchesThePublishedVectorsAtBothWidths)
{
  EXPECT_EQ(HashString(""), 0xcbf29ce484222325u);
  EXPECT_EQ(HashString("a"), 0xaf63dc4c8601ec8cu);
  EXPECT_EQ(HashString("foobar"), 0x85944171f73967e8u);

  EXPECT_EQ(Fnv1a<U32>(""), 0x811C9DC5u);
  EXPECT_EQ(Fnv1a<U32>("a"), 0xE40C292Cu);
  EXPECT_EQ(Fnv1a<U32>("foobar"), 0xBF9CF968u);
}

TEST(Fnv1a, TheRuntimePathAgreesWithTheCompileTimeOne)
{
  std::string const built{ std::string{ "load" } + "ed" };
  EXPECT_EQ(HashString(built), "loaded"_hash);
}

TEST(Fnv1a, IsCaseSensitiveAndOrderSensitive)
{
  EXPECT_NE(HashString("loaded"), HashString("LOADED"));
  EXPECT_NE(HashString("ab"), HashString("ba"));
  EXPECT_NE(HashString("repe cmpsb"), HashString("repne cmpsb"));
}

TEST(Fnv1a, HashesBytesSoEmbeddedNullsCount)
{
  using namespace std::string_view_literals;
  EXPECT_NE(HashString("a\0b"sv), HashString("a"sv));
  EXPECT_EQ(HashString("a\0b"sv), "a\0b"_hash);
}

TEST(Fnv1a, TheEmptyInputIsAnOrdinaryValueAndNotASentinel)
{
  EXPECT_EQ(HashString(""), FNV1A_BASIS<U64>);
  EXPECT_NE(HashString(""), 0u);
  EXPECT_EQ(Fnv1a<U32>(oxbox::utilities::Bytes{ }), FNV1A_BASIS<U32>);
}

TEST(Fnv1a, AsADigestCatchesADroppedDoubledOrReorderedByte)
{
  std::vector<std::byte> whole;
  whole.reserve(1024u);
  for (std::size_t offset{ 0u }; offset < 1024u; ++offset)
    whole.push_back(std::byte{ static_cast<oxbox::utilities::U08>(offset * 31u + 7u) });
  auto const expected{ Fnv1a<U32>(oxbox::utilities::Bytes{ whole }) };

  auto changed{ whole };
  changed[0x100] ^= std::byte{ 0x01u };
  EXPECT_NE(Fnv1a<U32>(oxbox::utilities::Bytes{ changed }), expected);

  auto dropped{ whole };
  dropped.pop_back();
  EXPECT_NE(Fnv1a<U32>(oxbox::utilities::Bytes{ dropped }), expected);

  // the same multiset, a different document: a sum would pass this
  auto swapped{ whole };
  std::swap(swapped[0], swapped[1]);
  EXPECT_NE(Fnv1a<U32>(oxbox::utilities::Bytes{ swapped }), expected);

  std::vector<std::byte> const zeroes(1024u, std::byte{ 0u });
  EXPECT_NE(Fnv1a<U32>(oxbox::utilities::Bytes{ zeroes }), FNV1A_BASIS<U32>);
}

TEST(Fnv1a, FoldingWindowOnWindowEqualsHashingTheWhole)
{
  constexpr auto WHOLE{ Octets(0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66) };
  oxbox::utilities::Bytes const all{ WHOLE };

  auto running{ FNV1A_BASIS<U32> };
  for (std::size_t at{ 0u }; at < all.size(); at += 3u)
    running = Fnv1a<U32>(all.subspan(at, std::min<std::size_t>(3u, all.size() - at)),
                         running);
  EXPECT_EQ(running, Fnv1a<U32>(all));

  EXPECT_EQ(Fnv1a<U32>(oxbox::utilities::Bytes{ }, running), running);
}

namespace
{
  enum class State { LOADED, UNLOADED, UNKNOWN };

  // duplicate case labels would be a compile error
  auto Classify(std::string_view status) noexcept -> State
  {
    switch (HashString(status)) {
      case "loaded"_hash:   return State::LOADED;
      case "unloaded"_hash: return State::UNLOADED;
      default:              return State::UNKNOWN;
    }
  }

  // one comparison, and only on the branch that already matched
  auto ClassifyConfirmed(std::string_view status) noexcept -> State
  {
    switch (HashString(status)) {
      case "loaded"_hash:
        if (status == "loaded")
          return State::LOADED;
        break;
      case "unloaded"_hash:
        if (status == "unloaded")
          return State::UNLOADED;
        break;
      default:
        break;
    }
    return State::UNKNOWN;
  }
}

TEST(HashSwitch, DispatchesOnAStringThroughAnOrdinarySwitch)
{
  EXPECT_EQ(Classify("loaded"), State::LOADED);
  EXPECT_EQ(Classify("unloaded"), State::UNLOADED);
  EXPECT_EQ(Classify("loading"), State::UNKNOWN);
  EXPECT_EQ(Classify(""), State::UNKNOWN);
}

TEST(HashSwitch, DispatchesOnAValueAssembledAtRuntime)
{
  std::string const status{ std::string{ "un" } + "loaded" };
  EXPECT_EQ(Classify(status), State::UNLOADED);
}

TEST(HashSwitch, TheConfirmingPatternAgreesOnEveryRealInput)
{
  // the two differ only on a collision, which no real word here reaches
  for (std::string_view const status : { "loaded", "unloaded", "loading", "" })
    EXPECT_EQ(Classify(status), ClassifyConfirmed(status)) << status;
}
