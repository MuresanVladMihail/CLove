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
static char errorBuffer[256];

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

/* Counts non-overlapping occurrences of a "<tag" element opener. Good enough to
 * tell a user what is in a drawing we could not draw: it can be fooled by the
 * literal text inside a comment or a string, which only ever makes the message
 * less precise, never wrong about the document being empty. */
static int svg_countElements(const char *text, size_t len, const char *tag) {
    size_t taglen = strlen(tag);
    int count = 0;
    size_t i;

    if (!text || taglen == 0)
        return 0;

    for (i = 0; i + taglen + 1 < len; i++) {
        if (text[i] != '<' || strncmp(text + i + 1, tag, taglen) != 0)
            continue;
        /* "<image" must not also match "<imagefoo". */
        char after = text[i + 1 + taglen];
        if (after == '>' || after == '/' || after == ' ' || after == '\t'
            || after == '\n' || after == '\r') {
            count++;
            i += taglen;
        }
    }
    return count;
}

/* A drawing that parses cleanly but holds nothing we can draw is the worst
 * failure mode there is: the image loads, rasterizes to a fully transparent
 * texture and draws as nothing at all, with no hint as to why. So say what the
 * document actually contained. */
static void svg_describeEmptyDocument(const char *text, size_t len) {
    int images = svg_countElements(text, len, "image");
    int texts  = svg_countElements(text, len, "text");
    int uses   = svg_countElements(text, len, "use");

    if (images > 0) {
        snprintf(errorBuffer, sizeof errorBuffer,
                 "SVG document has nothing to draw: its %d <image> element%s "
                 "(embedded or linked bitmaps) %s not supported. The drawing is "
                 "a bitmap in an SVG wrapper - export it as a .png and load that "
                 "instead.", images, images == 1 ? "" : "s", images == 1 ? "is" : "are");
    } else if (texts > 0) {
        snprintf(errorBuffer, sizeof errorBuffer,
                 "SVG document has nothing to draw: its %d <text> element%s %s "
                 "not supported. Convert the text to paths before saving "
                 "(Inkscape: Path > Object to Path).",
                 texts, texts == 1 ? "" : "s", texts == 1 ? "is" : "are");
    } else if (uses > 0) {
        snprintf(errorBuffer, sizeof errorBuffer,
                 "SVG document has nothing to draw: its %d <use> element%s %s "
                 "not supported. Unlink the clones before saving "
                 "(Inkscape: Edit > Clone > Unlink Clone).",
                 uses, uses == 1 ? "" : "s", uses == 1 ? "is" : "are");
    } else {
        snprintf(errorBuffer, sizeof errorBuffer,
                 "SVG document has nothing to draw: it contains no shapes.");
    }

    lastError = errorBuffer;
}

