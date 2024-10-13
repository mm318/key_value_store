#include <iostream>
#include <vector>
#include <functional>
#include <thread>
#include <chrono>
#include <numeric>
#include <random>

#include "hash_table.hpp"

using namespace std::chrono_literals;

constexpr size_t NUM_THREADS = 8;
constexpr std::chrono::seconds RUN_TIME = 20s;
constexpr char alphabet[] = "abcdefghijklmnopqrstuvwxyz";
constexpr char alphanumeric[] = "abcdefghijklmnopqrstuvwxyz0123456789";

std::string generate_random_key(std::mt19937 & generator)
{
  std::uniform_int_distribution<int> random_key_length(1, 4);
  std::uniform_int_distribution<int> random_letter(0, sizeof(alphabet) - 2);
  const int length = random_key_length(generator);
  std::string result; result.reserve(length);
  for (int i = 0; i < length; ++i) {
    const int index = random_letter(generator);
    result += alphabet[index];
  }
  return result;
}

std::string generate_random_value(std::mt19937 & generator)
{
  std::uniform_int_distribution<int> random_value_length(8, 921600);  // 900 kilobytes ("hundreds" of kilobytes)
  std::uniform_int_distribution<int> random_alphanumeric(0, sizeof(alphanumeric) - 2);
  const int length = random_value_length(generator);
  std::string result; result.reserve(length);
  for (int i = 0; i < length; ++i) {
    const int index = random_alphanumeric(generator);
    result += alphanumeric[index];
  }
  return result;
}

void stress_test(ConcurrentHashTable * hash_table,
                 const int id,
                 const std::chrono::time_point<std::chrono::steady_clock> start_time,
                 size_t & num_read_operations,
                 size_t & num_successful_write_operations,
                 size_t & num_failed_write_operations)
{
  std::cout << "Starting stressor #" << id << '\n';

  std::mt19937 generator;
  std::uniform_int_distribution<int> read_write(0, 1);

  num_read_operations = 0;
  num_successful_write_operations = 0;
  num_failed_write_operations = 0;
  while (std::chrono::steady_clock::now() < start_time + RUN_TIME) {
    const std::string key = generate_random_key(generator);
    const bool write = read_write(generator);
    if (write) {
      const std::string value = generate_random_value(generator);
      const bool success = hash_table->put(key, value);
      // std::cout << "put(" << key << ", (" << value.length() << " bytes))\n";
      if (success) {
        ++num_successful_write_operations;
      } else {
        ++num_failed_write_operations;
      }
    } else {
      const std::string result = hash_table->get(key);
      // std::cout << "get(" << key << ") returned " << result.length() << " bytes\n";
      if (!result.empty()) {
        ++num_read_operations;
      }
    }
  }
}

int main(const int argc, const char * argv[])
{
  ConcurrentHashTable * hash_table = new ConcurrentHashTable();

  // std::cout << "key-value pairs in hash table before stress test:\n";
  // for (auto iter = hash_table->begin(); iter != hash_table->end(); ++iter) {
  //   const std::pair<std::string, std::string> key_value_pair = *iter;
  //   std::cout << key_value_pair.first << ": " << key_value_pair.second << '\n';
  // }
  // std::cout << "end key-value pairs\n";

  std::cout << "[TEST] Dumping buffer usage\n";
  bool success = hash_table->dump_buffer_usage("before_test.png");
  if (success) {
    std::cout << "[TEST] Dumped buffer usage successfully\n";
  } else {
    std::cout << "[TEST] Dumping buffer usage failed\n";
  }

  std::cout << "\nRunning test for " << RUN_TIME.count() << " seconds...\n\n";

  std::vector<std::thread> threads; threads.reserve(NUM_THREADS);
  std::vector<size_t> num_read_operations(NUM_THREADS);
  std::vector<size_t> num_successful_write_operations(NUM_THREADS);
  std::vector<size_t> num_failed_write_operations(NUM_THREADS);
  const std::chrono::time_point<std::chrono::steady_clock> start_time = std::chrono::steady_clock::now();
  for (int i = 0; i < NUM_THREADS; ++i) {
    threads.emplace_back(stress_test,
                         hash_table,
                         i + 1,
                         start_time,
                         std::ref(num_read_operations[i]),
                         std::ref(num_successful_write_operations[i]),
                         std::ref(num_failed_write_operations[i]));
  }
  for (auto iter = threads.begin(); iter != threads.end(); ++iter) {
    iter->join();
  }

  std::cout << "\nTest results:\n\n";

  const size_t total_read_operations = accumulate(num_read_operations.begin(), num_read_operations.end(), 0);
  const size_t total_successful_write_operations = accumulate(num_successful_write_operations.begin(), num_successful_write_operations.end(), 0);
  const size_t total_failed_write_operations = accumulate(num_failed_write_operations.begin(), num_failed_write_operations.end(), 0);
  std::cout << "Number of operations in " << RUN_TIME.count() << " seconds:\n"
            << "read: " << total_read_operations << '\n'
            << "write: " << total_successful_write_operations << " (successful)\n"
            << "write: " << total_failed_write_operations << " (failed due to memory allocation and fragmentation)\n"
            << "total: " << total_read_operations + total_successful_write_operations << " (successful)\n"
            << '\n';
  hash_table->print_stats();

  std::cout << "[TEST] Dumping buffer usage\n";
  success = hash_table->dump_buffer_usage("after_test.png");
  if (success) {
    std::cout << "[TEST] Dumped buffer usage successfully\n";
  } else {
    std::cout << "[TEST] Dumping buffer usage failed\n";
  }

  // std::cout << "\nkey-value pairs in hash table after stress test:\n";
  // for (auto iter = hash_table->begin(); iter != hash_table->end(); ++iter) {
  //   const std::pair<std::string, std::string> key_value_pair = *iter;
  //   std::cout << key_value_pair.first << ": " << key_value_pair.second << '\n';
  // }
  // std::cout << "end key-value pairs\n";

  // purposely leak hash_table to simulate process crash

  return 0;
}
