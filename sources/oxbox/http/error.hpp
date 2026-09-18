#pragma once
// The module's one exception type: a transfer that never produced a complete
// response. A response is not an error -- 404 and 500 are delivered normally.

#include <stdexcept>

namespace oxbox::http::detail::error
{
  struct TransportError: std::runtime_error {
    using std::runtime_error::runtime_error;
  };
}

namespace oxbox::http
{
  using detail::error::TransportError;
}
