/*
#   clove
#
#   Copyright (C) 2020 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/
#include "../3rdparty/FH/src/value.h"

#include "../include/canvas.h"

#include "graphics_canvas.h"

static fh_c_obj_gc_callback gcCanvas(graphics_Canvas *c) {
    graphics_Canvas_free(c);
    free(c);
    return (fh_c_obj_gc_callback)1;
}

static int fn_love_graphics_newCanvas(struct fh_program *prog, struct fh_value *ret, struct fh_value *args, int n_args) {

    if (n_args != 2) {
        return fh_set_error(prog, "Expected 2 arguments, width and height");
    }

    for (int i = 0; i < 2; i++) {
        if (!fh_is_number(&args[i])) {
            return fh_set_error(prog, "Expected number at argument %d", i);
        }
    }

    int width = fh_get_number(&args[0]);
    int height = fh_get_number(&args[1]);

    // zero-initialized: graphics_Canvas_new()'s setup_quad() generates
    // image.vbo/ibo directly but never image.vao (unlike the normal
    // graphics_Image_new_with_ImageData() path), so a plain malloc() left
    // it as uninitialized garbage that graphics_Canvas_free() would later
    // pass to glDeleteVertexArrays().
    graphics_Canvas *c = calloc(1, sizeof(graphics_Canvas));

    graphics_Canvas_new(c, width, height);

    fh_c_obj_gc_callback *callback = gcCanvas;

    *ret = fh_new_c_obj(prog, c, callback, FH_GRAPHICS_CANVAS);
    return 0;
}

static int fn_love_graphics_setCanvas(struct fh_program *prog, struct fh_value *ret, struct fh_value *args, int n_args) {

    // LOVE's love.graphics.setCanvas() with no arguments goes back to drawing
    // on the screen, and the branch below already treats a non-canvas as
    // "unset" -- but the arity check rejected the no-argument spelling.
    if (n_args > 1)
        return fh_set_error(prog, "love_graphics_setCanvas(): expected 0 or 1 arguments, got %d", n_args);

    if (n_args == 1 && fh_is_c_obj_of_type(&args[0], FH_GRAPHICS_CANVAS)) {
       graphics_Canvas *c = fh_get_c_obj_value(&args[0]);
       graphics_setCanvas(c);
    } else {
        graphics_setCanvas(NULL);
    }

    *ret = fh_new_null();
    return 0;
}

/* A canvas had no accessors at all: no width, no height, no way to get the
 * pixels back out. LOVE's Canvas has getWidth/getHeight/getDimensions and
 * newImageData, and a game that renders to a texture generally needs at least
 * to know how big it is. */
static int fn_love_canvas_getWidth(struct fh_program *prog,
                                   struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_canvas_getWidth(): expected 1 argument, got %d", n_args);
    if (!fh_is_c_obj_of_type(&args[0], FH_GRAPHICS_CANVAS))
        return fh_set_error(prog, "Expected a canvas");

    graphics_Canvas *c = fh_get_c_obj_value(&args[0]);
    *ret = fh_new_number(graphics_Canvas_getWidth(c));
    return 0;
}

static int fn_love_canvas_getHeight(struct fh_program *prog,
                                    struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_canvas_getHeight(): expected 1 argument, got %d", n_args);
    if (!fh_is_c_obj_of_type(&args[0], FH_GRAPHICS_CANVAS))
        return fh_set_error(prog, "Expected a canvas");

    graphics_Canvas *c = fh_get_c_obj_value(&args[0]);
    *ret = fh_new_number(graphics_Canvas_getHeight(c));
    return 0;
}

static int fn_love_canvas_getDimensions(struct fh_program *prog,
                                        struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_canvas_getDimensions(): expected 1 argument, got %d", n_args);
    if (!fh_is_c_obj_of_type(&args[0], FH_GRAPHICS_CANVAS))
        return fh_set_error(prog, "Expected a canvas");

    graphics_Canvas *c = fh_get_c_obj_value(&args[0]);

    int pin_state = fh_get_pin_state(prog);
    struct fh_array *arr = fh_make_array(prog, true);
    if (!fh_grow_array_object(prog, arr, 2))
        return fh_set_error(prog, "out of memory");

    struct fh_value out = fh_new_array(prog);
    arr->items[0] = fh_new_number(graphics_Canvas_getWidth(c));
    arr->items[1] = fh_new_number(graphics_Canvas_getHeight(c));

    fh_restore_pin_state(prog, pin_state);
    out.data.obj = arr;
    *ret = out;
    return 0;
}

#define DEF_FN(name) { #name, fn_##name }
static const struct fh_named_c_func c_funcs[] = {
    DEF_FN(love_graphics_newCanvas),
    DEF_FN(love_graphics_setCanvas),
    DEF_FN(love_canvas_getWidth),
    DEF_FN(love_canvas_getHeight),
    DEF_FN(love_canvas_getDimensions),
};

void fh_graphics_canvas_register(struct fh_program *prog) {
     fh_add_c_funcs(prog, c_funcs, sizeof(c_funcs)/sizeof(c_funcs[0]));
}
