/*
#   clove
#
#   Copyright (C) 2019 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/

#include "ui.h"

#include "../3rdparty/FH/src/value.h"

#include "../3rdparty/microui/src/microui.h"
#include "../include/ui.h"

/* Widgets, layout and draw commands all read the top of one of microui's
 * stacks through expect(), which aborts the process. Nothing but a window, a
 * panel or a popup fills those stacks, so a script calling one of these at top
 * level used to take CLove down with "assertion failed" -- turn it into an
 * ordinary script error instead. */
#define UI_NEED_CONTAINER(fname) \
    do { \
        if (!ui_in_container()) \
            return fh_set_error(prog, fname "(): must be called inside a " \
                                "window, a panel or a popup"); \
    } while (0)


static fh_c_obj_gc_callback ui_container_gc(mu_Container *cnt) {
    free(cnt);
    return (fh_c_obj_gc_callback) 1;
}

static int fn_love_ui_newContainer(struct fh_program *prog,
                                   struct fh_value *ret, struct fh_value *args, int n_args) {
    mu_Container *cnt = malloc(sizeof(mu_Container));
    fh_c_obj_gc_callback *callback = ui_container_gc;
    *ret = fh_new_c_obj(prog, cnt, callback, FH_UI_TYPE);
    return 0;
}

static int fn_love_ui_layout_next(struct fh_program *prog,
                                  struct fh_value *ret, struct fh_value *args, int n_args) {
    UI_NEED_CONTAINER("love_ui_layout_next");
    UNUSED(args);
    if (n_args != 0) {
        return fh_set_error(prog, "This function doesn't expect any arguments");
    }
    mu_Rect rect = ui_layout_next();
    int pin_state = fh_get_pin_state(prog);
    struct fh_array *arr = fh_make_array(prog, true);
    fh_grow_array_object(prog, arr, 4);
    arr->items[0] = fh_new_number(rect.x);
    arr->items[1] = fh_new_number(rect.y);
    arr->items[2] = fh_new_number(rect.w);
    arr->items[3] = fh_new_number(rect.h);

    struct fh_value arr_value = fh_new_array(prog);
    arr_value.data.obj = arr;

    *ret = arr_value;
    fh_restore_pin_state(prog, pin_state);
    return 0;
}

static int fn_love_ui_layout_setNext(struct fh_program *prog,
                                     struct fh_value *ret, struct fh_value *args, int n_args) {
    UI_NEED_CONTAINER("love_ui_layout_setNext");
    if (n_args != 5)
        return fh_set_error(prog, "Expected 5 arguments, got %d\n", n_args);

    for (int i = 0; i < 3; i++) {
        if (!fh_is_number(&args[i]))
            return fh_set_error(prog, "Expected number at argument index %d", i);
    }

    if (!fh_is_bool(&args[4]))
        return fh_set_error(prog, "Expected boolean at index 4");

    mu_Rect r;
    r.x = (int) fh_get_number(&args[0]);
    r.y = (int) fh_get_number(&args[1]);
    r.w = (int) fh_get_number(&args[2]);
    r.h = (int) fh_get_number(&args[3]);

    bool relative = fh_get_bool(&args[4]);

    mu_layout_set_next(ui_get_context(), r, relative);
    *ret = fh_new_null();
    return 0;
}

static int fn_love_ui_rect(struct fh_program *prog,
                           struct fh_value *ret, struct fh_value *args, int n_args) {
    UI_NEED_CONTAINER("love_ui_rect");
    if (n_args < 7)
        return fh_set_error(prog, "Expected at least 7 arguments, got %d", n_args);

    for (int i = 0; i < 6; i++) {
        if (!fh_is_number(&args[i])) {
            return fh_set_error(prog, "Expected number at argument %d", i);
        }
    }

    mu_Rect rect;
    rect.x = fh_get_number(&args[0]);
    rect.y = fh_get_number(&args[1]);
    rect.w = fh_get_number(&args[2]);
    rect.h = fh_get_number(&args[3]);

    mu_Color color;
    color.r = fh_get_number(&args[4]);
    color.g = fh_get_number(&args[5]);
    color.b = fh_get_number(&args[6]);
    color.a = fh_optnumber(args, n_args, 7, 255);

    ui_rect(rect, color);
    *ret = fh_new_null();
    return 0;
}

static int fn_love_ui_control_text(struct fh_program *prog,
                                   struct fh_value *ret, struct fh_value *args, int n_args) {
    UI_NEED_CONTAINER("love_ui_control_text");
    if (n_args < 6)
        return fh_set_error(prog, "Expected at least 6 arguments, got %d", n_args);

    if (!fh_is_string(&args[0])) {
        return fh_set_error(prog, "Expected string for argument 0");
    }

    const char *str = fh_get_string(&args[0]);

    for (int i = 1; i < 6; i++) {
        if (!fh_is_number(&args[i])) {
            return fh_set_error(prog, "Expected number at argument %d", i);
        }
    }

    mu_Rect rect;
    rect.x = fh_get_number(&args[1]);
    rect.y = fh_get_number(&args[2]);
    rect.w = fh_get_number(&args[3]);
    rect.h = fh_get_number(&args[4]);

    int colorid = (int) fh_get_number(&args[5]);
    int opt = fh_optnumber(args, n_args, 6, MU_OPT_ALIGNRIGHT);
    ui_draw_control_text(str, rect, colorid, opt);
    *ret = fh_new_null();
    return 0;
}

