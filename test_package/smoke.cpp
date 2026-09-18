// One real path through each module, against the shipped headers and
// libraries. The http include also proves the package propagates boost and
// openssl, which fetch.hpp needs and a consumer would fail to compile without.
#include <oxbox/http/fetch.hpp>
#include <oxbox/serialization/io.hpp>
#include <oxbox/serialization/serializable.hpp>
#include <oxbox/utilities/unicode.hpp>

#include <_buildutil/reflect.hpp>

#include <cstdint>
#include <string>

using namespace oxbox;
using namespace http;
using namespace serialization;
using namespace std;
using namespace string_literals;
using namespace string_view_literals;
using namespace utilities;

namespace
{
  struct Probe
  {
    friend constexpr auto reflect_scheme(Probe*);

    int64_t addr;
    string  tag;

    auto operator==(Probe const&) const -> bool = default;
  };

  constexpr auto reflect_scheme(Probe*)
  {
    using T = Probe;
    return ::reflect::class_scheme<
      ::reflect::member_scheme<"addr", &T::addr>,
      ::reflect::member_scheme<"tag",  &T::tag>>{ };
  }
}

int main()
{
  auto star{ AsBytes(u8"*"sv) };
  Probe const probe{ static_cast<int64_t>(
    DecodeFromBytes<char32_t>(star, Encoding::UTF8).value_or(char32_t{ })), "smoke" };
  auto const round = FromJson<Probe>(ToJson(probe));
  auto const where = SplitUrl("https://example.org/v1/models");
  return round == probe && probe.addr == 42 && where.port == "443" ? 0 : 1;
}
