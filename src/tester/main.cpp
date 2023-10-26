#include <cassert>
#include <iostream>
#include "file_backed_buffer.hpp"


void memfill(uint8_t * buffer, const size_t buffer_size)
{
  union PatternAccessor {
    uint32_t data;
    uint8_t byte[sizeof(data)];
  };

  constexpr PatternAccessor pattern = {.data = 0xDEADBEEF};

  for (int i = 0; i < buffer_size; ++i) {
    buffer[i] = pattern.byte[sizeof(pattern) - (i % sizeof(pattern)) - 1];
  }
}

int main(const int argc, const char * argv[])
{
  if (argc < 2) {
    std::cerr << "usage: " << argv[0] << " <dbfilename>\n";
    return -1;
  }

  FileBackedBuffer buffer(argv[1]);
  uint8_t * alloc1 = buffer.alloc(16);
  assert(alloc1 != nullptr);
  memfill(alloc1, 16);
  uint8_t * alloc2 = buffer.alloc(32);
  assert(alloc2 != nullptr);
  memfill(alloc2, 32);
  uint8_t * alloc3 = buffer.alloc(64);
  assert(alloc3 != nullptr);
  memfill(alloc3, 64);
  uint8_t * alloc4 = buffer.alloc(128);
  assert(alloc4 != nullptr);
  memfill(alloc4, 128);

  buffer.free(alloc4);

  return 0;
}