static int fn_love_ui_getContainerScroll(struct fh_program *prog,
                                         struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_ui_getContainerScroll(): expected 1 argument, got %d", n_args);
    if (!fh_is_c_obj_of_type(&args[0], FH_UI_TYPE)) {
        return fh_set_error(prog, "Expected argument 0 to be an ui container");
    }
    mu_Container *cnt = fh_get_c_obj_value(&args[0]);
    int pin_state = fh_get_pin_state(prog);
    struct fh_array *arr = fh_make_array(prog, true);
    fh_grow_array_object(prog, arr, 2);
    arr->items[0] = fh_new_number(cnt->scroll.x);
    arr->items[1] = fh_new_number(cnt->scroll.y);

    struct fh_value arr_value = fh_new_array(prog);
    arr_value.data.obj = arr;

    *ret = arr_value;
    fh_restore_pin_state(prog, pin_state);
    return 0;
}

static int fn_love_ui_setContainerScroll(struct fh_program *prog,
                                         struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 3)
        return fh_set_error(prog, "love_ui_setContainerScroll(): expected 3 arguments, got %d", n_args);
    if (!fh_is_c_obj_of_type(&args[0], FH_UI_TYPE)) {
        return fh_set_error(prog, "Expected argument 0 to be an ui container");
    }
    mu_Container *cnt = fh_get_c_obj_value(&args[0]);

    int x = (int) fh_get_number(&args[1]);
    int y = (int) fh_get_number(&args[2]);

    cnt->scroll.x = x;
    cnt->scroll.y = y;
    *ret = fh_new_null();
    return 0;
}

static int fn_love_ui_getContainerContentSize(struct fh_program *prog,
                                              struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_ui_getContainerContentSize(): expected 1 argument, got %d", n_args);
    if (!fh_is_c_obj_of_type(&args[0], FH_UI_TYPE)) {
        return fh_set_error(prog, "Expected argument 0 to be an ui container");
    }
    mu_Container *cnt = fh_get_c_obj_value(&args[0]);
    int pin_state = fh_get_pin_state(prog);
    struct fh_array *arr = fh_make_array(prog, true);
    fh_grow_array_object(prog, arr, 2);
    arr->items[0] = fh_new_number(cnt->content_size.x);
    arr->items[1] = fh_new_number(cnt->content_size.y);

    struct fh_value arr_value = fh_new_array(prog);
    arr_value.data.obj = arr;

    *ret = arr_value;
    fh_restore_pin_state(prog, pin_state);
    return 0;
}

static int fn_love_ui_getContainerInfo(struct fh_program *prog,
                                       struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_ui_getContainerInfo(): expected 1 argument, got %d", n_args);
    if (!fh_is_c_obj_of_type(&args[0], FH_UI_TYPE)) {
        return fh_set_error(prog, "Expected argument 0 to be an ui container");
    }
    mu_Container *cnt = fh_get_c_obj_value(&args[0]);
    int x = cnt->rect.x;
    int y = cnt->rect.y;
    int w = cnt->rect.w;
    int h = cnt->rect.h;
    int zindex = cnt->zindex;
    bool open = cnt->open;

    int pin_state = fh_get_pin_state(prog);
    struct fh_array *arr = fh_make_array(prog, true);
    fh_grow_array_object(prog, arr, 6);
    arr->items[0] = fh_new_number(x);
    arr->items[1] = fh_new_number(y);
    arr->items[2] = fh_new_number(w);
    arr->items[3] = fh_new_number(h);
    arr->items[4] = fh_new_number(zindex);
    arr->items[5] = fh_new_bool(open);

    struct fh_value arr_value = fh_new_array(prog);
    arr_value.data.obj = arr;

    *ret = arr_value;
    fh_restore_pin_state(prog, pin_state);
    return 0;
}

static int fn_love_ui_setContainerInfo(struct fh_program *prog,
                                       struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args < 1)
        return fh_set_error(prog, "love_ui_setContainerInfo(): expected at least 1 argument, got %d", n_args);
    if (!fh_is_c_obj_of_type(&args[0], FH_UI_TYPE)) {
        return fh_set_error(prog, "Expected argument 0 to be an ui container");
    }
    mu_Container *cnt = fh_get_c_obj_value(&args[0]);


    int x = (int) fh_optnumber(args, n_args, 1, cnt->rect.x);
    int y = (int) fh_optnumber(args, n_args, 2, cnt->rect.y);
    int w = (int) fh_optnumber(args, n_args, 3, cnt->rect.w);
    int h = (int) fh_optnumber(args, n_args, 4, cnt->rect.h);
    int zindex = (int) fh_optnumber(args, n_args, 5, cnt->zindex);
    int open = (int) fh_optnumber(args, n_args, 6, cnt->open);

    cnt->rect.x = x;
    cnt->rect.y = y;
    cnt->rect.w = w;
    cnt->rect.h = h;
    cnt->zindex = zindex;
    cnt->open = open;

    *ret = fh_new_null();
    return 0;
}

static int fn_love_ui_begin_popup(struct fh_program *prog,
                                  struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_ui_begin_popup(): expected 1 argument, got %d", n_args);
    if (!fh_is_string(&args[0])) {
        return fh_set_error(prog, "expected title, string at index 0");
    }

    const char *name = fh_get_string(&args[0]);

    *ret = fh_new_bool(ui_begin_popup(name));

    return 0;
}

