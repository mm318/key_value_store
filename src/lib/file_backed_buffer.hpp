#ifndef _FILE_BACKED_BUFFER_HPP_
#define _FILE_BACKED_BUFFER_HPP_

#include <cstddef>
#include <cstdint>


class FileBackedBuffer
{
public:
  FileBackedBuffer(const char * filename);
  ~FileBackedBuffer();

  uint8_t * alloc(const size_t alloc_size);

  void free(uint8_t * pointer);

private:
  int m_fd;
  int m_db_size;
  void * m_base;
};

#endif  // _FILE_BACKED_BUFFER_HPP_
