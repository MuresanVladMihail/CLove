/*
#   clove
#
#   Copyright (C) 2021 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/

#include "../include/triangulate.h"

#include <stdlib.h>

static float crossVerts(float const* verts, int i, int j, int k) {
  float dx1 = verts[j*2]   - verts[i*2];
  float dy1 = verts[j*2+1] - verts[i*2+1];
  float dx2 = verts[k*2]   - verts[j*2];
  float dy2 = verts[k*2+1] - verts[j*2+1];
  return dx1 * dy2 - dy1 * dx2;
}

bool math_isConvex(float const* verts, int count) {
  int i = count - 2;
  int j = count - 1;
  int k = 0;

  float w = crossVerts(verts, i, j, k);

  while(k+1 < count) {
    i = j;
    j = k;
    ++k;

    float w2 = crossVerts(verts, i, j, k);

    if((int)w*w2 < 0.0f) {
      return false;
    }
  }

  return true;
}

int math_triangulateIndexCount(int count) {
    return count < 3 ? 0 : (count - 2) * 3;
}

/* Is p inside the triangle abc? Uses the same sign test on all three edges,
 * which is exact enough here because the ear test only ever asks about points
 * of the polygon itself. */
static bool pointInTriangle(float px, float py,
                            float ax, float ay, float bx, float by, float cx, float cy) {
    float d1 = (px - bx) * (ay - by) - (ax - bx) * (py - by);
    float d2 = (px - cx) * (by - cy) - (bx - cx) * (py - cy);
    float d3 = (px - ax) * (cy - ay) - (cx - ax) * (py - ay);

    bool neg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
    bool pos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);

    return !(neg && pos);
}

/* Twice the signed area. Positive means counter-clockwise in a y-down
 * coordinate system, which is the one CLove draws in. */
static float signedArea2(float const* verts, int count) {
    float sum = 0.0f;
    for (int i = 0, j = count - 1; i < count; j = i++) {
        sum += verts[j * 2] * verts[i * 2 + 1] - verts[i * 2] * verts[j * 2 + 1];
    }
    return sum;
}

int math_triangulate(float const* verts, int count, uint32_t* outIndices) {
    if (verts == NULL || outIndices == NULL || count < 3) {
        return 0;
    }

    /* A closed outline -- last point equal to the first -- is what a caller
     * naturally has after drawing the same polygon as a line, so accept it
     * rather than triangulating a zero-length edge. */
    if (count > 3
            && verts[0] == verts[(count - 1) * 2]
            && verts[1] == verts[(count - 1) * 2 + 1]) {
        count--;
        if (count < 3) {
            return 0;
        }
    }

    if (count == 3) {
        outIndices[0] = 0;
        outIndices[1] = 1;
        outIndices[2] = 2;
        return 3;
    }

    /* The remaining ring, as indices into the caller's vertex list. Ear
     * clipping wants a consistent winding, so a clockwise polygon is walked
     * backwards instead of being copied and reversed. */
    uint16_t stackRing[64];
    uint16_t* ring = stackRing;
    if (count > (int) (sizeof(stackRing) / sizeof(stackRing[0]))) {
        ring = malloc(sizeof(uint16_t) * (size_t) count);
        if (ring == NULL) {
            return 0;
        }
    }

    bool ccw = signedArea2(verts, count) > 0.0f;
    for (int i = 0; i < count; i++) {
        ring[i] = (uint16_t) (ccw ? i : (count - 1 - i));
    }

    int n = count;
    int written = 0;

    /* Every successful clip removes one vertex, so a polygon that is actually
     * simple finishes in at most n - 2 rounds. The squared bound gives the
     * scan room to walk past vertices that are not ears yet; a self-crossing
     * outline runs out of it instead of spinning here forever. */
    int guard = count * count + 8;

    while (n > 3 && guard-- > 0) {
        bool clipped = false;

        for (int i = 0; i < n; i++) {
            int ia = ring[(i + n - 1) % n];
            int ib = ring[i];
            int ic = ring[(i + 1) % n];

            float ax = verts[ia * 2], ay = verts[ia * 2 + 1];
            float bx = verts[ib * 2], by = verts[ib * 2 + 1];
            float cx = verts[ic * 2], cy = verts[ic * 2 + 1];

            /* Reflex corners cannot be ears. */
            float cross = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
            if (cross <= 0.0f) {
                continue;
            }

            /* Nor can a corner whose triangle swallows another vertex. */
            bool contains = false;
            for (int k = 0; k < n; k++) {
                int id = ring[k];
                if (id == ia || id == ib || id == ic) {
                    continue;
                }
                if (pointInTriangle(verts[id * 2], verts[id * 2 + 1], ax, ay, bx, by, cx, cy)) {
                    contains = true;
                    break;
                }
            }
            if (contains) {
                continue;
            }

            outIndices[written++] = (uint32_t) ia;
            outIndices[written++] = (uint32_t) ib;
            outIndices[written++] = (uint32_t) ic;

            for (int k = i; k < n - 1; k++) {
                ring[k] = ring[k + 1];
            }
            n--;
            clipped = true;
            break;
        }

        if (!clipped) {
            /* No ear left anywhere. A simple polygon always has one, so the
               outline must cross itself -- though the converse does not hold,
               see the header. */
            if (ring != stackRing) {
                free(ring);
            }
            return 0;
        }
    }

    if (n == 3) {
        outIndices[written++] = ring[0];
        outIndices[written++] = ring[1];
        outIndices[written++] = ring[2];
    }

    if (ring != stackRing) {
        free(ring);
    }

    return written;
}
