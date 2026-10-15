#pragma once

#include <stdint.h>
#include <string.h>

/* Independent coverage/min/complement pins for mask-algebra.ips. Each row
 * is equal. The first columns retain alpha during legacy clipped painting. */
static void mask_algebra_pixel(uint32_t x, uint8_t color[4]) {
    static const uint8_t mixed[] = {166,178,163,128}, partial[] = {94,172,223,127};
    static const uint8_t blue[] = {94,172,223,255}, amber[] = {239,186,104,255};
    memcpy(color, x < 2 ? mixed : x == 2 ? partial : x < 5 ? blue : amber, 4);
}
