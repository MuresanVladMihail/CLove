/*
#   clove
#
#   Copyright (C) 2026 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/
#pragma once

#include <stdbool.h>

/* The screen a game shows when the script fails.
 *
 * Until this existed, a script error printed a traceback to a terminal the
 * player very likely does not have open and the window vanished. LOVE has
 * shown its blue screen for this since forever, and it is the difference
 * between "the game closed" and "here is what went wrong, and you can copy
 * it". CLove's is pink, out of the logo: #E33C77 with a #FB78AC frame.
 *
 * It runs its own little loop -- the game's is over by then -- and returns
 * when the player closes it. The process still exits non-zero afterwards, so
 * scripts and CI see a failure either way.
 *
 * Returns false when it could not be shown (no window, or switched off), in
 * which case the caller should report the error the old way.
 */
bool error_screen_show(const char *message);

/* Off when CLOVE_NO_ERROR_SCREEN is set in the environment (the test runner
 * does), or when config.fh sets error_screen = false. */
void error_screen_setEnabled(bool enabled);

bool error_screen_isEnabled(void);
