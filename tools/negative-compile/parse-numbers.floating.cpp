#include "oxbox/utilities/number-text.hpp"

using Rejected = decltype(oxbox::utilities::ParseNumbers<double, 2>("1:2", ':'));
