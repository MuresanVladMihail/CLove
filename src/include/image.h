/*
#   clove
#
#   Copyright (C) 2016-2020 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/
#pragma once

#include "imagedata.h"
#include "gl.h"
#include "vector.h"
#include "quad.h"
#include "gltools.h"

typedef enum {
  graphics_WrapMode_clamp = GL_CLAMP_TO_EDGE,
  graphics_WrapMode_repeat = GL_REPEAT
} graphics_WrapMode;


typedef struct {
  graphics_WrapMode verMode;
  graphics_WrapMode horMode;
} graphics_Wrap;


typedef struct {
  unsigned int texID;
  unsigned int vbo;
  unsigned int ibo;
  GLuint vao;
  //Size the image is drawn at. For vector art this stays the size the drawing
  //was authored at, no matter how big the texture behind it currently is.
  int width;
  int height;
  //Size of the texture actually held by the GPU.
  int texWidth;
  int texHeight;
  //Used to store the path to loaded image. May return null.
  const char *path;
  mat4x4 tr2d;
  //Vector art only: the drawing this image was rasterized from (owned here,
  //freed by graphics_Image_free), the scale the current texture was rasterized
  //at, and a scale pinned by the game. NULL/0 for ordinary raster images.
  svg_Document *svg;
  float vectorScale;
  float vectorPinScale;
} graphics_Image;


void graphics_image_init(void);

void graphics_Image_new_with_ImageData(graphics_Image *dst, image_ImageData *data);

void graphics_Image_free(graphics_Image *obj);

void graphics_Image_setFilter(graphics_Image *img, graphics_Filter const *filter);

void graphics_Image_getFilter(graphics_Image *img, graphics_Filter *filter);

void graphics_Image_setWrap(graphics_Image *img, graphics_Wrap const *wrap);

void graphics_Image_getWrap(graphics_Image *img, graphics_Wrap *wrap);

void graphics_Image_refresh(graphics_Image *img, image_ImageData *data);

//True when the image was loaded from vector art and can still be re-rasterized.
int graphics_Image_isVector(graphics_Image const *img);

//Scale the texture currently held by the image was rasterized at.
float graphics_Image_getVectorScale(graphics_Image const *img);

//Pins the rasterization scale of vector art (1.0 = the authored size), which
//also turns off the automatic re-rasterization done while drawing. A scale <= 0
//hands the image back to automatic mode. Returns 0 for raster images and when
//the rasterization failed.
int graphics_Image_setVectorScale(graphics_Image *img, float scale);

//Largest rasterization the GL can (and CLove is willing to) hold.
int graphics_Image_getMaxVectorSize(void);

void graphics_Image_draw(graphics_Image *image, graphics_Quad const *quad, float x, float y, float r, float sx,
                         float sy, float ox, float oy, float kx, float ky);
