/*
#   clove
#
#   Copyright (C) 2019-2026 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/

#include "mouse.h"

#include "../include/mouse.h"

#include "../3rdparty/FH/src/value.h"
#include "../3rdparty/SDL2/include/SDL.h"

static struct {
    struct fh_program *prog;
} moduleData;

void fh_mouse_pressed(int x, int y, int button) {
    /* Optional callback: a game that doesn't handle the mouse isn't an error. */
    if (!fh_function_exists(moduleData.prog, "love_mousepressed")) {
        return;
    }
    struct fh_value _x = fh_new_number(x);
    struct fh_value _y = fh_new_number(y);
    struct fh_value _button = fh_new_string(moduleData.prog, mouse_button_to_str(button));

    struct fh_value args = fh_new_array(moduleData.prog);
    fh_grow_array(moduleData.prog, &args, 3);

    struct fh_array *arr = (struct fh_array *) args.data.obj;
    arr->items[0] = _x;
    arr->items[1] = _y;
    arr->items[2] = _button;

    if (fh_call_function(moduleData.prog, "love_mousepressed", &args, 1, NULL) < 0) {
        fh_set_error(moduleData.prog, fh_get_error(moduleData.prog));
    }
}

void fh_mouse_released(int x, int y, int button) {
    if (!fh_function_exists(moduleData.prog, "love_mousereleased")) {
        return;
    }
    struct fh_value _x = fh_new_number(x);
    struct fh_value _y = fh_new_number(y);
    struct fh_value _button = fh_new_string(moduleData.prog, mouse_button_to_str(button));

    struct fh_value args = fh_new_array(moduleData.prog);
    fh_grow_array(moduleData.prog, &args, 3);

    struct fh_array *arr = (struct fh_array *) args.data.obj;
    arr->items[0] = _x;
    arr->items[1] = _y;
    arr->items[2] = _button;

    if (fh_call_function(moduleData.prog, "love_mousereleased", &args, 1, NULL) < 0) {
        fh_set_error(moduleData.prog, fh_get_error(moduleData.prog));
    }
}

void fh_mouse_wheelmoved(int y) {
    if (!fh_function_exists(moduleData.prog, "love_wheelmoved")) {
        return;
    }
    struct fh_value _y = fh_new_number(y);

    if (fh_call_function(moduleData.prog, "love_wheelmoved", &_y, 1, NULL) < 0) {
        fh_set_error(moduleData.prog, fh_get_error(moduleData.prog));
    }
}


static int fn_love_mouse_isDown(struct fh_program *prog, struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_mouse_isDown(): expected 1 argument, got %d", n_args);

    // LOVE numbers the buttons; CLove has always named them. Accept both, so
    // love_mouse_isDown(1) does what a reader of LOVE's docs expects instead
    // of failing a type check.
    const char *name;
    if (fh_is_number(&args[0])) {
        switch ((int)fh_get_number(&args[0])) {
        case 1:  name = "l";  break;
        case 2:  name = "r";  break;
        case 3:  name = "m";  break;
        case 4:  name = "x1"; break;
        case 5:  name = "x2"; break;
        default:
            return fh_set_error(prog, "love_mouse_isDown(): button %d is not one of 1..5",
                                (int)fh_get_number(&args[0]));
        }
    } else if (fh_is_string(&args[0])) {
        name = fh_get_string(&args[0]);
    } else {
        return fh_set_error(prog, "Expected a button name or number, got %s",
                            fh_type_to_str(prog, args[0].type));
    }

    // An unknown name used to come back as a permanent `false` -- the error
    // below could never fire, because mouse_isDown() returned 0 both for
    // "not pressed" and for "no such button".
    if (!mouse_isButtonName(name)) {
        return fh_set_error(prog, "love_mouse_isDown(): '%s' is not a button "
                                  "(use \"l\", \"r\", \"m\", \"x1\", \"x2\" or 1..5)", name);
    }

    *ret = fh_new_bool(mouse_isDown(name) != 0);
    return 0;
}

static int fn_love_mouse_getPosition(struct fh_program *prog, struct fh_value *ret, struct fh_value *args, int n_args)  {
   UNUSED(args);
   UNUSED(n_args);
    int x, y;
    mouse_getPosition(&x, &y);

    struct fh_value _x = fh_new_number(x);
    struct fh_value _y = fh_new_number(y);

    struct fh_value pos = fh_new_array(prog);
    fh_grow_array(prog, &pos, 2);

    struct fh_array *arr = (struct fh_array *) pos.data.obj;
    arr->items[0] = _x;
    arr->items[1] = _y;

    *ret = pos;
    return 0;
}

