/*
#   clove
#
#   Copyright (C) 2016-2026 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/

#include <stdlib.h>
#include <stdbool.h>

#include "include/utils.h"

#include "include/joystick.h"

#ifdef USE_LUA
#include "luaapi/joystick.h"
#endif

static struct {
    joystick_Joystick* list;
    int joystick_count;
} moduleData;

joystick_Joystick* joystick_get(SDL_JoystickID id) {
    for (int i = 0; i < moduleData.joystick_count; i++) {
        joystick_Joystick* js = &moduleData.list[i];
        if (js->id == id)
            return js;
    }
    clove_error("CLove error, returning null joystick instance \n");
    return 0;
}

static joystick_Joystick* openJoystick(int index) {
    moduleData.list->joystick = SDL_OpenJoystick(index);

    if (moduleData.list->joystick == NULL) {
        clove_error("Joystick error: %s \n", SDL_GetError());
        return NULL;
    }
    moduleData.list->id = SDL_GetJoystickID(moduleData.list->joystick);

    if (SDL_IsGamepad(index))
        moduleData.list->controller = SDL_OpenGamepad(index);
    else
        moduleData.list->controller = 0;

    return moduleData.list;
}

void joystick_init() {
    moduleData.list = realloc(moduleData.list, sizeof(joystick_Joystick) * 1);
    /* true on success in SDL3; `!= 0` took the failure branch on success
     * and left the joystick subsystem looking broken when it was fine. */
    if (!SDL_InitSubSystem(SDL_INIT_JOYSTICK)) {
        clove_error("Joystick error %s \n", SDL_GetError());
        return;
    }

    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        clove_error("Joystick error %s \n", SDL_GetError());
        return;
    }
    /* SDL3 has no joystick count: it hands back the list of ids instead. */
    int count = 0;
    SDL_JoystickID *ids = SDL_GetJoysticks(&count);
    SDL_free(ids);
    moduleData.joystick_count = count;
}

void joystick_added(int id) {
    joystick_Joystick* js = openJoystick(id);
}

// Close all devices when clove quits
void joystick_close() {
    for (int i = 0; i < moduleData.joystick_count; i++) {
        joystick_Joystick* js = joystick_get(i);
        if (SDL_JoystickConnected(js->joystick))
            SDL_CloseJoystick(js->joystick);
        free(js);
    }
}
// Close a certain device when it has been disconected from machine
void joystick_remove(int id) {
    joystick_Joystick* js = joystick_get(id);
    SDL_CloseJoystick(js->joystick);
}

float joystick_getAxis(joystick_Joystick* joystick, int axis) {
    int16_t val = SDL_GetJoystickAxis(joystick->joystick, axis);
    return val / 32767.0f;
}

bool joystick_isDown(joystick_Joystick* joystick, int button) {
    return SDL_GetJoystickButton(joystick->joystick, button);
}

void joystick_buttonDown(int id, int button, int state) {
#ifdef USE_LUA
    l_joystick_pressed(id, button);
#endif
}

void joystick_buttonUp(int id, int button, int state) {
#ifdef USE_LUA
    l_joystick_released(id, button);
#endif
}

int joystick_getCount() {
    return moduleData.joystick_count;
}

int joystick_getNumAxes(joystick_Joystick* joystick) {
    return SDL_GetNumJoystickAxes(joystick->joystick);
}

int joystick_getNumButtons(joystick_Joystick* joystick) {
    return SDL_GetNumJoystickButtons(joystick->joystick);
}

int joystick_getNumBalls(joystick_Joystick* joystick) {
    return SDL_GetNumJoystickBalls(joystick->joystick);
}

const char* joystick_getName(joystick_Joystick* joystick) {
    /* SDL3 asks the opened joystick for its name rather than the index it
     * was opened from -- ids are not indices any more. */
    return SDL_GetJoystickName(joystick->joystick);
}

bool joystick_isConnected(joystick_Joystick* joystick) {
    return SDL_JoystickConnected(joystick->joystick);
}

