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

#include "imagedata.h"

/* Loading a texture off the main thread.
 *
 * Decoding a 2048x2048 PNG takes 15-17 ms here -- a whole frame at 60 Hz --
 * so a level that pulls in twenty of them stalls for a third of a second.
 * That decode is pure CPU work on a file, which is exactly what a worker
 * thread is for.
 *
 * The GL upload is *not* on the worker: a GL context belongs to one thread,
 * and sharing one across threads is the sort of thing that works until it
 * does not. So a worker produces an image_ImageData -- pixels in memory --
 * and the main thread turns that into a graphics_Image when it collects the
 * job. The upload is the cheap half.
 *
 * Vector art is deliberately excluded. src/graphics/svg.c keeps one shared
 * NSVGrasterizer, so two workers rasterizing at once would corrupt it, and an
 * SVG measures about 1 ms anyway -- there is nothing to win.
 */

typedef enum {
    asyncload_Status_pending,
    asyncload_Status_ready,
    asyncload_Status_failed,
    asyncload_Status_unknown    /* no such job: never issued, or already taken */
} asyncload_Status;

/* `workers` <= 0 picks a count from the processor count. Safe to call twice. */
void asyncload_init(int workers);

/* Waits for the workers to finish what they are on, then frees everything --
 * including the results of jobs nobody collected. */
void asyncload_shutdown(void);

/* Queues a raster image. Returns a job id, or -1 if the path is vector art,
 * is missing, or the queue could not grow. */
int asyncload_requestImage(char const *path);

asyncload_Status asyncload_status(int id);

/* Hands over a finished job's pixels; the caller owns them and the job is
 * forgotten. NULL unless the job is ready. */
image_ImageData *asyncload_take(int id);

/* Jobs queued or running, i.e. not yet ready or failed. */
int asyncload_pending(void);

/* The path a job was asked for -- handy for an error message. NULL if the job
 * is unknown. */
char const *asyncload_path(int id);
