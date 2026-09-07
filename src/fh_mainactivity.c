/*
#   clove
#
#   Copyright (C) 2019-2026 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/

#ifdef USE_FH

#include <stdlib.h>

#include "include/fh_mainactivity.h"

#include "3rdparty/FH/src/value.h"

#include "fhapi/keyboard.h"
#include "fhapi/mouse.h"
#include "fhapi/joystick.h"
#include "fhapi/timer.h"
#include "fhapi/graphics_geometry.h"
#include "fhapi/image.h"
#include "fhapi/graphics.h"
#include "fhapi/graphics_window.h"
#include "fhapi/math.h"
#include "fhapi/graphics_font.h"
#include "fhapi/graphics_bitmapfont.h"
#include "fhapi/filesystem.h"
#include "fhapi/audio.h"
#include "fhapi/graphics_batch.h"
#include "fhapi/event.h"
#include "fhapi/graphics_mesh.h"
#include "fhapi/graphics_quad.h"
#include "fhapi/graphics_shader.h"
#include "fhapi/graphics_particlesystem.h"
#include "fhapi/love.h"
#include "fhapi/ui.h"
#include "fhapi/tween.h"
#include "fhapi/system.h"
#include "fhapi/asset.h"
#include "fhapi/graphics_canvas.h"

#include "include/error_screen.h"
#include "fhapi/config.h"
#include "fhapi/physics.h"

#include "include/batch.h"
#include "include/geometry.h"
#include "include/ui.h"
#include "include/asyncload.h"

#include "../native/game.h"

// #define USE_NATIVE 1

typedef struct {
    bool called_quit;
    struct fh_program *prog;
    struct fh_value delta;
    struct fh_value focus;
    struct fh_value opt;
} MainLoopData;

static MainLoopData loopData;

/* Runs the optional love_quit callback and answers whether the quit should go
 * ahead. Returning true from love_quit aborts it, the way LOVE's love.quit
 * does -- without that a tool has no way to ask "save first?" when the window's
 * close button is pressed, because SDL_EVENT_QUIT is not something a script sees. */
static bool quit_function(void) {
    bool allow = true;
    if (fh_function_exists(loopData.prog, "love_quit")) {
        struct fh_value ret = fh_new_null();
        if (fh_call_function(loopData.prog, "love_quit", NULL, 0, &ret) < 0) {
            clove_error("Error: %s\n", fh_get_error(loopData.prog));
        } else if (fh_is_bool(&ret) && fh_get_bool(&ret)) {
            allow = false;
        }
    }
#ifdef USE_NATIVE
    game_quit();
#endif
    return allow;
}

/* love_resize(w, h) -- optional, and two real arguments, the way LOVE's
 * love.resize has them. A program that lays anything out by hand needs to be
 * told, and asking love_window_getWidth() every frame is not the same thing:
 * it cannot tell you *that* it changed. */
static void resize_function(int width, int height) {
    if (!fh_function_exists(loopData.prog, "love_resize"))
        return;

    struct fh_value args[2];
    args[0] = fh_new_number(width);
    args[1] = fh_new_number(height);
    if (fh_call_function(loopData.prog, "love_resize", args, 2, NULL) == -2) {
        clove_error("Error: %s\n", fh_get_error(loopData.prog));
    }
}

/* love_focus(focused) and love_mousefocus(focused) -- dispatched from the SDL
 * window events below, which is where LOVE dispatches love.focus too.
 *
 * This used to be polled: focus_function() ran at the top of every frame,
 * called graphics_hasFocus() and invoked the callback unconditionally, so a
 * game got sixty identical calls a second. Diffing against the last value
 * would have fixed the flood but not the design -- a poll cannot see a
 * transition that begins and ends inside one frame (alt-tab away and back
 * quickly and the game never learns it lost focus), and it reports a change
 * one frame after it happened, because the event pump runs at the end of the
 * loop. Reading it off the event has neither problem. */
static void focus_callback(char const *name, bool focused) {
    if (!fh_function_exists(loopData.prog, name))
        return;

    loopData.focus.data.b = focused;
    if (fh_call_function(loopData.prog, name, &loopData.focus, 1, NULL) == -2) {
        clove_error("Error: %s\n", fh_get_error(loopData.prog));
    }
}

