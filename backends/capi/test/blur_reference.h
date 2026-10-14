#pragma once
#include <stdint.h>

/* Independent sigma=1 impulse pins, alpha 128, constant vertically. The top
 * row blurs RGBA, the bottom blurs coverage and paints white through a clip. */
static void gaussian_blur_pixel(uint32_t x, uint32_t y, uint8_t color[4]) {
    static const uint8_t alpha[7] = {1,7,31,51,31,7,1};
    color[0] = 255; color[1] = y == 0 ? 80 : 255;
    color[2] = y == 0 ? 40 : 255; color[3] = alpha[x];
}
