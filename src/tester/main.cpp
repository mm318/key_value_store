#include <iostream>
#include "file_backed_buffer.hpp"


int main(const int argc, const char * argv[])
{
  if (argc < 2) {
    std::cerr << "usage: " << argv[0] << " <dbfilename>\n";
    return -1;
  }

  FileBackedBuffer buffer(argv[1]);

  return 0;
}
