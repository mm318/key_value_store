#ifndef _FILE_BACKED_BUFFER_HPP_
#define _FILE_BACKED_BUFFER_HPP_

#include <cstddef>
#include <cstdint>
#include <utility>
#include <mutex>


using FileByteOffset = size_t;

constexpr FileByteOffset NULL_OFFSET = 0;

// using a file backed buffer to meet requirement of data persistence across process crashes
class FileBackedBuffer
{
public:
  FileBackedBuffer(const char * filename, const size_t buffer_size);
  ~FileBackedBuffer();

  // TODO: look into replacing this naive allocator implementation with open source jemalloc algorithm or something similar
  uint8_t * alloc(const size_t alloc_size);
  void free(const uint8_t * pointer);

  class const_iterator
  {
  public:
    const_iterator(const FileBackedBuffer * parent, const FileByteOffset offset) : m_parent(parent), m_offset(offset) {}

    std::pair<uint8_t *, size_t> operator*();

    const_iterator operator++();
    const_iterator operator--();

    bool operator==(const const_iterator & other) const { return other.m_parent == m_parent && other.m_offset == m_offset; }
    bool operator!=(const const_iterator & other) const { return other.m_parent != m_parent || other.m_offset != m_offset; }

  private:
    const FileBackedBuffer * m_parent;
    FileByteOffset m_offset;
  };

  const_iterator begin_allocated() const { return const_iterator(this, m_header->next_allocated_block_offset); }
  const_iterator end_allocated() const { return const_iterator(this, NULL_OFFSET); }

  const_iterator begin_free() const { return const_iterator(this, m_header->next_free_block_offset); }
  const_iterator end_free() const { return const_iterator(this, NULL_OFFSET); }

  void print_stats() const;
  bool dump_usage(const std::string & filename) const;

private:
  struct BufferHeader {
    FileByteOffset next_free_block_offset;
    FileByteOffset next_allocated_block_offset;
  };

  // buffer is split into allocated blocks, tracked by intrusive linked list
  // the tracking of each block has overhead (members other than data)
  struct Block {
    FileByteOffset prev_block_offset;
    FileByteOffset next_block_offset;
    size_t data_size;
    uint8_t data[];
  };

  uint8_t * to_pointer(const FileByteOffset offset) const { return m_base + offset; }
  FileByteOffset to_offset(const uint8_t * pointer) const { return pointer - m_base; }

  FileByteOffset & free_list() { return m_header->next_free_block_offset; }
  FileByteOffset & allocated_list() { return m_header->next_allocated_block_offset; }

  void remove_block_from_list(FileByteOffset & list_head, Block * block);
  void insert_block_to_list(FileByteOffset & list_head, Block * block);

  int m_fd;
  int m_db_size;  // size of buffer in bytes
  uint8_t * m_base;
  mutable std::mutex m_mutex;
  BufferHeader * m_header;
};

#endif  // _FILE_BACKED_BUFFER_HPP_
