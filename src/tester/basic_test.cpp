#include <cassert>
#include <cstring>
#include <thread>
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
  FileBackedBuffer buffer(buffer_filename, 67108864);

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
  memfill(alloc4, 128, 0XBA5EBA11);

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

  buffer.print_stats();
}

void test_hash_table()
{
  ConcurrentHashTable * hash_table = new ConcurrentHashTable();

  constexpr size_t NUM_THREADS = 8;
  std::vector<std::thread> threads; threads.reserve(NUM_THREADS);
  for (size_t i = 0; i < NUM_THREADS; ++i) {
    threads.emplace_back([hash_table, i]() -> void {
      const std::string thread_name = "thread" + std::to_string(i);
      for (int j = 0; j < 100; ++j) {
        const std::string key = "key" + std::to_string(j);
        const std::string existing = hash_table->get(key);
        if (existing.empty()) {
          hash_table->put(key, thread_name);
        } else {
          hash_table->put(key, existing + " + " + thread_name);
        }
      }
    });
  }
  for (auto iter = threads.begin(); iter != threads.end(); ++iter) {
    iter->join();
  }

  std::cout << "key-value pairs in hash table:\n";
  for (auto iter = hash_table->begin(); iter != hash_table->end(); ++iter) {
    const std::pair<std::string, std::string> key_value_pair = *iter;
    std::cout << key_value_pair.first << ": " << key_value_pair.second << '\n';
  }
  std::cout << "end key-value pairs\n\n";

  hash_table->print_stats();

  // purposely leak hash_table to simulate process crash
}

int main(const int argc, const char * argv[])
{
  if (argc >= 2) {
    assert(strcmp(argv[1], ConcurrentHashTable::BUFFER_FILENAME) != 0);
    test_buffer(argv[1]);
  } else {
    test_hash_table();
  }

  return 0;
}