static int fn_love_ui_open_popup(struct fh_program *prog,
                                 struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_ui_open_popup(): expected 1 argument, got %d", n_args);
    if (!fh_is_string(&args[0])) {
        return fh_set_error(prog, "expected title, string at index 0");
    }

    const char *name = fh_get_string(&args[0]);

    ui_open_popup(name);

    *ret = fh_new_null();
    return 0;
}

/* love_ui_setWindowOpen(name, open)
 *
 * microui's close button latches a window shut: its container's open flag goes
 * to 0 and love_ui_begin_window() answers false from then on. Call this to
 * show it again -- typically when whatever check box or menu item owns the
 * window is switched back on. */
static int fn_love_ui_setWindowOpen(struct fh_program *prog,
                                    struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 2)
        return fh_set_error(prog, "love_ui_setWindowOpen(): expected 2 arguments, got %d", n_args);
    if (!fh_is_string(&args[0]))
        return fh_set_error(prog, "love_ui_setWindowOpen(): expected the window name as a string");
    if (!fh_is_bool(&args[1]))
        return fh_set_error(prog, "love_ui_setWindowOpen(): expected a boolean");

    ui_set_window_open(fh_get_string(&args[0]), fh_get_bool(&args[1]) ? 1 : 0);
    *ret = fh_new_null();
    return 0;
}

/* love_ui_mouse_over() -> bool
 *
 * Whether the pointer is over any microui window, panel or popup. A game that
 * draws its own viewport beneath floating UI asks this before it acts on a
 * click, so a press meant for a window does not also land in the scene. */
static int fn_love_ui_mouse_over(struct fh_program *prog,
                                 struct fh_value *ret, struct fh_value *args, int n_args) {
    (void) args;
    if (n_args != 0)
        return fh_set_error(prog, "love_ui_mouse_over(): expected no arguments, got %d", n_args);

    *ret = fh_new_bool(ui_mouse_over() != 0);
    return 0;
}

/* love_ui_popup_open(name) -> bool
 *
 * Whether the named popup is on screen. love_ui_begin_popup() still returns
 * true on the frame the popup closes, so a script that needs to know "is a
 * menu up right now" -- to keep the click that dismisses it from also doing
 * something underneath -- has to ask this instead. Call it from the same
 * window that opened the popup: the name is hashed against the id stack. */
static int fn_love_ui_popup_open(struct fh_program *prog,
                                 struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_ui_popup_open(): expected 1 argument, got %d", n_args);
    if (!fh_is_string(&args[0]))
        return fh_set_error(prog, "love_ui_popup_open(): expected the popup name as a string");

    *ret = fh_new_bool(ui_popup_open(fh_get_string(&args[0])) != 0);
    return 0;
}

static int fn_love_ui_end_popup(struct fh_program *prog,
                                struct fh_value *ret, struct fh_value *args, int n_args) {
    UI_NEED_CONTAINER("love_ui_end_popup");
    UNUSED(prog);
    UNUSED(args);
    UNUSED(n_args);

    ui_end_popup();
    *ret = fh_new_null();
    return 0;
}

static int fn_love_ui_begin_window(struct fh_program *prog,
                                   struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args < 5)
        return fh_set_error(prog, "love_ui_begin_window(): expected at least 5 arguments, got %d", n_args);
    if (!fh_is_string(&args[0])) {
        return fh_set_error(prog, "expected title, string at index 0");
    }

    for (int i = 1; i <= 4; i++) {
        if (!fh_is_number(&args[i])) {
            return fh_set_error(prog, "Expected number value at index %d", i);
        }
    }

    const char *title = fh_get_string(&args[0]);

    mu_Rect rect;
    rect.x = fh_get_number(&args[1]);
    rect.y = fh_get_number(&args[2]);
    rect.w = fh_get_number(&args[3]);
    rect.h = fh_get_number(&args[4]);

    int opt = fh_optnumber(args, n_args, 5, 0);

    *ret = fh_new_bool(ui_begin_window(title, rect, opt));
    return 0;
}

static int fn_love_ui_end_window(struct fh_program *prog,
                                 struct fh_value *ret, struct fh_value *args, int n_args) {
    UI_NEED_CONTAINER("love_ui_end_window");
    UNUSED(prog);
    UNUSED(args);
    UNUSED(n_args);
    ui_end_window();
    *ret = fh_new_null();
    return 0;
}

static int fn_love_ui_layout_row(struct fh_program *prog,
                                 struct fh_value *ret, struct fh_value *args, int n_args) {
    UI_NEED_CONTAINER("love_ui_layout_row");
    if (n_args != 3) {
        return fh_set_error(prog, "Invalid number of arguments, expected 3 got: %d", n_args);
    }

    if (!fh_is_number(&args[0])) {
        return fh_set_error(prog, "Expected number as argument 0");
    }

    const int no_items = (int) fh_get_number(&args[0]);
    const int height = (int) fh_optnumber(args, n_args, 2, 0);

    if (no_items > -1 && fh_is_array(&args[1])) {
        struct fh_value *arr = &args[1];
        const int len = fh_get_array_len(arr);
        int *size = malloc((size_t) len * sizeof(int));
        if (!size)
            return fh_set_error(prog, "out of memory");
        struct fh_array *a = GET_VAL_ARRAY(arr);
        for (int i = 0; i < len; i++) {
            if (!fh_is_number(&a->items[i])) {
                free(size);
                return fh_set_error(prog, "Expected index %d in array to be of type number, got %s", i,
                                    fh_type_to_str(prog, a->items[i].type));
            }
            size[i] = (int) fh_get_number(&a->items[i]);
        }
        ui_layout_row(no_items, size, height);
        free(size);
    } else if (no_items == -1 && fh_is_null(&args[1])) {
        ui_layout_row(-1, NULL, height);
    } else {
        return fh_set_error(prog, "Expected null or array of numbers as argument 1");
    }
    *ret = fh_new_null();
    return 0;
}

