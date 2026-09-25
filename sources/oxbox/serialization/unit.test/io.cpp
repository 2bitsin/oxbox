// NOLINTBEGIN(misc-non-private-member-variables-in-classes): test scaffolding -- public members
// are the project's own allowance in tests
#include "oxbox/platform/scratch-area.hpp"
#include "oxbox/serialization/io.hpp"
#include "oxbox/serialization/serializable.hpp"

#include <_buildutil/reflect.hpp>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{

struct Probe {
  friend constexpr auto reflect_scheme(Probe*);

  std::string   name;
  std::int32_t  count{};
  bool          on{};

  auto operator==(Probe const&) const -> bool = default;
};

constexpr auto reflect_scheme(Probe*)
{
  using T = Probe;
  return ::reflect::class_scheme<
    ::reflect::member_scheme<"name",  &T::name>,
    ::reflect::member_scheme<"count", &T::count>,
    ::reflect::member_scheme<"on",    &T::on>>{ };
}

// Not constexpr: under MSVC's debug STL _ITERATOR_DEBUG_LEVEL gives every
// container a heap-allocated _Container_proxy, so a constexpr object holding
// a std::string is refused with C2131 however short the string is.
const auto SAMPLE = Probe{ .name = "héllo 🌍", .count = 42, .on = true };


// ── Sink / Source adapters (the streaming byte contracts) ────────────

auto Put(auto& sink, std::string_view text) -> void {
  std::span<std::byte const> data{
    std::bit_cast<std::byte const*>(text.data()), text.size() };
  while (!data.empty()) ASSERT_FALSE(sink.Write(data).empty());
}

// pull a source dry through a fixed-size window
auto Drain(auto& source, std::size_t window = 16) -> std::string {
  std::string out;
  std::vector<std::byte> storage(window);
  for (;;) {
    std::span<std::byte> space{ storage };
    auto const got = source.Read(space);
    if (got.empty()) break;
    out.append(std::bit_cast<char const*>(got.data()), got.size());
  }
  return out;
}

TEST(Adapters, StringSinkAccumulates) {
  oxbox::serialization::StringSink s;
  Put(s, "foo");
  Put(s, " bar");
  Put(s, " baz");
  EXPECT_EQ(s.Out(), "foo bar baz");
}

TEST(Adapters, StringSourceStreamsItsView) {
  oxbox::serialization::StringSource s{"some payload"};
  EXPECT_EQ(Drain(s, 5), "some payload");
}

TEST(Adapters, IstreamSourceStreamsInChunks) {
  std::istringstream in{"line one\nline two\n"};
  oxbox::serialization::IstreamSource src{in};
  EXPECT_EQ(Drain(src, 7), "line one\nline two\n");
}

TEST(Adapters, ReadAdvancesTheBufferAndEofIsIdempotent) {
  oxbox::serialization::StringSource src{"abc"};
  std::array<std::byte, 8> storage{};
  std::span<std::byte> space{ storage };
  auto const got = src.Read(space);
  EXPECT_EQ(got.data(), storage.data());        // slice aliases caller storage
  EXPECT_EQ(got.size(), 3u);
  EXPECT_EQ(space.size(), storage.size() - 3u); // advanced past the filled part
  EXPECT_TRUE(src.Read(space).empty());         // end: empty return,
  EXPECT_EQ(space.size(), storage.size() - 3u); //   unadvanced,
  EXPECT_TRUE(src.Read(space).empty());         //   idempotently
}

TEST(Adapters, OstreamSinkForwardsChunks) {
  std::ostringstream out;
  oxbox::serialization::OstreamSink sink{out};
  Put(sink, "alpha");
  Put(sink, " beta");
  EXPECT_EQ(out.str(), "alpha beta");
}


