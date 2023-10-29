#ifndef _HASH_TABLE_HPP_
#define _HASH_TABLE_HPP_

#include <vector>
#include <deque>
#include <string>
#include <functional>
#include <atomic>
#include <mutex>
#include <memory>

#include "file_backed_buffer.hpp"

// using a hash table to implement the key-value store mechanism
// it locks on writes and is lockless on reads (except when the last reader of an expiring value needs to deallocate,
// similar to left-right concurrency control, but won't be doubling-up on memory allocations).
// this meets the heavily read-skewed usage pattern and still works with multiple writers
// collisions are resolved with open hashing / separate chaining instead of closed hashing / open addressing
// since the keys are strings of arbitrary length, which have infinitely many possibilities, a separate chainining
// hash table can technically keep accepting new keys indefinitely, so the hash table has less need to resize
class ConcurrentHashTable
{
  struct Bucket;

public:
  static constexpr char BUFFER_FILENAME[] = "kvstore.bin";

  ConcurrentHashTable();

  // basic functionality requirements: put() and get()
  bool put(const std::string & key, const std::string & value);
  std::string get(const std::string & key); // returns empty string if key is not found

  class const_iterator
  {
  public:
    const_iterator(std::deque<Bucket>::const_iterator iter) : m_iter(iter) {}

    std::pair<std::string, std::string> operator*();

    const_iterator operator++();
    const_iterator operator--();

    bool operator==(const const_iterator & other) const { return other.m_iter == m_iter; }
    bool operator!=(const const_iterator & other) const { return other.m_iter != m_iter; }

  private:
    std::deque<Bucket>::const_iterator m_iter;
  };

  const_iterator begin() const { return m_bucket_storage.cbegin(); }
  const_iterator end() const { return m_bucket_storage.cend(); }

  void print_stats() const;

private:
  class BufferFreer {
  public:
    BufferFreer(ConcurrentHashTable * parent) : m_parent(parent) {}

    void operator()(const char * ptr) { m_parent->m_buffer.free(reinterpret_cast<const uint8_t *>(ptr)); }

  private:
    ConcurrentHashTable * m_parent;
  };

  class KeyValuePair
  {
  public:
    KeyValuePair() {}

    void set(uint8_t * data_buffer, BufferFreer deleter, const std::string & key, const std::string & value);
    void set(const char * key_value_data, BufferFreer deleter);
    std::pair<std::shared_ptr<const char>, const char *> get() const;

  private:
    // this is one of the two places where reader-writer contention may occur
    // when reader wants to access and writer wants to update the same bucket
    // resolved with atomic load/store of this pointer. this also meets the strongly consistent requirement
    // writer-writer contention does not occur because second writer is locked out
    // at the beginning of put()
    std::shared_ptr<const char> m_key_value_data;
  };

  struct Bucket {
    Bucket() : next_bucket(nullptr) {}
    Bucket * next_bucket;
    KeyValuePair data;
  };

  std::pair<Bucket *, size_t> find_bucket_with_key(const std::string & key) const;
  Bucket * get_new_bucket();
  void store_bucket(Bucket * bucket, const size_t hash_table_index);

  std::mutex m_write_mutex;
  FileBackedBuffer m_buffer;
  std::deque<Bucket> m_bucket_storage;
  // this is one of the two places where reader-writer contention may occur
  // when reader is searching for the right bucket while writer is adding a bucket.
  // resolved because adding a bucket doesn't impact the subsequent pointers in the list (Bucket::next_bucket pointers),
  // only the top-level pointer needs to be updated, which is done atomically and with release semantics
  // writer-writer contention does not occur because second writer is locked out
  // at the beginning of put() until first writer completes
  std::vector<std::atomic<Bucket *>> m_hash_table;

  static constexpr std::hash<std::string> hasher = std::hash<std::string>();
};

#endif  // _HASH_TABLE_HPP_