static int fn_love_ui_layout_width(struct fh_program *prog,
                                   struct fh_value *ret, struct fh_value *args, int n_args) {
    UI_NEED_CONTAINER("love_ui_layout_width");
    if (n_args != 1)
        return fh_set_error(prog, "love_ui_layout_width(): expected 1 argument, got %d", n_args);
    if (!fh_is_number(&args[0]))
        return fh_set_error(prog, "Expected number");

    int width = (int) fh_get_number(&args[0]);
    mu_layout_width(ui_get_context(), width);
    *ret = fh_new_null();
    return 0;
}

static int fn_love_ui_layout_height(struct fh_program *prog,
                                    struct fh_value *ret, struct fh_value *args, int n_args) {
    UI_NEED_CONTAINER("love_ui_layout_height");
    if (n_args != 1)
        return fh_set_error(prog, "love_ui_layout_height(): expected 1 argument, got %d", n_args);
    if (!fh_is_number(&args[0]))
        return fh_set_error(prog, "Expected number");

    int height = (int) fh_get_number(&args[0]);
    mu_layout_height(ui_get_context(), height);
    *ret = fh_new_null();
    return 0;
}

static int fn_love_ui_header(struct fh_program *prog,
                             struct fh_value *ret, struct fh_value *args, int n_args) {
    UI_NEED_CONTAINER("love_ui_header");
    if (n_args < 1)
        return fh_set_error(prog, "love_ui_header(): expected at least 1 argument, got %d", n_args);
    if (!fh_is_string(&args[0])) {
        return fh_set_error(prog, "Expected string title for argument 0");
    }
    const char *title = fh_get_string(&args[0]);
    int opt = (int) fh_optnumber(args, n_args, 1, MU_OPT_ALIGNRIGHT);
    *ret = fh_new_bool(ui_header(title, opt));
    return 0;
}

static int fn_love_ui_begin_tree(struct fh_program *prog,
                                 struct fh_value *ret, struct fh_value *args, int n_args) {
    UI_NEED_CONTAINER("love_ui_begin_tree");
    if (n_args < 1)
        return fh_set_error(prog, "love_ui_begin_tree(): expected at least 1 argument, got %d", n_args);
    if (!fh_is_string(&args[0])) {
        return fh_set_error(prog, "Expected string for argument 0");
    }
    const char *title = fh_get_string(&args[0]);
    int opt = fh_optnumber(args, n_args, 1, MU_RES_ACTIVE);
    *ret = fh_new_bool(ui_begin_tree(title, opt));
    return 0;
}

static int fn_love_ui_end_tree(struct fh_program *prog,
                               struct fh_value *ret, struct fh_value *args, int n_args) {
    UI_NEED_CONTAINER("love_ui_end_tree");
    UNUSED(prog);
    UNUSED(args);
    UNUSED(n_args);
    ui_end_tree();
    *ret = fh_new_null();
    return 0;
}

static int fn_love_ui_begin_panel(struct fh_program *prog,
                                  struct fh_value *ret, struct fh_value *args, int n_args) {
    UI_NEED_CONTAINER("love_ui_begin_panel");
    if (n_args < 2)
        return fh_set_error(prog, "love_ui_begin_panel(): expected at least 2 arguments, got %d", n_args);
    if (!fh_is_c_obj_of_type(&args[0], FH_UI_TYPE) ||
        !fh_is_string(&args[1])) {
        return fh_set_error(prog, "Expected argument 0 to be an ui window and argument 1 to be a title");
    }

    mu_Container *cnt = fh_get_c_obj_value(&args[0]);
    const char *name = fh_get_string(&args[1]);
    int opt = (int) fh_optnumber(args, n_args, 2, MU_OPT_ALIGNRIGHT);
    ui_begin_panel(cnt, name, opt);

    *ret = fh_new_null();
    return 0;
}

static int fn_love_ui_end_panel(struct fh_program *prog,
                                struct fh_value *ret, struct fh_value *args, int n_args) {
    UI_NEED_CONTAINER("love_ui_end_panel");
    UNUSED(prog);
    UNUSED(ret);
    UNUSED(args);
    ui_end_panel();

    *ret = fh_new_null();
    return 0;
}

static int fn_love_ui_last_id(struct fh_program *prog,
                              struct fh_value *ret, struct fh_value *args, int n_args) {
    UNUSED(prog);
    UNUSED(ret);
    UNUSED(args);
    UNUSED(n_args);
    *ret = fh_new_number(ui_get_context()->last_id);
    return 0;
}

