#pragma once
// What a handler is handed and what it answers with. The field table, the
// method, the media type and the payload are the client half's: one request
// and one response are two directions of the same message, not two of them.

#include "oxbox/http/fetch.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace oxbox::http::detail::server_message
{
  using detail::fetch::FieldTable;
  using detail::fetch::MediaType;
  using detail::fetch::Method;
  using detail::fetch::Payload;

  // The codes this server answers with itself (RFC 9110 15).
  inline constexpr int OK                 { 200 };
  inline constexpr int BAD_REQUEST        { 400 };
  inline constexpr int NOT_FOUND          { 404 };
  inline constexpr int METHOD_NOT_ALLOWED { 405 };
  inline constexpr int CONTENT_TOO_LARGE  { 413 };
  inline constexpr int INTERNAL_ERROR     { 500 };

  struct ServerRequest {
    Method      method       { Method::get };
    // Origin-form and as it arrived, escapes and all (RFC 9112 3.2.1);
    // `path` is it up to the first '?' and `query` what followed.
    std::string target       {              };
    std::string path         {              };
    std::string query        {              };
    FieldTable  fields       {              };
    MediaType   content_type {              };
    std::string body         {              };

    // Case-insensitive, nullopt for absent, first wins (RFC 9110 5.1).
    [[nodiscard]] auto operator [] (std::string_view name) const
        -> std::optional<std::string_view>;
  };

  struct ServerResponse {
    int                    status  { OK };
    // Diagnostic only; the code's registered phrase when this is empty.
    std::string            reason  {    };
    FieldTable             fields  {    };
    // Nullopt answers with no body and no Content-* field at all.
    std::optional<Payload> payload {    };
  };

  [[nodiscard]] auto TargetPath(std::string_view target) -> std::string_view;
  [[nodiscard]] auto TargetQuery(std::string_view target) -> std::string_view;

  [[nodiscard]] auto Json(std::string body) -> ServerResponse;
  [[nodiscard]] auto Json(int status, std::string body) -> ServerResponse;
}

namespace oxbox::http
{
  using detail::server_message::BAD_REQUEST;
  using detail::server_message::CONTENT_TOO_LARGE;
  using detail::server_message::INTERNAL_ERROR;
  using detail::server_message::Json;
  using detail::server_message::METHOD_NOT_ALLOWED;
  using detail::server_message::NOT_FOUND;
  using detail::server_message::OK;
  using detail::server_message::ServerRequest;
  using detail::server_message::ServerResponse;
  using detail::server_message::TargetPath;
  using detail::server_message::TargetQuery;
}
