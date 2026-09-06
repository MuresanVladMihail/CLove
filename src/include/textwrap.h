/*
#   clove
#
#   Copyright (C) 2016-2025 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/
#pragma once

#include <stdbool.h>
#include <stdint.h>

// How wide one codepoint is, in the caller's font.
typedef int (*graphics_advance_fn)(void *ctx, uint32_t codepoint);

// Breaks `line` to `wraplimit` pixels, at spaces where it can, and returns
// the number of lines. `*wrappedtext` is freed and replaced with a buffer the
// caller then owns; pass a pointer to NULL for a fresh one. Returns -1 and
// leaves `*wrappedtext` alone if it runs out of memory.
int graphics_wrapText(char const *line, int wraplimit,
                      graphics_advance_fn advance, void *ctx, char **wrappedtext);