/*
 * Tear down everything and produce the process exit code (0 clean, 1 on a
 * script/engine error).
 *
 * Order matters: fh_deinit() runs the script VM's garbage collector, whose
 * c_obj destructors call back into the engine — images do glDeleteTextures,
 * audio sources do alDeleteSources. Those must run while the GL and OpenAL
 * contexts are still alive, so the VM is freed BEFORE graphics_shutdown()
 * and audio_close(). (Freeing it afterwards drove GL deletes against a
 * destroyed context and could abort in the driver's allocator.)
 *
 * audio_close() then precedes graphics_shutdown()'s SDL_Quit() so the audio
 * device is released before SDL tears down. (The bundled SDL 2.0.8 CoreAudio
 * backend used to stall ~15s closing the device here on macOS; SDL 2.32.10
 * fixed that.)
 */
static int clove_finish(int exit_code) {
    /* Before the VM: a worker still decoding holds an image_ImageData that
     * nobody has collected, and joining first means nothing is being written
     * while the rest of this tears down. */
    asyncload_shutdown();

    fh_deinit(loopData.prog);
    joystick_close();
    ui_deinit();
    graphics_geometry_free();
    graphics_Batch_shutdown();
    audio_close();
    graphics_shutdown();
    filesystem_free();
    return exit_code;
}

static const char ui_key_map[256] = {
    [ SDLK_LSHIFT & 0xff ] = MU_KEY_SHIFT,
    [ SDLK_RSHIFT & 0xff ] = MU_KEY_SHIFT,
    [ SDLK_LCTRL & 0xff ] = MU_KEY_CTRL,
    [ SDLK_RCTRL & 0xff ] = MU_KEY_CTRL,
    [ SDLK_LALT & 0xff ] = MU_KEY_ALT,
    [ SDLK_RALT & 0xff ] = MU_KEY_ALT,
    [ SDLK_RETURN & 0xff ] = MU_KEY_RETURN,
    [ SDLK_BACKSPACE & 0xff ] = MU_KEY_BACKSPACE,
};

static const char ui_button_map[256] = {
    [ SDL_BUTTON_LEFT & 0xff ] = MU_MOUSE_LEFT,
    [ SDL_BUTTON_RIGHT & 0xff ] = MU_MOUSE_RIGHT,
    [ SDL_BUTTON_MIDDLE & 0xff ] = MU_MOUSE_MIDDLE,
};

static struct fh_value update_args[2];


/* Every way a script can end the game goes through here: print the error the
 * way it always has -- a terminal is still where a developer looks -- and then,
 * if there is a window to draw in, show it to whoever is actually playing.
 * The exit code is unchanged either way, so tests and CI still see a failure.
 *
 * fh_get_error() renders into the same buffer it returns, so it is called once
 * and the result copied. */
static int clove_fail(void) {
    char message[2048];
    snprintf(message, sizeof(message), "%s", fh_get_error(loopData.prog));

    clove_error("ERROR: %s\n", message);
    error_screen_show(message);

    return clove_finish(1);
}


/* CLOVE_SCREENSHOT=<path> writes one frame to a .png and quits;
 * CLOVE_SCREENSHOT_FRAME=<n> picks which frame (default 60, so anything that
 * animates has settled). This is how the pictures in README.md are taken, so
 * they can be remade after a change instead of quietly going stale --
 * tools/make_screenshots.sh drives every example through it. */
static void screenshot_tick(void) {
    static const char *path = NULL;
    static long at = -1;
    static long frame = 0;

    if (at == -1) {
        path = SDL_getenv("CLOVE_SCREENSHOT");
        const char *when = SDL_getenv("CLOVE_SCREENSHOT_FRAME");
        at = (when && *when) ? strtol(when, NULL, 10) : 60;
        if (at < 1) { at = 1; }
    }
    if (!path) {
        return;
    }

    if (++frame >= at) {
        graphics_captureScreenshot(path);
        clove_running = false;
    }
}

