/*
#   clove
#
#   Copyright (C) 2016-2025 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/
#include <math.h>
#include <stdlib.h>

#include "../include/image.h"
#include "../include/imagedata.h"
#include "../include/graphics.h"
#include "../include/vertex.h"
#include "../include/shader.h"
#include "../include/matrixstack.h"
#include "../include/svg.h"
#include "../include/utils.h"

/* Ceiling for automatic re-rasterization of vector art: a 4096x4096 RGBA
 * texture is already 64MB, and a game that really wants more can pin the scale
 * itself with graphics_Image_setVectorScale(). */
#define GRAPHICS_MAX_VECTOR_SIZE 4096

static graphics_Vertex const imageData[] = {
    {{0.0f, 0.0f}, {0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
    {{1.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
    {{0.0f, 1.0f}, {0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
    {{1.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}}
};

static unsigned char const imageIndices[] = {0, 1, 2, 3};

void graphics_image_init(void) {
}

int graphics_Image_getMaxVectorSize(void) {
    static int cached = 0;

    if (cached == 0) {
        GLint value = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &value);
        cached = value > 0 ? (int) value : GRAPHICS_MAX_VECTOR_SIZE;
        if (cached > GRAPHICS_MAX_VECTOR_SIZE)
            cached = GRAPHICS_MAX_VECTOR_SIZE;
    }

    return cached;
}

int graphics_Image_isVector(graphics_Image const *img) {
    return img->svg != NULL;
}

float graphics_Image_getVectorScale(graphics_Image const *img) {
    return img->svg ? img->vectorScale : 1.0f;
}

/* Rasterizes the vector art at `scale` and replaces the texture with it. The
 * texture object itself is reused, so filter, wrap and swizzle settings (which
 * live on the object, not on the level) survive untouched. */
static int graphics_Image_rasterizeVector(graphics_Image *img, float scale) {
    int w, h;
    unsigned char *pixels = svg_Document_rasterize(img->svg, scale, &w, &h);
    if (!pixels) {
        clove_error("Error: could not rasterize vector image: %s\n", svg_error());
        return 0;
    }

    if (img->texID == 0)
        glGenTextures(1, &img->texID);

    glBindTexture(GL_TEXTURE_2D, img->texID);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
#ifndef CLOVE_WEB
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
#else
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
#endif
    free(pixels);

    img->texWidth = w;
    img->texHeight = h;
    img->vectorScale = scale;
    return 1;
}

int graphics_Image_setVectorScale(graphics_Image *img, float scale) {
    if (!img->svg)
        return 0;

    if (scale <= 0.0f) {
        /* Back to following whatever the image is drawn at. */
        img->vectorPinScale = 0.0f;
        return 1;
    }

    scale = svg_Document_clampScale(img->svg, scale, graphics_Image_getMaxVectorSize());
    img->vectorPinScale = scale;

    if (scale == img->vectorScale)
        return 1;

    return graphics_Image_rasterizeVector(img, scale);
}

/* Rasterization is not free, so the scale is quantized to powers of two: a
 * sprite that is being tweened bigger re-rasterizes a handful of times instead
 * of on every single frame. Shrinking only kicks in once the image is drawn at
 * less than a quarter of the texture it holds. */
static void graphics_Image_updateVectorScale(graphics_Image *image, float sx, float sy) {
    if (!image->svg || image->vectorPinScale > 0.0f)
        return;

    /* love_graphics_scale() and friends are part of the size on screen too. */
    mat4x4 const *head = matrixstack_head();
    float globalX = hypotf(head->m[0][0], head->m[0][1]);
    float globalY = hypotf(head->m[1][0], head->m[1][1]);

    float needX = fabsf(sx) * globalX;
    float needY = fabsf(sy) * globalY;
    float need = needX > needY ? needX : needY;

    if (!(need > 0.0f) || !(need < 1.0e6f))
        return;

    if (need <= image->vectorScale && need > image->vectorScale * 0.25f)
        return;

    float target = powf(2.0f, ceilf(log2f(need)));
    target = svg_Document_clampScale(image->svg, target, graphics_Image_getMaxVectorSize());

    if (target == image->vectorScale)
        return;

    if (!graphics_Image_rasterizeVector(image, target)) {
        /* Do not retry every frame: keep what is on the GPU and let the game
         * take over explicitly with graphics_Image_setVectorScale(). */
        image->vectorPinScale = image->vectorScale;
    }
}

static const graphics_Wrap defaultWrap = {
    .verMode = graphics_WrapMode_clamp,
    .horMode = graphics_WrapMode_clamp
};

static const graphics_Filter defaultFilter = {
    .maxAnisotropy = 1.0f,
    .mipmapLodBias = 1.0f,
    .minMode = graphics_FilterMode_linear,
    .magMode = graphics_FilterMode_linear,
    .mipmapMode = graphics_FilterMode_none
};

void graphics_Image_new_with_ImageData(graphics_Image *dst, image_ImageData *data) {
    dst->texID = 0;
    dst->path = NULL;
    dst->width = 0;
    dst->height = 0;
    dst->texWidth = 0;
    dst->texHeight = 0;
    dst->svg = NULL;
    dst->vectorScale = 1.0f;
    dst->vectorPinScale = 0.0f;

    glGenVertexArrays(1, &dst->vao);
    glBindVertexArray(dst->vao);

    glGenBuffers(1, &dst->vbo);
    glGenBuffers(1, &dst->ibo);

    glBindBuffer(GL_ARRAY_BUFFER, dst->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(imageData), imageData, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, dst->ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(imageIndices), imageIndices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(graphics_Vertex),
                          (void *) offsetof(graphics_Vertex, pos));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(graphics_Vertex),
                          (void *) offsetof(graphics_Vertex, uv));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(graphics_Vertex),
                          (void *) offsetof(graphics_Vertex, color));

    glBindVertexArray(0);

    graphics_Image_refresh(dst, data);
}

void graphics_Image_refresh(graphics_Image *img, image_ImageData *data) {
    /* Vector art hands its drawing over to the image, which from here on owns
     * it and re-rasterizes it while drawing. Refreshing with plain pixels drops
     * the drawing again - the image is raster art from that point on. */
    if (img->svg) {
        svg_Document_free(img->svg);
        img->svg = NULL;
        img->vectorPinScale = 0.0f;
    }
    if (image_ImageData_isVector(data)) {
        img->vectorScale = image_ImageData_getVectorScale(data);
        img->svg = image_ImageData_releaseVector(data);
    }

    if (img->texID == 0) glGenTextures(1, &img->texID);
    glBindTexture(GL_TEXTURE_2D, img->texID);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    img->texWidth = data->w;
    img->texHeight = data->h;
    img->width = data->w;
    img->height = data->h;
    img->path = data->path;

    if (img->svg) {
        /* The pixels are just the current rasterization of the drawing; the
         * game keeps addressing the image at the size it was authored at. */
        img->width = (int) lroundf(svg_Document_getWidth(img->svg));
        img->height = (int) lroundf(svg_Document_getHeight(img->svg));
    }

    GLenum format;
    GLint internalFormat;

#ifndef CLOVE_WEB
    switch (image_ImageData_getChannels((image_ImageData *) data)) {
        case 1: // alpha-only în engine
            format = GL_RED;
            internalFormat = GL_R8;

            // RGB = 1, A = R
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_ONE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_ONE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_ONE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_RED);
            break;

        case 2: // lum + alpha (R=lum, G=alpha)
            format = GL_RG;
            internalFormat = GL_RG8;

            // RGB = R, A = G
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_RED);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_RED);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_GREEN);
            break;

        case 3:
            format = GL_RGB;
            internalFormat = GL_RGB8;

            // default swizzle
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_RED);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_GREEN);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_BLUE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ONE);
            break;

        default: // 4
            format = GL_RGBA;
            internalFormat = GL_RGBA8;

            // default swizzle
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_RED);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_GREEN);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_BLUE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ALPHA);
            break;
    }