static int fn_love_ui_hasFocus(struct fh_program *prog,
                               struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_ui_hasFocus(): expected 1 argument, got %d", n_args);
    if (!fh_is_number(&args[0])) {
        return fh_set_error(prog, "expected id as number");
    }
    mu_Id id = (mu_Id) fh_get_number(&args[0]);
    *ret = fh_new_bool(ui_get_context()->focus == id);
    return 0;
}

static int fn_love_ui_setFocus(struct fh_program *prog,
                               struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_ui_setFocus(): expected 1 argument, got %d", n_args);
    if (!fh_is_number(&args[0])) {
        return fh_set_error(prog, "expected id as number");
    }
    int id = fh_get_number(&args[0]);
    mu_set_focus(ui_get_context(), id);
    *ret = fh_new_bool(true);
    return 0;
}

static int fn_love_ui_slider(struct fh_program *prog,
                             struct fh_value *ret, struct fh_value *args, int n_args) {
    UI_NEED_CONTAINER("love_ui_slider");
    if (n_args < 4)
        return fh_set_error(prog, "love_ui_slider(): expected at least 4 arguments, got %d", n_args);
    for (int i = 0; i < 4; i++) {
        if (!fh_is_number(&args[i])) {
            return fh_set_error(prog, "Argument %d was expected to be of type number", i);
        }
    }
    double value = fh_get_number(&args[0]);
    double low = fh_get_number(&args[1]);
    double high = fh_get_number(&args[2]);
    double step = fh_optnumber(args, n_args, 3, 0);
    /* The id keeps sliders apart; without one they would all be the same
       widget and none of them would respond. */
    int id = (int) fh_optnumber(args, n_args, 4, 0);
    int opt = (int) fh_optnumber(args, n_args, 5, MU_OPT_ALIGNRIGHT);
    /* how many decimals the value is shown with; 2 is microui's own default */
    int decimals = (int) fh_optnumber(args, n_args, 6, 2);
    *ret = fh_new_number(ui_slider(value, low, high, step, id, opt, decimals));
    return 0;
}

/*
 * love_ui_number(value, step, [id], [opt]) -> value
 *
 * A drag-to-edit number field: unlike a slider it has no fixed range, which
 * is what an inspector wants for coordinates and sizes. Give each field on a
 * panel a different id, or they all become the same widget.
 */
static int fn_love_ui_number(struct fh_program *prog,
                             struct fh_value *ret, struct fh_value *args, int n_args) {
    UI_NEED_CONTAINER("love_ui_number");
    if (n_args < 2)
        return fh_set_error(prog, "love_ui_number(): expected at least 2 arguments, got %d", n_args);
    for (int i = 0; i < 2; i++) {
        if (!fh_is_number(&args[i])) {
            return fh_set_error(prog, "Argument %d was expected to be of type number", i);
        }
    }
    double value = fh_get_number(&args[0]);
    double step = fh_get_number(&args[1]);
    int id = (int) fh_optnumber(args, n_args, 2, 0);
    int opt = (int) fh_optnumber(args, n_args, 3, MU_OPT_ALIGNCENTER);
    int decimals = (int) fh_optnumber(args, n_args, 4, 2);
    *ret = fh_new_number(ui_number(value, step, id, opt, decimals));
    return 0;
}

/*
 * love_ui_pushId(key) / love_ui_popId()
 *
 * microui hashes a widget's label into its id, so two list rows with the same
 * text are the same widget. Wrap each row in a push/pop of something unique
 * (an entity id, a loop index) to keep them apart.
 */
static int fn_love_ui_pushId(struct fh_program *prog,
                             struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_ui_pushId(): expected 1 argument, got %d", n_args);

    if (fh_is_number(&args[0])) {
        int key = (int) fh_get_number(&args[0]);
        ui_push_id(&key, sizeof(key));
    } else if (fh_is_string(&args[0])) {
        const char *key = fh_get_string(&args[0]);
        ui_push_id(key, (int) strlen(key));
    } else {
        return fh_set_error(prog, "love_ui_pushId(): expected a number or a string");
    }

    *ret = fh_new_null();
    return 0;
}

static int fn_love_ui_popId(struct fh_program *prog,
                            struct fh_value *ret, struct fh_value *args, int n_args) {
    UNUSED(prog);
    UNUSED(args);
    UNUSED(n_args);
    ui_pop_id();
    *ret = fh_new_null();
    return 0;
}

static int fn_love_ui_label(struct fh_program *prog,
                            struct fh_value *ret, struct fh_value *args, int n_args) {
    UI_NEED_CONTAINER("love_ui_label");
    if (n_args < 1)
        return fh_set_error(prog, "love_ui_label(): expected at least 1 argument, got %d", n_args);
    if (!fh_is_string(&args[0])) {
        return fh_set_error(prog, "Expected string label");
    }

    const char *label = fh_get_string(&args[0]);
    int opt = (int) fh_optnumber(args, n_args, 1, MU_OPT_ALIGNRIGHT);
    ui_label(label, opt);
    *ret = fh_new_null();
    return 0;
}

