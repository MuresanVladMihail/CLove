/*
#   clove
#
#   Copyright (C) 2019-2021 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/

#include "math.h"

#include "../3rdparty/noise/simplexnoise.h"
#include "../3rdparty/FH/src/value.h"

#include "../include/triangulate.h"

static int fn_love_math_isConvex(struct fh_program *prog,
                                 struct fh_value *ret, struct fh_value *args, int n_args)
{
    if (n_args != 1 || !fh_is_array(&args[0])) {
        return fh_set_error(prog, "Expected first argument in math_isConvex to be an array of vertices");
    }

    struct fh_value *arr = &args[0];
    int len = fh_get_array_len(arr);
    float *vertices = malloc(sizeof(float)*len);

    struct fh_array *a = GET_VAL_ARRAY(arr);
    for (int i = 0; i < len; i++) {
        if (!fh_is_number(&a->items[i])) {
            free (vertices);
            return fh_set_error(prog, "Expected index %d in array to be of type number, got %s", i, fh_type_to_str(prog, a->items[i].type));
        }
        vertices[i] = (float)fh_get_number(&a->items[i]);
    }
    *ret = fh_new_bool(math_isConvex(vertices, len/2));
    free(vertices);
    return 0;
}

static int fn_love_math_noise(struct fh_program *prog,
                              struct fh_value *ret, struct fh_value *args, int n_args)
{

    if (n_args == 0 || n_args > 4)
        return fh_set_error(prog, "Illegal number of arguments, expected between 1 and 4");

    double v[4];

    for (int i = 0; i < n_args; i++) {
        if (!fh_is_number(&args[i]))
            return fh_set_error(prog, "Expected number at index %d", i);
        v[i] = fh_get_number(&args[i]);
    }

    switch (n_args) {
    case 1:
        *ret = fh_new_number((float)simplexnoise_noise1(v[0]));
        break;
    case 2:
        *ret = fh_new_number((float)simplexnoise_noise2(v[0], v[1]));
        break;

    case 3:
        *ret = fh_new_number((float)simplexnoise_noise3(v[0], v[1], v[2]));
        break;

    case 4:
        *ret = fh_new_number((float)simplexnoise_noise4(v[0], v[1], v[2], v[3]));
        break;
    }
    return 0;
}


/* love_math_triangulate(points) -> [[x,y,x,y,x,y], ...]
 *
 * Ear-clips a simple polygon into triangles, the way love.math.triangulate
 * does. love_geometry_polygon("fill", ...) does this internally now, so a
 * script only needs this when it wants the triangles themselves -- to hand
 * them to Box2D, which takes convex shapes only, or to build a mesh.
 *
 * The polygon has to be simple. A self-crossing outline may come back with a
 * wrong answer rather than an error: ear clipping cannot detect that in
 * general, it only notices when it runs out of ears. */
static int fn_love_math_triangulate(struct fh_program *prog,
                                    struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_math_triangulate(): expected 1 argument (a flat x,y array), got %d", n_args);

    if (!fh_is_array(&args[0]))
        return fh_set_error(prog, "love_math_triangulate(): expected a flat x,y array, got %s",
                            fh_type_to_str(prog, args[0].type));

    struct fh_array *arr = GET_VAL_ARRAY(&args[0]);
    if (arr->len < 6 || arr->len % 2 != 0)
        return fh_set_error(prog,
                "love_math_triangulate(): expected at least three x,y pairs, got %d values",
                (int) arr->len);

    int count = (int) (arr->len / 2);

    float *verts = malloc(sizeof(float) * arr->len);
    if (!verts)
        return fh_set_error(prog, "out of memory");

    for (uint32_t i = 0; i < arr->len; i++) {
        if (!fh_is_number(&arr->items[i])) {
            free(verts);
            return fh_set_error(prog, "love_math_triangulate(): value %d is not a number", (int) i);
        }
        verts[i] = (float) fh_get_number(&arr->items[i]);
    }

    uint32_t *indices = malloc(sizeof(uint32_t) * (size_t) math_triangulateIndexCount(count));
    if (!indices) {
        free(verts);
        return fh_set_error(prog, "out of memory");
    }

    int written = math_triangulate(verts, count, indices);
    if (written <= 0) {
        free(verts);
        free(indices);
        return fh_set_error(prog,
                "love_math_triangulate(): no ear left to clip -- this outline crosses itself");
    }

    int tris = written / 3;
    int pin_state = fh_get_pin_state(prog);
    struct fh_array *out = fh_make_array(prog, true);
    if (!fh_grow_array_object(prog, out, tris)) {
        free(verts);
        free(indices);
        return fh_set_error(prog, "out of memory");
    }

    struct fh_value ret_val = fh_new_array(prog);

    for (int t = 0; t < tris; t++) {
        struct fh_array *tri = fh_make_array(prog, true);
        if (!fh_grow_array_object(prog, tri, 6)) {
            free(verts);
            free(indices);
            fh_restore_pin_state(prog, pin_state);
            return fh_set_error(prog, "out of memory");
        }
        for (int k = 0; k < 3; k++) {
            uint32_t v = indices[t * 3 + k];
            tri->items[k * 2]     = fh_new_number((double) verts[v * 2]);
            tri->items[k * 2 + 1] = fh_new_number((double) verts[v * 2 + 1]);
        }
        struct fh_value tri_val = fh_new_array(prog);
        tri_val.data.obj = tri;
        out->items[t] = tri_val;
    }

    free(verts);
    free(indices);

    fh_restore_pin_state(prog, pin_state);
    ret_val.data.obj = out;
    *ret = ret_val;
    return 0;
}

#define DEF_FN(name) { #name, fn_##name }
static const struct fh_named_c_func c_funcs[] = {
    DEF_FN(love_math_noise),
    DEF_FN(love_math_isConvex),
    DEF_FN(love_math_triangulate),
};

void fh_math_register(struct fh_program *prog) {
    fh_add_c_funcs(prog, c_funcs, sizeof(c_funcs)/sizeof(c_funcs[0]));
}
