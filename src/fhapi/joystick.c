/*
#   clove
#
#   Copyright (C) 2019-2026 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/

#include "joystick.h"

#include "../include/joystick.h"

#include "../include/utils.h"

#include "../3rdparty/FH/src/value.h"

static struct {
    struct fh_program *prog;
} moduleData;

void fh_joystick_pressed(int id, int button) {
    /* Optional callback: a game that doesn't handle joysticks isn't an error. */
    if (!fh_function_exists(moduleData.prog, "love_joystickpressed")) {
        return;
    }
    struct fh_value _id = fh_new_number(id);
    struct fh_value button_str = fh_new_string(moduleData.prog,
            joystick_convert_button_to_str(button));

    struct fh_value args = fh_new_array(moduleData.prog);
    fh_grow_array(moduleData.prog, &args, 2);

    struct fh_array *arr = (struct fh_array *) args.data.obj;
    arr->items[0] = _id;
    arr->items[1] = button_str;


    if (fh_call_function(moduleData.prog, "love_joystickpressed", &args, 1, NULL) < 0) {
        fh_set_error(moduleData.prog, "Error: %s\n", fh_get_error(moduleData.prog));
    }
}

void fh_joystick_released(int id, int button) {
    if (!fh_function_exists(moduleData.prog, "love_joystickreleased")) {
        return;
    }
    struct fh_value _id = fh_new_number(id);
    struct fh_value button_str = fh_new_string(moduleData.prog,
            joystick_convert_button_to_str(button));

    struct fh_value args = fh_new_array(moduleData.prog);
    fh_grow_array(moduleData.prog, &args, 2);

    struct fh_array *arr = (struct fh_array *) args.data.obj;
    arr->items[0] = _id;
    arr->items[1] = button_str;


    if (fh_call_function(moduleData.prog, "love_joystickreleased", &args, 1, NULL) < 0) {
        fh_set_error(moduleData.prog, "Error: %s\n", fh_get_error(moduleData.prog));
    }
}


// Every call takes the joystick's id, the number the connect/press callbacks
// report. joystick_get() returns NULL for an id that is not plugged in, and
// the engine functions below all dereference it, so each binding checks.
static joystick_Joystick *require_stick(struct fh_program *prog, struct fh_value *args,
                                        int n_args, char const *fn) {
    if (n_args < 1) {
        fh_set_error(prog, "%s: expected at least 1 argument (the joystick id), got %d", fn, n_args);
        return NULL;
    }
    if (!fh_is_number(&args[0])) {
        fh_set_error(prog, "%s: the joystick id must be a number", fn);
        return NULL;
    }

    joystick_Joystick *js = joystick_get((SDL_JoystickID) fh_get_number(&args[0]));
    if (!js) {
        fh_set_error(prog, "%s: no joystick with id %d is connected",
                     fn, (int) fh_get_number(&args[0]));
        return NULL;
    }
    return js;
}

static int fn_love_joystick_getCount(struct fh_program *prog,
                                     struct fh_value *ret, struct fh_value *args, int n_args) {
    UNUSED(args);
    if (n_args != 0)
        return fh_set_error(prog, "love_joystick_getCount(): expected no arguments, got %d", n_args);

    *ret = fh_new_number(joystick_getCount());
    return 0;
}

static int fn_love_joystick_getName(struct fh_program *prog,
                                    struct fh_value *ret, struct fh_value *args, int n_args) {
    joystick_Joystick *js = require_stick(prog, args, n_args, "love_joystick_getName()");
    if (!js) return -1;

    char const *name = joystick_getName(js);
    *ret = fh_new_string(prog, name ? name : "");
    return 0;
}

static int fn_love_joystick_isConnected(struct fh_program *prog,
                                        struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_joystick_isConnected(): expected 1 argument, got %d", n_args);
    if (!fh_is_number(&args[0]))
        return fh_set_error(prog, "love_joystick_isConnected(): the joystick id must be a number");

    // Unlike the rest, this one answers for an id that is not there rather
    // than erroring -- that is the question it is being asked.
    joystick_Joystick *js = joystick_get((SDL_JoystickID) fh_get_number(&args[0]));
    *ret = fh_new_bool(js != NULL && joystick_isConnected(js));
    return 0;
}

static int fn_love_joystick_isGamepad(struct fh_program *prog,
                                      struct fh_value *ret, struct fh_value *args, int n_args) {
    joystick_Joystick *js = require_stick(prog, args, n_args, "love_joystick_isGamepad()");
    if (!js) return -1;

    *ret = fh_new_bool(joystick_isGamepad(js));
    return 0;
}

