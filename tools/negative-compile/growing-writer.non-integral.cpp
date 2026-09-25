#include "oxbox/utilities/serdes.hpp"

void Refused()
{
  oxbox::utilities::GrowingWriter<> writer;
  writer.Put<double>(1.0);
}
