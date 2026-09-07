/*
#   clove
#
#   Copyright (C) 2021-2026 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/
#pragma once

#include <stdbool.h>
#include <stdint.h>

bool math_isConvex(float const* verts, int count);

/* Ear-clips a simple polygon into triangles.
 *
 * `verts` is `count` x,y pairs in either winding. `outIndices` receives
 * 3 * (count - 2) indices into that vertex list and must have room for them;
 * math_triangulateIndexCount() gives the number.
 *
 * Returns the number of indices written, or 0 when it cannot make progress --
 * fewer than three points, or a ring with no ear left to clip. That last case
 * means the outline crosses itself, but it is not a *test* for that: ear
 * clipping cannot detect self-intersection in general, and a bowtie will come
 * back with a triangulation that is simply wrong rather than with 0. Callers
 * that care must check the outline themselves.
 *
 * A caller that must draw something anyway can fall back to a fan, which is
 * right for convex input and merely ugly for the rest.
 */
int math_triangulate(float const* verts, int count, uint32_t* outIndices);

/* How many indices math_triangulate() will write for `count` points. */
int math_triangulateIndexCount(int count);
