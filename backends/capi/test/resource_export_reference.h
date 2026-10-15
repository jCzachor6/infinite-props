#pragma once
#include <stdint.h>
#include <string.h>

/* Independent integer D4 and legacy coverage pins for resource-exports.ips.
 * The two transforms compose to anti-transpose, at raster origin (6,4). */
static void resource_export_pixel(uint32_t x, uint32_t y, uint8_t color[4]) {
    static const uint8_t empty[4]={0,0,0,0}, partial[4]={17,34,51,128};
    static const uint8_t clipped[4]={136,16,25,128}, marker[4]={85,102,119,255};
    memcpy(color, x==7 && y==6 ? clipped : x==6 && y==4 ? marker :
        x>=6 && x<8 && y>=4 && y<7 ? partial : empty, 4);
}