void fh_main_loop(int argc, char **argv) {
    timer_step();
    matrixstack_origin();
    loopData.delta.data.num = (double) timer_getDelta();

    update_args[0] = loopData.delta;
    update_args[1] = loopData.opt;
    /* love_update is optional: a game may only draw. */
    if (fh_function_exists(loopData.prog, "love_update") &&
        fh_call_function(loopData.prog, "love_update", update_args, 2, NULL) == -2) {
        return;
    }

#ifdef USE_NATIVE
    game_update((float) timer_getDelta());
#endif

    /*if (clove_reload) {

        clove_reload = false;
    }*/

    graphics_clear();
    /* love_draw is optional too. */
    if (fh_function_exists(loopData.prog, "love_draw") &&
        fh_call_function(loopData.prog, "love_draw", &loopData.opt, 1, NULL) == -2) {
        return;
    }

#ifdef USE_NATIVE
    game_draw();
#endif
    ui_draw();

    screenshot_tick();

    graphics_swap();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        /* SDL3 gave every window event its own type instead of packing them
         * into one SDL_WINDOWEVENT with a sub-field, so these sit alongside
         * the key and mouse cases rather than in a switch of their own. */
        switch (event.type) {
            case SDL_EVENT_WINDOW_MOUSE_ENTER:
                graphics_setMouseFocus(true);
                focus_callback("love_mousefocus", true);
                break;
            case SDL_EVENT_WINDOW_MOUSE_LEAVE:
                graphics_setMouseFocus(false);
                focus_callback("love_mousefocus", false);
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                graphics_setFocus(false);
                focus_callback("love_focus", false);
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                graphics_setFocus(true);
                focus_callback("love_focus", true);
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
                /* SDL has already resized the window; only the drawing
                 * state is behind. Without this the projection and the GL
                 * viewport stay at the size the context was created with,
                 * so a resized window keeps drawing the scene into a
                 * corner of itself. */
                int w = event.window.data1;
                int h = event.window.data2;
                graphics_updateViewport(w, h);
                resize_function(w, h);
                break;
            }
            default:
                break;
        }
        switch (event.wheel.type) {
            case SDL_EVENT_MOUSE_WHEEL: {
                ui_input_scroll(0, event.wheel.y * -30);
                mouse_mousewheel(event.wheel.y);
                int _what = event.wheel.y == 1 ? SDL_EVENT_MOUSE_BUTTON_UP : SDL_EVENT_MOUSE_BUTTON_DOWN;
                mouse_mousepressed(event.button.x, event.button.y, _what);
                mouse_setButton(event.button.button);
                break;
            }
            default:
                break;
        }
        switch (event.type) {
            case SDL_EVENT_KEY_DOWN: {
                int c = ui_key_map[event.key.key & 0xff];
                if (c) {
                    ui_input_keydown(c);
                }
                keyboard_keypressed(event.key.key);
                break;
            }
            case SDL_EVENT_KEY_UP: {
                int c = ui_key_map[event.key.key & 0xff];
                if (c) {
                    ui_input_keyup(c);
                }
                keyboard_keyreleased(event.key.key);
                break;
            }
            case SDL_EVENT_TEXT_INPUT: {
                const char *text = event.text.text;
                ui_input_text(text);
                keyboard_textInput(text);
                break;
            }
            case SDL_EVENT_MOUSE_MOTION: {
                int x = event.motion.x;
                int y = event.motion.y;
                ui_input_mouse_move(x, y);
                mouse_mousemoved(x, y);
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                int x = event.button.x;
                int y = event.button.y;
                int btn = event.button.button;
                int ui_btn = ui_button_map[btn & 0xff];
                if (ui_btn) {
                    ui_input_mouse_down(ui_btn, x, y);
                }
                mouse_mousepressed(x, y, btn);
                mouse_setButton(btn);
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                int x = event.button.x;
                int y = event.button.y;
                int btn = event.button.button;
                int ui_btn = ui_button_map[btn & 0xff];
                if (ui_btn) {
                    ui_input_mouse_up(ui_btn, x, y);
                }
                mouse_mousereleased(x, y, btn);
                mouse_setButton(0);
                break;
            }
            case SDL_EVENT_JOYSTICK_ADDED:
                joystick_added(event.jdevice.which);
                break;
            case SDL_EVENT_JOYSTICK_REMOVED:
                joystick_remove(event.jdevice.which);
                break;
            case SDL_EVENT_JOYSTICK_AXIS_MOTION:
                break;
            case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
                joystick_buttonDown(event.jbutton.which, event.jbutton.button, event.jbutton.down);
                break;
            case SDL_EVENT_JOYSTICK_BUTTON_UP:
                joystick_buttonUp(event.jbutton.which, event.jbutton.button, event.jbutton.down);
                break;
#ifdef CLOVE_DESKTOP
            case SDL_EVENT_QUIT: {
                if (quit_function()) {
                    loopData.called_quit = true;
                    clove_running = false;
                }
                break;
            }
#endif
        }
    }
    audio_updateStreams();
}

