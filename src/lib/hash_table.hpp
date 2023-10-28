#ifndef _HASH_TABLE_HPP_
#define _HASH_TABLE_HPP_

#include <vector>
#include <deque>
#include <string>
#include <functional>

#include "file_backed_buffer.hpp"

// using a hash table to implement the key-value store mechanism
// it locks on writes and is lockless on reads. this meets the heavily read-skewed usage pattern and still works
// with multiple writers
// collisions are resolved with open hashing / separate chaining instead of closed hashing / open addressing
// since the keys are strings of arbitrary length, which have infinitely many possibilities, a separate chainining
// hash table can technically keep accepting new keys indefinitely, so the hash table has less need to resize
class ConcurrentHashTable
{
public:
  ConcurrentHashTable();

  void put(const std::string & key, const std::string & value);
  std::string get(const std::string & key); // returns empty string if key is not found

private:
  class KeyValuePair {
  public:
    KeyValuePair() : m_key(nullptr), m_value(nullptr) {}

    void set_buffer_and_key(uint8_t * buffer_data, const size_t buffer_size, const std::string * key = nullptr);

    size_t get_value_capacity() const { return m_value_capacity; }

    const char * get_key() const { return m_key; }
    const char * get_value() const { return m_value; }

    bool set_value(const std::string & value);

  private:
    const char * m_key;
    char * m_value;
    size_t m_value_capacity;
  };

  struct Bucket {
    Bucket() : next_bucket(nullptr) {}
    Bucket * next_bucket;
    KeyValuePair data;
  };

  std::pair<Bucket *, size_t> find_bucket_with_key(const std::string & key) const;
  Bucket * get_new_bucket();
  void store_bucket(Bucket * bucket, const size_t hash_table_index);

  FileBackedBuffer m_buffer;
  std::vector<Bucket*> m_hash_table;
  std::deque<Bucket> m_bucket_storage;
  // Bucket * m_free_bucket_list_head;  // don't need this as we're never erasing keys from store

  static constexpr std::hash<std::string> hasher = std::hash<std::string>();
};

#endif  // _HASH_TABLE_HPP_