static int fn_love_ui_res_state(struct fh_program *prog,
                                struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_ui_res_state(): expected 1 argument, got %d", n_args);
    if (!fh_is_string(&args[0])) {
        return fh_set_error(prog, "Expected string!");
    }
    const char *state = fh_get_string(&args[0]);
    int num = 0;
    if (strcmp(state, "active") == 0) {
        num = MU_RES_ACTIVE;
    } else if (strcmp(state, "submit") == 0) {
        num = MU_RES_SUBMIT;
    } else if (strcmp(state, "change") == 0) {
        num = MU_RES_CHANGE;
    } else {
        return fh_set_error(prog, "invalid resource state %s\n", state);
    }
    *ret = fh_new_number(num);
    return 0;
}

static int fn_love_ui_opt(struct fh_program *prog,
                          struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_ui_opt(): expected 1 argument, got %d", n_args);
    if (!fh_is_string(&args[0])) {
        return fh_set_error(prog, "Expected string!");
    }
    const char *opt = fh_get_string(&args[0]);
    int num = 0;
    if (strcmp(opt, "noclose") == 0) {
        num = MU_OPT_NOCLOSE;
    } else if (strcmp(opt, "noframe") == 0) {
        num = MU_OPT_NOFRAME;
    } else if (strcmp(opt, "notitle") == 0) {
        num = MU_OPT_NOTITLE;
    } else if (strcmp(opt, "noresize") == 0) {
        num = MU_OPT_NORESIZE;
    } else if (strcmp(opt, "popup") == 0) {
        num = MU_OPT_POPUP;
    } else if (strcmp(opt, "autosize") == 0) {
        num = MU_OPT_AUTOSIZE;;
    } else {
        return fh_set_error(prog, "invalid alignment %s\n", opt);
    }
    *ret = fh_new_number(num);
    return 0;
}

static int fn_love_ui_align(struct fh_program *prog,
                            struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_ui_align(): expected 1 argument, got %d", n_args);
    if (!fh_is_string(&args[0])) {
        return fh_set_error(prog, "Expected string!");
    }
    const char *align = fh_get_string(&args[0]);
    int num = 0;
    if (strcmp(align, "center") == 0) {
        num = MU_OPT_ALIGNCENTER;
    } else if (strcmp(align, "right") == 0) {
        num = MU_OPT_ALIGNRIGHT;
    } else {
        return fh_set_error(prog, "invalid alignment %s\n", align);
    }
    *ret = fh_new_number(num);
    return 0;
}

static int fn_love_ui_setColor(struct fh_program *prog,
                               struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 5)
        return fh_set_error(prog, "love_ui_setColor(): expected 5 arguments, got %d", n_args);
    for (int i = 0; i < 4; i++) {
        if (!fh_is_number(&args[i]))
            return fh_set_error(prog, "Expected number at argument index %d", i);
    }
    int colorId = (int) fh_get_number(&args[0]);
    unsigned char r = (unsigned char) fh_get_number(&args[1]);
    unsigned char g = (unsigned char) fh_get_number(&args[2]);
    unsigned char b = (unsigned char) fh_get_number(&args[3]);
    unsigned char a = (unsigned char) fh_get_number(&args[4]);

    ui_get_context()->style->colors[colorId].r = r;
    ui_get_context()->style->colors[colorId].g = g;
    ui_get_context()->style->colors[colorId].b = b;
    ui_get_context()->style->colors[colorId].a = a;

    *ret = fh_new_null();

    return 0;
}

static int fn_love_ui_getColor(struct fh_program *prog,
                               struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_ui_getColor(): expected 1 argument, got %d", n_args);
    if (!fh_is_string(&args[0])) {
        return fh_set_error(prog, "Expected string!");
    }
    const char *color = fh_get_string(&args[0]);
    int num = 0;

    int pin_state = fh_get_pin_state(prog);
    struct fh_array *arr = fh_make_array(prog, true);
    fh_grow_array_object(prog, arr, 5);

    if (strcmp(color, "text") == 0) {
        num = MU_COLOR_TEXT;
    } else if (strcmp(color, "border") == 0) {
        num = MU_COLOR_BORDER;
    } else if (strcmp(color, "windowBg") == 0) {
        num = MU_COLOR_WINDOWBG;
    } else if (strcmp(color, "titleBg") == 0) {
        num = MU_COLOR_TITLEBG;
    } else if (strcmp(color, "titleExt") == 0) {
        num = MU_COLOR_TITLETEXT;
    } else if (strcmp(color, "panelBg") == 0) {
        num = MU_COLOR_PANELBG;
    } else if (strcmp(color, "button") == 0) {
        num = MU_COLOR_BUTTON;
    } else if (strcmp(color, "buttonHover") == 0) {
        num = MU_COLOR_BUTTONHOVER;
    } else if (strcmp(color, "buttonFocus") == 0) {
        num = MU_COLOR_BUTTONFOCUS;
    } else if (strcmp(color, "base") == 0) {
        num = MU_COLOR_BASE;
    } else if (strcmp(color, "baseHover") == 0) {
        num = MU_COLOR_BASEHOVER;
    } else if (strcmp(color, "baseFocus") == 0) {
        num = MU_COLOR_BASEFOCUS;
    } else if (strcmp(color, "scrollBase") == 0) {
        num = MU_COLOR_SCROLLBASE;
    } else if (strcmp(color, "scrollThumb") == 0) {
        num = MU_COLOR_SCROLLTHUMB;
    } else {
        return fh_set_error(prog, "Invalid color argument %s", color);
    }

    mu_Color c = ui_get_context()->style->colors[num];

    arr->items[0] = fh_new_number(c.r);
    arr->items[1] = fh_new_number(c.g);
    arr->items[2] = fh_new_number(c.b);
    arr->items[3] = fh_new_number(c.a);
    arr->items[4] = fh_new_number(num);

    struct fh_value arr_value = fh_new_array(prog);
    arr_value.data.obj = arr;

    *ret = arr_value;
    fh_restore_pin_state(prog, pin_state);
    return 0;
}

