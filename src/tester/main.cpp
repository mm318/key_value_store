#include <cassert>
#include <iostream>
#include "file_backed_buffer.hpp"


void memfill(uint8_t * buffer, const size_t buffer_size, const uint32_t pattern_data)
{
  union PatternAccessor {
    uint32_t data;
    uint8_t byte[sizeof(data)];
  } pattern = {.data = pattern_data};

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
  memfill(alloc1, 16, 0xDEADBEEF);
  uint8_t * alloc2 = buffer.alloc(32);
  assert(alloc2 != nullptr);
  memfill(alloc2, 32, 0xDEADBEEF);
  uint8_t * alloc3 = buffer.alloc(64);
  assert(alloc3 != nullptr);
  memfill(alloc3, 64, 0xDEADBEEF);
  uint8_t * alloc4 = buffer.alloc(128);
  assert(alloc4 != nullptr);
  memfill(alloc4, 128, 0xB00BEEE5);

  buffer.free(alloc4);

  std::cout << "allocated data:\n";
  for (auto iter = buffer.begin_allocated(); iter != buffer.end_allocated(); ++iter) {
    const std::pair<uint8_t *, size_t> data = *iter;
    std::cout << static_cast<void *>(data.first) << ": " << std::hex;
    for (int j = 0; j < data.second; ++j) {
      std::cout << static_cast<int>(data.first[j]);
    }
    std::cout << std::dec << '\n';
  }

  std::cout << "free data:\n";
  for (auto iter = buffer.begin_free(); iter != buffer.end_free(); ++iter) {
    const std::pair<uint8_t *, size_t> data = *iter;
    std::cout << static_cast<void *>(data.first) << ": (size " << data.second << ")\n";
  }

  return 0;
}