static int fn_love_mouse_getX(struct fh_program *prog, struct fh_value *ret, struct fh_value *args, int n_args)  {
    UNUSED(prog);
    UNUSED(args);
    UNUSED(n_args);
    *ret = fh_new_number(mouse_getX());
    return 0;
}


static int fn_love_mouse_getY(struct fh_program *prog, struct fh_value *ret, struct fh_value *args, int n_args) {
    UNUSED(prog);
    UNUSED(args);
    UNUSED(n_args);
    *ret = fh_new_number(mouse_getY());
    return 0;
}

static int fn_love_mouse_setPosition(struct fh_program *prog, struct fh_value *ret, struct fh_value *args, int n_args)  {
    UNUSED(ret);
    if (n_args != 2) {
        return fh_set_error(prog, "Expected 2 arguments, got: %d\n", n_args);
    }

    if (!fh_is_number(&args[0]) || !fh_is_number(&args[1])) {
        return fh_set_error(prog, "Expected two numbers, got: %s and %s\n",
                            fh_type_to_str(prog, args[0].type), fh_type_to_str(prog, args[1].type));
    }

    int x = (int)fh_get_number(&args[0]);
    int y = (int)fh_get_number(&args[1]);
    mouse_setPosition(x, y);
    return 0;
}

static int fn_love_mouse_isVisible(struct fh_program *prog, struct fh_value *ret, struct fh_value *args, int n_args)  {
    UNUSED(prog);
    UNUSED(args);
    UNUSED(n_args);
    *ret = fh_new_bool(mouse_isVisible());
    // A binding returns 0 for success; this one said 1.
    return 0;
}

static int fn_love_mouse_setVisible(struct fh_program *prog, struct fh_value *ret, struct fh_value *args, int n_args)  {
    UNUSED(ret);
    if (n_args != 1)
        return fh_set_error(prog, "love_mouse_setVisible(): expected 1 argument, got %d", n_args);

    if (!fh_is_bool(&args[0])) {
        return fh_set_error(prog, "Expected type boolean, got %s\n", fh_type_to_str(prog, args[0].type));
    }

    mouse_setVisible(fh_get_bool(&args[0]));
    return 0;
}

static int fn_love_mouse_setX(struct fh_program *prog, struct fh_value *ret, struct fh_value *args, int n_args)  {
    UNUSED(ret);
    if (n_args != 1)
        return fh_set_error(prog, "love_mouse_setX(): expected 1 argument, got %d", n_args);

    if (!fh_is_number(&args[0])) {
        return fh_set_error(prog, "Expected type number, got %s\n", fh_type_to_str(prog, args[0].type));
    }
    mouse_setX((int)fh_get_number(&args[0]));
    return 0;
}

static int fn_love_mouse_setY(struct fh_program *prog, struct fh_value *ret, struct fh_value *args, int n_args) {
    UNUSED(ret);
    if (n_args != 1)
        return fh_set_error(prog, "love_mouse_setY(): expected 1 argument, got %d", n_args);

    if (!fh_is_number(&args[0])) {
        return fh_set_error(prog, "Expected type number, got %s\n", fh_type_to_str(prog, args[0].type));
    }
    mouse_setY((int)fh_get_number(&args[0]));
    return 0;
}

#define DEF_FN(name) { #name, fn_##name }
static const struct fh_named_c_func c_funcs[] = {
    DEF_FN(love_mouse_isDown),
    DEF_FN(love_mouse_setPosition),
    DEF_FN(love_mouse_getPosition),
    DEF_FN(love_mouse_getX),
    DEF_FN(love_mouse_getY),
    DEF_FN(love_mouse_isVisible),
    DEF_FN(love_mouse_setVisible),
    DEF_FN(love_mouse_setX),
    DEF_FN(love_mouse_setY),
};

void fh_mouse_register(struct fh_program* prog) {
    moduleData.prog = prog;
    fh_add_c_funcs(prog, c_funcs, sizeof(c_funcs)/sizeof(c_funcs[0]));
}
