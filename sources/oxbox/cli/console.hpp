#pragma once
// Under a pipe stdout is fully buffered, and a Windows GUI program starts
// with no console; attaching answers false off Windows.

namespace oxbox::cli::detail::console
{
  auto SetStdoutUnbuffered() noexcept -> void;

  auto AttachParentConsole() noexcept -> bool;

  auto PrepareConsole() noexcept -> void;
}

namespace oxbox::cli
{
  using detail::console::AttachParentConsole;
  using detail::console::PrepareConsole;
  using detail::console::SetStdoutUnbuffered;
}
