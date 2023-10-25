#include <iostream>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "file_backed_buffer.hpp"


constexpr size_t DATABASE_SIZE_BYTES = 67108864;

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

  std::cout << "[INFO] attempting to create database file\n";
  m_fd = open(filename, O_RDWR | O_CREAT | O_EXCL);
  if (m_fd < 0) {
    if (errno == EEXIST) {
      std::cout << "[INFO] database file already exists\n";
      m_fd = open(filename, O_RDWR);
      if (m_fd < 0) {
        int err = errno;
        std::cerr << "[ERROR] " << strerror(err) << '\n';
      }
    }
  } else {
    new_file = true;
    if (ftruncate(m_fd, DATABASE_SIZE_BYTES) != 0) {
      std::cerr << "[WARN] failed to resize database to " << DATABASE_SIZE_BYTES << " bytes\n";
    }
  }

  m_db_size = get_file_size(m_fd);
  std::cout << "[INFO] database size: " << m_db_size << " bytes\n";

  if (m_fd >= 0) {
    m_base = mmap(NULL, m_db_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, m_fd, 0);
    if (m_base == MAP_FAILED) {
      std::cerr << "[WARN] mmaping " << filename << " failed\n";
      m_base = nullptr;
    }
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
  return nullptr;
}

void FileBackedBuffer::free(uint8_t * pointer)
{

}