int fh_main_activity_load(int argc, char *argv[]) {
    fh_init();
    clove_reload = false;
    clove_running = true;
    loopData.called_quit = false;
    loopData.prog = fh_new_program();
    if (!loopData.prog) {
        clove_error("ERROR: out of memory for initializing language FH\n");
        return 1;
    }

    keyboard_init();
    joystick_init();
    timer_init();

    filesystem_init(argv[0], true);

    graphics_particlesystem_init();

    audio_init(true);
    //filesystem_setIdentity("./");

    printf("%s %s \n", "Platform:", filesystem_getOS());

    graphics_init(800, 600, 0, 1, 1);

    graphics_setBordless(0);
    graphics_setVsync(true);
    graphics_setFullscreen(false, 0);

    graphics_geometry_init();
    ui_init();

    graphics_loadAndSetIcon("icon.png");

    love_Version const *version = love_getVersion();
    printf("%s %s %d.%d.%d \n", "CLove version - ",
           version->codename, version->major, version->minor, version->revision);
    printf("FH version - %s\n", FH_VERSION);

    fh_keyboard_register(loopData.prog);
    fh_mouse_register(loopData.prog);
    fh_joystick_register(loopData.prog);
    fh_timer_register(loopData.prog);
    fh_graphics_geometry_register(loopData.prog);
    fh_image_register(loopData.prog);
    fh_graphics_register(loopData.prog);
    fh_graphics_window_register(loopData.prog);
    fh_math_register(loopData.prog);
    fh_graphics_font_register(loopData.prog);
    fh_graphics_bitmap_font_register(loopData.prog);
    fh_filesystem_register(loopData.prog);
    fh_audio_register(loopData.prog);
    fh_graphics_batch_register(loopData.prog);
    fh_event_register(loopData.prog);
    fh_graphics_mesh_register(loopData.prog);
    fh_graphics_quad_register(loopData.prog);
    fh_graphics_shader_register(loopData.prog);
    fh_graphics_particlesystem_register(loopData.prog);
    fh_ui_register(loopData.prog);
    fh_tween_register(loopData.prog);
    fh_system_register(loopData.prog);
    fh_asset_register(loopData.prog);

    /* The workers are the whole point of love.asset; start them once the
     * engine is up, and let clove_finish() join them. */
    asyncload_init(0);
    fh_graphics_canvas_register(loopData.prog);
    fh_physics_register(loopData.prog);
    fh_love_register(loopData.prog);

    bool dump_bytecode = false;
    bool run_package = false;

    if (argv[1]) {
        if (strstr(argv[1], ".love"))
            run_package = true;
        else
            clove_error("ERROR: couldn't find pack named \"%s\" to run\n", argv[1]);
    }

    if (argv[2] && strcmp(argv[2], "true") == 0) {
        dump_bytecode = true;
    }

    int ret = 0;
    if (run_package) {
        ret = fh_run_pack(loopData.prog, dump_bytecode, argv[1], "config.fh", NULL, argv, argc, false);
        if (ret == 0) {
            fh_config(loopData.prog);
        }
        ret = fh_run_pack(loopData.prog, dump_bytecode, argv[1], "main.fh", "main", argv, argc, true);
    } else {
        ret = fh_run_script_file(loopData.prog, dump_bytecode, "config.fh", NULL, argv, argc, false);
        if (ret == 0) {
            fh_config(loopData.prog);
        }
        ret = fh_run_script_file(loopData.prog, dump_bytecode, "main.fh", "main", argv, argc, true);
    }

    if (ret < 0) {
        return clove_fail();
    }

    loopData.delta = fh_new_number(1);
    loopData.focus = fh_new_bool(false);

    loopData.opt = fh_new_map(loopData.prog);

    /* love_load is optional: without it the state map handed to the other
     * callbacks simply stays empty. */
    if (fh_function_exists(loopData.prog, "love_load") &&
        fh_call_function(loopData.prog, "love_load", NULL, 0, &loopData.opt) < 0) {
        fh_running = false;
        return clove_fail();
    }


#ifdef USE_NATIVE
    game_load();
#endif

#ifdef CLOVE_WEB
    emscripten_set_main_loop(lua_main_loop, 60, 1);
#else
    while (clove_running && fh_running) {
        fh_main_loop(argc, argv);
    }
#endif

    /*
     * The logic:
     * When you set an error in FH the boolean "fh_running" will be set automatically to
     * 'false' meaning CLove will stop from running. If that's the case then we want to
     * print the error made because of FH!
     *
     * NOTE:
     * When the error happened because of CLove (not FH) then fh_running will be still set to
     * its default value, 'true', and the "if" from below won't be called but we will still
     * get the errors because of 'clove_error' function called in the errornous function.
     */
    int exit_code = 0;
    if (!fh_running) {
        char message[2048];
        snprintf(message, sizeof(message), "%s", fh_get_error(loopData.prog));
        clove_error("ERROR: %s\n", message);
        error_screen_show(message);
        exit_code = 1;
    }

    if (!loopData.called_quit) {
        /* The loop is already over, so love_quit() gets its chance to clean up
         * but no longer gets a say in whether we stop. */
        (void) quit_function();
    }
    return clove_finish(exit_code);
}

#endif
