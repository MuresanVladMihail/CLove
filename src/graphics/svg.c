/*
#   clove
#
#   Copyright (C) 2021 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../include/svg.h"

/* nanosvg wants the color keyword table for documents that spell their colors
 * out ("fill:cornflowerblue"), which Inkscape happily writes. */
#define NANOSVG_ALL_COLOR_KEYWORDS
#define NANOSVG_IMPLEMENTATION
#include "../include/nanosvg.h"

#define NANOSVGRAST_IMPLEMENTATION
#include "../include/nanosvgrast.h"

/* Inkscape's user unit is a CSS pixel at 96 dpi, so "px"/96 reproduces the
 * document exactly as the editor shows it. */
#define SVG_UNITS "px"
#define SVG_DPI 96.0f

/* Used when the caller has no GL_MAX_TEXTURE_SIZE at hand. Kept in step with
 * GRAPHICS_MAX_VECTOR_SIZE in graphics/image.c so a drawing is not first
 * rasterized huge here and then again, smaller, for the GPU. */
#define SVG_DEFAULT_MAX_SIZE 4096

struct svg_Document {
    NSVGimage *image;
    float width;
    float height;
};

/* One rasterizer is reused by every document: it only holds scratch buffers
 * and creating one per rasterization would throw that cache away. */
static NSVGrasterizer *rasterizer = NULL;
static const char *lastError = "";

const char *svg_error(void) {
    return lastError;
}

int svg_isVectorPath(const char *path) {
    if (!path)
        return 0;

    size_t len = strlen(path);
    if (len < 4)
        return 0;

    const char *ext = path + len - 4;
    return (ext[0] == '.'
            && (ext[1] == 's' || ext[1] == 'S')
            && (ext[2] == 'v' || ext[2] == 'V')
            && (ext[3] == 'g' || ext[3] == 'G'));
}

static svg_Document *svg_Document_new_with_NSVGimage(NSVGimage *image) {
    if (!image) {
        lastError = "could not parse SVG document";
        return NULL;
    }

    /* A drawing with no width/height and no viewBox parses fine but has
     * nothing to rasterize into - refuse it here instead of handing out a
     * 0x0 texture later on. */
    if (image->width < 1.0f || image->height < 1.0f) {
        nsvgDelete(image);
        lastError = "SVG document has no usable width/height (missing width, height or viewBox?)";
        return NULL;
    }

    svg_Document *doc = malloc(sizeof(svg_Document));
    if (!doc) {
        nsvgDelete(image);
        lastError = "out of memory";
        return NULL;
    }

    doc->image = image;
    doc->width = image->width;
    doc->height = image->height;
    return doc;
}

svg_Document *svg_Document_new_with_filename(const char *filename) {
    if (!filename) {
        lastError = "no filename given";
        return NULL;
    }

    lastError = "";
    return svg_Document_new_with_NSVGimage(nsvgParseFromFile(filename, SVG_UNITS, SVG_DPI));
}

svg_Document *svg_Document_new_with_memory(const char *text, size_t len) {
    if (!text) {
        lastError = "no SVG data given";
        return NULL;
    }

    /* nsvgParse() chews through the buffer it is given, so it gets a copy. */
    char *copy = malloc(len + 1);
    if (!copy) {
        lastError = "out of memory";
        return NULL;
    }
    memcpy(copy, text, len);
    copy[len] = '\0';

    lastError = "";
    NSVGimage *image = nsvgParse(copy, SVG_UNITS, SVG_DPI);
    free(copy);

    return svg_Document_new_with_NSVGimage(image);
}

float svg_Document_getWidth(const svg_Document *doc) {
    return doc ? doc->width : 0.0f;
}

float svg_Document_getHeight(const svg_Document *doc) {
    return doc ? doc->height : 0.0f;
}

float svg_Document_clampScale(const svg_Document *doc, float scale, int maxTextureSize) {
    if (!doc)
        return 1.0f;

    if (maxTextureSize <= 0)
        maxTextureSize = SVG_DEFAULT_MAX_SIZE;

    /* NaN and infinities compare false here, which is exactly what we want. */
    if (!(scale > 0.0f) || !(scale < 1.0e6f))
        scale = 1.0f;

    float largest = doc->width > doc->height ? doc->width : doc->height;
    if (largest > 0.0f) {
        float maxScale = (float) maxTextureSize / largest;
        if (scale > maxScale)
            scale = maxScale;
    }

    /* Never rasterize away to nothing: one pixel per axis is the floor. */
    float minScale = 1.0f / (doc->width > doc->height ? doc->width : doc->height);
    if (scale < minScale)
        scale = minScale;

    return scale;
}

unsigned char *svg_Document_rasterize(const svg_Document *doc, float scale,
                                      int *out_w, int *out_h) {
    if (!doc) {
        lastError = "no SVG document given";
        return NULL;
    }

    scale = svg_Document_clampScale(doc, scale, 0);

    int w = (int) ceilf(doc->width * scale);
    int h = (int) ceilf(doc->height * scale);
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    if (!rasterizer) {
        rasterizer = nsvgCreateRasterizer();
        if (!rasterizer) {
            lastError = "could not create the SVG rasterizer";
            return NULL;
        }
    }

    unsigned char *pixels = malloc((size_t) w * (size_t) h * 4);
    if (!pixels) {
        lastError = "out of memory while rasterizing SVG";
        return NULL;
    }

    /* nsvgRasterize() clears the destination itself before compositing. */
    nsvgRasterize(rasterizer, doc->image, 0.0f, 0.0f, scale, pixels, w, h, w * 4);

    if (out_w) *out_w = w;
    if (out_h) *out_h = h;

    lastError = "";
    return pixels;
}

void svg_Document_free(svg_Document *doc) {
    if (!doc)
        return;

    if (doc->image)
        nsvgDelete(doc->image);
    free(doc);
}

void svg_shutdown(void) {
    if (rasterizer) {
        nsvgDeleteRasterizer(rasterizer);
        rasterizer = NULL;
    }
}
