#pragma once

namespace oxbox::utilities
{
  
  template <typename... Ts>
  struct Visitor : Ts...
  {
    using Ts::operator()...;
  };
}
