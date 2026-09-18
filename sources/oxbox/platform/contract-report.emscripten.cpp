#include "oxbox/platform/contract-report.hpp"

#include "oxbox/platform/contract.hpp"

#include <string>

#include <emscripten/console.h>

namespace oxbox::platform::detail::contract_report
{
  auto ReportContractFailure(std::string_view line) -> void
  {
    Contracts<ContractMode::STOP>::Expects(!line.empty(),
                                           "the failure line says something");
    emscripten_console_error(std::string{ line }.c_str());
  }
}
