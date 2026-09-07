/*
#   clove
#
#   Copyright (C) 2015-2026 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/

#include <stdio.h>
#include <string.h>

#ifdef EMSCRIPTEN
# include <emscripten.h>
#endif

#ifdef USE_LUA
#include "include/lua_mainactivity.h"
#endif
#ifdef USE_FH
#include "include/fh_mainactivity.h"
#endif

#if defined(USE_LUA) && defined(USE_FH)
static int file_exists(char const *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return 0;
    }
    fclose(f);
    return 1;
}

static int ends_with(char const *s, char const *suffix) {
    size_t ls = strlen(s), lx = strlen(suffix);
    return ls >= lx && strcmp(s + ls - lx, suffix) == 0;
}
#endif

int main(int argc, char *argv[]) {
#if defined(USE_LUA) && defined(USE_FH)
    /* Both backends are compiled in, so this has to pick one. It used to run
     * the Lua activity to completion and then start the FH one as well, which
     * reported "can't open 'main.fh'" and returned 1 -- so a dual-backend
     * build always failed, however well the game had just run. */
    int wantLua = 0;

    if (argc > 1 && argv[1] != NULL) {
        wantLua = ends_with(argv[1], ".clove.tar") || ends_with(argv[1], ".lua");
    } else if (!file_exists("main.fh") && file_exists("main.lua")) {
        wantLua = 1;
    }

    if (wantLua) {
        lua_main_activity_load(argc, argv);
        return 0;
    }
    return fh_main_activity_load(argc, argv);
#elif defined(USE_LUA)
    lua_main_activity_load(argc, argv);
    return 0;
#elif defined(USE_FH)
    return fh_main_activity_load(argc, argv);
#else
    (void) argc;
    (void) argv;
    return 0;
#endif
}
