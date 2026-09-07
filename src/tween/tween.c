/*
#   clove
#
#   Copyright (C) 2016-2026 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/
#include "../include/tween.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static char const *easingNames[tween_Easing_count] = {
    "linear",
    "inQuad",    "outQuad",    "inOutQuad",
    "inCubic",   "outCubic",   "inOutCubic",
    "inQuart",   "outQuart",   "inOutQuart",
    "inQuint",   "outQuint",   "inOutQuint",
    "inSine",    "outSine",    "inOutSine",
    "inExpo",    "outExpo",    "inOutExpo",
    "inCirc",    "outCirc",    "inOutCirc",
    "inBack",    "outBack",    "inOutBack",
    "inElastic", "outElastic", "inOutElastic",
    "inBounce",  "outBounce",  "inOutBounce"
};

char const *tween_easingName(tween_Easing e) {
    if (e < 0 || e >= tween_Easing_count) {
        return "linear";
    }
    return easingNames[e];
}

bool tween_easingFromName(char const *name, tween_Easing *out) {
    if (!name) {
        return false;
    }
    for (int i = 0; i < tween_Easing_count; ++i) {
        if (strcmp(name, easingNames[i]) == 0) {
            *out = (tween_Easing) i;
            return true;
        }
    }
    return false;
}

static float outBounce(float t) {
    if (t < 1.0f / 2.75f) {
        return 7.5625f * t * t;
    }
    if (t < 2.0f / 2.75f) {
        t -= 1.5f / 2.75f;
        return 7.5625f * t * t + 0.75f;
    }
    if (t < 2.5f / 2.75f) {
        t -= 2.25f / 2.75f;
        return 7.5625f * t * t + 0.9375f;
    }
    t -= 2.625f / 2.75f;
    return 7.5625f * t * t + 0.984375f;
}

static float outElastic(float t) {
    if (t <= 0.0f) { return 0.0f; }
    if (t >= 1.0f) { return 1.0f; }
    const float p = 0.3f;
    return powf(2.0f, -10.0f * t) * sinf((t - p / 4.0f) * (2.0f * (float) M_PI) / p) + 1.0f;
}

static float inElastic(float t) {
    return 1.0f - outElastic(1.0f - t);
}

float tween_ease(tween_Easing e, float t) {
    // Clamped, so a caller that overshoots the duration cannot walk off the
    // end of a curve that is only defined on [0, 1].
    if (t <= 0.0f) { return 0.0f; }
    if (t >= 1.0f) { return 1.0f; }

    const float s = 1.70158f;      // the standard back-easing overshoot
    const float s2 = s * 1.525f;

    switch (e) {
    case tween_Easing_linear:     return t;

    case tween_Easing_inQuad:     return t * t;
    case tween_Easing_outQuad:    return 1.0f - (1.0f - t) * (1.0f - t);
    case tween_Easing_inOutQuad:  return t < 0.5f ? 2.0f * t * t
                                                  : 1.0f - powf(-2.0f * t + 2.0f, 2.0f) / 2.0f;

    case tween_Easing_inCubic:    return t * t * t;
    case tween_Easing_outCubic:   return 1.0f - powf(1.0f - t, 3.0f);
    case tween_Easing_inOutCubic: return t < 0.5f ? 4.0f * t * t * t
                                                  : 1.0f - powf(-2.0f * t + 2.0f, 3.0f) / 2.0f;

    case tween_Easing_inQuart:    return t * t * t * t;
    case tween_Easing_outQuart:   return 1.0f - powf(1.0f - t, 4.0f);
    case tween_Easing_inOutQuart: return t < 0.5f ? 8.0f * t * t * t * t
                                                  : 1.0f - powf(-2.0f * t + 2.0f, 4.0f) / 2.0f;

    case tween_Easing_inQuint:    return t * t * t * t * t;
    case tween_Easing_outQuint:   return 1.0f - powf(1.0f - t, 5.0f);
    case tween_Easing_inOutQuint: return t < 0.5f ? 16.0f * t * t * t * t * t
                                                  : 1.0f - powf(-2.0f * t + 2.0f, 5.0f) / 2.0f;

    case tween_Easing_inSine:     return 1.0f - cosf(t * (float) M_PI / 2.0f);
    case tween_Easing_outSine:    return sinf(t * (float) M_PI / 2.0f);
    case tween_Easing_inOutSine:  return -(cosf((float) M_PI * t) - 1.0f) / 2.0f;

    case tween_Easing_inExpo:     return powf(2.0f, 10.0f * t - 10.0f);
    case tween_Easing_outExpo:    return 1.0f - powf(2.0f, -10.0f * t);
    case tween_Easing_inOutExpo:  return t < 0.5f ? powf(2.0f, 20.0f * t - 10.0f) / 2.0f
                                                  : (2.0f - powf(2.0f, -20.0f * t + 10.0f)) / 2.0f;

    case tween_Easing_inCirc:     return 1.0f - sqrtf(1.0f - t * t);
    case tween_Easing_outCirc:    return sqrtf(1.0f - (t - 1.0f) * (t - 1.0f));
    case tween_Easing_inOutCirc:  return t < 0.5f
                                       ? (1.0f - sqrtf(1.0f - 4.0f * t * t)) / 2.0f
                                       : (sqrtf(1.0f - powf(-2.0f * t + 2.0f, 2.0f)) + 1.0f) / 2.0f;

    case tween_Easing_inBack:     return (s + 1.0f) * t * t * t - s * t * t;
    case tween_Easing_outBack:    return 1.0f + (s + 1.0f) * powf(t - 1.0f, 3.0f)
                                              + s * powf(t - 1.0f, 2.0f);
    case tween_Easing_inOutBack:  return t < 0.5f
                                       ? (powf(2.0f * t, 2.0f) * ((s2 + 1.0f) * 2.0f * t - s2)) / 2.0f
                                       : (powf(2.0f * t - 2.0f, 2.0f)
                                          * ((s2 + 1.0f) * (t * 2.0f - 2.0f) + s2) + 2.0f) / 2.0f;

    case tween_Easing_inElastic:  return inElastic(t);
    case tween_Easing_outElastic: return outElastic(t);
    case tween_Easing_inOutElastic:
        return t < 0.5f ? inElastic(t * 2.0f) / 2.0f
                        : outElastic(t * 2.0f - 1.0f) / 2.0f + 0.5f;

    case tween_Easing_inBounce:   return 1.0f - outBounce(1.0f - t);
    case tween_Easing_outBounce:  return outBounce(t);
    case tween_Easing_inOutBounce:
        return t < 0.5f ? (1.0f - outBounce(1.0f - 2.0f * t)) / 2.0f
                        : (1.0f + outBounce(2.0f * t - 1.0f)) / 2.0f;

    case tween_Easing_count:      break;
    }
    return t;
}

tween_Tween *tween_new(float const *from, size_t channels) {
    if (channels < 1 || from == NULL) {
        return NULL;
    }

    tween_Tween *t = calloc(1, sizeof(tween_Tween));
    if (!t) {
        return NULL;
    }

    t->channels = channels;
    t->from    = malloc(sizeof(float) * channels);
    t->current = malloc(sizeof(float) * channels);
    if (!t->from || !t->current) {
        tween_free(t);
        return NULL;
    }

    memcpy(t->from, from, sizeof(float) * channels);
    memcpy(t->current, from, sizeof(float) * channels);
    t->loops = 0;
    return t;
}

void tween_free(tween_Tween *t) {
    if (!t) {
        return;
    }
    for (size_t i = 0; i < t->stepCount; ++i) {
        free(t->steps[i].to);
    }
    free(t->steps);
    free(t->from);
    free(t->current);
    free(t);
}

bool tween_addStep(tween_Tween *t, float const *to, float duration,
                   float delay, tween_Easing easing) {
    if (!t || !to) {
        return false;
    }

    if (t->stepCount == t->stepCapacity) {
        size_t cap = t->stepCapacity ? t->stepCapacity * 2 : 4;
        tween_Step *grown = realloc(t->steps, sizeof(tween_Step) * cap);
        if (!grown) {
            return false;
        }
        t->steps = grown;
        t->stepCapacity = cap;
    }

    float *copy = malloc(sizeof(float) * t->channels);
    if (!copy) {
        return false;
    }
    memcpy(copy, to, sizeof(float) * t->channels);

    tween_Step *s = &t->steps[t->stepCount++];
    s->to       = copy;
    s->duration = duration < 0.0f ? 0.0f : duration;
    s->delay    = delay < 0.0f ? 0.0f : delay;
    s->easing   = easing;

    t->done = false;
    return true;
}

float tween_getDuration(tween_Tween const *t) {
    if (!t) {
        return 0.0f;
    }
    float total = 0.0f;
    for (size_t i = 0; i < t->stepCount; ++i) {
        total += t->steps[i].delay + t->steps[i].duration;
    }
    return total;
}

// Where a step starts from: the previous step's target, or the tween's own
// starting values for the first one. Running backwards flips that.
static float const *stepOrigin(tween_Tween const *t, size_t index) {
    if (index == 0) {
        return t->from;
    }
    return t->steps[index - 1].to;
}

static void applyStep(tween_Tween *t, size_t index, float local) {
    tween_Step const *s = &t->steps[index];
    float const *a = stepOrigin(t, index);
    float const *b = s->to;

    if (t->reversed) {
        float const *swap = a;
        a = b;
        b = swap;
    }

    float k = tween_ease(s->easing, local);
    for (size_t c = 0; c < t->channels; ++c) {
        t->current[c] = a[c] + (b[c] - a[c]) * k;
    }
}

// Finishes the current pass: snaps to the end, and either starts another loop
// or reports that the whole thing is over.
static bool finishPass(tween_Tween *t) {
    if (t->loops == 0) {
        t->done = true;
        return true;
    }

    if (t->loops > 0) {
        t->loops--;
    }

    if (t->yoyo) {
        t->reversed = !t->reversed;
    }

    t->step = 0;
    t->elapsed = 0.0f;

    // Snap to where the new pass begins. Without this a looping tween sat on
    // the previous pass's end value until the next update moved it, so a
    // caller that read get() right after the pass finished saw the wrong end.
    applyStep(t, t->reversed ? (t->stepCount - 1) : 0, 0.0f);
    return false;
}

bool tween_update(tween_Tween *t, float dt) {
    if (!t || t->done || t->paused || t->stepCount == 0) {
        return t ? t->done : true;
    }

    // A backwards pass runs the steps in reverse order too, so a yoyo retraces
    // its own path rather than jumping to the far end and easing back.
    while (dt > 0.0f) {
        size_t index = t->reversed ? (t->stepCount - 1 - t->step) : t->step;
        tween_Step const *s = &t->steps[index];
        float span = s->delay + s->duration;

        if (t->elapsed + dt < span) {
            t->elapsed += dt;
            dt = 0.0f;
        } else {
            dt -= (span - t->elapsed);
            t->elapsed = span;
        }

        float local = 1.0f;
        if (s->duration > 0.0f) {
            float into = t->elapsed - s->delay;
            local = into <= 0.0f ? 0.0f : into / s->duration;
            if (local > 1.0f) { local = 1.0f; }
        } else if (t->elapsed < s->delay) {
            local = 0.0f;
        }

        applyStep(t, index, local);

        if (t->elapsed < span) {
            break;
        }

        t->step++;
        t->elapsed = 0.0f;

        if (t->step >= t->stepCount) {
            if (finishPass(t)) {
                break;
            }
        }
    }

    return t->done;
}

void tween_reset(tween_Tween *t) {
    if (!t) {
        return;
    }
    t->step = 0;
    t->elapsed = 0.0f;
    t->done = false;
    t->reversed = false;
    memcpy(t->current, t->from, sizeof(float) * t->channels);
}

void tween_setLoops(tween_Tween *t, int loops, bool yoyo) {
    if (!t) {
        return;
    }
    t->loops = loops;
    t->yoyo = yoyo;
}

void tween_setPaused(tween_Tween *t, bool paused) {
    if (t) {
        t->paused = paused;
    }
}

float tween_getProgress(tween_Tween const *t) {
    if (!t || t->stepCount == 0) {
        return 1.0f;
    }
    if (t->done) {
        return 1.0f;
    }

    float total = tween_getDuration(t);
    if (total <= 0.0f) {
        return 1.0f;
    }

    float at = 0.0f;
    for (size_t i = 0; i < t->step && i < t->stepCount; ++i) {
        size_t index = t->reversed ? (t->stepCount - 1 - i) : i;
        at += t->steps[index].delay + t->steps[index].duration;
    }
    at += t->elapsed;

    float p = at / total;
    return p < 0.0f ? 0.0f : (p > 1.0f ? 1.0f : p);
}

void tween_seek(tween_Tween *t, float progress) {
    if (!t || t->stepCount == 0) {
        return;
    }

    if (progress < 0.0f) { progress = 0.0f; }
    if (progress > 1.0f) { progress = 1.0f; }

    tween_reset(t);
    float target = tween_getDuration(t) * progress;
    if (target <= 0.0f) {
        applyStep(t, 0, 0.0f);
        return;
    }

    // Seeking cannot be allowed to consume loops, so it walks the chain once
    // with looping switched off and puts it back afterwards.
    int loops = t->loops;
    bool yoyo = t->yoyo;
    t->loops = 0;
    t->yoyo = false;
    tween_update(t, target);
    t->loops = loops;
    t->yoyo = yoyo;

    // Seeking to the very end marked the tween done, even when it still had
    // loops to run -- the loop count was switched off for the walk above.
    if (loops != 0) {
        t->done = false;
    }
}

float const *tween_get(tween_Tween const *t) {
    return t ? t->current : NULL;
}
