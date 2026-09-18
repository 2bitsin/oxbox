#include "oxbox/http/server-message.hpp"

#include "oxbox/http/asio.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace oxbox::http::detail::server_message
{
  using namespace std::string_view_literals;

  namespace
  {
    constexpr char QUERY_MARK{ '?' };
  }

  auto ServerRequest::operator [] (std::string_view name) const
      -> std::optional<std::string_view>
  {
    auto const found{ fields.find(
        boost::beast::string_view{ name.data(), name.size() }) };
    if (found == fields.end())
      return std::nullopt;
    auto const value{ found->value() };
    return std::string_view{ value.data(), value.size() };
  }

  auto TargetPath(std::string_view target) -> std::string_view
  {
    auto const mark{ target.find(QUERY_MARK) };
    return mark == std::string_view::npos ? target : target.substr(0, mark);
  }

  auto TargetQuery(std::string_view target) -> std::string_view
  {
    auto const mark{ target.find(QUERY_MARK) };
    return mark == std::string_view::npos ? ""sv : target.substr(mark + 1u);
  }

  auto Json(std::string body) -> ServerResponse
  {
    return Json(OK, std::move(body));
  }

  auto Json(int status, std::string body) -> ServerResponse
  {
    return ServerResponse{
        .status  = status,
        .payload = Payload{ std::string{ detail::fetch::JSON_MEDIA_TYPE },
                            std::move(body) } };
  }
}