bool joystick_isGamepad(joystick_Joystick* joystick) {
    return joystick->controller != 0;
}

int joystick_getHatCount(joystick_Joystick* joystick) {
    return SDL_GetNumJoystickHats(joystick->joystick);
}

int joystick_getHat(joystick_Joystick* joystick, int hat) {
    return SDL_GetJoystickHat(joystick->joystick, hat);
}

float joystick_getGamepadAxis(joystick_Joystick* joystick, int axis) {
    return SDL_GetGamepadAxis(joystick->controller, (SDL_GamepadAxis) axis) / 32767.0f;
}


int joystick_convert_str_to_button(const char* v) {
    if (strcmp("a", v) == 0)
        return SDL_GAMEPAD_BUTTON_SOUTH;
    else if (strcmp("b", v) == 0)
        return SDL_GAMEPAD_BUTTON_EAST;
    else if (strcmp("x", v) == 0)
        return SDL_GAMEPAD_BUTTON_WEST;
    else if (strcmp("y", v) == 0)
        return SDL_GAMEPAD_BUTTON_NORTH;
    else if (strcmp("back", v) == 0)
        return SDL_GAMEPAD_BUTTON_BACK;
    else if (strcmp("guide", v) == 0)
        return SDL_GAMEPAD_BUTTON_GUIDE;
    else if (strcmp("start", v) == 0)
        return SDL_GAMEPAD_BUTTON_START;
    else if (strcmp("leftstick", v) == 0)
        return SDL_GAMEPAD_BUTTON_LEFT_STICK;
    else if (strcmp("rightstick", v) == 0)
        return SDL_GAMEPAD_BUTTON_RIGHT_STICK;
    else if (strcmp("leftshoulder", v) == 0)
        return SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
    else if (strcmp("rightshoulder", v) == 0)
        return SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
    else if (strcmp("dpup", v) == 0)
        return SDL_GAMEPAD_BUTTON_DPAD_UP;
    else if (strcmp("dpdown", v) == 0)
        return SDL_GAMEPAD_BUTTON_DPAD_DOWN;
    else if (strcmp("dpleft", v) == 0)
        return SDL_GAMEPAD_BUTTON_DPAD_LEFT;
    else if (strcmp("dpright", v) == 0)
        return SDL_GAMEPAD_BUTTON_DPAD_RIGHT;

    clove_error("Error: %s is not a valid joystick button!\n", v);
    return 0;
}

const char* joystick_convert_button_to_str(int v) {
    if (v == SDL_GAMEPAD_BUTTON_SOUTH)
        return "a";
     else if (v == SDL_GAMEPAD_BUTTON_EAST)
        return "b";
     else if (v == SDL_GAMEPAD_BUTTON_WEST)
        return "x";
     else if (v == SDL_GAMEPAD_BUTTON_NORTH)
        return "y";
     else if (v == SDL_GAMEPAD_BUTTON_BACK)
        return "back";
     else if (v == SDL_GAMEPAD_BUTTON_GUIDE)
        return "guide";
     else if (v == SDL_GAMEPAD_BUTTON_START)
        return "start";
     else if (v == SDL_GAMEPAD_BUTTON_LEFT_STICK)
        return "leftstick";
     else if (v == SDL_GAMEPAD_BUTTON_RIGHT_STICK)
        return "rightstick";
     else if (v == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)
        return "leftshoulder";
     else if (v == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)
        return "rightshoulder";
     else if (v == SDL_GAMEPAD_BUTTON_DPAD_UP)
        return "dpup";
     else if (v == SDL_GAMEPAD_BUTTON_DPAD_DOWN)
        return "dpdown";
     else if (v == SDL_GAMEPAD_BUTTON_DPAD_LEFT)
        return "dpleft";
     else if (v == SDL_GAMEPAD_BUTTON_DPAD_RIGHT)
        return "dpright";

    clove_error("Error: %d is not a valid joystick button!\n", v);
    return "";

}

