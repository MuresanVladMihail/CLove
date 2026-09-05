/*
#   clove
#
#   Copyright (C) 2026 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/
#pragma once

#include <stddef.h>

/*
 * Vector art (SVG) support.
 *
 * A svg_Document is the parsed, still-resolution-independent drawing. It is
 * rasterized on demand into a plain RGBA8 buffer (straight, non-premultiplied
 * alpha - the same layout stb_image hands back for a PNG), so everything
 * downstream (image_ImageData, graphics_Image, the batches, ...) keeps working
 * on ordinary pixels.
 *
 * Keeping the document around after the first rasterization is what makes the
 * art actually vectorial: graphics_Image re-rasterizes it when it is drawn
 * bigger than the texture it currently holds, instead of magnifying pixels.
 *
 * Files exported by Inkscape (plain or "Inkscape SVG") load as they are; see
 * SKILLS.md for the handful of SVG features the parser ignores (text has to be
 * converted to paths, clip paths and filters are not applied).
 */
typedef struct svg_Document svg_Document;

/* True for paths that should be loaded as vector art (".svg", any case). */
int svg_isVectorPath(const char *path);

/* Both return NULL on failure; svg_error() then describes what went wrong.
 * The memory buffer is not modified and does not have to stay alive. */
svg_Document *svg_Document_new_with_filename(const char *filename);

svg_Document *svg_Document_new_with_memory(const char *text, size_t len);

/* Intrinsic size of the drawing, in pixels (96 dpi user units). */
float svg_Document_getWidth(const svg_Document *doc);

float svg_Document_getHeight(const svg_Document *doc);

/* Largest scale that still fits inside maxTextureSize; also clamps scales that
 * are zero, negative or not finite back into a usable range. */
float svg_Document_clampScale(const svg_Document *doc, float scale, int maxTextureSize);

/* Rasterizes at 'scale' (1.0 = intrinsic size) into a freshly malloc'd RGBA8
 * buffer of *out_w * *out_h * 4 bytes; the caller owns it (free()).
 * Returns NULL on failure. */
unsigned char *svg_Document_rasterize(const svg_Document *doc, float scale,
                                      int *out_w, int *out_h);

void svg_Document_free(svg_Document *doc);

/* Reason for the last failure, never NULL. */
const char *svg_error(void);

/* Releases the shared rasterizer. Safe to call more than once; a later
 * rasterization just builds a new one. */
void svg_shutdown(void);
