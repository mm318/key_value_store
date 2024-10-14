#include <cstdio>
#include <iostream>
#include <array>
#include <utility>
#include <cmath>

#include "file_backed_buffer.hpp"
#include "font8x8_basic.h"
#include "fpng.h"

static constexpr int MAX_DIAGRAM_DIMENSION = 12000;  // output diagram should be no bigger than 12000 px by 12000 px
static constexpr int NUM_BYTES_PER_PIXEL = 4;        // number of bytes in db buffer represented by one pixel in diagram
static constexpr int MAX_ANNOTATION_LEN = 64;

static constexpr unsigned int NUM_CHANNELS = 3;
static constexpr std::array<uint8_t, NUM_CHANNELS> RGB_ANNOTATION = {0xFF, 0xFF, 0xFF};
static constexpr std::array<uint8_t, NUM_CHANNELS> RGB_OVERHEAD = {0x90, 0xD5, 0xFF};
static constexpr std::array<uint8_t, NUM_CHANNELS> RGB_DATA = {0x2E, 0x6F, 0x40};
static constexpr std::array<uint8_t, NUM_CHANNELS> RGB_UNUSED = {0x55, 0x55, 0x55};

static std::once_flag init_fpng;

class Image
{
public:
  Image(const int num_rows, const int num_cols) :
    m_num_rows(num_rows),
    m_num_cols(num_cols),
    m_image(NUM_CHANNELS * num_rows * num_cols, 0) {}

  int xy_to_idx(const int x, const int y) const
  {
    if (x < 0 || x >= m_num_cols) {
      return -1;
    }
    if (y < 0 || y >= m_num_rows) {
      return -1;
    }
    return y * m_num_cols + x;
  }

  std::pair<int, int> idx_to_xy(const int idx) const
  {
    if (idx < 0 || idx >= m_num_rows * m_num_cols) {
      return std::make_pair(-1, -1);
    }
    return std::make_pair(idx % m_num_cols, idx / m_num_cols);
  }

  void set_pixel(const int pixel_idx, const std::array<uint8_t, NUM_CHANNELS> & color)
  {
    if (pixel_idx < 0) {
      return;
    }
    m_image[pixel_idx * NUM_CHANNELS + 0] = color[0];
    m_image[pixel_idx * NUM_CHANNELS + 1] = color[1];
    m_image[pixel_idx * NUM_CHANNELS + 2] = color[2];
  }

  void draw_text(const int x_start, const int y_start, const char * text, const std::array<uint8_t, NUM_CHANNELS> & color)
  {
    constexpr int CHARACTER_HEIGHT = 8;
    constexpr int CHARACTER_WIDTH = 8;
    int c;
    int i = 0;
    while ((c = text[i]) != '\0') {
      for (int y = 0; y < CHARACTER_HEIGHT; ++y) {
        for (int x = 0; x < CHARACTER_WIDTH; ++x) {
          bool fill = font8x8_basic[c][y] & (1 << x);
          if (fill) {
            set_pixel(xy_to_idx(x_start + CHARACTER_WIDTH * i + x, y_start + y), color);
          }
        }
      }
      ++i;
    }
  }

  const void * raw_data() const { return m_image.data(); }

private:
  int m_num_rows;
  int m_num_cols;
  std::vector<uint8_t> m_image;
};


