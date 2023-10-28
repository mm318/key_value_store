#include <iostream>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "file_backed_buffer.hpp"


constexpr size_t BUFFER_SIZE_BYTES = 67108864;


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

FileBackedBuffer::FileBackedBuffer(const char * filename) : m_fd(-1), m_base(nullptr)
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
    if (ftruncate(m_fd, BUFFER_SIZE_BYTES) != 0) {
      std::cerr << "[WARN] failed to resize buffer file to " << BUFFER_SIZE_BYTES << " bytes\n";
    }
  }

  m_db_size = get_file_size(m_fd);
  std::cout << "[INFO] buffer file size: " << m_db_size << " bytes\n";

  if (m_fd >= 0) {
    // using an mmap'd file to provide easy to use interface for client code
    // it also meets the requirement of strongly consistent, given standard proper mutex use / memory fencing
    m_base = static_cast<uint8_t *>(mmap(NULL, m_db_size, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0));
    if (m_base == MAP_FAILED) {
      std::cerr << "[ERROR] mmaping " << filename << " failed\n";
      m_base = nullptr;
      exit(-1);
    }
  } else {
    exit(-1);
  }

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

      return curr_block->data;
    }

    curr_free_block = curr_block->next_block;
  }

  return nullptr;
}

void FileBackedBuffer::free(const uint8_t * pointer)
{
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
