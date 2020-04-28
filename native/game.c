/*
#   clove
#   Copyright (C) 2017-2020 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/
#include "game.h"

#include "../src/3rdparty/SDL2/include/SDL.h"
#include <stdio.h>

#include <stdbool.h>
#include <stdio.h>

#include "../src/include/graphics.h"
#include "../src/include/geometry.h"
#include "../src/include/matrixstack.h"
#include "../src/include/keyboard.h"
#include "../src/include/canvas.h"
#include "../src/include/ui.h"

/*
static const graphics_Quad defaultQuad = {
    .x = 0.0f,
    .y = 0.0f,
    .w = 1.0f,
    .h = 1.0f
};

graphics_Canvas c;
graphics_Canvas c2;
float x= 0;
*/

void game_load(void) {
/*
    graphics_Canvas_new(&c, 400, 200);
    graphics_Canvas_new(&c2, 800, 600);

    graphics_setCanvas(&c);
    graphics_setBackgroundColor(.2f, .6f, .5f, 1);
    graphics_setColor(0.6f, 0.5f, 1.0f, 1);
    graphics_clear();
    graphics_setBlendMode(graphics_BlendMode_alpha);
    graphics_geometry_fillCircle(10, 120, 32, 12, 0, 1, 1, 0, 0);
    graphics_setCanvas(NULL);
*/
}

void game_update(float delta) {
/*
    graphics_setCanvas(&c);
        graphics_setBackgroundColor(.9f, .1f, .2f, 1);
        graphics_setColor(0.6f, 1, 1.0f, .5f);
        graphics_clear();
        graphics_geometry_fillCircle(x, 120, 32, 12, 0, 1, 1, 0, 0);
        x += 100 *delta;
    graphics_setCanvas(NULL);

    graphics_setCanvas(&c2);
        graphics_setBackgroundColor(.8f, .6f, .5f, 1);
        graphics_setColor(1.0f, 1.0f, 1.0f, 1);
        graphics_clear();
        graphics_geometry_rectangle(true, 400, 200, 32, 32, 0, 1, 1, 0, 0);
    graphics_setCanvas(NULL);
*/
}

void game_draw(void) {
/*
    graphics_Canvas_draw(&c2, &defaultQuad, 0, 0, 0, 1, 1, 0, 0, 0, 0);
    graphics_Canvas_draw(&c, &defaultQuad, 0, 0, 0, 1, 1, 0, 0, 0, 0);

    graphics_setColor(0.2f, 0.2f, 0.2f, 1);
    graphics_geometry_rectangle(true, 400, 400, 64, 64, 0, 1, 1, 0, 0);
*/
}

void game_quit(void) {
}
