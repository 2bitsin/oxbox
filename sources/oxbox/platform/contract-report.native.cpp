#include "oxbox/platform/contract-report.hpp"

#include "oxbox/platform/contract.hpp"

#include <cstdio>
#include <print>

namespace oxbox::platform::detail::contract_report
{
  auto ReportContractFailure(std::string_view line) -> void
  {
    Contracts<ContractMode::STOP>::Expects(!line.empty(),
                                           "the failure line says something");
    std::println(stderr, "{}", line);
    std::fflush(stderr);
  }
}
