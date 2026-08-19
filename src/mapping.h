#ifndef MAPPING_H
#define MAPPING_H

#define GRID_COLS 30
#define GRID_ROWS 4
#define LED_COUNT (GRID_COLS * GRID_ROWS)

/*
 * The box is wired as a single serpentine strip of 120 LEDs, no filler
 * LEDs between rows: LED row 0 starts at the right edge and runs
 * right-to-left, row 1 runs left-to-right, row 2 right-to-left, row 3
 * left-to-right. LED index increases continuously from 0 to 119.
 *
 * image_row follows normal image conventions (0 = top row of the frame).
 * Confirmed against the physical box with the R/G/B/R test pattern: image
 * row 0 (top of frame) is LED row 0, i.e. no vertical flip -- despite the
 * strip being described as wired bottom-to-top, LED row 0 reads as the
 * visual top of the box, not the bottom.
 */
int led_index_for_pixel(int col, int image_row);

#endif
