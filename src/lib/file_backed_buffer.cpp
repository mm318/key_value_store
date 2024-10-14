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
#include <cmath>

#include "file_backed_buffer.hpp"
#include "fpng.h"


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
    free_list() = sizeof(BufferHeader);
    used_list() = NULL_OFFSET;

    Block * new_block = reinterpret_cast<Block *>(to_pointer(free_list()));
    new_block->data_size = m_db_size - sizeof(BufferHeader) - sizeof(Block);
    new_block->prev_block_offset = NULL_OFFSET;
    new_block->next_block_offset = NULL_OFFSET;
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

  FileByteOffset curr_free_block_offset = free_list();
  while (curr_free_block_offset != NULL_OFFSET) {
    Block * curr_block = reinterpret_cast<Block *>(to_pointer(curr_free_block_offset));
    if (curr_block->data_size >= alloc_size) {
      remove_block_from_list(free_list(), curr_block);

      // if the size of the currently available block is >100 bytes greater than the requested size
      // then split the currently available block into two. >100 bytes is a heurestic
      if (curr_block->data_size >= alloc_size + sizeof(Block) + 100) {
        Block * split_block = reinterpret_cast<Block *>(curr_block->data + alloc_size);
        split_block->data_size = curr_block->data_size - alloc_size - sizeof(Block);
        insert_block_to_free_list(split_block);
        curr_block->data_size = alloc_size;
      }

      insert_block_to_used_list(curr_block);

      result = curr_block->data;
      // *result = '\0'; // perform a non-comprehensive but cheap data reset
      break;
    }

    curr_free_block_offset = curr_block->next_block_offset;
  }

  if (result == nullptr) {
    std::cerr << "[WARN] Failed to allocate " << alloc_size << " bytes\n";
    // assert(false);
  }

  return result;
}

void FileBackedBuffer::free(const uint8_t * pointer)
{
  std::unique_lock<std::mutex> write_lock(m_mutex);
  Block * block = const_cast<Block *>(reinterpret_cast<const Block *>(pointer - sizeof(Block)));
  remove_block_from_list(used_list(), block);
  insert_block_to_free_list(block);
}

void FileBackedBuffer::remove_block_from_list(FileByteOffset & list_head, Block * curr_block)
{
  if (curr_block->prev_block_offset != NULL_OFFSET) {
    Block * prev_block = reinterpret_cast<Block *>(to_pointer(curr_block->prev_block_offset));
    prev_block->next_block_offset = curr_block->next_block_offset;
  } else {
    list_head = curr_block->next_block_offset;
  }

  if (curr_block->next_block_offset != NULL_OFFSET) {
    Block * next_block = reinterpret_cast<Block *>(to_pointer(curr_block->next_block_offset));
    next_block->prev_block_offset = curr_block->prev_block_offset;
  }

  curr_block->prev_block_offset = NULL_OFFSET;
  curr_block->next_block_offset = NULL_OFFSET;
}

// inserts block at the front of the list
void FileBackedBuffer::insert_block_to_used_list(Block * block)
{
  if (used_list() != NULL_OFFSET) {
    Block * next_block = reinterpret_cast<Block *>(to_pointer(used_list()));
    next_block->prev_block_offset = to_offset(block);
  }

  block->prev_block_offset = NULL_OFFSET;
  block->next_block_offset = used_list();

  used_list() = to_offset(block);
}

