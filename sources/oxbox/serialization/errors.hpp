#pragma once

// Not new types: these name the family defined in utilities/errors.hpp.

#include "oxbox/utilities/errors.hpp"

namespace oxbox::serialization
{
  using utilities::FileOpenError;
  using utilities::InvalidArgument;
  using utilities::IoError;
  using utilities::MissingField;
  using utilities::ParseError;
  using utilities::TypeMismatch;
}
