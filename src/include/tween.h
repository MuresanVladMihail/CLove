/*
#   clove
#
#   Copyright (C) 2016-2026 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/
#pragma once

#include <stdbool.h>
#include <stddef.h>

// Robert Penner's easing set, the names LOVE users know from flux and
// tween.lua. tween_Easing_linear is the identity.
typedef enum {
    tween_Easing_linear,
    tween_Easing_inQuad,     tween_Easing_outQuad,     tween_Easing_inOutQuad,
    tween_Easing_inCubic,    tween_Easing_outCubic,    tween_Easing_inOutCubic,
    tween_Easing_inQuart,    tween_Easing_outQuart,    tween_Easing_inOutQuart,
    tween_Easing_inQuint,    tween_Easing_outQuint,    tween_Easing_inOutQuint,
    tween_Easing_inSine,     tween_Easing_outSine,     tween_Easing_inOutSine,
    tween_Easing_inExpo,     tween_Easing_outExpo,     tween_Easing_inOutExpo,
    tween_Easing_inCirc,     tween_Easing_outCirc,     tween_Easing_inOutCirc,
    tween_Easing_inBack,     tween_Easing_outBack,     tween_Easing_inOutBack,
    tween_Easing_inElastic,  tween_Easing_outElastic,  tween_Easing_inOutElastic,
    tween_Easing_inBounce,   tween_Easing_outBounce,   tween_Easing_inOutBounce,
    tween_Easing_count
} tween_Easing;

// One leg of a tween: where the values are going, over how long, with what
// shape. A tween is a list of these, run one after another.
typedef struct {
    float        *to;
    float         duration;
    float         delay;
    tween_Easing  easing;
} tween_Step;

typedef struct {
    float      *from;        // the values this leg started at
    float      *current;     // what get() reads
    size_t      channels;

    tween_Step *steps;
    size_t      stepCount;
    size_t      stepCapacity;

    size_t      step;        // which leg is running
    float       elapsed;     // into the current leg, delay included

    int         loops;       // how many more times to run the whole thing; -1 = forever
    bool        yoyo;        // run backwards on every other pass
    bool        reversed;    // which way the current pass is going
    bool        paused;
    bool        done;
} tween_Tween;

char const  *tween_easingName(tween_Easing e);
bool         tween_easingFromName(char const *name, tween_Easing *out);
float        tween_ease(tween_Easing e, float t);

// `from` is copied. The tween starts with no steps: add at least one.
tween_Tween *tween_new(float const *from, size_t channels);
void         tween_free(tween_Tween *t);

// Queues a leg. `to` must have the tween's channel count.
bool         tween_addStep(tween_Tween *t, float const *to, float duration,
                           float delay, tween_Easing easing);

// Advances by dt and returns true once every leg (and every loop) is done.
bool         tween_update(tween_Tween *t, float dt);

void         tween_reset(tween_Tween *t);
void         tween_setLoops(tween_Tween *t, int loops, bool yoyo);
void         tween_setPaused(tween_Tween *t, bool paused);

// 0..1 across the whole chain, ignoring loops.
float        tween_getProgress(tween_Tween const *t);
void         tween_seek(tween_Tween *t, float progress);

float const *tween_get(tween_Tween const *t);
float        tween_getDuration(tween_Tween const *t);
