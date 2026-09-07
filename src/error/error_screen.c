/*
#   clove
#
#   Copyright (C) 2026 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/
#include "../include/error_screen.h"

#include <stdlib.h>
#include <string.h>

#include "../3rdparty/SDL3/include/SDL3/SDL.h"

#include "../include/canvas.h"
#include "../include/font.h"
#include "../include/geometry.h"
#include "../include/graphics.h"
#include "../include/matrixstack.h"
#include "../include/system.h"

/* Out of opt/CLoveLogo.png, so the screen is recognisably CLove's and not a
 * generic crash dialog: the body colour, the lighter frame around it and the
 * white the wordmark is drawn in. */
#define IN(x) ((x) / 255.0f)
static const float BODY[3]  = { IN(227), IN(60),  IN(119) };   /* #E33C77 */
static const float FRAME[3] = { IN(251), IN(120), IN(172) };   /* #FB78AC */

static bool enabled = true;

void error_screen_setEnabled(bool value) { enabled = value; }

bool error_screen_isEnabled(void) { return enabled; }

/* The message as fh_get_error() renders it is "<where>: error: <what>" and
 * then a blank line and the traceback. The first line is what the player
 * needs at a glance, so it is pulled out and set larger. */
static void splitMessage(const char *message, char *headline, size_t headlineSize,
                         const char **rest) {
    const char *nl = strchr(message, '\n');
    size_t n = nl ? (size_t) (nl - message) : strlen(message);
    if (n >= headlineSize) {
        n = headlineSize - 1;
    }
    memcpy(headline, message, n);
    headline[n] = '\0';

    *rest = nl ? nl + 1 : message + strlen(message);
    while (**rest == '\n') {
        (*rest)++;
    }
}

static void draw(graphics_Font *big, graphics_Font *small, const char *headline,
                 const char *rest, bool copied, int scroll) {
    const int w = graphics_getWidth();
    const int h = graphics_getHeight();
    const int frame = 10;
    const int margin = frame + 30;
    const int limit = w - margin * 2;

    graphics_setBackgroundColor(BODY[0], BODY[1], BODY[2], 1.0f);
    graphics_clear();

    /* the lighter border the logo has */
    graphics_setColor(FRAME[0], FRAME[1], FRAME[2], 1.0f);
    graphics_geometry_rectangle(true, 0, 0, (float) w, (float) frame, 0, 1, 1, 0, 0);
    graphics_geometry_rectangle(true, 0, (float) (h - frame), (float) w, (float) frame, 0, 1, 1, 0, 0);
    graphics_geometry_rectangle(true, 0, 0, (float) frame, (float) h, 0, 1, 1, 0, 0);
    graphics_geometry_rectangle(true, (float) (w - frame), 0, (float) frame, (float) h, 0, 1, 1, 0, 0);

    int y = margin;

    graphics_setColor(1.0f, 1.0f, 1.0f, 1.0f);
    graphics_Font_render(big, "Something broke.", margin, y, 0, 1, 1, 0, 0, 0, 0);
    y += graphics_Font_getHeight(big) + 14;

    graphics_setColor(FRAME[0], FRAME[1], FRAME[2], 1.0f);
    graphics_geometry_rectangle(true, (float) margin, (float) y, (float) limit, 2.0f, 0, 1, 1, 0, 0);
    y += 22;

    graphics_setColor(1.0f, 1.0f, 1.0f, 1.0f);
    graphics_Font_printf(big, headline, margin, y, limit, graphics_TextAlign_left,
                         0, 1, 1, 0, 0, 0, 0);
    y += graphics_Font_getHeight(big) * 2 + 26;

    /* The traceback sits in its own slightly darker block, so a wall of frames
     * reads as one thing rather than as more of the message. */
    const int footer = margin + graphics_Font_getHeight(small) + 12;
    const int panelTop = y;
    const int panelHeight = h - footer - panelTop;

    if (rest && rest[0] != '\0' && panelHeight > 24) {
        graphics_setColor(0.0f, 0.0f, 0.0f, 0.12f);
        graphics_geometry_rectangle(true, (float) (margin - 14), (float) panelTop,
                                    (float) (limit + 28), (float) panelHeight, 0, 1, 1, 0, 0);

        /* clipped so a long traceback scrolls inside the block instead of
         * running off the bottom of the window. glScissor measures from the
         * bottom left, unlike everything else here. */
        graphics_setScissor(margin - 14, h - panelTop - panelHeight, limit + 28, panelHeight);
        graphics_setColor(1.0f, 1.0f, 1.0f, 0.85f);
        graphics_Font_printf(small, rest, margin, panelTop + 12 - scroll, limit,
                             graphics_TextAlign_left, 0, 1, 1, 0, 0, 0, 0);
        graphics_clearScissor();
    }

    graphics_setColor(FRAME[0], FRAME[1], FRAME[2], 1.0f);
    const char *hint = copied
        ? "copied \xe2\x80\x94 paste it wherever you report this        esc or Q to quit"
        : "C to copy the whole thing        up/down or the wheel to scroll        esc or Q to quit";
    graphics_Font_render(small, hint, margin, h - footer + 6,
                         0, 1, 1, 0, 0, 0, 0);

    { const char *shot = SDL_getenv("CLOVE_ERROR_SCREENSHOT");
      if (shot) { graphics_captureScreenshot(shot); } }
    graphics_swap();
}

bool error_screen_show(const char *message) {
    /* The test runner sets this: an error screen waits for a keypress, and a
     * test suite has nobody to press one. */
    if (SDL_getenv("CLOVE_NO_ERROR_SCREEN") != NULL) {
        return false;
    }

    if (!enabled || !message || !graphics_isCreated()) {
        return false;
    }

    /* Whatever the game was in the middle of -- a canvas, a shader, a scissor,
     * a transform, a colour -- is none of our business and all of it would
     * stop this from drawing. */
    graphics_setCanvas(NULL);
    graphics_reset();

    graphics_Font big;
    graphics_Font small;
    if (graphics_Font_new(&big, NULL, 20) != 0 || graphics_Font_new(&small, NULL, 13) != 0) {
        return false;
    }

    char headline[512];
    const char *rest = NULL;
    splitMessage(message, headline, sizeof(headline), &rest);

    bool copied = false;
    bool running = true;
    int scroll = 0;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                switch (event.key.key) {
                case SDLK_ESCAPE:
                case SDLK_Q:
                    running = false;
                    break;
                case SDLK_C:
                    system_setClipboardText(message);
                    copied = true;
                    break;
                case SDLK_DOWN:
                    scroll += 24;
                    break;
                case SDLK_UP:
                    scroll -= 24;
                    if (scroll < 0) { scroll = 0; }
                    break;
                default:
                    break;
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                scroll -= event.wheel.y * 32;
                if (scroll < 0) { scroll = 0; }
                break;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                running = false;
                break;
            default:
                break;
            }
        }

        draw(&big, &small, headline, rest, copied, scroll);
        if (SDL_getenv("CLOVE_ERROR_SCREENSHOT")) { running = false; }
        SDL_Delay(16);   /* nothing here is animated; do not spin a core on it */
    }

    graphics_Font_free(&big);
    graphics_Font_free(&small);
    return true;
}
