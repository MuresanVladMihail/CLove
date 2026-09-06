/*
#   clove
#
#   Copyright (C) 2016-2025 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/
#include "tween.h"

#include <stdlib.h>
#include <string.h>

#include "../include/tween.h"
#include "../include/utils.h"
#include "../3rdparty/FH/src/value.h"

// A tween carries one number or a whole array of them -- a point, a colour,
// a rectangle -- and reports back in whatever shape it was given, so the
// wrapper remembers which that was.
typedef struct {
    tween_Tween *tween;
    bool         isArray;
} fh_tween_t;

static fh_c_obj_gc_callback tween_gc(fh_tween_t *w) {
    tween_free(w->tween);
    free(w);
    return (fh_c_obj_gc_callback) 1;
}

// Reads a number or an array of numbers into a float buffer the caller owns.
static float *read_channels(struct fh_program *prog, struct fh_value *v,
                            size_t *count, bool *wasArray) {
    if (fh_is_number(v)) {
        float *out = malloc(sizeof(float));
        if (!out) {
            fh_set_error(prog, "out of memory");
            return NULL;
        }
        out[0] = (float) fh_get_number(v);
        *count = 1;
        *wasArray = false;
        return out;
    }

    if (!fh_is_array(v)) {
        fh_set_error(prog, "expected a number or an array of numbers, got %s",
                     fh_type_to_str(prog, v->type));
        return NULL;
    }

    struct fh_array *arr = GET_VAL_ARRAY(v);
    if (arr->len < 1) {
        fh_set_error(prog, "expected at least one value");
        return NULL;
    }

    float *out = malloc(sizeof(float) * arr->len);
    if (!out) {
        fh_set_error(prog, "out of memory");
        return NULL;
    }

    for (uint32_t i = 0; i < arr->len; i++) {
        if (!fh_is_number(&arr->items[i])) {
            free(out);
            fh_set_error(prog, "value %d is not a number", (int) i);
            return NULL;
        }
        out[i] = (float) fh_get_number(&arr->items[i]);
    }

    *count = arr->len;
    *wasArray = true;
    return out;
}

static int as_value(struct fh_program *prog, struct fh_value *ret,
                    fh_tween_t *w, float const *values) {
    if (!w->isArray) {
        *ret = fh_new_number((double) values[0]);
        return 0;
    }

    size_t n = w->tween->channels;
    int pin_state = fh_get_pin_state(prog);
    struct fh_array *arr = fh_make_array(prog, true);
    if (!fh_grow_array_object(prog, arr, n)) {
        return fh_set_error(prog, "out of memory");
    }

    struct fh_value out = fh_new_array(prog);
    for (size_t i = 0; i < n; i++) {
        arr->items[i] = fh_new_number((double) values[i]);
    }

    fh_restore_pin_state(prog, pin_state);
    out.data.obj = arr;
    *ret = out;
    return 0;
}

static int read_easing(struct fh_program *prog, struct fh_value *args, int n_args,
                       int index, char const *fn, tween_Easing *out) {
    *out = tween_Easing_linear;
    if (n_args <= index || fh_is_null(&args[index])) {
        return 0;
    }
    if (!fh_is_string(&args[index])) {
        return fh_set_error(prog, "%s: the easing must be a string", fn);
    }
    if (!tween_easingFromName(fh_get_string(&args[index]), out)) {
        return fh_set_error(prog, "%s: '%s' is not an easing "
                                  "(love_tween_getEasings() lists them)",
                            fn, fh_get_string(&args[index]));
    }
    return 0;
}

static int fn_love_tween_new(struct fh_program *prog,
                             struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args < 3 || n_args > 5)
        return fh_set_error(prog,
                "love_tween_new(): expected 3 to 5 arguments "
                "(from, to, duration [, easing [, delay]]), got %d", n_args);

    if (!fh_is_number(&args[2]))
        return fh_set_error(prog, "love_tween_new(): the duration must be a number");

    tween_Easing easing;
    if (read_easing(prog, args, n_args, 3, "love_tween_new()", &easing) < 0) {
        return -1;
    }

    size_t fromCount = 0, toCount = 0;
    bool fromArray = false, toArray = false;

    float *from = read_channels(prog, &args[0], &fromCount, &fromArray);
    if (!from) {
        return -1;
    }

    float *to = read_channels(prog, &args[1], &toCount, &toArray);
    if (!to) {
        free(from);
        return -1;
    }

    if (fromCount != toCount) {
        free(from);
        free(to);
        return fh_set_error(prog, "love_tween_new(): `from` has %d values and `to` has %d",
                            (int) fromCount, (int) toCount);
    }

    tween_Tween *t = tween_new(from, fromCount);
    free(from);
    if (!t) {
        free(to);
        return fh_set_error(prog, "out of memory");
    }

    bool ok = tween_addStep(t, to, (float) fh_get_number(&args[2]),
                            (float) fh_optnumber(args, n_args, 4, 0.0), easing);
    free(to);
    if (!ok) {
        tween_free(t);
        return fh_set_error(prog, "out of memory");
    }

    fh_tween_t *w = malloc(sizeof(fh_tween_t));
    if (!w) {
        tween_free(t);
        return fh_set_error(prog, "out of memory");
    }
    w->tween = t;
    w->isArray = fromArray || toArray;

    fh_c_obj_gc_callback *callback = tween_gc;
    *ret = fh_new_c_obj(prog, w, callback, FH_TWEEN_TYPE);
    return 0;
}

static int fn_love_tween_then(struct fh_program *prog,
                              struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args < 3 || n_args > 5)
        return fh_set_error(prog,
                "love_tween_then(): expected 3 to 5 arguments "
                "(tween, to, duration [, easing [, delay]]), got %d", n_args);

    if (!fh_is_c_obj_of_type(&args[0], FH_TWEEN_TYPE))
        return fh_set_error(prog, "Expected a tween");
    if (!fh_is_number(&args[2]))
        return fh_set_error(prog, "love_tween_then(): the duration must be a number");

    fh_tween_t *w = fh_get_c_obj_value(&args[0]);

    tween_Easing easing;
    if (read_easing(prog, args, n_args, 3, "love_tween_then()", &easing) < 0) {
        return -1;
    }

    size_t count = 0;
    bool isArray = false;
    float *to = read_channels(prog, &args[1], &count, &isArray);
    if (!to) {
        return -1;
    }

    if (count != w->tween->channels) {
        free(to);
        return fh_set_error(prog, "love_tween_then(): this tween carries %d values, got %d",
                            (int) w->tween->channels, (int) count);
    }

    bool ok = tween_addStep(w->tween, to, (float) fh_get_number(&args[2]),
                            (float) fh_optnumber(args, n_args, 4, 0.0), easing);
    free(to);
    if (!ok) {
        return fh_set_error(prog, "out of memory");
    }

    // Returns the tween, so a chain reads as one expression.
    *ret = args[0];
    return 0;
}

static int fn_love_tween_update(struct fh_program *prog,
                                struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 2)
        return fh_set_error(prog, "love_tween_update(): expected 2 arguments (tween, dt), got %d", n_args);

    if (!fh_is_c_obj_of_type(&args[0], FH_TWEEN_TYPE) || !fh_is_number(&args[1]))
        return fh_set_error(prog, "Expected a tween and a dt");

    fh_tween_t *w = fh_get_c_obj_value(&args[0]);
    *ret = fh_new_bool(tween_update(w->tween, (float) fh_get_number(&args[1])));
    return 0;
}

static int fn_love_tween_get(struct fh_program *prog,
                             struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_tween_get(): expected 1 argument, got %d", n_args);

    if (!fh_is_c_obj_of_type(&args[0], FH_TWEEN_TYPE))
        return fh_set_error(prog, "Expected a tween");

    fh_tween_t *w = fh_get_c_obj_value(&args[0]);
    return as_value(prog, ret, w, tween_get(w->tween));
}

static int fn_love_tween_isDone(struct fh_program *prog,
                                struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_tween_isDone(): expected 1 argument, got %d", n_args);
    if (!fh_is_c_obj_of_type(&args[0], FH_TWEEN_TYPE))
        return fh_set_error(prog, "Expected a tween");

    fh_tween_t *w = fh_get_c_obj_value(&args[0]);
    *ret = fh_new_bool(w->tween->done);
    return 0;
}

static int fn_love_tween_getProgress(struct fh_program *prog,
                                     struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_tween_getProgress(): expected 1 argument, got %d", n_args);
    if (!fh_is_c_obj_of_type(&args[0], FH_TWEEN_TYPE))
        return fh_set_error(prog, "Expected a tween");

    fh_tween_t *w = fh_get_c_obj_value(&args[0]);
    *ret = fh_new_number((double) tween_getProgress(w->tween));
    return 0;
}

static int fn_love_tween_getDuration(struct fh_program *prog,
                                     struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_tween_getDuration(): expected 1 argument, got %d", n_args);
    if (!fh_is_c_obj_of_type(&args[0], FH_TWEEN_TYPE))
        return fh_set_error(prog, "Expected a tween");

    fh_tween_t *w = fh_get_c_obj_value(&args[0]);
    *ret = fh_new_number((double) tween_getDuration(w->tween));
    return 0;
}

static int fn_love_tween_seek(struct fh_program *prog,
                              struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 2)
        return fh_set_error(prog, "love_tween_seek(): expected 2 arguments (tween, 0..1), got %d", n_args);
    if (!fh_is_c_obj_of_type(&args[0], FH_TWEEN_TYPE) || !fh_is_number(&args[1]))
        return fh_set_error(prog, "Expected a tween and a progress");

    fh_tween_t *w = fh_get_c_obj_value(&args[0]);
    tween_seek(w->tween, (float) fh_get_number(&args[1]));
    *ret = fh_new_null();
    return 0;
}

static int fn_love_tween_reset(struct fh_program *prog,
                               struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_tween_reset(): expected 1 argument, got %d", n_args);
    if (!fh_is_c_obj_of_type(&args[0], FH_TWEEN_TYPE))
        return fh_set_error(prog, "Expected a tween");

    fh_tween_t *w = fh_get_c_obj_value(&args[0]);
    tween_reset(w->tween);
    *ret = fh_new_null();
    return 0;
}

static int fn_love_tween_setLoops(struct fh_program *prog,
                                  struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args < 2 || n_args > 3)
        return fh_set_error(prog,
                "love_tween_setLoops(): expected 2 or 3 arguments (tween, loops [, yoyo]), got %d", n_args);
    if (!fh_is_c_obj_of_type(&args[0], FH_TWEEN_TYPE) || !fh_is_number(&args[1]))
        return fh_set_error(prog, "Expected a tween and a loop count (-1 for forever)");

    bool yoyo = false;
    if (n_args > 2) {
        if (!fh_is_bool(&args[2]))
            return fh_set_error(prog, "love_tween_setLoops(): yoyo must be a bool");
        yoyo = fh_get_bool(&args[2]);
    }

    fh_tween_t *w = fh_get_c_obj_value(&args[0]);
    tween_setLoops(w->tween, (int) fh_get_number(&args[1]), yoyo);
    *ret = fh_new_null();
    return 0;
}

static int fn_love_tween_setPaused(struct fh_program *prog,
                                   struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 2)
        return fh_set_error(prog, "love_tween_setPaused(): expected 2 arguments, got %d", n_args);
    if (!fh_is_c_obj_of_type(&args[0], FH_TWEEN_TYPE) || !fh_is_bool(&args[1]))
        return fh_set_error(prog, "Expected a tween and a bool");

    fh_tween_t *w = fh_get_c_obj_value(&args[0]);
    tween_setPaused(w->tween, fh_get_bool(&args[1]));
    *ret = fh_new_null();
    return 0;
}

static int fn_love_tween_isPaused(struct fh_program *prog,
                                  struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_tween_isPaused(): expected 1 argument, got %d", n_args);
    if (!fh_is_c_obj_of_type(&args[0], FH_TWEEN_TYPE))
        return fh_set_error(prog, "Expected a tween");

    fh_tween_t *w = fh_get_c_obj_value(&args[0]);
    *ret = fh_new_bool(w->tween->paused);
    return 0;
}

// The easing curve on its own, for code that wants the shape without a tween
// -- a shader uniform, a camera shake, a bar that fills.
static int fn_love_tween_ease(struct fh_program *prog,
                              struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 2)
        return fh_set_error(prog, "love_tween_ease(): expected 2 arguments (easing, t), got %d", n_args);
    if (!fh_is_string(&args[0]) || !fh_is_number(&args[1]))
        return fh_set_error(prog, "Expected an easing name and a t in 0..1");

    tween_Easing easing;
    if (!tween_easingFromName(fh_get_string(&args[0]), &easing))
        return fh_set_error(prog, "love_tween_ease(): '%s' is not an easing", fh_get_string(&args[0]));

    *ret = fh_new_number((double) tween_ease(easing, (float) fh_get_number(&args[1])));
    return 0;
}

static int fn_love_tween_getEasings(struct fh_program *prog,
                                    struct fh_value *ret, struct fh_value *args, int n_args) {
    UNUSED(args);
    if (n_args != 0)
        return fh_set_error(prog, "love_tween_getEasings(): expected no arguments, got %d", n_args);

    int pin_state = fh_get_pin_state(prog);
    struct fh_array *arr = fh_make_array(prog, true);
    if (!fh_grow_array_object(prog, arr, tween_Easing_count))
        return fh_set_error(prog, "out of memory");

    struct fh_value out = fh_new_array(prog);
    for (int i = 0; i < tween_Easing_count; i++) {
        arr->items[i] = fh_new_string(prog, tween_easingName((tween_Easing) i));
    }

    fh_restore_pin_state(prog, pin_state);
    out.data.obj = arr;
    *ret = out;
    return 0;
}

#define DEF_FN(name) { #name, fn_##name }
static const struct fh_named_c_func c_funcs[] = {
    DEF_FN(love_tween_new),
    DEF_FN(love_tween_then),
    DEF_FN(love_tween_update),
    DEF_FN(love_tween_get),
    DEF_FN(love_tween_isDone),
    DEF_FN(love_tween_getProgress),
    DEF_FN(love_tween_getDuration),
    DEF_FN(love_tween_seek),
    DEF_FN(love_tween_reset),
    DEF_FN(love_tween_setLoops),
    DEF_FN(love_tween_setPaused),
    DEF_FN(love_tween_isPaused),
    DEF_FN(love_tween_ease),
    DEF_FN(love_tween_getEasings),
};

void fh_tween_register(struct fh_program *prog) {
    fh_add_c_funcs(prog, c_funcs, sizeof(c_funcs) / sizeof(c_funcs[0]));
}
