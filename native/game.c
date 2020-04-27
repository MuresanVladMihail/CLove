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

static const graphics_Quad defaultQuad = {
    .x = 0.0f,
    .y = 0.0f,
    .w = 1.0f,
    .h = 1.0f
};

graphics_Canvas c;

void game_load(void) {
    graphics_Canvas_new(&c, 600, 600);

    graphics_setCanvas(&c);
    graphics_setColor(.3f, .2f, .4f, 1);
    graphics_geometry_rectangle(true, 40, 40, 64, 64, 0, 1, 1, 0, 0);
    graphics_setCanvas(NULL);
}

void game_update(float delta) {

}

void game_draw(void) {
    graphics_setBackgroundColor(.8f, .6f, .5f, 1);
    graphics_geometry_rectangle(true, 400, 400, 32, 32, 0, 1, 1, 0, 0);
    graphics_Canvas_draw(&c, &defaultQuad, 220, 220, 0, 1, 1, 0, 0, 0, 0);
}

void game_quit(void) {
}

