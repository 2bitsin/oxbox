#pragma once
// What every suite in this module needs to say something about a file. TempPath
// is a name inside a ScratchArea that nothing has created yet.

#include "oxbox/platform/mapped-file.hpp"
#include "oxbox/platform/scratch-area.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iterator>
#include <random>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace oxbox::platform::test
{
  namespace stdfs = std::filesystem;

  // the area removes whatever the name becomes, recursively, on destruction
  class TempPath
  {
  public:
    explicit TempPath(std::string_view purpose = "test")
    : _area{ purpose }, _path{ _area.File("scratch") }
    { }

    TempPath(TempPath const&)                      = delete;
    auto operator = (TempPath const&) -> TempPath& = delete;
    TempPath(TempPath&&) noexcept                  = default;
    auto operator = (TempPath&&) noexcept -> TempPath& = default;
    ~TempPath() noexcept                           = default;

    auto Path() const noexcept -> stdfs::path const& { return _path; }

    // not const: it creates the directory
    auto Directory() -> stdfs::path const&
    { stdfs::create_directories(_path); return _path; }

    operator stdfs::path const& () const noexcept { return _path; }

  private:
    ScratchArea _area;
    stdfs::path _path;
  };

  // how a leftover temporary is caught: its name is not something a test predicts
  inline auto EntryCount(stdfs::path const& directory) -> std::size_t
  {
    return static_cast<std::size_t>(
      std::ranges::distance(stdfs::directory_iterator{ directory }));
  }

  // written the ordinary way -- the thing under test must not set its own fixture
  inline auto WriteSample(stdfs::path const& path, std::string_view text) -> void
  {
    std::ofstream out{ path, std::ios::binary };
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
  }

  inline auto ReadSample(stdfs::path const& path) -> std::string
  {
    std::ifstream in{ path, std::ios::binary };
    return std::string{ std::istreambuf_iterator<char>{ in },
                        std::istreambuf_iterator<char>{ } };
  }

  inline auto ByteAt(MappedFile const& file, std::size_t index) -> char
  { return static_cast<char>(file[index]); }

  inline auto Contains(std::string_view haystack, std::string_view needle) -> bool
  { return haystack.find(needle) != std::string_view::npos; }

  // Everything after the last ": ". Tests assert on this without spelling any
  // strerror text, which is the C library's to word and moves with the locale.
  inline auto ReasonOf(std::string_view message) -> std::string_view
  {
    auto const mark{ message.rfind(": ") };
    return mark == std::string_view::npos
         ? std::string_view{ } : message.substr(mark + 2u);
  }

  // 1 EiB clears the user address-space ceiling on 4-level paging (128 TiB) and
  // 5-level alike (64 PiB), and nothing about it touches a filesystem.
  inline constexpr std::size_t UNMAPPABLE{ std::size_t{ 1u } << 60u };
}