static int fn_love_ui_text(struct fh_program *prog,
                           struct fh_value *ret, struct fh_value *args, int n_args) {
    UI_NEED_CONTAINER("love_ui_text");
    if (n_args != 1)
        return fh_set_error(prog, "love_ui_text(): expected 1 argument, got %d", n_args);
    if (!fh_is_string(&args[0])) {
        return fh_set_error(prog, "Expected string!");
    }
    const char *text = fh_get_string(&args[0]);
    ui_text(text);
    *ret = fh_new_null();
    return 0;
}

static int fn_love_ui_checkbox(struct fh_program *prog,
                               struct fh_value *ret, struct fh_value *args, int n_args) {
    UI_NEED_CONTAINER("love_ui_checkbox");
    if (n_args != 3)
        return fh_set_error(prog, "love_ui_checkbox(): expected 3 arguments, got %d", n_args);
    if (!fh_is_string(&args[0])) {
        return fh_set_error(prog, "Expected string label");
    } else if (!fh_is_bool(&args[1])) {
        return fh_set_error(prog, "Expected state boolean");
    } else if (!fh_is_number(&args[2])) {
        return fh_set_error(prog, "Expected id");
    }

    const char *label = fh_get_string(&args[0]);
    bool state = fh_get_bool(&args[1]);
    int id = (int) fh_get_number(&args[2]);
    *ret = fh_new_bool(ui_checkbox(label, state, id));
    return 0;
}


static int fn_love_ui_button(struct fh_program *prog,
                             struct fh_value *ret, struct fh_value *args, int n_args) {
    UI_NEED_CONTAINER("love_ui_button");
    if (n_args != 2)
        return fh_set_error(prog, "love_ui_button(): expected 2 arguments, got %d", n_args);
    if (!fh_is_string(&args[0])) {
        return fh_set_error(prog, "Expected string label");
    } else if (!fh_is_number(&args[1])) {
        return fh_set_error(prog, "Expected id");
    }

    const char *label = fh_get_string(&args[0]);
    int opt = (int) fh_optnumber(args, n_args, 1, MU_OPT_ALIGNRIGHT);
    *ret = fh_new_bool(ui_button(label, opt));
    return 0;
}

/*
 * Text boxes keep their buffer on the C side: microui edits the buffer in
 * place across frames, and an FH string cannot be mutated that way. Each
 * script-supplied id owns one buffer, so several text boxes can be live at
 * once -- the previous implementation shared a single static buffer, wrote
 * into the FH string object it was handed, and read back a buffer that was
 * never filled, so it always returned an empty string.
 */
#define UI_TEXTBOX_CAPACITY 256
#define UI_TEXTBOX_SLOTS    32

static struct {
    int id;
    bool in_use;
    char buf[UI_TEXTBOX_CAPACITY];
} textboxes[UI_TEXTBOX_SLOTS];

static int textbox_slot(int id) {
    int free_slot = -1;
    for (int i = 0; i < UI_TEXTBOX_SLOTS; ++i) {
        if (textboxes[i].in_use && textboxes[i].id == id) {
            return i;
        }
        if (!textboxes[i].in_use && free_slot < 0) {
            free_slot = i;
        }
    }
    if (free_slot < 0) {
        return -1;
    }
    textboxes[free_slot].in_use = true;
    textboxes[free_slot].id = id;
    textboxes[free_slot].buf[0] = '\0';
    return free_slot;
}

/*
 * love_ui_clear_textbox([id]) -- empties one text box, or every one when
 * called without an id.
 */
static int fn_love_ui_clear_textbox(struct fh_program *prog,
                                    struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args == 0) {
        memset(textboxes, 0, sizeof(textboxes));
        *ret = fh_new_null();
        return 0;
    }
    if (!fh_is_number(&args[0])) {
        return fh_set_error(prog, "love_ui_clear_textbox(): expected the text box id as a number");
    }
    int id = (int) fh_get_number(&args[0]);
    for (int i = 0; i < UI_TEXTBOX_SLOTS; ++i) {
        if (textboxes[i].in_use && textboxes[i].id == id) {
            textboxes[i].buf[0] = '\0';
            break;
        }
    }
    *ret = fh_new_null();
    return 0;
}

/*
 * love_ui_textbox(id, [initial_text], [opt]) -> [res, text]
 *
 * `id` names the buffer this text box edits. `initial_text` seeds it the
 * first time that id is seen (later frames keep whatever the user typed).
 * `res` is a MU_RES_* bitmask -- compare it against love_ui_res_state(...).
 */
