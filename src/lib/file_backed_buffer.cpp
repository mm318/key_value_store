#include <iostream>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <cassert>
#include <limits>

#include "file_backed_buffer.hpp"


static int get_file_size(int fd)
{
  struct stat stat_buf;
  if (fstat(fd, &stat_buf) < 0) {
    int err = errno;
    std::cerr << "[ERROR] " << strerror(err) << '\n';
    return -1;
  } else {
    return stat_buf.st_size;
  }
}

FileBackedBuffer::FileBackedBuffer(const char * filename, const size_t buffer_size) : m_fd(-1), m_base(nullptr)
{
  bool new_file = false;

  std::cout << "[INFO] attempting to create buffer file\n";
  m_fd = open(filename, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
  if (m_fd < 0) {
    if (errno == EEXIST) {
      std::cout << "[INFO] buffer file already exists\n";
      m_fd = open(filename, O_RDWR);
      if (m_fd < 0) {
        int err = errno;
        std::cerr << "[ERROR] " << strerror(err) << '\n';
      }
    }
  } else {
    new_file = true;
    if (ftruncate(m_fd, buffer_size) != 0) {
      std::cerr << "[WARN] failed to resize buffer file to " << buffer_size << " bytes\n";
    }
  }

  m_db_size = get_file_size(m_fd);
  std::cout << "[INFO] buffer file size: " << m_db_size << " bytes\n";

  if (m_fd >= 0) {
    // using an mmap'd file to provide easy to use interface for client code
    m_base = static_cast<uint8_t *>(mmap(NULL, m_db_size, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0));
    if (m_base == MAP_FAILED) {
      std::cerr << "[ERROR] mmaping " << filename << " failed\n";
      m_base = nullptr;
    }
  }
  assert(m_base != nullptr);

  // initialize the buffer
  m_header = reinterpret_cast<BufferHeader *>(m_base);
  if (m_header != nullptr && new_file) {
    std::cout << "[INFO] initializing buffer file contents\n";
    m_header->next_free_block = sizeof(BufferHeader);
    m_header->next_allocated_block = NULL_OFFSET;

    Block * new_block = reinterpret_cast<Block *>(to_pointer(m_header->next_free_block));
    new_block->data_size = m_db_size - sizeof(BufferHeader) - sizeof(Block);
    new_block->prev_block = NULL_OFFSET;
    new_block->next_block = NULL_OFFSET;
  }
}

FileBackedBuffer::~FileBackedBuffer()
{
  if (m_base != nullptr) {
    munmap(m_base, m_db_size);
  }
  if (m_fd >= 0) {
    close(m_fd);
  }
}

uint8_t * FileBackedBuffer::alloc(const size_t alloc_size)
{
  std::unique_lock<std::mutex> write_lock(m_mutex);

  uint8_t * result = nullptr;

  FileByteOffset curr_free_block = m_header->next_free_block;
  while (curr_free_block != NULL_OFFSET) {
    Block * curr_block = reinterpret_cast<Block *>(to_pointer(curr_free_block));
    if (curr_block->data_size >= alloc_size) {
      remove_block_from_list(free_list(), curr_block);

      if (curr_block->data_size >= alloc_size + sizeof(Block) + 100 /* only 100 bytes oversized do we split the block, it's a heurestic */) {
        Block * new_block = reinterpret_cast<Block *>(curr_block->data + alloc_size);
        new_block->data_size = curr_block->data_size - alloc_size - sizeof(Block);
        insert_block_to_list(free_list(), new_block);
        curr_block->data_size = alloc_size;
      }

      insert_block_to_list(allocated_list(), curr_block);

      result = curr_block->data;
      break;
    }

    curr_free_block = curr_block->next_block;
  }

  assert(result != nullptr);
  return result;
}

void FileBackedBuffer::free(const uint8_t * pointer)
{
  std::unique_lock<std::mutex> write_lock(m_mutex);
  Block * block = const_cast<Block *>(reinterpret_cast<const Block *>(pointer - sizeof(Block)));
  remove_block_from_list(allocated_list(), block);
  insert_block_to_list(free_list(), block);
}

void FileBackedBuffer::remove_block_from_list(FileByteOffset & list_head, Block * curr_block)
{
  if (curr_block->prev_block != NULL_OFFSET) {
    Block * prev_block = reinterpret_cast<Block *>(to_pointer(curr_block->prev_block));
    prev_block->next_block = curr_block->next_block;
  } else {
    list_head = curr_block->next_block;
  }

  if (curr_block->next_block != NULL_OFFSET) {
    Block * next_block = reinterpret_cast<Block *>(to_pointer(curr_block->next_block));
    next_block->prev_block = curr_block->prev_block;
  }

  curr_block->prev_block = NULL_OFFSET;
  curr_block->next_block = NULL_OFFSET;
}

void FileBackedBuffer::insert_block_to_list(FileByteOffset & list_head, Block * block)
{
  if (list_head != NULL_OFFSET) {
    Block * next_block = reinterpret_cast<Block *>(to_pointer(list_head));
    next_block->prev_block = to_offset(reinterpret_cast<uint8_t *>(block));
  }

  block->prev_block = NULL_OFFSET;
  block->next_block = list_head;

  list_head = to_offset(reinterpret_cast<uint8_t *>(block));
}

std::pair<uint8_t *, size_t> FileBackedBuffer::const_iterator::operator*()
{
  if (m_offset == NULL_OFFSET) {
    return std::make_pair(nullptr, 0);
  }
  Block * block = reinterpret_cast<Block *>(m_parent->to_pointer(m_offset));
  return std::make_pair(block->data, block->data_size);
}

FileBackedBuffer::const_iterator FileBackedBuffer::const_iterator::operator++()
{
  if (m_offset != NULL_OFFSET) {
    Block * block = reinterpret_cast<Block *>(m_parent->to_pointer(m_offset));
    m_offset = block->next_block;
  }
  return *this;
}

FileBackedBuffer::const_iterator FileBackedBuffer::const_iterator::operator--()
{
  if (m_offset != NULL_OFFSET) {
    Block * block = reinterpret_cast<Block *>(m_parent->to_pointer(m_offset));
    m_offset = block->prev_block;
  }
  return *this;
}

void FileBackedBuffer::print_stats() const
{
  size_t num_allocated_blocks = 0;
  size_t smallest_allocated_block_size = std::numeric_limits<size_t>::max();
  size_t largest_allocated_block_size = 0;
  size_t total_allocated_block_size = 0;
  for (auto iter = begin_allocated(); iter != end_allocated(); ++iter) {
    const std::pair<uint8_t *, size_t> data = *iter;
    const size_t block_size = data.second + sizeof(Block);
    if (block_size < smallest_allocated_block_size) {
      smallest_allocated_block_size = block_size;
    }
    if (block_size > largest_allocated_block_size) {
      largest_allocated_block_size = block_size;
    }
    total_allocated_block_size += block_size;
    ++num_allocated_blocks;
  }
  float average_allocated_block_size = static_cast<float>(total_allocated_block_size) / num_allocated_blocks;

  size_t num_free_blocks = 0;
  size_t smallest_free_block_size = std::numeric_limits<size_t>::max();
  size_t largest_free_block_size = 0;
  size_t total_free_block_size = 0;
  for (auto iter = begin_free(); iter != end_free(); ++iter) {
    const std::pair<uint8_t *, size_t> data = *iter;
    const size_t block_size = data.second + sizeof(Block);
    if (block_size < smallest_free_block_size) {
      smallest_free_block_size = block_size;
    }
    if (block_size > largest_free_block_size) {
      largest_free_block_size = block_size;
    }
    total_free_block_size += block_size;
    ++num_free_blocks;
  }
  float average_free_block_size = static_cast<float>(total_free_block_size) / num_free_blocks;
  float fragmentation = 0.0f;
  if (total_free_block_size != 0) {
    fragmentation = static_cast<float>(total_free_block_size - largest_free_block_size) / total_free_block_size;
  }

  std::cout << "file buffer stats:\n"
            << "allocated blocks: " << num_allocated_blocks << '\n'
            << "smallest allocated block (bytes): " << smallest_allocated_block_size << '\n'
            << "largest allocated block (bytes): " << largest_allocated_block_size << '\n'
            << "total allocated block (bytes): " << total_allocated_block_size << '\n'
            << "average allocated block (bytes): " << average_allocated_block_size << '\n'
            << "free blocks: " << num_free_blocks << '\n'
            << "smallest free block (bytes): " << smallest_free_block_size << '\n'
            << "largest free block (bytes): " << largest_free_block_size << '\n'
            << "total free block (bytes): " << total_free_block_size << '\n'
            << "average free block (bytes): " << average_free_block_size << '\n'
            << "free space fragmentation: " << fragmentation << '\n'
            << '\n';
}