static svg_Document *svg_Document_new_with_NSVGimage(NSVGimage *image,
                                                     const char *text, size_t len) {
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

    /* Refusing this is deliberate. Handing back a document that rasterizes to
     * nothing looks exactly like a successful load right up until the frame is
     * drawn, and then there is nothing to go on. */
    if (image->shapes == NULL) {
        nsvgDelete(image);
        svg_describeEmptyDocument(text, len);
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
    FILE *fp;
    long size;
    char *text;
    svg_Document *doc;

    if (!filename) {
        lastError = "no filename given";
        return NULL;
    }

    lastError = "";

    /* Read the file here rather than letting nanosvg do it, so the source text
     * is still around to explain an empty document with. */
    fp = fopen(filename, "rb");
    if (!fp) {
        lastError = "could not open the file";
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0 || (size = ftell(fp)) < 0
        || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        lastError = "could not read the file";
        return NULL;
    }

    text = malloc((size_t) size + 1);
    if (!text) {
        fclose(fp);
        lastError = "out of memory";
        return NULL;
    }
    if (fread(text, 1, (size_t) size, fp) != (size_t) size) {
        fclose(fp);
        free(text);
        lastError = "could not read the file";
        return NULL;
    }
    fclose(fp);
    text[size] = '\0';

    doc = svg_Document_new_with_memory(text, (size_t) size);
    free(text);
    return doc;
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

    return svg_Document_new_with_NSVGimage(image, text, len);
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

/* ------------------------------------------------------------------------
 * Contours
 *
 * nanosvg keeps every path as cubic beziers, so an outline that physics or
 * hit-testing can use has to be flattened first. The subdivision test below is
 * the usual one: recurse while the control points sit further than `tolerance`
 * from the chord, which spends points on curvature and none on straight runs.
 * ------------------------------------------------------------------------ */

#define SVG_CONTOUR_DEFAULT_TOLERANCE 0.25f
#define SVG_CONTOUR_MAX_DEPTH 10

typedef struct {
    float *points;
    int count;
    int capacity;
} svg_PointBuffer;

static int svg_PointBuffer_push(svg_PointBuffer *buf, float x, float y) {
    if (buf->count == buf->capacity) {
        int capacity = buf->capacity ? buf->capacity * 2 : 64;
        float *points = realloc(buf->points, (size_t) capacity * 2 * sizeof(float));
        if (!points)
            return 0;
        buf->points = points;
        buf->capacity = capacity;
    }
    buf->points[2 * buf->count] = x;
    buf->points[2 * buf->count + 1] = y;
    ++buf->count;
    return 1;
}

static void svg_flattenCubic(svg_PointBuffer *buf,
                             float x1, float y1, float x2, float y2,
                             float x3, float y3, float x4, float y4,
                             float tolerance, int depth) {
    if (depth > SVG_CONTOUR_MAX_DEPTH) {
        svg_PointBuffer_push(buf, x4, y4);
        return;
    }

    /* distance of the two control points from the chord, squared-compared so
     * there is no square root in the hot path */
    float dx = x4 - x1;
    float dy = y4 - y1;
    float d2 = fabsf((x2 - x4) * dy - (y2 - y4) * dx);
    float d3 = fabsf((x3 - x4) * dy - (y3 - y4) * dx);
    float d = d2 + d3;

    if (d * d <= tolerance * (dx * dx + dy * dy)) {
        svg_PointBuffer_push(buf, x4, y4);
        return;
    }

    float x12 = (x1 + x2) * 0.5f, y12 = (y1 + y2) * 0.5f;
    float x23 = (x2 + x3) * 0.5f, y23 = (y2 + y3) * 0.5f;
    float x34 = (x3 + x4) * 0.5f, y34 = (y3 + y4) * 0.5f;
    float x123 = (x12 + x23) * 0.5f, y123 = (y12 + y23) * 0.5f;
    float x234 = (x23 + x34) * 0.5f, y234 = (y23 + y34) * 0.5f;
    float x1234 = (x123 + x234) * 0.5f, y1234 = (y123 + y234) * 0.5f;

    svg_flattenCubic(buf, x1, y1, x12, y12, x123, y123, x1234, y1234, tolerance, depth + 1);
    svg_flattenCubic(buf, x1234, y1234, x234, y234, x34, y34, x4, y4, tolerance, depth + 1);
}

svg_Contour *svg_Document_getContours(const svg_Document *doc, float tolerance,
                                      int *out_count) {
    if (out_count)
        *out_count = 0;
    if (!doc || !doc->image)
        return NULL;

    if (!(tolerance > 0.0f))
        tolerance = SVG_CONTOUR_DEFAULT_TOLERANCE;

    int capacity = 0;
    int count = 0;
    svg_Contour *contours = NULL;

    for (NSVGshape *shape = doc->image->shapes; shape; shape = shape->next) {
        if (!(shape->flags & NSVG_FLAGS_VISIBLE))
            continue;

        for (NSVGpath *path = shape->paths; path; path = path->next) {
            if (path->npts < 2)
                continue;

            svg_PointBuffer buf = { NULL, 0, 0 };
            if (!svg_PointBuffer_push(&buf, path->pts[0], path->pts[1])) {
                free(buf.points);
                continue;
            }

            for (int i = 0; i + 3 < path->npts; i += 3) {
                const float *p = &path->pts[i * 2];
                svg_flattenCubic(&buf,
                                 p[0], p[1], p[2], p[3],
                                 p[4], p[5], p[6], p[7],
                                 tolerance, 0);
            }

            /* A closed path ends where it started; keeping both copies would
             * give physics a zero-length edge. */
            if (path->closed && buf.count > 1) {
                float dx = buf.points[0] - buf.points[2 * (buf.count - 1)];
                float dy = buf.points[1] - buf.points[2 * (buf.count - 1) + 1];
                if (dx * dx + dy * dy < 1e-6f)
                    --buf.count;
            }

            if (buf.count < 2) {
                free(buf.points);
                continue;
            }

            if (count == capacity) {
                int newCapacity = capacity ? capacity * 2 : 8;
                svg_Contour *grown = realloc(contours, (size_t) newCapacity * sizeof(svg_Contour));
                if (!grown) {
                    free(buf.points);
                    svg_Contours_free(contours, count);
                    lastError = "out of memory while flattening the drawing";
                    return NULL;
                }
                contours = grown;
                capacity = newCapacity;
            }

            contours[count].points = buf.points;
            contours[count].count = buf.count;
            contours[count].closed = path->closed ? 1 : 0;
            ++count;
        }
    }

    if (out_count)
        *out_count = count;
    return contours;
}

void svg_Contours_free(svg_Contour *contours, int count) {
    if (!contours)
        return;
    for (int i = 0; i < count; ++i)
        free(contours[i].points);
    free(contours);
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
