/*
#   clove
#
#   Copyright (C) 2020 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/
#pragma once

#include "../3rdparty/FH/src/fh.h"

// 7 was also FH_GRAPHICS_QUAD, so a canvas passed every `is this a quad?`
// check and vice versa -- love_quad_getViewport(canvas) read four floats out
// of a graphics_Canvas and handed them back as a viewport.
#define FH_GRAPHICS_CANVAS 18

void fh_graphics_canvas_register(struct fh_program *prog);
