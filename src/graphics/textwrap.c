/*
#   clove
#
#   Copyright (C) 2016-2025 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/
#include "../include/textwrap.h"
#include "../include/utf8.h"

#include <stdlib.h>
#include <string.h>

// Shared by the TTF and bitmap fonts. Both used to carry their own copy of
// this loop, and both copies:
//
//  - broke in the middle of a word, wherever the pixel limit happened to
//    fall, rather than at the last space;
//  - wrote the codepoint back as a single (char), so any text outside ASCII
//    came out mangled;
//  - grew the buffer against the *input* length while writing up to two
//    bytes per iteration, and then wrote the terminator past every check --
//    so a string that reached its limit walked off the end of the
//    allocation. The caller's malloc(strlen(line)) was one byte short of
//    holding even an unwrapped copy.
//
// The buffer is grown here instead, so a caller passes a null pointer and
// takes ownership of what comes back.

struct buffer {
    char  *data;
    size_t len;
    size_t cap;
};

static bool buffer_reserve(struct buffer *b, size_t extra) {
    if (b->len + extra + 1 <= b->cap) {
        return true;
    }

    size_t cap = b->cap ? b->cap * 2 : 64;
    while (cap < b->len + extra + 1) {
        cap *= 2;
    }

    char *grown = realloc(b->data, cap);
    if (!grown) {
        return false;
    }

    b->data = grown;
    b->cap = cap;
    return true;
}

static bool buffer_put(struct buffer *b, char const *bytes, size_t n) {
    if (!buffer_reserve(b, n)) {
        return false;
    }
    memcpy(b->data + b->len, bytes, n);
    b->len += n;
    return true;
}

int graphics_wrapText(char const *line, int wraplimit,
                      graphics_advance_fn advance, void *ctx, char **wrappedtext) {
    struct buffer out = { NULL, 0, 0 };
    int lines = 1;
    int width = 0;

    // Where the current line could be broken instead, and how wide the line
    // was up to that point.
    size_t breakAt = 0;
    int    breakWidth = 0;
    bool   haveBreak = false;

    char const *p = line;
    while (*p) {
        char const *start = p;
        uint32_t cp = utf8_scan(&p);
        if (cp == 0) {
            break;
        }
        size_t n = (size_t)(p - start);   // the codepoint's own bytes

        if (cp == '\n') {
            if (!buffer_put(&out, start, n)) { goto oom; }
            lines++;
            width = 0;
            haveBreak = false;
            continue;
        }

        width += advance(ctx, cp);

        if (cp == ' ') {
            // A space is where this line may be broken, and it is the space
            // itself that becomes the newline.
            breakAt = out.len;
            breakWidth = width;
            haveBreak = true;
        }

        if (wraplimit > 0 && width > wraplimit && out.len > 0) {
            if (haveBreak && breakAt < out.len) {
                // Turn the remembered space into the line break. The text
                // after it moves down, so the new line's width is whatever
                // has accumulated since.
                out.data[breakAt] = '\n';
                width -= breakWidth;
                haveBreak = false;
            } else if (cp != ' ') {
                // A single word wider than the limit: it has to break
                // somewhere, so break here.
                if (!buffer_put(&out, "\n", 1)) { goto oom; }
                width = advance(ctx, cp);
            }
            lines++;
        }

        if (!buffer_put(&out, start, n)) { goto oom; }
    }

    if (!buffer_reserve(&out, 0)) { goto oom; }
    if (!out.data) {
        // An empty input still owes the caller a string, not a null pointer:
        // the bitmap version used to write the terminator through one.
        out.data = malloc(1);
        if (!out.data) { goto oom; }
    }
    out.data[out.len] = '\0';

    free(*wrappedtext);
    *wrappedtext = out.data;
    return lines;

oom:
    free(out.data);
    return -1;
}
