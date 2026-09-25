#include "oxbox/utilities/serdes.hpp"

enum class Kind : unsigned short { VALUE = 0x1234 };

void Accepted()
{
  oxbox::utilities::GrowingWriter<> writer;
  writer.Put<unsigned short>(0x1234);
  writer.Put(Kind::VALUE);
}
