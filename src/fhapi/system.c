/*
#   clove
#
#   Copyright (C) 2016-2026 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/
#include "system.h"

#include <string.h>

#include "../include/system.h"
#include "../include/utils.h"

#include "../3rdparty/FH/src/value.h"

/* love.system was bound on the Lua side and not on the FH one, so a game
 * written in the default language could not ask what it was running on, use
 * the clipboard, or size a thread pool. The engine functions were already
 * there; this is only the binding. */

static int fn_love_system_getOS(struct fh_program *prog,
                                struct fh_value *ret, struct fh_value *args, int n_args) {
    UNUSED(args);
    if (n_args != 0)
        return fh_set_error(prog, "love_system_getOS(): expected no arguments, got %d", n_args);

    const char *os = system_getOS();
    *ret = fh_new_string(prog, os ? os : "unknown");
    return 0;
}

static int fn_love_system_getProcessorCount(struct fh_program *prog,
                                            struct fh_value *ret, struct fh_value *args, int n_args) {
    UNUSED(args);
    if (n_args != 0)
        return fh_set_error(prog, "love_system_getProcessorCount(): expected no arguments, got %d", n_args);

    *ret = fh_new_number(system_getProcessorCount());
    return 0;
}

static int fn_love_system_getClipboardText(struct fh_program *prog,
                                           struct fh_value *ret, struct fh_value *args, int n_args) {
    UNUSED(args);
    if (n_args != 0)
        return fh_set_error(prog, "love_system_getClipboardText(): expected no arguments, got %d", n_args);

    const char *text = system_getClipboardText();
    *ret = fh_new_string(prog, text ? text : "");
    return 0;
}

static int fn_love_system_setClipboardText(struct fh_program *prog,
                                           struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_system_setClipboardText(): expected 1 argument, got %d", n_args);
    if (!fh_is_string(&args[0]))
        return fh_set_error(prog, "love_system_setClipboardText(): expected a string");

    system_setClipboardText(fh_get_string(&args[0]));
    *ret = fh_new_null();
    return 0;
}

/* love_system_getPowerInfo() -> [state, seconds, percent]
 *
 * `state` is SDL's word for it ("battery", "charging", "charged", "nobattery",
 * "unknown"); `seconds` and `percent` are -1 when the platform will not say,
 * which is common on a desktop. */
static int fn_love_system_getPowerInfo(struct fh_program *prog,
                                       struct fh_value *ret, struct fh_value *args, int n_args) {
    UNUSED(args);
    if (n_args != 0)
        return fh_set_error(prog, "love_system_getPowerInfo(): expected no arguments, got %d", n_args);

    system_PowerState power = system_getPowerInfo();

    int pin_state = fh_get_pin_state(prog);
    struct fh_array *arr = fh_make_array(prog, true);
    if (!fh_grow_array_object(prog, arr, 3))
        return fh_set_error(prog, "out of memory");

    struct fh_value out = fh_new_array(prog);
    arr->items[0] = fh_new_string(prog, power.state ? power.state : "unknown");
    arr->items[1] = fh_new_number(power.seconds);
    arr->items[2] = fh_new_number(power.percent);

    fh_restore_pin_state(prog, pin_state);
    out.data.obj = arr;
    *ret = out;
    return 0;
}

#define DEF_FN(name) { #name, fn_##name }
static const struct fh_named_c_func c_funcs[] = {
    DEF_FN(love_system_getOS),
    DEF_FN(love_system_getProcessorCount),
    DEF_FN(love_system_getClipboardText),
    DEF_FN(love_system_setClipboardText),
    DEF_FN(love_system_getPowerInfo),
};

void fh_system_register(struct fh_program *prog) {
    fh_add_c_funcs(prog, c_funcs, sizeof(c_funcs) / sizeof(c_funcs[0]));
}
