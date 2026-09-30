#pragma once
// What counts as a map: a container that names both its key and its mapped type.

namespace oxbox::utilities::detail::map_like
{
  // std::set names a key_type too, hence the mapped_type requirement
  template <typename M>
  concept MapLike = requires {
    typename M::key_type;
    typename M::mapped_type;
  };
}

namespace oxbox::utilities
{
  using detail::map_like::MapLike;
}
