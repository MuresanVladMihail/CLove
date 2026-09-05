/*
#   clove
#
#   Copyright (C) 2016-2020 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/
#pragma once

#include <stdint.h>

#include "svg.h"

typedef struct {
    unsigned char r, g, b, a;
} pixel;

typedef struct {
    int w;
    int h;
    int x;
    int y;
    unsigned int c;
    const char *path;
    unsigned char *surface;
    pixel *pixels;
    /* Vector art only (see image_ImageData_new_with_svg): the drawing the
     * pixels above were rasterized from, and the scale used for it. `svg` is
     * NULL for ordinary raster images, and also once a graphics_Image has
     * taken the document over (image_ImageData_releaseVector). */
    svg_Document *svg;
    float svg_scale;
} image_ImageData;

char const *image_error(void);

void image_ImageData_new_with_size(image_ImageData *dst, int width, int height,
                                   int num_channels);

void image_ImageData_new_with_surface(image_ImageData *dst, unsigned char *surface,
                                      unsigned int width, unsigned int height, unsigned int num_channels);

void image_ImageData_new_with_filename(image_ImageData *dst, char const *filename);

/* Loads vector art and rasterizes it at `scale` (1.0 = the size the drawing
 * was authored at). image_ImageData_new_with_filename() calls this by itself
 * for ".svg" files, so scripts never have to care. */
void image_ImageData_new_with_svg(image_ImageData *dst, char const *filename, float scale);

int image_ImageData_isVector(image_ImageData *dst);

float image_ImageData_getVectorScale(image_ImageData *dst);

/* Re-rasterizes vector art at a new scale, replacing the pixels. Returns 0 for
 * raster images or when the rasterization failed (the old pixels are kept). */
int image_ImageData_setVectorScale(image_ImageData *dst, float scale);

/* Hands the vector document over to the caller, who is then responsible for
 * freeing it. The pixels stay behind untouched. */
svg_Document *image_ImageData_releaseVector(image_ImageData *dst);

int image_ImageData_getWidth(image_ImageData *dst);

int image_ImageData_getHeight(image_ImageData *dst);

int image_ImageData_getChannels(image_ImageData *dst);

pixel image_ImageData_getPixel(image_ImageData *dst, int x, int y);

int image_ImageData_setPixel(image_ImageData *dst, int x, int y, pixel p);

//named encode in Love
int image_ImageData_save(image_ImageData *dst, const char *format, const char *filename);

const char *image_ImageData_getPath(image_ImageData *dst);

unsigned char *image_ImageData_getSurface(image_ImageData *dst);

void image_ImageData_setSurface(image_ImageData *dst, unsigned char *data);

void image_ImageData_free(image_ImageData *data);

void image_init(void);
