#include <cassert>
#include <cstring>
#include <iostream>
#include <iomanip>

#include "file_backed_buffer.hpp"
#include "hash_table.hpp"


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

void test_buffer(const char * buffer_filename)
{
  FileBackedBuffer buffer(buffer_filename);

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
      std::cout << std::setfill('0') << std::setw(2) << std::right << static_cast<int>(data.first[j]);
    }
    std::cout << std::dec << '\n';
  }

  std::cout << "free data:\n";
  for (auto iter = buffer.begin_free(); iter != buffer.end_free(); ++iter) {
    const std::pair<uint8_t *, size_t> data = *iter;
    std::cout << static_cast<void *>(data.first) << ": (size " << data.second << ")\n";
  }
}

int main(const int argc, const char * argv[])
{
  if (argc >= 2) {
    assert(strcmp(argv[1], ConcurrentHashTable::BUFFER_FILENAME) != 0);
    test_buffer(argv[1]);
  }

  ConcurrentHashTable hash_table;

  std::cout << "key-value pairs in hash table:\n";
  for (auto iter = hash_table.begin(); iter != hash_table.end(); ++iter) {
    const std::pair<std::string, std::string> key_value_pair = *iter;
    std::cout << key_value_pair.first << ": " << key_value_pair.second << '\n';
  }

  hash_table.put("asdf", "asdf");
  std::cout << hash_table.get("asdf") << '\n';
  hash_table.put("asdf", "asdfasdf");
  std::cout << hash_table.get("asdf") << '\n';
  hash_table.put("asdf", "as");
  std::cout << hash_table.get("asdf") << '\n';

  return 0;
}
