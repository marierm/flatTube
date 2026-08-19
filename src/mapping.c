#include "mapping.h"

int led_index_for_pixel(int col, int image_row)
{
  int phys_row = (GRID_ROWS - 1) - image_row;
  int base = phys_row * GRID_COLS;
  int even_row = (phys_row % 2) == 0;
  int offset = even_row ? (GRID_COLS - 1 - col) : col;
  return base + offset;
}
