/*
 * TinyGL Ordered Dithering Implementation
 *
 * Reduces color banding artifacts by adding spatially-distributed noise
 * before quantization. Uses Bayer matrices for minimal computational overhead.
 *
 * Copyright (c) 2025 HelixScreen Project
 * License: MIT (compatible with TinyGL's license)
 */

#ifndef _tgl_zdither_h_
#define _tgl_zdither_h_

#include "../include/zfeatures.h"
#include "../include/zbuffer.h"

/* Enable dithering by default - can be disabled at compile time */
#ifndef TGL_FEATURE_DITHERING
#define TGL_FEATURE_DITHERING 1
#endif

#if TGL_FEATURE_DITHERING

/* 4x4 Bayer ordered dithering matrix (normalized to 0-15) */
static const unsigned char bayer_matrix_4x4[4][4] = {
    { 0,  8,  2, 10},
    {12,  4, 14,  6},
    { 3, 11,  1,  9},
    {15,  7, 13,  5}
};

/* 8x8 Bayer matrix for higher quality (normalized to 0-63) */
static const unsigned char bayer_matrix_8x8[8][8] = {
    { 0, 32,  8, 40,  2, 34, 10, 42},
    {48, 16, 56, 24, 50, 18, 58, 26},
    {12, 44,  4, 36, 14, 46,  6, 38},
    {60, 28, 52, 20, 62, 30, 54, 22},
    { 3, 35, 11, 43,  1, 33,  9, 41},
    {51, 19, 59, 27, 49, 17, 57, 25},
    {15, 47,  7, 39, 13, 45,  5, 37},
    {63, 31, 55, 23, 61, 29, 53, 21}
};

/* Select matrix size - 4x4 is faster, 8x8 is higher quality */
#ifndef TGL_DITHER_SIZE
#define TGL_DITHER_SIZE 4
#endif

#if TGL_DITHER_SIZE == 8
    #define DITHER_MATRIX bayer_matrix_8x8
    #define DITHER_MASK 7
    #define DITHER_SCALE 4  /* 64/16 = 4, to scale to 4-bit threshold */
#else
    #define DITHER_MATRIX bayer_matrix_4x4
    #define DITHER_MASK 3
    #define DITHER_SCALE 1  /* 16/16 = 1, already 4-bit threshold */
#endif

/* Dithering strength - adjust based on output bit depth */
#if TGL_FEATURE_RENDER_BITS == 32
    /* 8-bit per channel output */
    #define DITHER_AMPLITUDE 16  /* Adds ±8 levels of noise for visibility */
#elif TGL_FEATURE_RENDER_BITS == 16
    /* 5/6/5 bit output needs stronger dithering */
    #define DITHER_AMPLITUDE 32  /* Adds ±16 levels of noise */
#endif

/* Apply ordered dithering to a color component */
static inline int dither_component(int value, int x, int y) {
    int threshold = DITHER_MATRIX[y & DITHER_MASK][x & DITHER_MASK] / DITHER_SCALE;
    int dither = (threshold - 8) * DITHER_AMPLITUDE / 8;

    /* Add dither and clamp to valid range */
    value += dither;
    if (value < 0) value = 0;
    if (value > 255) value = 255;

    return value;
}

/* Dithered version of RGB_TO_PIXEL for 32-bit mode */
#if TGL_FEATURE_RENDER_BITS == 32

#define RGB_TO_PIXEL_DITHERED(r, g, b, x, y) \
    ((dither_component(((r) >> 16) & 0xff, x, y) << 16) | \
     (dither_component(((g) >> 16) & 0xff, x, y) << 8) | \
     (dither_component(((b) >> 16) & 0xff, x, y)))

/* Use the original RGB_TO_PIXEL from zbuffer.h for non-dithered */
#define RGB_TO_PIXEL_NODITHER(r,g,b) RGB_TO_PIXEL(r,g,b)

#elif TGL_FEATURE_RENDER_BITS == 16
/* Dithered version for 16-bit 565 mode */
#define RGB_TO_PIXEL_DITHERED(r, g, b, x, y) \
    ((dither_component(((r) >> 16) & 0xff, x, y) & 0xF8) << 8) | \
    ((dither_component(((g) >> 16) & 0xff, x, y) & 0xFC) << 3) | \
    ((dither_component(((b) >> 16) & 0xff, x, y) & 0xF8) >> 3)

/* Use the original RGB_TO_PIXEL from zbuffer.h for non-dithered */
#define RGB_TO_PIXEL_NODITHER(r,g,b) RGB_TO_PIXEL(r,g,b)
#endif

/* Global dithering enable flag (can be toggled at runtime) */
extern int tgl_dithering_enabled;

/* Initialize dithering (sets default state) */
static inline void tgl_dither_init(void) {
    tgl_dithering_enabled = 0;  /* Disabled by default - dithering is currently broken */
}

/* Enable/disable dithering at runtime */
static inline void tgl_set_dithering(int enabled) {
    tgl_dithering_enabled = enabled;
}

/* Macro to select dithered or non-dithered path based on runtime flag */
#define RGB_TO_PIXEL_COND(r, g, b, x, y) \
    (tgl_dithering_enabled ? \
        RGB_TO_PIXEL_DITHERED(r, g, b, x, y) : \
        RGB_TO_PIXEL_NODITHER(r, g, b))

#else /* TGL_FEATURE_DITHERING == 0 */

/* Dithering disabled at compile time - use original macros */
#define RGB_TO_PIXEL_DITHERED(r, g, b, x, y) RGB_TO_PIXEL(r, g, b)
#define RGB_TO_PIXEL_NODITHER(r, g, b) RGB_TO_PIXEL(r, g, b)
#define RGB_TO_PIXEL_COND(r, g, b, x, y) RGB_TO_PIXEL(r, g, b)
#define tgl_dither_init() ((void)0)
#define tgl_set_dithering(enabled) ((void)0)

#endif /* TGL_FEATURE_DITHERING */

#endif /* _tgl_zdither_h_ */