#else
    switch (image_ImageData_getChannels((image_ImageData *) data)) {
        case 1: // alpha only
            format = GL_LUMINANCE;
            internalFormat = GL_LUMINANCE;
            break;
        case 2: // greyscale
            format = GL_LUMINANCE_ALPHA;
            internalFormat = GL_LUMINANCE_ALPHA;
            break;
        case 3:
            format = GL_RGB;
            internalFormat = GL_RGB;
            break;
        case 4:
            format = GL_RGBA;
            internalFormat = GL_RGBA;
            break;

        default:
            format = GL_RGBA;
            internalFormat = GL_RGBA;
            break;
    }
#endif
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, data->w, data->h, 0, format, GL_UNSIGNED_BYTE, data->surface);

    graphics_Image_setFilter(img, &defaultFilter);
    graphics_Image_setWrap(img, &defaultWrap);
}

void graphics_Image_free(graphics_Image *obj) {
    if (obj->svg) {
        svg_Document_free(obj->svg);
        obj->svg = NULL;
    }
    glDeleteTextures(1, &obj->texID);
    glDeleteBuffers(1, &obj->ibo);
    glDeleteBuffers(1, &obj->vbo);
    glDeleteVertexArrays(1, &obj->vao);
}

void graphics_Image_setFilter(graphics_Image *img, graphics_Filter const *filter) {
    graphics_Texture_setFilter(img->texID, filter);
}

void graphics_Image_getFilter(graphics_Image *img, graphics_Filter *filter) {
    graphics_Texture_getFilter(img->texID, filter);
}

void graphics_Image_setWrap(graphics_Image *img, graphics_Wrap const *wrap) {
    glBindTexture(GL_TEXTURE_2D, img->texID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap->horMode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap->verMode);
}

void graphics_Image_getWrap(graphics_Image *img, graphics_Wrap *wrap) {
    glBindTexture(GL_TEXTURE_2D, img->texID);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (int *) &wrap->horMode);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (int *) &wrap->verMode);
}

void graphics_Image_draw(graphics_Image *image, graphics_Quad const *quad,
                         float x, float y, float r, float sx, float sy,
                         float ox, float oy, float kx, float ky) {
    graphics_Image_updateVectorScale(image, sx, sy);

    glBindVertexArray(image->vao);
    glBindBuffer(GL_ARRAY_BUFFER, image->vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, image->ibo);

    glBufferData(GL_ARRAY_BUFFER, sizeof(imageData), imageData, GL_STATIC_DRAW);

    m4x4_newTransform2d(&image->tr2d, x, y, r, sx, sy, ox, oy, kx, ky);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, image->texID);

    graphics_drawArray(quad, &image->tr2d, image->ibo, 4, GL_TRIANGLE_STRIP, GL_UNSIGNED_BYTE,
                       graphics_getColor(), image->width * quad->w, image->height * quad->h);
    glBindVertexArray(0);
}
