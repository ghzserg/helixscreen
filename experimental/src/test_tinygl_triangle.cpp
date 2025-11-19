// Copyright (c) 2025 Preston Brown <pbrown@brown-house.net>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// TinyGL Hello Triangle Test
// Renders a simple colored triangle and saves it to a PPM file

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef ENABLE_TINYGL_3D

extern "C" {
#include <GL/gl.h>
#include <zbuffer.h>
}

// Helper function to write PPM file (portable pixmap format)
static void write_ppm(const char* filename, int width, int height, unsigned char* rgb_data) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) {
        fprintf(stderr, "Error: Failed to open %s for writing\n", filename);
        return;
    }

    fprintf(fp, "P6\n%d %d\n255\n", width, height);
    fwrite(rgb_data, 1, width * height * 3, fp);
    fclose(fp);

    printf("✓ Wrote %s (%dx%d)\n", filename, width, height);
}

int main(int argc, char** argv) {
    const int WIDTH = 640;
    const int HEIGHT = 480;

    printf("TinyGL Hello Triangle Test\n");
    printf("═══════════════════════════\n\n");

    // Allocate framebuffer
    unsigned int* framebuffer = (unsigned int*)calloc(WIDTH * HEIGHT, sizeof(unsigned int));
    if (!framebuffer) {
        fprintf(stderr, "Error: Failed to allocate framebuffer\n");
        return 1;
    }

    // Initialize TinyGL with RGB565 mode
    ZBuffer* zb = nullptr;
    if (TGL_FEATURE_RENDER_BITS == 32) {
        zb = ZB_open(WIDTH, HEIGHT, ZB_MODE_RGBA, 0);
        printf("→ Using 32-bit RGBA mode\n");
    } else {
        zb = ZB_open(WIDTH, HEIGHT, ZB_MODE_5R6G5B, 0);
        printf("→ Using 16-bit RGB565 mode\n");
    }

    if (!zb) {
        fprintf(stderr, "Error: ZB_open failed\n");
        free(framebuffer);
        return 1;
    }

    printf("→ Framebuffer size: %dx%d\n", WIDTH, HEIGHT);

    // Initialize GL context
    glInit(zb);

    // Set up viewport
    glViewport(0, 0, WIDTH, HEIGHT);

    // Clear to dark gray background
    glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    printf("→ Rendering colored triangle...\n");

    // Draw a simple colored triangle
    glBegin(GL_TRIANGLES);

    // Red vertex (top)
    glColor3f(1.0f, 0.0f, 0.0f);
    glVertex3f(0.0f, 0.6f, 0.0f);

    // Green vertex (bottom left)
    glColor3f(0.0f, 1.0f, 0.0f);
    glVertex3f(-0.6f, -0.6f, 0.0f);

    // Blue vertex (bottom right)
    glColor3f(0.0f, 0.0f, 1.0f);
    glVertex3f(0.6f, -0.6f, 0.0f);

    glEnd();

    // Copy framebuffer from TinyGL
    ZB_copyFrameBuffer(zb, framebuffer, WIDTH * sizeof(unsigned int));

    // Convert to RGB24 for PPM output
    unsigned char* rgb_data = (unsigned char*)malloc(WIDTH * HEIGHT * 3);
    if (!rgb_data) {
        fprintf(stderr, "Error: Failed to allocate RGB buffer\n");
        ZB_close(zb);
        glClose();
        free(framebuffer);
        return 1;
    }

    // Extract RGB components
    for (int i = 0; i < WIDTH * HEIGHT; i++) {
        if (TGL_FEATURE_RENDER_BITS == 32) {
            rgb_data[i * 3 + 0] = (framebuffer[i] >> 16) & 0xFF;  // R
            rgb_data[i * 3 + 1] = (framebuffer[i] >> 8) & 0xFF;   // G
            rgb_data[i * 3 + 2] = (framebuffer[i] >> 0) & 0xFF;   // B
        } else {
            // RGB565 extraction
            rgb_data[i * 3 + 0] = GET_RED(framebuffer[i]);
            rgb_data[i * 3 + 1] = GET_GREEN(framebuffer[i]);
            rgb_data[i * 3 + 2] = GET_BLUE(framebuffer[i]);
        }
    }

    // Write PPM file
    const char* output_file = (argc > 1) ? argv[1] : "triangle.ppm";
    write_ppm(output_file, WIDTH, HEIGHT, rgb_data);

    // Cleanup
    free(rgb_data);
    free(framebuffer);
    ZB_close(zb);
    glClose();

    printf("\n✓ Triangle test complete!\n");
    printf("  View with: open %s (macOS) or xdg-open %s (Linux)\n", output_file, output_file);

    return 0;
}

#else

int main(int argc, char** argv) {
    fprintf(stderr, "Error: TinyGL support not compiled in\n");
    fprintf(stderr, "Rebuild with: make ENABLE_TINYGL_3D=yes\n");
    return 1;
}

#endif