// ── Stream-form Serialize / Deserialize ──────────────────────────────
TEST(SerializeTo, OstreamRoundTripWithJson) {
  std::ostringstream out;
  oxbox::serialization::SerializeTo<oxbox::serialization::JsonFormat>(SAMPLE, out);
  EXPECT_FALSE(out.str().empty());

  std::istringstream in{out.str()};
  auto back = oxbox::serialization::DeserializeFrom<Probe>(in, oxbox::serialization::JsonFormat{});
  EXPECT_EQ(back, SAMPLE);
}

TEST(SerializeTo, OstreamRoundTripWithYaml) {
  std::ostringstream out;
  oxbox::serialization::SerializeTo<oxbox::serialization::YamlFormat>(SAMPLE, out);
  EXPECT_FALSE(out.str().empty());

  std::istringstream in{out.str()};
  auto back = oxbox::serialization::DeserializeFrom<Probe>(in, oxbox::serialization::YamlFormat{});
  EXPECT_EQ(back, SAMPLE);
}


// ── Path-form (extension-keyed) Serialize / Deserialize ──────────────
class PathDispatch : public ::testing::Test {
protected:
  oxbox::platform::ScratchArea _scratch{ "path-dispatch" }; // NOLINT(misc-non-private-member-variables-in-classes) gtest fixture, read by derived TEST_F bodies
};

TEST_F(PathDispatch, JsonExtensionRoundTrips) {
  auto const path = _scratch.File("probe.json");
  oxbox::serialization::SerializeTo(SAMPLE, path);
  auto back = oxbox::serialization::DeserializeFrom<Probe>(path);
  EXPECT_EQ(back, SAMPLE);
}

TEST_F(PathDispatch, YamlExtensionRoundTrips) {
  auto const path = _scratch.File("probe.yaml");
  oxbox::serialization::SerializeTo(SAMPLE, path);
  auto back = oxbox::serialization::DeserializeFrom<Probe>(path);
  EXPECT_EQ(back, SAMPLE);
}

TEST_F(PathDispatch, YmlExtensionAlsoRoundTrips) {
  auto const path = _scratch.File("probe.yml");
  oxbox::serialization::SerializeTo(SAMPLE, path);
  auto back = oxbox::serialization::DeserializeFrom<Probe>(path);
  EXPECT_EQ(back, SAMPLE);
}

TEST_F(PathDispatch, XmlExtensionRoundTrips) {
  auto const path = _scratch.File("probe.xml");
  oxbox::serialization::SerializeTo(SAMPLE, path);
  auto back = oxbox::serialization::DeserializeFrom<Probe>(path);
  EXPECT_EQ(back, SAMPLE);
}

TEST_F(PathDispatch, UnknownExtensionThrows) {
  auto const path = _scratch.File("probe.unknown");
  EXPECT_THROW(
    oxbox::serialization::SerializeTo(SAMPLE, path),
    oxbox::serialization::ParseError);
}

TEST_F(PathDispatch, MissingFileThrowsFileOpenError) {
  auto const path = _scratch.File("definitely-not-here.json");
  EXPECT_THROW(
    (oxbox::serialization::DeserializeFrom<Probe>(path)),
    oxbox::serialization::FileOpenError);
}

// 
TEST(SerializeTo, MutableObjectUsesNonConstOstreamOverload) {
  Probe p = SAMPLE;
  std::ostringstream out;
  oxbox::serialization::SerializeTo<oxbox::serialization::JsonFormat>(p, out);
  EXPECT_FALSE(out.str().empty());
}

TEST_F(PathDispatch, MutableObjectUsesNonConstPathOverloads) {
  Probe p = SAMPLE;
  auto const explicit_path = _scratch.File("mutable-explicit.json");
  oxbox::serialization::SerializeTo<oxbox::serialization::JsonFormat>(p, explicit_path);  // explicit format, T&
  EXPECT_EQ(oxbox::serialization::DeserializeFrom<Probe>(explicit_path), p);

  auto const ext_path = _scratch.File("mutable-ext.json");
  oxbox::serialization::SerializeTo(p, ext_path);                                  // extension-dispatched, T&
  EXPECT_EQ(oxbox::serialization::DeserializeFrom<Probe>(ext_path), p);
}