bool FileBackedBuffer::dump_usage(const std::string & filename) const
{
  std::unique_lock<std::mutex> read_lock(m_mutex);

  float num_pixels = std::ceil(static_cast<float>(m_db_size) / static_cast<float>(NUM_BYTES_PER_PIXEL));
  float diagram_dimension_px = std::ceil(std::sqrt(num_pixels));
  if (diagram_dimension_px > MAX_DIAGRAM_DIMENSION) {
    std::cerr << "[ERROR] buffer size " << m_db_size << " bytes is too big for dumping usage diagram\n";
    return false;
  }

  int num_cols = static_cast<int>(diagram_dimension_px);
  int num_rows = static_cast<int>(std::ceil(num_pixels / num_cols));
  Image diagram_image(num_rows, num_cols);

  // plot space occupied by header
  for (unsigned int i = 0; i < sizeof(BufferHeader) / NUM_BYTES_PER_PIXEL; ++i) {
    diagram_image.set_pixel(i, RGB_OVERHEAD);
  }

  // plot space occupied by used blocks
  FileByteOffset curr_used_block_offset = m_header->next_used_block_offset;
  while (curr_used_block_offset != NULL_OFFSET) {
    unsigned int pixel_offset = curr_used_block_offset / NUM_BYTES_PER_PIXEL;
    for (unsigned int i = 0; i < sizeof(Block) / NUM_BYTES_PER_PIXEL; ++i) {
      diagram_image.set_pixel(pixel_offset + i, RGB_OVERHEAD);
    }

    pixel_offset = (curr_used_block_offset + sizeof(Block)) / NUM_BYTES_PER_PIXEL;
    const Block * curr_block = reinterpret_cast<Block *>(to_pointer(curr_used_block_offset));
    for (unsigned int i = 0; i < curr_block->data_size / NUM_BYTES_PER_PIXEL; ++i) {
      diagram_image.set_pixel(pixel_offset + i, RGB_DATA);
    }

    curr_used_block_offset = curr_block->next_block_offset;
  }

  // plot space occupied by free blocks
  FileByteOffset curr_free_block_offset = m_header->next_free_block_offset;
  while (curr_free_block_offset != NULL_OFFSET) {
    unsigned int pixel_offset = curr_free_block_offset / NUM_BYTES_PER_PIXEL;
    for (unsigned int i = 0; i < sizeof(Block) / NUM_BYTES_PER_PIXEL; ++i) {
      diagram_image.set_pixel(pixel_offset + i, RGB_OVERHEAD);
    }

    pixel_offset = (curr_free_block_offset + sizeof(Block)) / NUM_BYTES_PER_PIXEL;
    const Block * curr_block = reinterpret_cast<Block *>(to_pointer(curr_free_block_offset));
    for (unsigned int i = 0; i < curr_block->data_size / NUM_BYTES_PER_PIXEL; ++i) {
      diagram_image.set_pixel(pixel_offset + i, RGB_UNUSED);
    }

    curr_free_block_offset = curr_block->next_block_offset;
  }

  char annotation[MAX_ANNOTATION_LEN];

  // annotate used blocks
  curr_used_block_offset = m_header->next_used_block_offset;
  while (curr_used_block_offset != NULL_OFFSET) {
    unsigned int pixel_offset = (curr_used_block_offset + sizeof(Block)) / NUM_BYTES_PER_PIXEL;
    const Block * curr_block = reinterpret_cast<Block *>(to_pointer(curr_used_block_offset));

    const auto [x, y] = diagram_image.idx_to_xy(pixel_offset + 1);
    snprintf(annotation, MAX_ANNOTATION_LEN, "U:%luB", curr_block->data_size);
    diagram_image.draw_text(x, y, annotation, RGB_ANNOTATION);

    curr_used_block_offset = curr_block->next_block_offset;
  }

  // annotate free blocks
  curr_free_block_offset = m_header->next_free_block_offset;
  while (curr_free_block_offset != NULL_OFFSET) {
    unsigned int pixel_offset = (curr_free_block_offset + sizeof(Block)) / NUM_BYTES_PER_PIXEL;
    const Block * curr_block = reinterpret_cast<Block *>(to_pointer(curr_free_block_offset));

    const auto [x, y] = diagram_image.idx_to_xy(pixel_offset + 1);
    snprintf(annotation, MAX_ANNOTATION_LEN, "F:%luB", curr_block->data_size);
    diagram_image.draw_text(x, y, annotation, RGB_ANNOTATION);

    curr_free_block_offset = curr_block->next_block_offset;
  }

  std::call_once(init_fpng, fpng::fpng_init);
  return fpng::fpng_encode_image_to_file(filename.c_str(), diagram_image.raw_data(), num_cols, num_rows, NUM_CHANNELS);
}
