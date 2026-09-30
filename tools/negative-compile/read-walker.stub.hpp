#pragma once
// A reader backend declared and never defined: the fixtures compile a read and never link it.

#include "oxbox/serialization/read-walker.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

struct StubReader
{
  template <typename T>
  auto Read() -> T;

  auto IsNull()                     -> bool;
  auto Subtree()                    -> int;
  auto EnterObject()                -> void;
  auto HasField(std::string_view)   -> bool;
  auto FieldNames()                 -> std::vector<std::string>;
  auto EnterField(std::string_view) -> void;
  auto LeaveField()                 -> void;
  auto LeaveObject()                -> void;
  auto EnterArray()                 -> void;
  auto HasNext()                    -> bool;
  auto EnterNext()                  -> void;
  auto LeaveNext()                  -> void;
  auto LeaveArray()                 -> void;
  auto Path()                       -> std::filesystem::path const&;
};

template <typename T>
auto ReadInto(T& out) -> void
{
  StubReader reader;
  oxbox::serialization::detail::ReadWalker<StubReader> walk{ reader };
  walk(out);
}
