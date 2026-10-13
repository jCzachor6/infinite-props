#pragma once

#include <stdint.h>
#include <string.h>

/* Exact rational Porter-Duff pins for source-over-panels.ips, seed 26.
 * No runtime compositor is used to calculate the expected host pixels. */
static void source_over_panel_pixel(uint32_t x, uint32_t y, uint8_t color[4]) {
    static const uint8_t empty[] = {0,0,0,0}, background[] = {36,48,64,255};
    static const uint8_t base[] = {112,160,216,128}, opaque_base[] = {74,104,140,255};
    static const uint8_t highlight[] = {142,161,183,255}, shadow[] = {59,96,136,192};
    memcpy(color, y < 12 ? background : empty, 4);
    if (((x >= 2 && x < 14) || (x >= 18 && x < 30)) && y >= 4 && y < 20) {
        memcpy(color, y < 12 ? opaque_base : base, 4);
        if ((x >= 3 && x < 13) || (x >= 19 && x < 29)) {
            if (y >= 5 && y < 7) memcpy(color, highlight, 4);
            if (y >= 17 && y < 19) memcpy(color, shadow, 4);
        }
    }
}
