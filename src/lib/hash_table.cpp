#include <iostream>
#include <cstring>
#include <algorithm>
#include <cassert>

#include "hash_table.hpp"


constexpr size_t HASH_TABLE_SIZE = 266671;  // targeting about 200000 elements in hash table at 75% load factor
constexpr char ConcurrentHashTable::BUFFER_FILENAME[];
constexpr std::hash<std::string> ConcurrentHashTable::hasher;

ConcurrentHashTable::ConcurrentHashTable() : m_buffer(BUFFER_FILENAME), m_hash_table(HASH_TABLE_SIZE)
{
  // load what's already in the buffer
  for (auto iter = m_buffer.begin_allocated(); iter != m_buffer.end_allocated(); ++iter) {
    const std::pair<uint8_t *, size_t> data = *iter;

    Bucket * new_bucket = get_new_bucket();
    new_bucket->data.set(reinterpret_cast<char *>(data.first), BufferFreer(this));

    size_t hash = hasher(new_bucket->data.get().first.get());
    size_t hash_table_index = hash % m_hash_table.size();
    store_bucket(new_bucket, hash_table_index);
  }
}

void ConcurrentHashTable::put(const std::string & key, const std::string & value)
{
  std::unique_lock<std::mutex> write_lock(m_write_mutex);
  
  const size_t allocation_size = key.length() + 1 + value.length() + 1;

  std::pair<Bucket *, size_t> result = find_bucket_with_key(key);
  Bucket * bucket = (result.first == nullptr) ? get_new_bucket() : result.first;
  bucket->data.set(m_buffer.alloc(allocation_size), BufferFreer(this), key, value);

  if (result.first == nullptr) {
    store_bucket(bucket, result.second);
  }
}

std::string ConcurrentHashTable::get(const std::string & key)
{
  std::pair<Bucket *, size_t> result = find_bucket_with_key(key);
  if (result.first == nullptr) {
    return std::string();
  }
  return result.first->data.get().second;
}

std::pair<ConcurrentHashTable::Bucket *, size_t> ConcurrentHashTable::find_bucket_with_key(const std::string & key) const
{
  size_t hash = hasher(key);
  size_t hash_table_index = hash % m_hash_table.size();

  Bucket * curr_bucket = m_hash_table[hash_table_index].load(std::memory_order_acquire);
  if (curr_bucket == nullptr) {
    return std::make_pair(nullptr, hash_table_index);
  }

  while (key != curr_bucket->data.get().first.get()) {
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

void ConcurrentHashTable::KeyValuePair::set(uint8_t * data_buffer, BufferFreer deleter, const std::string & key, const std::string & value)
{
  char * key_data = reinterpret_cast<char *>(data_buffer);
  strncpy(key_data, key.c_str(), key.length() + 1);

  char * key_data_end = strchr(key_data, '\0');
  assert(key_data_end != nullptr);

  char * value_data = key_data_end + 1;
  strncpy(value_data, value.c_str(), value.length() + 1);

  set(key_data, deleter);
}

void ConcurrentHashTable::KeyValuePair::set(const char * key_value_data, BufferFreer deleter)
{
  std::atomic_store_explicit(&m_key_value_data, std::shared_ptr<const char>(key_value_data, deleter), std::memory_order_release);
}

std::pair<std::shared_ptr<const char>, const char *> ConcurrentHashTable::KeyValuePair::get() const
{
  std::shared_ptr<const char> key = std::atomic_load_explicit(&m_key_value_data, std::memory_order_acquire);
  const char * value = strchr(key.get(), '\0') + 1;
  return std::make_pair(std::move(key), value);
}

std::pair<std::string, std::string> ConcurrentHashTable::const_iterator::operator*()
{
  auto key_value_pair = m_iter->data.get();
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
