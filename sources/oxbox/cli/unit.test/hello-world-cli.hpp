#pragma once

#include "oxbox/cli/main.hpp"

#include "oxbox/cli/unit.test/print-version-cli.hpp"

#include <cstdint>
#include <numbers>
#include <string_view>
#include <string>
#include <unordered_map>
#include <vector>
#include <list>

namespace oxbox::cli::detail::hello_world_cli
{

  using namespace std;
  using namespace std::string_literals;
  using namespace std::string_view_literals;

  // the name is lowercased and underscores become dashes: OPTION_DOS is option-dos
  enum class EnumerationVariable: int16_t
  {
    NONE,
    OPTION_UNO  = 1, /* Description of OPTION_UNO */
    OPTION_DOS,      /* Description of OPTION_DOS */
    OPTION_TRES = 3, /* Description of OPTION_TRES */

  };

  constexpr auto reflect_scheme(EnumerationVariable*);

  struct HelloWorld: Command
  {
    friend constexpr auto reflect_scheme(HelloWorld*);

    using enum EnumerationVariable;

    // A member registers as --member-variable-name, needs a default or an
    // empty state, and takes its help text from the comment beside it.

    using S2SMapping = unordered_map<string, string>;

    bool                default_negative_flag{ false                  }; /* This flag defaults to false if the flag is not specified */             
    bool                default_positive_flag{ true                   }; /* This flag defaults to true  if the flag is not specified */             
    string              optional_string_value{ "i didn't change this" }; /* This is an optional string with a default value. */
    vector<string>      variable_string_array{                        }; // Optional variable string array: repeat the option, or separate values with commas
    list<string>        variable_string_llist{                        }; // Optional variable string list (any container that emplaces,
                                                                         // inserts, emplaces_back or pushes_back), described over more
                                                                         // than one line in the other comment style
    int32_t             optional_int32t_value{ -99999999              }; /* Optional int32_t value  */
    uint64_t            optional_uint64_value{ 0xDEADBEEF0BADC0DEu    }; /* Optional uint64_t value */
    double              optional_double_value{ numbers::pi_v<double>  }; /* Optional double value   */
    optional<string>    optional_optional_str{ nullopt                }; // Optional string: an empty value and an absent option stay distinguishable
    S2SMapping          key_to_value_mappging{                        }; // Key to value mapping, spelled --key-to-value-mappging:foo=hello,bar=world
    EnumerationVariable enumerated_option_val{ OPTION_UNO             }; // Enumerated option; with no default the first enumerator is chosen

    // Declaring an operator() at all declares this command a verb.
    auto operator() (int64_t                   value0,     /* Description for value 0 */
                     array<float, 2>           value1to2,  /* Description for combined value 1 and 2*/
                     tuple<string, int, float> value3to5,  /* Description for combined value 3, 4 and 5 */
                     RangeView<string_view>    value6toN   /* Description for the remaining parameters from 6 onward */
                    ) const -> CliResult;                  /* Have not decided what this will be but it shouldn't matter */
  } ;

  // Initialize and no operator(): the layer that dispatches rather than a verb.
  struct HelloWorldCli: Command
  {
    friend constexpr auto reflect_scheme(HelloWorldCli*);

    string        greeting_language{ "en"  }; /* which language the greeting is spoken in */
    bool          verbose_run      { false }; /* say more about what the program is doing */

    // Command::Get owns the one instance, so a member only names it
    HelloWorld&   say_hello           { Command::Get<HelloWorld>()   }; /* This is a subcommand invoked as --say-hello */
    PrintVersion& print_version_switch{ Command::Get<PrintVersion>() }; /* This is a subcommand invoked as --print-version-switch */

    // A method answering a Command is a subcommand spelled without dashes.
    auto print_version_positional() const noexcept -> PrintVersion&
    { return Command::Get<PrintVersion>(); } /* This is the same subcommand invoked as print-version-positional */

    // a reflected method answering a CliResult is itself the subcommand
    auto say_greeting(string         to_whom   /* who is being greeted */,
                      optional<int>  how_often /* say it more than once */
                     ) -> CliResult;           /* Greet somebody by name, with no command of its own */

    // public, no parameters, answering a CliResult: the whole declaration
    auto Initialize() -> CliResult;  /* brings up whatever the program shares */
  } ;
}

namespace oxbox::cli
{
  using detail::hello_world_cli::HelloWorld;
  using detail::hello_world_cli::HelloWorldCli;
}