static int fn_love_ui_textbox(struct fh_program *prog,
                              struct fh_value *ret, struct fh_value *args, int n_args) {
    UI_NEED_CONTAINER("love_ui_textbox");
    if (n_args < 1)
        return fh_set_error(prog, "love_ui_textbox(): expected at least 1 argument, got %d", n_args);
    if (!fh_is_number(&args[0]))
        return fh_set_error(prog, "love_ui_textbox(): expected the text box id as a number");

    int id = (int) fh_get_number(&args[0]);
    int slot = textbox_slot(id);
    if (slot < 0)
        return fh_set_error(prog, "love_ui_textbox(): out of text box slots (max %d)", UI_TEXTBOX_SLOTS);

    bool fresh = textboxes[slot].buf[0] == '\0';
    if (fresh && n_args > 1 && fh_is_string(&args[1])) {
        const char *initial = fh_get_string(&args[1]);
        size_t len = strlen(initial);
        if (len >= UI_TEXTBOX_CAPACITY) {
            len = UI_TEXTBOX_CAPACITY - 1;
        }
        memcpy(textboxes[slot].buf, initial, len);
        textboxes[slot].buf[len] = '\0';
    }

    int opt = (int) fh_optnumber(args, n_args, 2, 0);
    int res = ui_textbox(textboxes[slot].buf, UI_TEXTBOX_CAPACITY, opt);

    int pin_state = fh_get_pin_state(prog);
    struct fh_array *arr = fh_make_array(prog, true);
    fh_grow_array_object(prog, arr, 2);
    arr->items[0] = fh_new_number(res);
    arr->items[1] = fh_new_string(prog, textboxes[slot].buf);

    struct fh_value arr_value = fh_new_array(prog);
    arr_value.data.obj = arr;

    *ret = arr_value;
    fh_restore_pin_state(prog, pin_state);
    return 0;
}

static int fn_love_ui_layout_begin_column(struct fh_program *prog,
                                          struct fh_value *ret, struct fh_value *args, int n_args) {
    UI_NEED_CONTAINER("love_ui_layout_begin_column");
    UNUSED(prog);
    UNUSED(args);
    UNUSED(n_args);
    ui_layout_begin_column();
    *ret = fh_new_null();
    return 0;
}

static int fn_love_ui_layout_end_column(struct fh_program *prog,
                                        struct fh_value *ret, struct fh_value *args, int n_args) {
    UI_NEED_CONTAINER("love_ui_layout_end_column");
    UNUSED(prog);
    UNUSED(args);
    UNUSED(n_args);
    ui_layout_end_column();
    *ret = fh_new_null();
    return 0;
}


static int fn_love_ui_begin(struct fh_program *prog,
                            struct fh_value *ret, struct fh_value *args, int n_args) {
    UNUSED(prog);
    UNUSED(args);
    UNUSED(n_args);
    ui_begin();
    *ret = fh_new_null();
    return 0;
}

static int fn_love_ui_end(struct fh_program *prog,
                          struct fh_value *ret, struct fh_value *args, int n_args) {
    UNUSED(prog);
    UNUSED(args);
    UNUSED(n_args);
    ui_end();
    *ret = fh_new_null();
    return 0;
}

#define DEF_FN(name) { #name, fn_##name }
static const struct fh_named_c_func c_funcs[] = {
    DEF_FN(love_ui_layout_row),
    DEF_FN(love_ui_begin_tree),
    DEF_FN(love_ui_end_tree),
    DEF_FN(love_ui_begin_window),
    DEF_FN(love_ui_end_window),
    DEF_FN(love_ui_header),
    DEF_FN(love_ui_checkbox),
    DEF_FN(love_ui_button),
    DEF_FN(love_ui_textbox),
    DEF_FN(love_ui_clear_textbox),
    DEF_FN(love_ui_label),
    DEF_FN(love_ui_slider),
    DEF_FN(love_ui_number),
    DEF_FN(love_ui_pushId),
    DEF_FN(love_ui_popId),
    DEF_FN(love_ui_begin),
    DEF_FN(love_ui_end),
    DEF_FN(love_ui_align),
    DEF_FN(love_ui_begin_panel),
    DEF_FN(love_ui_end_panel),
    DEF_FN(love_ui_setFocus),
    DEF_FN(love_ui_hasFocus),
    DEF_FN(love_ui_last_id),
    DEF_FN(love_ui_newContainer),
    DEF_FN(love_ui_getContainerInfo),
    DEF_FN(love_ui_setContainerInfo),
    DEF_FN(love_ui_layout_begin_column),
    DEF_FN(love_ui_layout_end_column),
    DEF_FN(love_ui_text),
    DEF_FN(love_ui_layout_next),
    DEF_FN(love_ui_layout_setNext),
    DEF_FN(love_ui_rect),
    DEF_FN(love_ui_control_text),
    DEF_FN(love_ui_layout_width),
    DEF_FN(love_ui_layout_height),
    DEF_FN(love_ui_getColor),
    DEF_FN(love_ui_setColor),
    DEF_FN(love_ui_getContainerScroll),
    DEF_FN(love_ui_setContainerScroll),
    DEF_FN(love_ui_getContainerContentSize),
    DEF_FN(love_ui_opt),
    DEF_FN(love_ui_begin_popup),
    DEF_FN(love_ui_end_popup),
    DEF_FN(love_ui_popup_open),
    DEF_FN(love_ui_mouse_over),
    DEF_FN(love_ui_setWindowOpen),
    DEF_FN(love_ui_open_popup),
    DEF_FN(love_ui_res_state)
};

void fh_ui_register(struct fh_program *prog) {
    fh_add_c_funcs(prog, c_funcs, sizeof(c_funcs) / sizeof(c_funcs[0]));
}
