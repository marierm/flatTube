#ifndef MAPPING_H
#define MAPPING_H

#define GRID_COLS 30
#define GRID_ROWS 4
#define LED_COUNT (GRID_COLS * GRID_ROWS)

/*
 * The box is wired as a single serpentine strip of 120 LEDs, no filler
 * LEDs between rows: physical row 0 (bottom) starts at the right edge and
 * runs right-to-left, row 1 runs left-to-right, row 2 right-to-left, row 3
 * (top) left-to-right. LED index increases continuously from 0 at
 * bottom-right to 119 at the top.
 *
 * image_row follows normal image conventions (0 = top row of the frame),
 * so it is flipped to the physical row before applying the serpentine
 * direction.
 */
int led_index_for_pixel(int col, int image_row);

#endif