static int fn_love_joystick_isDown(struct fh_program *prog,
                                   struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 2)
        return fh_set_error(prog, "love_joystick_isDown(): expected 2 arguments (id, button), got %d", n_args);

    joystick_Joystick *js = require_stick(prog, args, n_args, "love_joystick_isDown()");
    if (!js) return -1;

    // The button may be its number or the name the callbacks hand back.
    int button;
    if (fh_is_string(&args[1])) {
        button = joystick_convert_str_to_button(fh_get_string(&args[1]));
        if (button < 0)
            return fh_set_error(prog, "love_joystick_isDown(): '%s' is not a button name",
                                fh_get_string(&args[1]));
    } else if (fh_is_number(&args[1])) {
        button = (int) fh_get_number(&args[1]);
    } else {
        return fh_set_error(prog, "love_joystick_isDown(): expected a button name or number");
    }

    *ret = fh_new_bool(joystick_isDown(js, button));
    return 0;
}

static int fn_love_joystick_getAxis(struct fh_program *prog,
                                    struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 2)
        return fh_set_error(prog, "love_joystick_getAxis(): expected 2 arguments (id, axis), got %d", n_args);

    joystick_Joystick *js = require_stick(prog, args, n_args, "love_joystick_getAxis()");
    if (!js) return -1;

    if (!fh_is_number(&args[1]))
        return fh_set_error(prog, "love_joystick_getAxis(): the axis must be a number");

    int axis = (int) fh_get_number(&args[1]);
    if (axis < 0 || axis >= joystick_getNumAxes(js))
        return fh_set_error(prog, "love_joystick_getAxis(): this joystick has %d axes, asked for %d",
                            joystick_getNumAxes(js), axis);

    *ret = fh_new_number((double) joystick_getAxis(js, axis));
    return 0;
}

static int fn_love_joystick_getGamepadAxis(struct fh_program *prog,
                                           struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 2)
        return fh_set_error(prog, "love_joystick_getGamepadAxis(): expected 2 arguments (id, axis), got %d", n_args);

    joystick_Joystick *js = require_stick(prog, args, n_args, "love_joystick_getGamepadAxis()");
    if (!js) return -1;

    if (!fh_is_number(&args[1]))
        return fh_set_error(prog, "love_joystick_getGamepadAxis(): the axis must be a number");

    *ret = fh_new_number((double) joystick_getGamepadAxis(js, (int) fh_get_number(&args[1])));
    return 0;
}

static int fn_love_joystick_getAxisCount(struct fh_program *prog,
                                         struct fh_value *ret, struct fh_value *args, int n_args) {
    joystick_Joystick *js = require_stick(prog, args, n_args, "love_joystick_getAxisCount()");
    if (!js) return -1;

    *ret = fh_new_number(joystick_getNumAxes(js));
    return 0;
}

static int fn_love_joystick_getButtonCount(struct fh_program *prog,
                                           struct fh_value *ret, struct fh_value *args, int n_args) {
    joystick_Joystick *js = require_stick(prog, args, n_args, "love_joystick_getButtonCount()");
    if (!js) return -1;

    *ret = fh_new_number(joystick_getNumButtons(js));
    return 0;
}

static int fn_love_joystick_getBallCount(struct fh_program *prog,
                                         struct fh_value *ret, struct fh_value *args, int n_args) {
    joystick_Joystick *js = require_stick(prog, args, n_args, "love_joystick_getBallCount()");
    if (!js) return -1;

    *ret = fh_new_number(joystick_getNumBalls(js));
    return 0;
}

static int fn_love_joystick_getHatCount(struct fh_program *prog,
                                        struct fh_value *ret, struct fh_value *args, int n_args) {
    joystick_Joystick *js = require_stick(prog, args, n_args, "love_joystick_getHatCount()");
    if (!js) return -1;

    *ret = fh_new_number(joystick_getHatCount(js));
    return 0;
}

static int fn_love_joystick_getHat(struct fh_program *prog,
                                   struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 2)
        return fh_set_error(prog, "love_joystick_getHat(): expected 2 arguments (id, hat), got %d", n_args);

    joystick_Joystick *js = require_stick(prog, args, n_args, "love_joystick_getHat()");
    if (!js) return -1;

    if (!fh_is_number(&args[1]))
        return fh_set_error(prog, "love_joystick_getHat(): the hat must be a number");

    *ret = fh_new_number(joystick_getHat(js, (int) fh_get_number(&args[1])));
    return 0;
}

#define DEF_FN(name) { #name, fn_##name }
static const struct fh_named_c_func c_funcs[] = {
    DEF_FN(love_joystick_getCount),
    DEF_FN(love_joystick_getName),
    DEF_FN(love_joystick_isConnected),
    DEF_FN(love_joystick_isGamepad),
    DEF_FN(love_joystick_isDown),
    DEF_FN(love_joystick_getAxis),
    DEF_FN(love_joystick_getGamepadAxis),
    DEF_FN(love_joystick_getAxisCount),
    DEF_FN(love_joystick_getButtonCount),
    DEF_FN(love_joystick_getBallCount),
    DEF_FN(love_joystick_getHatCount),
    DEF_FN(love_joystick_getHat),
};

void fh_joystick_register(struct fh_program *prog) {
    moduleData.prog = prog;
    fh_add_c_funcs(prog, c_funcs, sizeof(c_funcs)/sizeof(c_funcs[0]));
}