TEST_F(PathDispatch, WriteToUnopenablePathThrowsFileOpenError) {
  auto const path = _scratch.File("no-such-subdir") / "probe.json";  // parent dir absent -> open fails
  EXPECT_THROW(oxbox::serialization::SerializeTo(SAMPLE, path), oxbox::serialization::FileOpenError);
}


// ── TokenStreamSource: the streaming byte contract ───────────────────

TEST(TokenStream, DefaultTraitsJoinWithTheFirstSeparator) {
  std::vector<std::string> const args{ "a", "b", "c" };
  oxbox::serialization::TokenStreamSource src{ args };
  EXPECT_EQ(Drain(src), "a b c");
}

TEST(TokenStream, EmptySeparatorSetConcatenatesRaw) {
  std::vector<std::string> const args{ "{\"a\":", "1}" };
  oxbox::serialization::TokenStreamSource src{ args,
    oxbox::serialization::TokenTraits<char>{ .separator = {} } };
  EXPECT_EQ(Drain(src), "{\"a\":1}");   // no separators, no quoting, no escaping
}

TEST(TokenStream, AnySeparatorInTheSetForcesQuotesTheFirstOneJoins) {
  std::vector<std::string> const args{ "k;v", "a,b", "plain" };
  oxbox::serialization::TokenStreamSource src{ args,
    oxbox::serialization::TokenTraits<char>{ .separator = ";," } };
  EXPECT_EQ(Drain(src), "\"k;v\";\"a,b\";plain");
}

TEST(TokenStream, ReadAdvancesTheBufferAndReturnsTheFilledSlice) {
  std::vector<std::string> const args{ "abc" };
  oxbox::serialization::TokenStreamSource src{ args };
  std::array<std::byte, 64> storage{};
  std::span<std::byte> space{ storage };
  auto const got = src.Read(space);
  EXPECT_EQ(got.data(), storage.data());        // slice aliases the caller's storage
  EXPECT_EQ(got.size(), 3u);
  EXPECT_EQ(space.size(), storage.size() - 3u); // advanced past the filled region
  auto const end = src.Read(space);             // end of stream:
  EXPECT_TRUE(end.empty());                     //   empty return,
  EXPECT_EQ(space.size(), storage.size() - 3u); //   buffer not advanced,
  EXPECT_TRUE(src.Read(space).empty());         //   idempotently
}

TEST(TokenStream, OneByteWindowReassemblesTheStream) {
  std::vector<std::string> const args{ "--name", "ada l" };
  oxbox::serialization::TokenStreamSource src{ args };
  EXPECT_EQ(Drain(src, 1), "--name \"ada l\"");
}

TEST(TokenStream, ElementsWithSeparatorsQuoteWholeOthersRideBare) {
  std::vector<std::string> const args{ "-v", "a b", "", "\tx" };
  oxbox::serialization::TokenStreamSource src{ args };
  EXPECT_EQ(Drain(src), "-v \"a b\" \"\" \"\tx\"");
}

