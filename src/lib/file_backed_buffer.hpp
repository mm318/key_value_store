#ifndef _FILE_BACKED_BUFFER_HPP_
#define _FILE_BACKED_BUFFER_HPP_

#include <cstddef>
#include <cstdint>

using FileByteOffset = size_t;

constexpr FileByteOffset NULL_OFFSET = 0;

// using a file backed buffer to meet requirement of data persistence across process crashes
class FileBackedBuffer
{
public:
  FileBackedBuffer(const char * filename);
  ~FileBackedBuffer();

  uint8_t * alloc(const size_t alloc_size);

  void free(uint8_t * pointer);

private:
  struct BufferHeader {
    FileByteOffset next_free_block;
    FileByteOffset next_allocated_block;
  };

  // buffer is split into allocated blocks, tracked by intrusive linked list
  // the tracking of each block has overhead (members other than data)
  struct Block {
    FileByteOffset prev_block;
    FileByteOffset next_block;
    size_t data_size;
    uint8_t data[];
  };

  uint8_t * to_pointer(const FileByteOffset offset) const { return m_base + offset; }
  FileByteOffset to_offset(const uint8_t * pointer) const { return pointer - m_base; }

  FileByteOffset & free_list() { return m_header->next_free_block; }
  FileByteOffset & allocated_list() { return m_header->next_allocated_block; }

  void remove_block_from_list(FileByteOffset & list_head, Block * block);
  void insert_block_to_list(FileByteOffset & list_head, Block * block);

  int m_fd;
  int m_db_size;
  uint8_t * m_base;
  BufferHeader * m_header;
};

#endif  // _FILE_BACKED_BUFFER_HPP_