// inserts block in sorted order
void FileBackedBuffer::insert_block_to_free_list(Block * block)
{
  FileByteOffset block_offset = to_offset(block);

  FileByteOffset curr_free_block_offset = free_list();
  if (curr_free_block_offset == NULL_OFFSET) {
    free_list() = block_offset;
    return;
  }

  Block * curr_block = nullptr;
  while (true) {
    curr_block = reinterpret_cast<Block *>(to_pointer(curr_free_block_offset));
    if (curr_free_block_offset > block_offset) {
      break;
    }
    if (curr_block->next_block_offset == NULL_OFFSET) {
      break;
    }

    curr_free_block_offset = curr_block->next_block_offset;
  }

  FileByteOffset prev_free_block_offset;
  FileByteOffset next_free_block_offset;;
  if (curr_free_block_offset < block_offset) {
    assert(curr_block->next_block_offset == NULL_OFFSET);
    prev_free_block_offset = curr_free_block_offset;
    next_free_block_offset = curr_block->next_block_offset;
  } else if (curr_free_block_offset > block_offset) {
    if (!(curr_block->prev_block_offset == NULL_OFFSET || curr_block->prev_block_offset < block_offset)) {
      std::cerr << "[DEBUG] curr_block->prev_block_offset " << curr_block->prev_block_offset << "\n";
    }
    assert(curr_block->prev_block_offset == NULL_OFFSET || curr_block->prev_block_offset < block_offset);
    prev_free_block_offset = curr_block->prev_block_offset;
    next_free_block_offset = curr_free_block_offset;
  } else {
    std::cerr << "[ERROR] block is already in list!\n";
    assert(false);
  }
  Block * prev_block = (prev_free_block_offset == NULL_OFFSET)
                       ? nullptr : reinterpret_cast<Block *>(to_pointer(prev_free_block_offset));
  Block * next_block = (next_free_block_offset == NULL_OFFSET)
                       ? nullptr : reinterpret_cast<Block *>(to_pointer(next_free_block_offset));;

  block->prev_block_offset = prev_free_block_offset;
  block->next_block_offset = next_free_block_offset;
  if (prev_block != nullptr) {
    prev_block->next_block_offset = block_offset;
  } else {
    assert(curr_free_block_offset == free_list());
    free_list() = block_offset;
  }
  if (next_block != nullptr) {
    next_block->prev_block_offset = block_offset;
  }

  const bool prev_contiguous = prev_block != nullptr
                               && (prev_free_block_offset + sizeof(Block) + prev_block->data_size == prev_block->next_block_offset);
  const bool next_contiguous = next_block != nullptr
                               && (block_offset + sizeof(Block) + block->data_size == block->next_block_offset);
  if (prev_contiguous && next_contiguous) {
    remove_block_from_list(free_list(), block);
    remove_block_from_list(free_list(), next_block);
    prev_block->data_size += (sizeof(Block) + block->data_size) + (sizeof(Block) + next_block->data_size);
  } else if (prev_contiguous) {
    remove_block_from_list(free_list(), block);
    prev_block->data_size += (sizeof(Block) + block->data_size);
  } else if (next_contiguous) {
    remove_block_from_list(free_list(), next_block);
    block->data_size += (sizeof(Block) + next_block->data_size);
  }
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
    m_offset = block->next_block_offset;
  }
  return *this;
}

FileBackedBuffer::const_iterator FileBackedBuffer::const_iterator::operator--()
{
  if (m_offset != NULL_OFFSET) {
    Block * block = reinterpret_cast<Block *>(m_parent->to_pointer(m_offset));
    m_offset = block->prev_block_offset;
  }
  return *this;
}

void FileBackedBuffer::print_stats() const
{
  // calculate stats for used blocks
  size_t num_used_blocks = 0;
  size_t smallest_used_block_size = std::numeric_limits<size_t>::max();
  size_t largest_used_block_size = 0;
  size_t total_used_block_size = 0;
  for (auto iter = begin_used(); iter != end_used(); ++iter) {
    const std::pair<uint8_t *, size_t> data = *iter;
    const size_t block_size = data.second + sizeof(Block);
    if (block_size < smallest_used_block_size) {
      smallest_used_block_size = block_size;
    }
    if (block_size > largest_used_block_size) {
      largest_used_block_size = block_size;
    }
    total_used_block_size += block_size;
    ++num_used_blocks;
  }
  float average_used_block_size = static_cast<float>(total_used_block_size) / num_used_blocks;

  // calculate stats for free blocks
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

  // calculate fragmentation (based on https://stackoverflow.com/a/4587077)
  float fragmentation = 0.0f;
  if (total_free_block_size != 0) {
    fragmentation = static_cast<float>(total_free_block_size - largest_free_block_size) / total_free_block_size;
  }

  std::cout << "file buffer stats:\n"
            << "  used blocks: " << num_used_blocks << '\n'
            << "    smallest used block size (bytes): " << smallest_used_block_size << '\n'
            << "    largest used block size (bytes): " << largest_used_block_size << '\n'
            << "    total used block size (bytes): " << total_used_block_size << '\n'
            << "    average used block size (bytes): " << average_used_block_size << '\n'
            << "  free blocks: " << num_free_blocks << '\n'
            << "    smallest free block size (bytes): " << smallest_free_block_size << '\n'
            << "    largest free block size (bytes): " << largest_free_block_size << '\n'
            << "    total free block size (bytes): " << total_free_block_size << '\n'
            << "    average free block size (bytes): " << average_free_block_size << '\n'
            << "  free space fragmentation: " << fragmentation << '\n'
            << '\n';
}