TEST(TokenStream, QuoteAndEscapeCharactersAreEscapedInPlace) {
  std::vector<std::string> const args{ R"(say "hi")", R"(C:\tmp)", R"({"a":1})" };
  oxbox::serialization::TokenStreamSource src{ args };
  EXPECT_EQ(Drain(src), R"("say \"hi\"" C:\\tmp {\"a\":1})");
}

TEST(TokenStream, EmptyElementsVanishWithoutSeparators) {
  std::vector<std::string> const args{ "a", "", "b" };
  oxbox::serialization::TokenStreamSource src{ args,
    oxbox::serialization::TokenTraits<char>{ .separator = {} } };
  EXPECT_EQ(Drain(src), "ab");
}

// all five character types the concepts admit, each with a non-BMP
// code point (a surrogate pair where the width forces one)
TEST(TokenStream, AllFiveCharacterTypesArriveAsUtf8) {
  std::vector<std::string> const ascii{ "plain", "ascii" };
  oxbox::serialization::TokenStreamSource src_c{ ascii };
  EXPECT_EQ(Drain(src_c), "plain ascii");

  std::vector<std::u8string> const utf8{ u8"héllo", u8"🌍" };
  oxbox::serialization::TokenStreamSource src8{ utf8 };
  EXPECT_EQ(Drain(src8), "héllo 🌍");

  std::vector<std::wstring> const wide{ L"grüß", L"🌍" };
  oxbox::serialization::TokenStreamSource srcw{ wide };
  EXPECT_EQ(Drain(srcw), "grüß 🌍");

  std::vector<std::u16string> const utf16{ u"héllo", u"🌍" };
  oxbox::serialization::TokenStreamSource src16{ utf16 };
  EXPECT_EQ(Drain(src16), "héllo 🌍");

  std::vector<std::u32string> const ucs4{ U"héllo", U"🌍" };
  oxbox::serialization::TokenStreamSource src32{ ucs4 };
  EXPECT_EQ(Drain(src32), "héllo 🌍");
}

// quoting and escaping run per UNIT of C, whatever the width
TEST(TokenStream, QuotingWorksInEveryCharacterWidth) {
  std::vector<std::u8string> const u8s{ u8"--name", u8"ada l" };
  oxbox::serialization::TokenStreamSource s8{ u8s, oxbox::serialization::TokenTraits<char8_t>{} };
  EXPECT_EQ(Drain(s8), "--name \"ada l\"");

  std::vector<std::wstring> const ws{ L"--name", L"ada l" };
  oxbox::serialization::TokenStreamSource sw{ ws, oxbox::serialization::TokenTraits<wchar_t>{} };
  EXPECT_EQ(Drain(sw), "--name \"ada l\"");

  std::vector<std::u16string> const u16s{ u"say \"hi\"", u"C:\\tmp" };
  oxbox::serialization::TokenStreamSource s16{ u16s, oxbox::serialization::TokenTraits<char16_t>{} };
  EXPECT_EQ(Drain(s16), R"("say \"hi\"" C:\\tmp)");

  std::vector<std::u32string> const u32s{ U"say \"hi\"", U"C:\\tmp" };
  oxbox::serialization::TokenStreamSource s32{ u32s, oxbox::serialization::TokenTraits<char32_t>{} };
  EXPECT_EQ(Drain(s32), R"("say \"hi\"" C:\\tmp)");
}

TEST(TokenStream, TruncatedUtf8TailBecomesReplacementAtTheBoundary) {
  std::vector<std::string> const args{ "a\xC3" };  // lone UTF-8 lead byte
  oxbox::serialization::TokenStreamSource src{ args };
  EXPECT_EQ(Drain(src), "a\xEF\xBF\xBD");
}

TEST(TokenStream, PrvalueElementsAreAdoptedNotCopied) {
  auto spelled = std::views::iota(1, 4)
    | std::views::transform([](int i) { return std::to_string(i * 10); });
  oxbox::serialization::TokenStreamSource src{ spelled };
  EXPECT_EQ(Drain(src), "10 20 30");
}

TEST(TokenStream, AnEndlessSequenceStreamsWithoutMaterializing) {
  auto naturals = std::views::iota(1)
    | std::views::transform([](int i) { return std::to_string(i); });
  oxbox::serialization::TokenStreamSource src{ naturals };
  std::array<std::byte, 11> storage{};
  std::span<std::byte> space{ storage };
  auto const got = src.Read(space);   // returns without running to the "end"
  EXPECT_EQ((std::string_view{ std::bit_cast<char const*>(got.data()), got.size() }),
            "1 2 3 4 5 6");
}

}  // namespace
// NOLINTEND(misc-non-private-member-variables-in-classes)
