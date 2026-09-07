/*
#   clove
#
#   Copyright (C) 2016-2025 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/
#include "asset.h"

#include <stdlib.h>

#include "image.h"

#include "../include/asyncload.h"
#include "../include/image.h"
#include "../include/utils.h"

#include "../3rdparty/FH/src/value.h"

/* love.asset -- loading a texture without stalling the frame.
 *
 *     let job = love_asset_loadImage("level/bg.png");
 *     ...
 *     if (love_asset_status(job) == "ready") { self.bg = love_asset_take(job); }
 *
 * Decoding a 2048x2048 PNG measures 15-17 ms here, a whole frame at 60 Hz, so
 * a level that pulls in twenty of them stalls for a third of a second. The
 * decode happens on a worker; the GL upload happens in take(), on this thread,
 * because a GL context belongs to one thread and the upload is the cheap half.
 */

static int fn_love_asset_loadImage(struct fh_program *prog,
                                   struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_asset_loadImage(): expected 1 argument (a path), got %d", n_args);
    if (!fh_is_string(&args[0]))
        return fh_set_error(prog, "love_asset_loadImage(): expected a path");

    const char *path = fh_get_string(&args[0]);
    int id = asyncload_requestImage(path);

    if (id < 0) {
        /* Vector art is the likely reason, and it deserves saying so: svg.c
         * keeps one shared rasterizer, so it cannot be decoded on a worker --
         * and at about a millisecond there would be nothing to gain. */
        return fh_set_error(prog,
                "love_asset_loadImage(): could not queue '%s'. Vector art (.svg) has to be "
                "loaded with love_graphics_newImage(); it rasterizes in about a millisecond.",
                path);
    }

    *ret = fh_new_number(id);
    return 0;
}

static int fn_love_asset_status(struct fh_program *prog,
                                struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_asset_status(): expected 1 argument (a job), got %d", n_args);
    if (!fh_is_number(&args[0]))
        return fh_set_error(prog, "love_asset_status(): expected a job handle");

    char const *word = "unknown";
    switch (asyncload_status((int) fh_get_number(&args[0]))) {
    case asyncload_Status_pending: word = "pending"; break;
    case asyncload_Status_ready:   word = "ready";   break;
    case asyncload_Status_failed:  word = "failed";  break;
    case asyncload_Status_unknown: break;
    }

    *ret = fh_new_string(prog, word);
    return 0;
}

/* Hands back the finished Image, and the job is done with. The GL upload is
 * here, on the thread that owns the context. */
static int fn_love_asset_take(struct fh_program *prog,
                              struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_asset_take(): expected 1 argument (a job), got %d", n_args);
    if (!fh_is_number(&args[0]))
        return fh_set_error(prog, "love_asset_take(): expected a job handle");

    int id = (int) fh_get_number(&args[0]);
    asyncload_Status status = asyncload_status(id);

    if (status == asyncload_Status_pending) {
        return fh_set_error(prog, "love_asset_take(): job %d is still loading -- "
                                  "check love_asset_status() first", id);
    }
    if (status == asyncload_Status_failed) {
        char const *path = asyncload_path(id);
        return fh_set_error(prog, "love_asset_take(): job %d failed to load%s%s", id,
                            path ? ": " : "", path ? path : "");
    }
    if (status == asyncload_Status_unknown) {
        return fh_set_error(prog, "love_asset_take(): there is no job %d "
                                  "(never queued, or already taken)", id);
    }

    image_ImageData *data = asyncload_take(id);
    if (!data) {
        return fh_set_error(prog, "love_asset_take(): job %d had no pixels", id);
    }

    fh_image_t *img = malloc(sizeof(fh_image_t));
    if (!img) {
        image_ImageData_free(data);
        free(data);
        return fh_set_error(prog, "out of memory");
    }

    img->data = data;
    img->img = malloc(sizeof(graphics_Image));
    if (!img->img) {
        image_ImageData_free(data);
        free(data);
        free(img);
        return fh_set_error(prog, "out of memory");
    }

    graphics_Image_new_with_ImageData(img->img, data);

    fh_c_obj_gc_callback *callback = fh_image_freeCallback;
    *ret = fh_new_c_obj(prog, img, callback, FH_IMAGE_TYPE);
    return 0;
}

static int fn_love_asset_pending(struct fh_program *prog,
                                 struct fh_value *ret, struct fh_value *args, int n_args) {
    UNUSED(args);
    if (n_args != 0)
        return fh_set_error(prog, "love_asset_pending(): expected no arguments, got %d", n_args);

    *ret = fh_new_number(asyncload_pending());
    return 0;
}

#define DEF_FN(name) { #name, fn_##name }
static const struct fh_named_c_func c_funcs[] = {
    DEF_FN(love_asset_loadImage),
    DEF_FN(love_asset_status),
    DEF_FN(love_asset_take),
    DEF_FN(love_asset_pending),
};

void fh_asset_register(struct fh_program *prog) {
    fh_add_c_funcs(prog, c_funcs, sizeof(c_funcs) / sizeof(c_funcs[0]));
}
