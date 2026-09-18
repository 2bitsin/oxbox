#pragma once
// The road a failed contract takes is the host's, so this is split by tag.

#include <string_view>

namespace oxbox::platform::detail::contract_report
{
  auto ReportContractFailure(std::string_view line) -> void;
}