bool FileBackedBuffer::dump_usage(const std::string & filename) const
{
  constexpr int NUM_BYTES_PER_PIXEL = 4;        // number of bytes in db buffer represented by one pixel in diagram
  constexpr int MAX_DIAGRAM_DIMENSION = 12000;  // output diagram should be no bigger than 8000 px by 8000 px

  std::unique_lock<std::mutex> read_lock(m_mutex);

  float num_pixels = std::ceil(static_cast<float>(m_db_size) / static_cast<float>(NUM_BYTES_PER_PIXEL));
  float diagram_dimension_px = std::ceil(std::sqrt(num_pixels));
  if (diagram_dimension_px > MAX_DIAGRAM_DIMENSION) {
    std::cerr << "[ERROR] buffer size " << m_db_size << " bytes is too big for dumping usage diagram\n";
    return false;
  }

  constexpr unsigned int NUM_CHANNELS = 3;
  constexpr uint8_t RGB_OVERHEAD[NUM_CHANNELS] = {0x90, 0xD5, 0xFF};
  constexpr uint8_t RGB_DATA[NUM_CHANNELS] = {0x2E, 0x6F, 0x40};
  constexpr uint8_t RGB_UNUSED[NUM_CHANNELS] = {0x00, 0x00, 0x00};

  int num_cols = static_cast<int>(diagram_dimension_px);
  int num_rows = static_cast<int>(std::ceil(num_pixels / num_cols));
  std::vector<uint8_t> diagram_image(NUM_CHANNELS * num_rows * num_cols, 0xCC);

  // account for header
  for (unsigned int i = 0; i < sizeof(BufferHeader) / NUM_BYTES_PER_PIXEL; ++i) {
    diagram_image[i * NUM_CHANNELS + 0] = RGB_OVERHEAD[0];
    diagram_image[i * NUM_CHANNELS + 1] = RGB_OVERHEAD[1];
    diagram_image[i * NUM_CHANNELS + 2] = RGB_OVERHEAD[2];
  }

  // account for free blocks
  FileByteOffset curr_free_block_offset = m_header->next_free_block_offset;
  while (curr_free_block_offset != NULL_OFFSET) {
    unsigned int pixel_offset = curr_free_block_offset / NUM_BYTES_PER_PIXEL;
    for (unsigned int i = 0; i < sizeof(Block) / NUM_BYTES_PER_PIXEL; ++i) {
      diagram_image[(pixel_offset + i) * NUM_CHANNELS + 0] = RGB_OVERHEAD[0];
      diagram_image[(pixel_offset + i) * NUM_CHANNELS + 1] = RGB_OVERHEAD[1];
      diagram_image[(pixel_offset + i) * NUM_CHANNELS + 2] = RGB_OVERHEAD[2];
    }

    pixel_offset = (curr_free_block_offset + sizeof(Block)) / NUM_BYTES_PER_PIXEL;
    const Block * curr_block = reinterpret_cast<Block *>(to_pointer(curr_free_block_offset));
    for (unsigned int i = 0; i < curr_block->data_size / NUM_BYTES_PER_PIXEL; ++i) {
      diagram_image[(pixel_offset + i) * NUM_CHANNELS + 0] = RGB_UNUSED[0];
      diagram_image[(pixel_offset + i) * NUM_CHANNELS + 1] = RGB_UNUSED[1];
      diagram_image[(pixel_offset + i) * NUM_CHANNELS + 2] = RGB_UNUSED[2];
    }

    curr_free_block_offset = curr_block->next_block_offset;
  }

  // account for used blocks
  FileByteOffset curr_used_block_offset = m_header->next_used_block_offset;
  while (curr_used_block_offset != NULL_OFFSET) {
    unsigned int pixel_offset = curr_used_block_offset / NUM_BYTES_PER_PIXEL;
    for (unsigned int i = 0; i < sizeof(Block) / NUM_BYTES_PER_PIXEL; ++i) {
      diagram_image[(pixel_offset + i) * NUM_CHANNELS + 0] = RGB_OVERHEAD[0];
      diagram_image[(pixel_offset + i) * NUM_CHANNELS + 1] = RGB_OVERHEAD[1];
      diagram_image[(pixel_offset + i) * NUM_CHANNELS + 2] = RGB_OVERHEAD[2];
    }

    pixel_offset = (curr_used_block_offset + sizeof(Block)) / NUM_BYTES_PER_PIXEL;
    const Block * curr_block = reinterpret_cast<Block *>(to_pointer(curr_used_block_offset));
    for (unsigned int i = 0; i < curr_block->data_size / NUM_BYTES_PER_PIXEL; ++i) {
      diagram_image[(pixel_offset + i) * NUM_CHANNELS + 0] = RGB_DATA[0];
      diagram_image[(pixel_offset + i) * NUM_CHANNELS + 1] = RGB_DATA[1];
      diagram_image[(pixel_offset + i) * NUM_CHANNELS + 2] = RGB_DATA[2];
    }

    curr_used_block_offset = curr_block->next_block_offset;
  }

  fpng::fpng_init();
  return fpng::fpng_encode_image_to_file(filename.c_str(), diagram_image.data(), num_cols, num_rows, NUM_CHANNELS);
}
