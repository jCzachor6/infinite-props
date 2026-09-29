// The single stb_image implementation for the test binary (the writer
// implementation lives in ipgen_lib). Kept in one translation unit so
// several test files can decode PNGs.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
