#include <iostream>
#include <cstring>
#include <algorithm>
#include <cassert>
#include <limits>

#include "hash_table.hpp"


constexpr size_t BUFFER_SIZE = 536870912;   // bytes
constexpr size_t HASH_TABLE_SIZE = 266671;  // targeting about 200000 elements in hash table at 75% load factor
constexpr char ConcurrentHashTable::BUFFER_FILENAME[];
constexpr std::hash<std::string> ConcurrentHashTable::hasher;

ConcurrentHashTable::ConcurrentHashTable() : m_buffer(BUFFER_FILENAME, BUFFER_SIZE), m_hash_table(HASH_TABLE_SIZE)
{
  // load what's already in the buffer
  for (auto iter = m_buffer.begin_allocated(); iter != m_buffer.end_allocated(); ++iter) {
    const std::pair<uint8_t *, size_t> data = *iter;

    Bucket * new_bucket = get_new_bucket();
    new_bucket->key_value_pair.set(reinterpret_cast<char *>(data.first), BufferFreer(this));

    size_t hash = hasher(new_bucket->key_value_pair.get().first.get());
    size_t hash_table_index = hash % m_hash_table.size();
    store_bucket(new_bucket, hash_table_index);
  }
}

bool ConcurrentHashTable::put(const std::string & key, const std::string & value)
{
  std::unique_lock<std::mutex> write_lock(m_write_mutex);

  const size_t allocation_size = key.length() + 1 + value.length() + 1;
  uint8_t * data_buffer = m_buffer.alloc(allocation_size);
  if (data_buffer == nullptr) {
    return false;
  }

  std::pair<Bucket *, size_t> result = find_bucket_with_key(key);
  Bucket * bucket = (result.first == nullptr) ? get_new_bucket() : result.first;
  bucket->key_value_pair.set(reinterpret_cast<char *>(data_buffer), BufferFreer(this), key, value);

  if (result.first == nullptr) {
    store_bucket(bucket, result.second);
  }

  return true;
}

std::string ConcurrentHashTable::get(const std::string & key)
{
  std::pair<Bucket *, size_t> result = find_bucket_with_key(key);
  if (result.first == nullptr) {
    return std::string();
  }
  return result.first->key_value_pair.get().second;
}

std::pair<ConcurrentHashTable::Bucket *, size_t> ConcurrentHashTable::find_bucket_with_key(const std::string & key) const
{
  size_t hash = hasher(key);
  size_t hash_table_index = hash % m_hash_table.size();

  Bucket * curr_bucket = m_hash_table[hash_table_index].load(std::memory_order_acquire);
  if (curr_bucket == nullptr) {
    return std::make_pair(nullptr, hash_table_index);
  }

  while (key != curr_bucket->key_value_pair.get().first.get()) {
    curr_bucket = curr_bucket->next_bucket;
    if (curr_bucket == nullptr) {
      break;
    }
  }

  return std::make_pair(curr_bucket, hash_table_index);
}

ConcurrentHashTable::Bucket * ConcurrentHashTable::get_new_bucket()
{
  m_bucket_storage.emplace_back();
  return &m_bucket_storage.back();
}

void ConcurrentHashTable::store_bucket(Bucket * bucket, const size_t hash_table_index)
{
  bucket->next_bucket = m_hash_table[hash_table_index].load(std::memory_order_relaxed);
  m_hash_table[hash_table_index].store(bucket, std::memory_order_release);
}

// information to be stored in key_value_data: <key> + '\0' + <value> + '\0'
void ConcurrentHashTable::KeyValuePair::set(char * key_value_data,
                                            BufferFreer deleter,
                                            const std::string & key,
                                            const std::string & value)
{
  char * key_data = key_value_data;
  strncpy(key_data, key.c_str(), key.length() + 1);

  char * key_data_end = strchr(key_data, '\0');
  assert(key_data_end != nullptr);

  char * value_data = key_data_end + 1;
  strncpy(value_data, value.c_str(), value.length() + 1);

  set(key_data, deleter);
}

void ConcurrentHashTable::KeyValuePair::set(char * key_value_data, BufferFreer deleter)
{
  std::atomic_store_explicit(&m_key_value_data,
                             std::shared_ptr<const char>(key_value_data, deleter),
                             std::memory_order_release);
}

std::pair<std::shared_ptr<const char>, const char *> ConcurrentHashTable::KeyValuePair::get() const
{
  std::shared_ptr<const char> key = std::atomic_load_explicit(&m_key_value_data, std::memory_order_acquire);
  const char * value = strchr(key.get(), '\0') + 1;
  return std::make_pair(std::move(key), value);
}

std::pair<std::string, std::string> ConcurrentHashTable::const_iterator::operator*()
{
  auto key_value_pair = m_iter->key_value_pair.get();
  return std::make_pair(key_value_pair.first.get(), key_value_pair.second);
}

ConcurrentHashTable::const_iterator ConcurrentHashTable::const_iterator::operator++()
{
  ++m_iter;
  return *this;
}

ConcurrentHashTable::const_iterator ConcurrentHashTable::const_iterator::operator--()
{
  --m_iter;
  return *this;
}

void ConcurrentHashTable::print_stats() const
{
  size_t num_key_value_pairs = 0;
  size_t smallest_value_size = std::numeric_limits<size_t>::max();
  size_t largest_value_size = 0;
  float average_value_size = 0.0f;
  for (auto iter = begin(); iter != end(); ++iter) {
    const std::pair<std::string, std::string> key_value_pair = *iter;
    if (key_value_pair.second.length() < smallest_value_size) {
      smallest_value_size = key_value_pair.second.length();
    }
    if (key_value_pair.second.length() > largest_value_size) {
      largest_value_size = key_value_pair.second.length();
    }
    average_value_size += key_value_pair.second.length();
    ++num_key_value_pairs;
  }
  average_value_size /= static_cast<float>(num_key_value_pairs);

  size_t num_table_elements = 0;
  for (auto iter = m_hash_table.begin(); iter != m_hash_table.end(); ++iter) {
    if (iter->load(std::memory_order_relaxed) != nullptr) {
      ++num_table_elements;
    }
  }
  float load_factor = static_cast<float>(num_table_elements) / m_hash_table.size();

  std::cout << "hash table stats:\n"
            << "key-value pairs: " << num_key_value_pairs << '\n'
            << "elements in table: " << num_table_elements << '\n'
            << "load factor: " << load_factor << '\n'
            << "smallest value size (bytes): " << smallest_value_size << '\n'
            << "largest value size (bytes): " << largest_value_size << '\n'
            << "average value size (bytes): " << average_value_size << '\n'
            << '\n';

  m_buffer.print_stats();
}
