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
    new_bucket->data.set_buffer_and_key(data.first, data.second);
    size_t hash = hasher(new_bucket->data.get_key());
    size_t hash_table_index = hash % m_hash_table.size();
    store_bucket(new_bucket, hash_table_index);
  }
}

void ConcurrentHashTable::put(const std::string & key, const std::string & value)
{
  std::pair<Bucket *, size_t> result = find_bucket_with_key(key);
  if (result.first == nullptr) {
    Bucket * new_bucket = get_new_bucket();
    size_t allocation_size = key.length() + 1 + value.length() + 1;   // could optimize to account for growth of value length
    new_bucket->data.set_buffer_and_key(m_buffer.alloc(allocation_size), allocation_size, &key);
    assert(new_bucket->data.set_value(value));
    store_bucket(new_bucket, result.second);
  } else {
    Bucket * bucket = result.first;
    if (value.length() + 1 > bucket->data.get_value_capacity()) {
      m_buffer.free(reinterpret_cast<const uint8_t *>(bucket->data.get_key()));
      size_t allocation_size = key.length() + 1 + value.length() + 1; // could optimize to account for growth of value length
      bucket->data.set_buffer_and_key(m_buffer.alloc(allocation_size), allocation_size, &key);
    }
    assert(bucket->data.set_value(value));
  }
}

std::string ConcurrentHashTable::get(const std::string & key)
{
  std::pair<Bucket *, size_t> result = find_bucket_with_key(key);
  if (result.first == nullptr) {
    return std::string();
  }
  return result.first->data.get_value();
}

std::pair<ConcurrentHashTable::Bucket *, size_t> ConcurrentHashTable::find_bucket_with_key(const std::string & key) const
{
  size_t hash = hasher(key);
  size_t hash_table_index = hash % m_hash_table.size();

  Bucket * curr_bucket = m_hash_table[hash_table_index];
  if (curr_bucket == nullptr) {
    return std::make_pair(nullptr, hash_table_index);
  }

  while (key != curr_bucket->data.get_key()) {
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
  bucket->next_bucket = m_hash_table[hash_table_index];
  m_hash_table[hash_table_index] = bucket;
}

void ConcurrentHashTable::KeyValuePair::set_buffer_and_key(uint8_t * buffer_data, const size_t buffer_size, const std::string * key)
{
  if (buffer_data == nullptr) {
    std::cerr << "[ERROR] out of memory\n";
    assert(false);
  }

  char * key_data = reinterpret_cast<char *>(buffer_data);
  if (key != nullptr) {
    if (key->length() + 1 > buffer_size) {
      std::cerr << "[ERROR] can't fit key " << *key << " in provided buffer\n";
      assert(false);
    }
    strcpy(key_data, key->c_str());
  }

  char * key_data_end = strchr(key_data, '\0');
  assert(key_data_end != nullptr);

  m_key = key_data;
  m_value = key_data_end + 1;
  m_value_capacity = buffer_size - (m_value - m_key);
}

bool ConcurrentHashTable::KeyValuePair::set_value(const std::string & value)
{
  strncpy(m_value, value.c_str(), std::min(value.length() + 1, m_value_capacity));
  return value.length() + 1 <= m_value_capacity;
}

std::pair<std::string, std::string> ConcurrentHashTable::const_iterator::operator*()
{
  return std::make_pair(m_iter->data.get_key(), m_iter->data.get_value());
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
