/*
 * TinyGL Ordered Dithering Implementation
 *
 * Global state and initialization for dithering
 *
 * Copyright (c) 2025 HelixScreen Project
 * License: MIT (compatible with TinyGL's license)
 */

#include "zdither.h"
#include "../include/GL/gl.h"

#if TGL_FEATURE_DITHERING

/* Global dithering enable flag */
int tgl_dithering_enabled = 0;  /* Disabled by default - dithering is currently broken */

/* GL API function to enable/disable dithering */
void glSetDithering(GLboolean enabled) {
    tgl_dithering_enabled = enabled ? 1 : 0;
}

/* GL API function to get dithering state */
GLboolean glGetDithering(void) {
    return tgl_dithering_enabled ? GL_TRUE : GL_FALSE;
}

#else

/* Stub implementations when dithering is disabled at compile time */
void glSetDithering(GLboolean enabled) {
    (void)enabled;  /* Unused */
}

GLboolean glGetDithering(void) {
    return GL_FALSE;
}

#endif
