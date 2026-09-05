/*
#   clove
#
#   Copyright (C) 2016-2025 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/

/*
 * love.physics for FH, on top of Box2D 3.x.
 *
 * Names follow CLove's flattened LÖVE convention: love.physics.newWorld() is
 * love_physics_newWorld(), world:update(dt) is love_world_update(world, dt),
 * body:getX() is love_body_getX(body), and so on.
 *
 * Everything the script passes or receives is in pixels and radians; the
 * pixel/meter conversion (love_physics_setMeter, 30 by default) happens here
 * so scripts never see meters, exactly as in LÖVE.
 *
 * Differences from LÖVE 11, all forced by Box2D 3 dropping features:
 *   - Gear and pulley joints do not exist. Friction and rope joints do, but
 *     are built on the motor and distance joints respectively.
 *   - Collision callbacks are function *names* (strings), because FH can only
 *     call a global function by name from C.
 *   - Only beginContact/endContact are dispatched; Box2D 3 has no post-solve
 *     event with LÖVE's shape.
 *   - Fixture/body user data can hold a number or a string, not an arbitrary
 *     script value: FH gives C no way to keep one alive across a GC cycle.
 */

#include "physics.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../3rdparty/FH/src/value.h"

#include "../include/physics.h"

/* ------------------------------------------------------------------ */
/* Script object plumbing                                              */
/* ------------------------------------------------------------------ */

/* A collision as the callbacks see it. The manifold is copied because Box2D
 * only lends it for the duration of the event list. */
typedef struct {
    physics_Fixture *a;
    physics_Fixture *b;
    b2Manifold manifold;
    bool touching;
} PhysicsContact;

static void world_gc(void *ptr)   { physics_World_release(ptr); }
static void body_gc(void *ptr)    { physics_Body_release(ptr); }
static void shape_gc(void *ptr)   { physics_Shape_release(ptr); }
static void fixture_gc(void *ptr) { physics_Fixture_release(ptr); }
static void joint_gc(void *ptr)   { physics_Joint_release(ptr); }

static void contact_gc(void *ptr) {
    PhysicsContact *contact = ptr;
    physics_Fixture_release(contact->a);
    physics_Fixture_release(contact->b);
    free(contact);
}

/* fh_new_c_obj() leaves the object collectable straight away, which is what
 * we want for a value handed back to the script. Values built to be passed
 * *into* a script call have to survive the allocations that follow them, so
 * they are pinned instead and released with the pin state. */
static struct fh_value new_c_obj_pinned(struct fh_program *prog, void *ptr,
                                        fh_c_obj_gc_callback callback, int type) {
    struct fh_c_obj *obj = fh_make_c_obj(prog, true, ptr, callback);
    if (!obj)
        return fh_new_null();
    obj->type = type;
    return (struct fh_value){ .data = { .obj = obj }, .type = FH_VAL_C_OBJ };
}

/* Wrap an existing object in a new script value, taking a reference for it. */
static struct fh_value body_value(struct fh_program *prog, physics_Body *body) {
    if (!body)
        return fh_new_null();
    physics_Body_retain(body);
    struct fh_value value = fh_new_c_obj(prog, body, body_gc, FH_PHYSICS_BODY);
    if (fh_is_null(&value))
        physics_Body_release(body);
    return value;
}

static struct fh_value fixture_value(struct fh_program *prog, physics_Fixture *fixture) {
    if (!fixture)
        return fh_new_null();
    physics_Fixture_retain(fixture);
    struct fh_value value = fh_new_c_obj(prog, fixture, fixture_gc, FH_PHYSICS_FIXTURE);
    if (fh_is_null(&value))
        physics_Fixture_release(fixture);
    return value;
}

static struct fh_value joint_value(struct fh_program *prog, physics_Joint *joint) {
    if (!joint)
        return fh_new_null();
    physics_Joint_retain(joint);
    struct fh_value value = fh_new_c_obj(prog, joint, joint_gc, FH_PHYSICS_JOINT);
    if (fh_is_null(&value))
        physics_Joint_release(joint);
    return value;
}

/* ------------------------------------------------------------------ */
/* Argument helpers                                                    */
/* ------------------------------------------------------------------ */

static int type_error(struct fh_program *prog, int index, const char *what) {
    return fh_set_error(prog, "Expected a %s as argument %d", what, index + 1);
}

static void *check_obj(struct fh_program *prog, struct fh_value *args, int n_args,
                       int index, int type, const char *what) {
    if (index >= n_args || !fh_is_c_obj_of_type(&args[index], type)) {
        type_error(prog, index, what);
        return NULL;
    }
    return fh_get_c_obj_value(&args[index]);
}

/* The "check_*" accessors also insist the underlying Box2D object is still
 * alive, so a method called on something the script (or its world) destroyed
 * is a clean script error instead of a use-after-free. The "raw_*" ones only
 * type-check, for isDestroyed() and destroy(). */
#define DEF_ACCESSORS(lower, Type, TYPEID, label)                                    \
static Type *raw_##lower(struct fh_program *prog, struct fh_value *args,             \
                         int n_args, int index) {                                    \
    return check_obj(prog, args, n_args, index, TYPEID, label);                      \
}                                                                                    \
static Type *check_##lower(struct fh_program *prog, struct fh_value *args,           \
                           int n_args, int index) {                                  \
    Type *obj = raw_##lower(prog, args, n_args, index);                              \
    if (!obj)                                                                        \
        return NULL;                                                                 \
    if (!Type##_isValid(obj)) {                                                      \
        fh_set_error(prog, "This %s has been destroyed", label);                     \
        return NULL;                                                                 \
    }                                                                                \
    return obj;                                                                      \
}

DEF_ACCESSORS(world,   physics_World,   FH_PHYSICS_WORLD,   "World")
DEF_ACCESSORS(body,    physics_Body,    FH_PHYSICS_BODY,    "Body")
DEF_ACCESSORS(fixture, physics_Fixture, FH_PHYSICS_FIXTURE, "Fixture")
DEF_ACCESSORS(joint,   physics_Joint,   FH_PHYSICS_JOINT,   "Joint")

static physics_Shape *check_shape(struct fh_program *prog, struct fh_value *args,
                                  int n_args, int index) {
    return check_obj(prog, args, n_args, index, FH_PHYSICS_SHAPE, "Shape");
}

static PhysicsContact *check_contact(struct fh_program *prog, struct fh_value *args,
                                     int n_args, int index) {
    return check_obj(prog, args, n_args, index, FH_PHYSICS_CONTACT, "Contact");
}

static const char *joint_type_to_string(physics_JointType type);

/* A joint of one specific kind, for the per-type joint methods. The kind
 * checked is LÖVE's, not Box2D's: friction and rope joints share their Box2D
 * type with the motor and distance joints they are built from. */
static physics_Joint *check_joint_of(struct fh_program *prog, struct fh_value *args,
                                     int n_args, int index, physics_JointType type) {
    physics_Joint *joint = check_joint(prog, args, n_args, index);
    if (!joint)
        return NULL;
    if (joint->type != type) {
        fh_set_error(prog, "Expected a %s joint as argument %d",
                     joint_type_to_string(type), index + 1);
        return NULL;
    }
    return joint;
}

static int check_number(struct fh_program *prog, struct fh_value *args, int n_args,
                        int index, double *out) {
    if (index >= n_args || !fh_is_number(&args[index]))
        return type_error(prog, index, "number");
    *out = fh_get_number(&args[index]);
    return 0;
}

static bool opt_bool(struct fh_value *args, int n_args, int index, bool fallback) {
    if (index >= n_args || !fh_is_bool(&args[index]))
        return fallback;
    return fh_get_bool(&args[index]);
}

/* ------------------------------------------------------------------ */
/* Returning tuples                                                    */
/* ------------------------------------------------------------------ */

/* CLove returns multiple values as an array (see love_quad_getViewport). The
 * array is pinned while it is being filled so the objects stored into it are
 * reachable if a later allocation triggers a collection. */
#define ARRAY_BEGIN(count)                                             \
    int pin_state = fh_get_pin_state(prog);                            \
    struct fh_array *arr = fh_make_array(prog, true);                  \
    if (!arr || !fh_grow_array_object(prog, arr, (uint32_t)(count))) {  \
        fh_restore_pin_state(prog, pin_state);                         \
        return fh_set_error(prog, "out of memory");                    \
    }

#define ARRAY_END()                                                    \
    do {                                                               \
        fh_restore_pin_state(prog, pin_state);                         \
        *ret = (struct fh_value){ .data = { .obj = arr },              \
                                  .type = FH_VAL_ARRAY };              \
    } while (0)

static int return_pair(struct fh_program *prog, struct fh_value *ret, double a, double b) {
    ARRAY_BEGIN(2);
    arr->items[0] = fh_new_number(a);
    arr->items[1] = fh_new_number(b);
    ARRAY_END();
    return 0;
}

/* ------------------------------------------------------------------ */
/* love.physics — module functions                                     */
/* ------------------------------------------------------------------ */

static int fn_love_physics_setMeter(struct fh_program *prog,
                                    struct fh_value *ret, struct fh_value *args, int n_args) {
    double scale;
    if (check_number(prog, args, n_args, 0, &scale) < 0)
        return -1;
    if (scale <= 0.0)
        return fh_set_error(prog, "The meter scale must be greater than 0");

    physics_setMeter((float)scale);
    *ret = fh_new_null();
    return 0;
}

static int fn_love_physics_getMeter(struct fh_program *prog,
                                    struct fh_value *ret, struct fh_value *args, int n_args) {
    (void)args; (void)n_args;
    (void)prog;
    *ret = fh_new_number(physics_getMeter());
    return 0;
}

static int fn_love_physics_newWorld(struct fh_program *prog,
                                    struct fh_value *ret, struct fh_value *args, int n_args) {
    float gx = (float)fh_optnumber(args, n_args, 0, 0.0);
    float gy = (float)fh_optnumber(args, n_args, 1, 0.0);
    bool sleep = opt_bool(args, n_args, 2, true);

    physics_World *world = physics_World_new(gx, gy, sleep);
    if (!world)
        return fh_set_error(prog, "Could not create the physics World");

    *ret = fh_new_c_obj(prog, world, world_gc, FH_PHYSICS_WORLD);
    if (fh_is_null(ret)) {
        physics_World_release(world);
        return fh_set_error(prog, "out of memory");
    }
    return 0;
}

static int body_type_from_string(struct fh_program *prog, const char *name, b2BodyType *out) {
    if (strcmp(name, "static") == 0)
        *out = b2_staticBody;
    else if (strcmp(name, "dynamic") == 0)
        *out = b2_dynamicBody;
    else if (strcmp(name, "kinematic") == 0)
        *out = b2_kinematicBody;
    else
        return fh_set_error(prog, "Unknown body type \"%s\" "
                                  "(expected \"static\", \"dynamic\" or \"kinematic\")", name);
    return 0;
}

static const char *body_type_to_string(b2BodyType type) {
    switch (type) {
    case b2_dynamicBody:   return "dynamic";
    case b2_kinematicBody: return "kinematic";
    default:               return "static";
    }
}

static int fn_love_physics_newBody(struct fh_program *prog,
                                   struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_World *world = check_world(prog, args, n_args, 0);
    if (!world)
        return -1;

    float x = (float)fh_optnumber(args, n_args, 1, 0.0);
    float y = (float)fh_optnumber(args, n_args, 2, 0.0);

    b2BodyType type = b2_staticBody;
    if (n_args > 3 && !fh_is_null(&args[3])) {
        if (!fh_is_string(&args[3]))
            return type_error(prog, 3, "string");
        if (body_type_from_string(prog, fh_get_string(&args[3]), &type) < 0)
            return -1;
    }

    physics_Body *body = physics_Body_new(world, x, y, type);
    if (!body)
        return fh_set_error(prog, "Could not create the Body");

    *ret = fh_new_c_obj(prog, body, body_gc, FH_PHYSICS_BODY);
    if (fh_is_null(ret)) {
        physics_Body_release(body);
        return fh_set_error(prog, "out of memory");
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Shapes                                                              */
/* ------------------------------------------------------------------ */

static int return_shape(struct fh_program *prog, struct fh_value *ret, physics_Shape *shape) {
    if (!shape)
        return fh_set_error(prog, "Could not create the Shape (bad geometry?)");

    *ret = fh_new_c_obj(prog, shape, shape_gc, FH_PHYSICS_SHAPE);
    if (fh_is_null(ret)) {
        physics_Shape_release(shape);
        return fh_set_error(prog, "out of memory");
    }
    return 0;
}

/*
 * Reads a vertex list written either as loose numbers — newPolygonShape(x1,
 * y1, x2, y2, ...) — or as a single array, newPolygonShape([x1, y1, ...]).
 * Returns a malloc'd x/y array and writes the vertex (not coordinate) count,
 * or NULL with the error set.
 */
static float *read_points(struct fh_program *prog, struct fh_value *args, int n_args,
                          int first, int *pointCount) {
    const struct fh_value *values;
    int count;

    if (first < n_args && fh_is_array(&args[first])) {
        struct fh_array *source = GET_OBJ_ARRAY(args[first].data.obj);
        values = source->items;
        count = (int)source->len;
    } else {
        values = args + first;
        count = n_args - first;
    }

    if (count < 4 || (count & 1)) {
        fh_set_error(prog, "Expected an even number of coordinates "
                           "(at least two points), got %d", count < 0 ? 0 : count);
        return NULL;
    }

    float *points = malloc((size_t)count * sizeof(float));
    if (!points) {
        fh_set_error(prog, "out of memory");
        return NULL;
    }
    for (int i = 0; i < count; i++) {
        if (!fh_is_number(&values[i])) {
            free(points);
            fh_set_error(prog, "Coordinate %d is not a number", i + 1);
            return NULL;
        }
        points[i] = (float)fh_get_number(&values[i]);
    }

    *pointCount = count / 2;
    return points;
}

/* newCircleShape(radius) or newCircleShape(x, y, radius) */
static int fn_love_physics_newCircleShape(struct fh_program *prog,
                                          struct fh_value *ret, struct fh_value *args, int n_args) {
    double x = 0.0, y = 0.0, radius;

    if (n_args == 1) {
        if (check_number(prog, args, n_args, 0, &radius) < 0)
            return -1;
    } else if (n_args == 3) {
        if (check_number(prog, args, n_args, 0, &x) < 0 ||
            check_number(prog, args, n_args, 1, &y) < 0 ||
            check_number(prog, args, n_args, 2, &radius) < 0)
            return -1;
    } else {
        return fh_set_error(prog, "Expected 1 argument (radius) or 3 (x, y, radius), got %d", n_args);
    }

    if (radius <= 0.0)
        return fh_set_error(prog, "The circle radius must be greater than 0");

    return return_shape(prog, ret, physics_Shape_newCircle((float)x, (float)y, (float)radius));
}

/* newRectangleShape(w, h) or newRectangleShape(x, y, w, h [, angle]) */
static int fn_love_physics_newRectangleShape(struct fh_program *prog,
                                             struct fh_value *ret, struct fh_value *args, int n_args) {
    double x = 0.0, y = 0.0, w, h, angle = 0.0;

    if (n_args == 2) {
        if (check_number(prog, args, n_args, 0, &w) < 0 ||
            check_number(prog, args, n_args, 1, &h) < 0)
            return -1;
    } else if (n_args == 4 || n_args == 5) {
        if (check_number(prog, args, n_args, 0, &x) < 0 ||
            check_number(prog, args, n_args, 1, &y) < 0 ||
            check_number(prog, args, n_args, 2, &w) < 0 ||
            check_number(prog, args, n_args, 3, &h) < 0)
            return -1;
        angle = fh_optnumber(args, n_args, 4, 0.0);
    } else {
        return fh_set_error(prog, "Expected 2 arguments (width, height) or 4-5 "
                                  "(x, y, width, height [, angle]), got %d", n_args);
    }

    if (w <= 0.0 || h <= 0.0)
        return fh_set_error(prog, "The rectangle width and height must be greater than 0");

    return return_shape(prog, ret,
                        physics_Shape_newRectangle((float)x, (float)y, (float)w, (float)h, (float)angle));
}

static int fn_love_physics_newPolygonShape(struct fh_program *prog,
                                           struct fh_value *ret, struct fh_value *args, int n_args) {
    int pointCount = 0;
    float *points = read_points(prog, args, n_args, 0, &pointCount);
    if (!points)
        return -1;

    if (pointCount < 3 || pointCount > PHYSICS_MAX_POLYGON_VERTICES) {
        free(points);
        return fh_set_error(prog, "A polygon shape needs between 3 and %d points, got %d",
                            PHYSICS_MAX_POLYGON_VERTICES, pointCount);
    }

    physics_Shape *shape = physics_Shape_newPolygon(points, pointCount);
    free(points);
    return return_shape(prog, ret, shape);
}

static int fn_love_physics_newEdgeShape(struct fh_program *prog,
                                        struct fh_value *ret, struct fh_value *args, int n_args) {
    double x1, y1, x2, y2;
    if (n_args != 4)
        return fh_set_error(prog, "Expected 4 arguments (x1, y1, x2, y2), got %d", n_args);
    if (check_number(prog, args, n_args, 0, &x1) < 0 ||
        check_number(prog, args, n_args, 1, &y1) < 0 ||
        check_number(prog, args, n_args, 2, &x2) < 0 ||
        check_number(prog, args, n_args, 3, &y2) < 0)
        return -1;

    return return_shape(prog, ret,
                        physics_Shape_newEdge((float)x1, (float)y1, (float)x2, (float)y2));
}

/* newChainShape(loop, x1, y1, x2, y2, ...) or newChainShape(loop, [x1, y1, ...]) */
static int fn_love_physics_newChainShape(struct fh_program *prog,
                                         struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args < 1 || !fh_is_bool(&args[0]))
        return type_error(prog, 0, "boolean (loop)");

    int pointCount = 0;
    float *points = read_points(prog, args, n_args, 1, &pointCount);
    if (!points)
        return -1;

    physics_Shape *shape = physics_Shape_newChain(fh_get_bool(&args[0]), points, pointCount);
    free(points);
    return return_shape(prog, ret, shape);
}

static int fn_love_physics_newFixture(struct fh_program *prog,
                                      struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Body *body = check_body(prog, args, n_args, 0);
    if (!body)
        return -1;
    physics_Shape *shape = check_shape(prog, args, n_args, 1);
    if (!shape)
        return -1;

    float density = (float)fh_optnumber(args, n_args, 2, 1.0);

    physics_Fixture *fixture = physics_Fixture_new(body, shape, density);
    if (!fixture)
        return fh_set_error(prog, "Could not attach the Shape to the Body");

    *ret = fh_new_c_obj(prog, fixture, fixture_gc, FH_PHYSICS_FIXTURE);
    if (fh_is_null(ret)) {
        physics_Fixture_release(fixture);
        return fh_set_error(prog, "out of memory");
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Joints                                                              */
/* ------------------------------------------------------------------ */

static float body_angle(b2BodyId body) {
    return b2Rot_GetAngle(b2Body_GetRotation(body));
}

/* A world anchor in pixels, as the body sees it (local meters). */
static b2Vec2 local_anchor(b2BodyId body, double x, double y) {
    return b2Body_GetLocalPoint(body, physics_scaleDownVec((float)x, (float)y));
}

static int return_joint(struct fh_program *prog, struct fh_value *ret,
                        physics_World *world, physics_JointType type, b2JointId id,
                        physics_Body *bodyA, physics_Body *bodyB) {
    physics_Joint *joint = physics_Joint_new(world, type, id, bodyA, bodyB);
    if (!joint)
        return fh_set_error(prog, "Could not create the Joint");

    *ret = fh_new_c_obj(prog, joint, joint_gc, FH_PHYSICS_JOINT);
    if (fh_is_null(ret)) {
        physics_Joint_release(joint);
        return fh_set_error(prog, "out of memory");
    }
    return 0;
}

/* Both bodies of a two-body joint, checked to live in the same world. */
static int joint_bodies(struct fh_program *prog, struct fh_value *args, int n_args,
                        physics_Body **a, physics_Body **b) {
    *a = check_body(prog, args, n_args, 0);
    if (!*a)
        return -1;
    *b = check_body(prog, args, n_args, 1);
    if (!*b)
        return -1;
    if ((*a)->world != (*b)->world)
        return fh_set_error(prog, "Both bodies of a joint must belong to the same World");
    if (*a == *b)
        return fh_set_error(prog, "A joint needs two different bodies");
    return 0;
}

static int fn_love_physics_newDistanceJoint(struct fh_program *prog,
                                            struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Body *a, *b;
    double x1, y1, x2, y2;
    if (joint_bodies(prog, args, n_args, &a, &b) < 0)
        return -1;
    if (check_number(prog, args, n_args, 2, &x1) < 0 ||
        check_number(prog, args, n_args, 3, &y1) < 0 ||
        check_number(prog, args, n_args, 4, &x2) < 0 ||
        check_number(prog, args, n_args, 5, &y2) < 0)
        return -1;

    b2Vec2 worldA = physics_scaleDownVec((float)x1, (float)y1);
    b2Vec2 worldB = physics_scaleDownVec((float)x2, (float)y2);

    b2DistanceJointDef def = b2DefaultDistanceJointDef();
    def.bodyIdA = a->id;
    def.bodyIdB = b->id;
    def.localAnchorA = b2Body_GetLocalPoint(a->id, worldA);
    def.localAnchorB = b2Body_GetLocalPoint(b->id, worldB);
    def.length = b2Distance(worldA, worldB);
    def.collideConnected = opt_bool(args, n_args, 6, false);

    return return_joint(prog, ret, a->world, physics_JointType_distance,
                        b2CreateDistanceJoint(a->world->id, &def), a, b);
}

/* A rope joint is a distance joint that may only shorten: LÖVE's RopeJoint
 * with Box2D 3's limit machinery. */
static int fn_love_physics_newRopeJoint(struct fh_program *prog,
                                        struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Body *a, *b;
    double x1, y1, x2, y2, maxLength;
    if (joint_bodies(prog, args, n_args, &a, &b) < 0)
        return -1;
    if (check_number(prog, args, n_args, 2, &x1) < 0 ||
        check_number(prog, args, n_args, 3, &y1) < 0 ||
        check_number(prog, args, n_args, 4, &x2) < 0 ||
        check_number(prog, args, n_args, 5, &y2) < 0 ||
        check_number(prog, args, n_args, 6, &maxLength) < 0)
        return -1;
    if (maxLength <= 0.0)
        return fh_set_error(prog, "The rope length must be greater than 0");

    float length = physics_scaleDown((float)maxLength);

    b2DistanceJointDef def = b2DefaultDistanceJointDef();
    def.bodyIdA = a->id;
    def.bodyIdB = b->id;
    def.localAnchorA = local_anchor(a->id, x1, y1);
    def.localAnchorB = local_anchor(b->id, x2, y2);
    def.length = length;
    def.minLength = 0.0f;
    def.maxLength = length;
    def.enableSpring = true;   /* free below maxLength ... */
    def.hertz = 0.0f;          /* ... with no spring pulling it back */
    def.dampingRatio = 0.0f;
    def.enableLimit = true;
    def.collideConnected = opt_bool(args, n_args, 7, false);

    return return_joint(prog, ret, a->world, physics_JointType_rope,
                        b2CreateDistanceJoint(a->world->id, &def), a, b);
}

static int fn_love_physics_newRevoluteJoint(struct fh_program *prog,
                                            struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Body *a, *b;
    double x, y;
    if (joint_bodies(prog, args, n_args, &a, &b) < 0)
        return -1;
    if (check_number(prog, args, n_args, 2, &x) < 0 ||
        check_number(prog, args, n_args, 3, &y) < 0)
        return -1;

    b2RevoluteJointDef def = b2DefaultRevoluteJointDef();
    def.bodyIdA = a->id;
    def.bodyIdB = b->id;
    def.localAnchorA = local_anchor(a->id, x, y);
    def.localAnchorB = local_anchor(b->id, x, y);
    def.referenceAngle = body_angle(b->id) - body_angle(a->id);
    def.collideConnected = opt_bool(args, n_args, 4, false);

    return return_joint(prog, ret, a->world, physics_JointType_revolute,
                        b2CreateRevoluteJoint(a->world->id, &def), a, b);
}

static int fn_love_physics_newPrismaticJoint(struct fh_program *prog,
                                             struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Body *a, *b;
    double x, y, ax, ay;
    if (joint_bodies(prog, args, n_args, &a, &b) < 0)
        return -1;
    if (check_number(prog, args, n_args, 2, &x) < 0 ||
        check_number(prog, args, n_args, 3, &y) < 0 ||
        check_number(prog, args, n_args, 4, &ax) < 0 ||
        check_number(prog, args, n_args, 5, &ay) < 0)
        return -1;

    b2Vec2 axis = b2Normalize((b2Vec2){ (float)ax, (float)ay });

    b2PrismaticJointDef def = b2DefaultPrismaticJointDef();
    def.bodyIdA = a->id;
    def.bodyIdB = b->id;
    def.localAnchorA = local_anchor(a->id, x, y);
    def.localAnchorB = local_anchor(b->id, x, y);
    def.localAxisA = b2Body_GetLocalVector(a->id, axis);
    def.referenceAngle = body_angle(b->id) - body_angle(a->id);
    def.collideConnected = opt_bool(args, n_args, 6, false);

    return return_joint(prog, ret, a->world, physics_JointType_prismatic,
                        b2CreatePrismaticJoint(a->world->id, &def), a, b);
}

static int fn_love_physics_newWheelJoint(struct fh_program *prog,
                                         struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Body *a, *b;
    double x, y, ax, ay;
    if (joint_bodies(prog, args, n_args, &a, &b) < 0)
        return -1;
    if (check_number(prog, args, n_args, 2, &x) < 0 ||
        check_number(prog, args, n_args, 3, &y) < 0 ||
        check_number(prog, args, n_args, 4, &ax) < 0 ||
        check_number(prog, args, n_args, 5, &ay) < 0)
        return -1;

    b2Vec2 axis = b2Normalize((b2Vec2){ (float)ax, (float)ay });

    b2WheelJointDef def = b2DefaultWheelJointDef();
    def.bodyIdA = a->id;
    def.bodyIdB = b->id;
    def.localAnchorA = local_anchor(a->id, x, y);
    def.localAnchorB = local_anchor(b->id, x, y);
    def.localAxisA = b2Body_GetLocalVector(a->id, axis);
    def.collideConnected = opt_bool(args, n_args, 6, false);

    return return_joint(prog, ret, a->world, physics_JointType_wheel,
                        b2CreateWheelJoint(a->world->id, &def), a, b);
}

static int fn_love_physics_newWeldJoint(struct fh_program *prog,
                                        struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Body *a, *b;
    double x, y;
    if (joint_bodies(prog, args, n_args, &a, &b) < 0)
        return -1;
    if (check_number(prog, args, n_args, 2, &x) < 0 ||
        check_number(prog, args, n_args, 3, &y) < 0)
        return -1;

    b2WeldJointDef def = b2DefaultWeldJointDef();
    def.bodyIdA = a->id;
    def.bodyIdB = b->id;
    def.localAnchorA = local_anchor(a->id, x, y);
    def.localAnchorB = local_anchor(b->id, x, y);
    def.referenceAngle = body_angle(b->id) - body_angle(a->id);
    def.collideConnected = opt_bool(args, n_args, 4, false);

    return return_joint(prog, ret, a->world, physics_JointType_weld,
                        b2CreateWeldJoint(a->world->id, &def), a, b);
}

static int fn_love_physics_newMotorJoint(struct fh_program *prog,
                                         struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Body *a, *b;
    if (joint_bodies(prog, args, n_args, &a, &b) < 0)
        return -1;

    b2MotorJointDef def = b2DefaultMotorJointDef();
    def.bodyIdA = a->id;
    def.bodyIdB = b->id;
    /* LÖVE seeds the offsets with the bodies' current relative placement. */
    def.linearOffset = b2Body_GetLocalPoint(a->id, b2Body_GetPosition(b->id));
    def.angularOffset = body_angle(b->id) - body_angle(a->id);
    def.correctionFactor = (float)fh_optnumber(args, n_args, 2, 0.3);
    def.collideConnected = opt_bool(args, n_args, 3, false);

    return return_joint(prog, ret, a->world, physics_JointType_motor,
                        b2CreateMotorJoint(a->world->id, &def), a, b);
}

/* Box2D 3 has no friction joint; a motor joint that holds the bodies' current
 * relative placement with no positional correction is the same constraint. */
static int fn_love_physics_newFrictionJoint(struct fh_program *prog,
                                            struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Body *a, *b;
    if (joint_bodies(prog, args, n_args, &a, &b) < 0)
        return -1;

    b2MotorJointDef def = b2DefaultMotorJointDef();
    def.bodyIdA = a->id;
    def.bodyIdB = b->id;
    def.linearOffset = b2Body_GetLocalPoint(a->id, b2Body_GetPosition(b->id));
    def.angularOffset = body_angle(b->id) - body_angle(a->id);
    def.correctionFactor = 0.0f;
    def.maxForce = 0.0f;
    def.maxTorque = 0.0f;
    def.collideConnected = opt_bool(args, n_args, 4, false);

    return return_joint(prog, ret, a->world, physics_JointType_friction,
                        b2CreateMotorJoint(a->world->id, &def), a, b);
}

static int fn_love_physics_newMouseJoint(struct fh_program *prog,
                                         struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Body *body = check_body(prog, args, n_args, 0);
    double x, y;
    if (!body)
        return -1;
    if (check_number(prog, args, n_args, 1, &x) < 0 ||
        check_number(prog, args, n_args, 2, &y) < 0)
        return -1;

    float mass = b2Body_GetMass(body->id);

    b2MouseJointDef def = b2DefaultMouseJointDef();
    def.bodyIdA = physics_World_mouseGround(body->world);
    def.bodyIdB = body->id;
    def.target = physics_scaleDownVec((float)x, (float)y);
    /* LÖVE's default pull strength. */
    def.maxForce = 1000.0f * (mass > 0.0f ? mass : 1.0f);

    return return_joint(prog, ret, body->world, physics_JointType_mouse,
                        b2CreateMouseJoint(body->world->id, &def), NULL, body);
}

/* ------------------------------------------------------------------ */
/* World                                                               */
/* ------------------------------------------------------------------ */

/*
 * Box2D 3 reports collisions as event lists gathered during the step instead
 * of calling back mid-solve, so world:update() drains them right afterwards
 * and calls the script functions named with world:setCallbacks().
 */
static int dispatch_contact(struct fh_program *prog, const char *function,
                            b2ShapeId shapeA, b2ShapeId shapeB,
                            const b2Manifold *manifold, bool touching) {
    physics_Fixture *a = physics_Fixture_fromShapeId(shapeA);
    physics_Fixture *b = physics_Fixture_fromShapeId(shapeB);

    PhysicsContact *contact = calloc(1, sizeof(PhysicsContact));
    if (!contact)
        return fh_set_error(prog, "out of memory");
    contact->a = a;
    contact->b = b;
    contact->touching = touching;
    if (manifold)
        contact->manifold = *manifold;
    physics_Fixture_retain(a);
    physics_Fixture_retain(b);

    /* The three arguments have to stay reachable across each other's
     * allocation and across the call itself, so they are pinned. */
    int pin_state = fh_get_pin_state(prog);
    struct fh_value call_args[3];
    call_args[2] = new_c_obj_pinned(prog, contact, contact_gc, FH_PHYSICS_CONTACT);
    if (fh_is_null(&call_args[2])) {
        contact_gc(contact);
        fh_restore_pin_state(prog, pin_state);
        return fh_set_error(prog, "out of memory");
    }

    /* A shape destroyed in the same step no longer maps back to a fixture;
     * the callback then sees null rather than a dead object. */
    physics_Fixture *pair[2] = { a, b };
    for (int i = 0; i < 2; i++) {
        if (!pair[i]) {
            call_args[i] = fh_new_null();
            continue;
        }
        physics_Fixture_retain(pair[i]);
        call_args[i] = new_c_obj_pinned(prog, pair[i], fixture_gc, FH_PHYSICS_FIXTURE);
        if (fh_is_null(&call_args[i]))
            physics_Fixture_release(pair[i]);
    }

    int result = fh_call_function(prog, function, call_args, 3, NULL);
    fh_restore_pin_state(prog, pin_state);
    return result < 0 ? -1 : 0;
}

static int dispatch_contacts(struct fh_program *prog, physics_World *world) {
    if (!world->beginContact && !world->endContact)
        return 0;

    b2ContactEvents events = b2World_GetContactEvents(world->id);

    /* The event arrays belong to the Box2D world, so stop the moment a
     * callback destroys it (or clears the callback) under us. */
    if (world->beginContact) {
        for (int i = 0; i < events.beginCount; i++) {
            if (!physics_World_isValid(world) || !world->beginContact)
                return 0;
            const b2ContactBeginTouchEvent *event = events.beginEvents + i;
            if (dispatch_contact(prog, world->beginContact, event->shapeIdA,
                                 event->shapeIdB, &event->manifold, true) < 0)
                return -1;
        }
    }
    if (world->endContact) {
        for (int i = 0; i < events.endCount; i++) {
            if (!physics_World_isValid(world) || !world->endContact)
                return 0;
            const b2ContactEndTouchEvent *event = events.endEvents + i;
            if (dispatch_contact(prog, world->endContact, event->shapeIdA,
                                 event->shapeIdB, NULL, false) < 0)
                return -1;
        }
    }
    return 0;
}

static int fn_love_world_update(struct fh_program *prog,
                                struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_World *world = check_world(prog, args, n_args, 0);
    double dt;
    if (!world)
        return -1;
    if (check_number(prog, args, n_args, 1, &dt) < 0)
        return -1;

    int subSteps = (int)fh_optinteger(args, n_args, 2, 4);
    if (subSteps < 1)
        subSteps = 1;

    physics_World_update(world, (float)dt, subSteps);

    *ret = fh_new_null();
    return dispatch_contacts(prog, world);
}

static int fn_love_world_setGravity(struct fh_program *prog,
                                    struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_World *world = check_world(prog, args, n_args, 0);
    double x, y;
    if (!world)
        return -1;
    if (check_number(prog, args, n_args, 1, &x) < 0 ||
        check_number(prog, args, n_args, 2, &y) < 0)
        return -1;

    b2World_SetGravity(world->id, physics_scaleDownVec((float)x, (float)y));
    *ret = fh_new_null();
    return 0;
}

static int fn_love_world_getGravity(struct fh_program *prog,
                                    struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_World *world = check_world(prog, args, n_args, 0);
    if (!world)
        return -1;

    b2Vec2 gravity = b2World_GetGravity(world->id);
    return return_pair(prog, ret, physics_scaleUp(gravity.x), physics_scaleUp(gravity.y));
}

static int fn_love_world_setSleepingAllowed(struct fh_program *prog,
                                            struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_World *world = check_world(prog, args, n_args, 0);
    if (!world)
        return -1;
    if (n_args < 2 || !fh_is_bool(&args[1]))
        return type_error(prog, 1, "boolean");

    b2World_EnableSleeping(world->id, fh_get_bool(&args[1]));
    *ret = fh_new_null();
    return 0;
}

static int fn_love_world_isSleepingAllowed(struct fh_program *prog,
                                           struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_World *world = check_world(prog, args, n_args, 0);
    if (!world)
        return -1;
    *ret = fh_new_bool(b2World_IsSleepingEnabled(world->id));
    return 0;
}

static int fn_love_world_getBodyCount(struct fh_program *prog,
                                      struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_World *world = check_world(prog, args, n_args, 0);
    if (!world)
        return -1;
    *ret = fh_new_number(world->bodyCount);
    return 0;
}

static int fn_love_world_getBodies(struct fh_program *prog,
                                   struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_World *world = check_world(prog, args, n_args, 0);
    if (!world)
        return -1;

    ARRAY_BEGIN(world->bodyCount);
    for (int i = 0; i < world->bodyCount; i++)
        arr->items[i] = body_value(prog, world->bodies[i]);
    ARRAY_END();
    return 0;
}

static int fn_love_world_getJointCount(struct fh_program *prog,
                                       struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_World *world = check_world(prog, args, n_args, 0);
    if (!world)
        return -1;
    *ret = fh_new_number(world->jointCount);
    return 0;
}

static int fn_love_world_getJoints(struct fh_program *prog,
                                   struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_World *world = check_world(prog, args, n_args, 0);
    if (!world)
        return -1;

    ARRAY_BEGIN(world->jointCount);
    for (int i = 0; i < world->jointCount; i++)
        arr->items[i] = joint_value(prog, world->joints[i]);
    ARRAY_END();
    return 0;
}

/* setCallbacks(world, beginContactName, endContactName) — FH can only call a
 * global function by name from C, so these are strings (or null to clear). */
static int callback_name(struct fh_program *prog, struct fh_value *args, int n_args,
                         int index, const char **out) {
    *out = NULL;
    if (index >= n_args || fh_is_null(&args[index]))
        return 0;
    if (!fh_is_string(&args[index]))
        return type_error(prog, index, "function name (string) or null");
    *out = fh_get_string(&args[index]);
    return 0;
}

static int fn_love_world_setCallbacks(struct fh_program *prog,
                                      struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_World *world = check_world(prog, args, n_args, 0);
    const char *begin, *end;
    if (!world)
        return -1;
    if (callback_name(prog, args, n_args, 1, &begin) < 0 ||
        callback_name(prog, args, n_args, 2, &end) < 0)
        return -1;

    if (!physics_World_setBeginContact(world, begin) ||
        !physics_World_setEndContact(world, end))
        return fh_set_error(prog, "out of memory");

    *ret = fh_new_null();
    return 0;
}

static int fn_love_world_getCallbacks(struct fh_program *prog,
                                      struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_World *world = check_world(prog, args, n_args, 0);
    if (!world)
        return -1;

    ARRAY_BEGIN(2);
    arr->items[0] = world->beginContact ? fh_new_string(prog, world->beginContact) : fh_new_null();
    arr->items[1] = world->endContact   ? fh_new_string(prog, world->endContact)   : fh_new_null();
    ARRAY_END();
    return 0;
}

/* --- queries --- */

typedef struct {
    b2ShapeId *shapes;
    b2Vec2 *points;
    b2Vec2 *normals;
    float *fractions;
    int count, capacity;
} QueryResults;

static bool query_push(QueryResults *results, b2ShapeId shape, b2Vec2 point,
                       b2Vec2 normal, float fraction) {
    if (results->count == results->capacity) {
        int capacity = results->capacity ? results->capacity * 2 : 16;
        b2ShapeId *shapes = realloc(results->shapes, (size_t)capacity * sizeof(b2ShapeId));
        b2Vec2 *points = realloc(results->points, (size_t)capacity * sizeof(b2Vec2));
        b2Vec2 *normals = realloc(results->normals, (size_t)capacity * sizeof(b2Vec2));
        float *fractions = realloc(results->fractions, (size_t)capacity * sizeof(float));
        if (shapes) results->shapes = shapes;
        if (points) results->points = points;
        if (normals) results->normals = normals;
        if (fractions) results->fractions = fractions;
        if (!shapes || !points || !normals || !fractions)
            return false;
        results->capacity = capacity;
    }
    results->shapes[results->count] = shape;
    results->points[results->count] = point;
    results->normals[results->count] = normal;
    results->fractions[results->count] = fraction;
    results->count++;
    return true;
}

static void query_free(QueryResults *results) {
    free(results->shapes);
    free(results->points);
    free(results->normals);
    free(results->fractions);
}

static float raycast_collect(b2ShapeId shape, b2Vec2 point, b2Vec2 normal,
                             float fraction, void *context) {
    query_push(context, shape, point, normal, fraction);
    return 1.0f;  /* keep going: LÖVE's rayCast reports every hit */
}

static bool overlap_collect(b2ShapeId shape, void *context) {
    b2Vec2 zero = { 0.0f, 0.0f };
    query_push(context, shape, zero, zero, 0.0f);
    return true;
}

/* One hit as [fixture, x, y, nx, ny, fraction], LÖVE's rayCast callback
 * arguments in array form. */
static struct fh_value raycast_hit_value(struct fh_program *prog, const QueryResults *results, int i) {
    int pin_state = fh_get_pin_state(prog);
    struct fh_array *hit = fh_make_array(prog, true);
    if (!hit || !fh_grow_array_object(prog, hit, 6)) {
        fh_restore_pin_state(prog, pin_state);
        return fh_new_null();
    }
    hit->items[0] = fixture_value(prog, physics_Fixture_fromShapeId(results->shapes[i]));
    hit->items[1] = fh_new_number(physics_scaleUp(results->points[i].x));
    hit->items[2] = fh_new_number(physics_scaleUp(results->points[i].y));
    hit->items[3] = fh_new_number(results->normals[i].x);
    hit->items[4] = fh_new_number(results->normals[i].y);
    hit->items[5] = fh_new_number(results->fractions[i]);
    fh_restore_pin_state(prog, pin_state);
    return (struct fh_value){ .data = { .obj = hit }, .type = FH_VAL_ARRAY };
}

static int fn_love_world_rayCast(struct fh_program *prog,
                                 struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_World *world = check_world(prog, args, n_args, 0);
    double x1, y1, x2, y2;
    if (!world)
        return -1;
    if (check_number(prog, args, n_args, 1, &x1) < 0 ||
        check_number(prog, args, n_args, 2, &y1) < 0 ||
        check_number(prog, args, n_args, 3, &x2) < 0 ||
        check_number(prog, args, n_args, 4, &y2) < 0)
        return -1;

    b2Vec2 origin = physics_scaleDownVec((float)x1, (float)y1);
    b2Vec2 target = physics_scaleDownVec((float)x2, (float)y2);

    QueryResults results = { 0 };
    b2World_CastRay(world->id, origin, b2Sub(target, origin),
                    b2DefaultQueryFilter(), raycast_collect, &results);

    int pin_state = fh_get_pin_state(prog);
    struct fh_array *arr = fh_make_array(prog, true);
    if (!arr || !fh_grow_array_object(prog, arr, (uint32_t)results.count)) {
        fh_restore_pin_state(prog, pin_state);
        query_free(&results);
        return fh_set_error(prog, "out of memory");
    }
    for (int i = 0; i < results.count; i++)
        arr->items[i] = raycast_hit_value(prog, &results, i);
    fh_restore_pin_state(prog, pin_state);
    query_free(&results);

    *ret = (struct fh_value){ .data = { .obj = arr }, .type = FH_VAL_ARRAY };
    return 0;
}

static int fn_love_world_rayCastClosest(struct fh_program *prog,
                                        struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_World *world = check_world(prog, args, n_args, 0);
    double x1, y1, x2, y2;
    if (!world)
        return -1;
    if (check_number(prog, args, n_args, 1, &x1) < 0 ||
        check_number(prog, args, n_args, 2, &y1) < 0 ||
        check_number(prog, args, n_args, 3, &x2) < 0 ||
        check_number(prog, args, n_args, 4, &y2) < 0)
        return -1;

    b2Vec2 origin = physics_scaleDownVec((float)x1, (float)y1);
    b2Vec2 target = physics_scaleDownVec((float)x2, (float)y2);
    b2RayResult hit = b2World_CastRayClosest(world->id, origin, b2Sub(target, origin),
                                             b2DefaultQueryFilter());
    if (!hit.hit) {
        *ret = fh_new_null();
        return 0;
    }

    QueryResults results = { 0 };
    if (!query_push(&results, hit.shapeId, hit.point, hit.normal, hit.fraction)) {
        query_free(&results);
        return fh_set_error(prog, "out of memory");
    }
    *ret = raycast_hit_value(prog, &results, 0);
    query_free(&results);
    return 0;
}

static int fn_love_world_queryBoundingBox(struct fh_program *prog,
                                          struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_World *world = check_world(prog, args, n_args, 0);
    double x1, y1, x2, y2;
    if (!world)
        return -1;
    if (check_number(prog, args, n_args, 1, &x1) < 0 ||
        check_number(prog, args, n_args, 2, &y1) < 0 ||
        check_number(prog, args, n_args, 3, &x2) < 0 ||
        check_number(prog, args, n_args, 4, &y2) < 0)
        return -1;

    b2AABB aabb;
    aabb.lowerBound = physics_scaleDownVec((float)(x1 < x2 ? x1 : x2), (float)(y1 < y2 ? y1 : y2));
    aabb.upperBound = physics_scaleDownVec((float)(x1 > x2 ? x1 : x2), (float)(y1 > y2 ? y1 : y2));

    QueryResults results = { 0 };
    b2World_OverlapAABB(world->id, aabb, b2DefaultQueryFilter(), overlap_collect, &results);

    int pin_state = fh_get_pin_state(prog);
    struct fh_array *arr = fh_make_array(prog, true);
    if (!arr || !fh_grow_array_object(prog, arr, (uint32_t)results.count)) {
        fh_restore_pin_state(prog, pin_state);
        query_free(&results);
        return fh_set_error(prog, "out of memory");
    }
    for (int i = 0; i < results.count; i++)
        arr->items[i] = fixture_value(prog, physics_Fixture_fromShapeId(results.shapes[i]));
    fh_restore_pin_state(prog, pin_state);
    query_free(&results);

    *ret = (struct fh_value){ .data = { .obj = arr }, .type = FH_VAL_ARRAY };
    return 0;
}

static int fn_love_world_destroy(struct fh_program *prog,
                                 struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_World *world = raw_world(prog, args, n_args, 0);
    if (!world)
        return -1;
    physics_World_destroy(world);
    *ret = fh_new_null();
    return 0;
}

static int fn_love_world_isDestroyed(struct fh_program *prog,
                                     struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_World *world = raw_world(prog, args, n_args, 0);
    if (!world)
        return -1;
    *ret = fh_new_bool(!physics_World_isValid(world));
    return 0;
}

/* ------------------------------------------------------------------ */
/* User data (numbers and strings)                                     */
/* ------------------------------------------------------------------ */

static struct fh_value userdata_value(struct fh_program *prog, const physics_UserData *data) {
    switch (data->type) {
    case physics_UserData_number: return fh_new_number(data->number);
    case physics_UserData_string: return fh_new_string(prog, data->string);
    default:                      return fh_new_null();
    }
}

static int set_userdata(struct fh_program *prog, physics_UserData *data,
                        struct fh_value *args, int n_args, int index) {
    if (index >= n_args || fh_is_null(&args[index])) {
        physics_UserData_clear(data);
        return 0;
    }
    if (fh_is_number(&args[index])) {
        physics_UserData_setNumber(data, fh_get_number(&args[index]));
        return 0;
    }
    if (fh_is_string(&args[index])) {
        if (!physics_UserData_setString(data, fh_get_string(&args[index])))
            return fh_set_error(prog, "out of memory");
        return 0;
    }
    return fh_set_error(prog, "User data must be a number, a string or null "
                              "(FH cannot keep an arbitrary value alive from C)");
}

/* ------------------------------------------------------------------ */
/* Body                                                                */
/* ------------------------------------------------------------------ */

/* The repetitive accessors. `body` and, for the setters, `value`/`flag` are
 * in scope in the expression or statement passed in. */
#define DEF_BODY_GET(name, expr)                                                 \
static int fn_love_body_##name(struct fh_program *prog, struct fh_value *ret,    \
                               struct fh_value *args, int n_args) {              \
    physics_Body *body = check_body(prog, args, n_args, 0);                      \
    if (!body)                                                                   \
        return -1;                                                               \
    *ret = (expr);                                                               \
    return 0;                                                                    \
}

#define DEF_BODY_SET_NUMBER(name, stmt)                                          \
static int fn_love_body_##name(struct fh_program *prog, struct fh_value *ret,    \
                               struct fh_value *args, int n_args) {              \
    physics_Body *body = check_body(prog, args, n_args, 0);                      \
    double value;                                                                \
    if (!body)                                                                   \
        return -1;                                                               \
    if (check_number(prog, args, n_args, 1, &value) < 0)                          \
        return -1;                                                               \
    stmt;                                                                        \
    *ret = fh_new_null();                                                        \
    return 0;                                                                    \
}

#define DEF_BODY_SET_BOOL(name, stmt)                                            \
static int fn_love_body_##name(struct fh_program *prog, struct fh_value *ret,    \
                               struct fh_value *args, int n_args) {              \
    physics_Body *body = check_body(prog, args, n_args, 0);                      \
    if (!body)                                                                   \
        return -1;                                                               \
    if (n_args < 2 || !fh_is_bool(&args[1]))                                     \
        return type_error(prog, 1, "boolean");                                   \
    bool flag = fh_get_bool(&args[1]);                                           \
    stmt;                                                                        \
    *ret = fh_new_null();                                                        \
    return 0;                                                                    \
}

/* Two numbers in, two out — getWorldPoint(x, y) and friends. */
#define DEF_BODY_TRANSFORM(name, call, scaleResult)                              \
static int fn_love_body_##name(struct fh_program *prog, struct fh_value *ret,    \
                               struct fh_value *args, int n_args) {              \
    physics_Body *body = check_body(prog, args, n_args, 0);                      \
    double x, y;                                                                 \
    if (!body)                                                                   \
        return -1;                                                               \
    if (check_number(prog, args, n_args, 1, &x) < 0 ||                            \
        check_number(prog, args, n_args, 2, &y) < 0)                              \
        return -1;                                                               \
    b2Vec2 out = call;                                                           \
    return scaleResult                                                           \
        ? return_pair(prog, ret, physics_scaleUp(out.x), physics_scaleUp(out.y))  \
        : return_pair(prog, ret, out.x, out.y);                                  \
}

DEF_BODY_GET(getX, fh_new_number(physics_scaleUp(b2Body_GetPosition(body->id).x)))
DEF_BODY_GET(getY, fh_new_number(physics_scaleUp(b2Body_GetPosition(body->id).y)))
DEF_BODY_GET(getAngle, fh_new_number(body_angle(body->id)))
DEF_BODY_GET(getAngularVelocity, fh_new_number(b2Body_GetAngularVelocity(body->id)))
DEF_BODY_GET(getMass, fh_new_number(b2Body_GetMass(body->id)))
/* Rotational inertia is mass * distance^2, so it needs the scale twice. */
DEF_BODY_GET(getInertia,
             fh_new_number(physics_scaleUp(physics_scaleUp(b2Body_GetRotationalInertia(body->id)))))
DEF_BODY_GET(getLinearDamping, fh_new_number(b2Body_GetLinearDamping(body->id)))
DEF_BODY_GET(getAngularDamping, fh_new_number(b2Body_GetAngularDamping(body->id)))
DEF_BODY_GET(getGravityScale, fh_new_number(b2Body_GetGravityScale(body->id)))
DEF_BODY_GET(isFixedRotation, fh_new_bool(b2Body_IsFixedRotation(body->id)))
DEF_BODY_GET(isBullet, fh_new_bool(b2Body_IsBullet(body->id)))
DEF_BODY_GET(isAwake, fh_new_bool(b2Body_IsAwake(body->id)))
DEF_BODY_GET(isSleepingAllowed, fh_new_bool(b2Body_IsSleepEnabled(body->id)))
DEF_BODY_GET(isActive, fh_new_bool(b2Body_IsEnabled(body->id)))
DEF_BODY_GET(getType, fh_new_string(prog, body_type_to_string(b2Body_GetType(body->id))))
DEF_BODY_GET(getUserData, userdata_value(prog, &body->userData))

DEF_BODY_SET_NUMBER(setAngle,
                    b2Body_SetTransform(body->id, b2Body_GetPosition(body->id),
                                        b2MakeRot((float)value)))
DEF_BODY_SET_NUMBER(setAngularVelocity, b2Body_SetAngularVelocity(body->id, (float)value))
DEF_BODY_SET_NUMBER(setLinearDamping, b2Body_SetLinearDamping(body->id, (float)value))
DEF_BODY_SET_NUMBER(setAngularDamping, b2Body_SetAngularDamping(body->id, (float)value))
DEF_BODY_SET_NUMBER(setGravityScale, b2Body_SetGravityScale(body->id, (float)value))
/* Torque and angular impulse are force * distance: scaled twice, like inertia. */
DEF_BODY_SET_NUMBER(applyTorque,
                    b2Body_ApplyTorque(body->id,
                                       physics_scaleDown(physics_scaleDown((float)value)), true))
DEF_BODY_SET_NUMBER(applyAngularImpulse,
                    b2Body_ApplyAngularImpulse(body->id,
                                               physics_scaleDown(physics_scaleDown((float)value)), true))
DEF_BODY_SET_NUMBER(setX,
                    b2Body_SetTransform(body->id,
                                        (b2Vec2){ physics_scaleDown((float)value),
                                                  b2Body_GetPosition(body->id).y },
                                        b2Body_GetRotation(body->id)))
DEF_BODY_SET_NUMBER(setY,
                    b2Body_SetTransform(body->id,
                                        (b2Vec2){ b2Body_GetPosition(body->id).x,
                                                  physics_scaleDown((float)value) },
                                        b2Body_GetRotation(body->id)))

DEF_BODY_SET_BOOL(setFixedRotation, b2Body_SetFixedRotation(body->id, flag))
DEF_BODY_SET_BOOL(setBullet, b2Body_SetBullet(body->id, flag))
DEF_BODY_SET_BOOL(setAwake, b2Body_SetAwake(body->id, flag))
DEF_BODY_SET_BOOL(setSleepingAllowed, b2Body_EnableSleep(body->id, flag))
DEF_BODY_SET_BOOL(setActive, flag ? b2Body_Enable(body->id) : b2Body_Disable(body->id))

DEF_BODY_TRANSFORM(getWorldPoint,
                   b2Body_GetWorldPoint(body->id, physics_scaleDownVec((float)x, (float)y)), true)
DEF_BODY_TRANSFORM(getLocalPoint,
                   b2Body_GetLocalPoint(body->id, physics_scaleDownVec((float)x, (float)y)), true)
DEF_BODY_TRANSFORM(getWorldVector,
                   b2Body_GetWorldVector(body->id, (b2Vec2){ (float)x, (float)y }), false)
DEF_BODY_TRANSFORM(getLocalVector,
                   b2Body_GetLocalVector(body->id, (b2Vec2){ (float)x, (float)y }), false)
DEF_BODY_TRANSFORM(getLinearVelocityFromWorldPoint,
                   b2Body_GetWorldPointVelocity(body->id,
                                                physics_scaleDownVec((float)x, (float)y)), true)
DEF_BODY_TRANSFORM(getLinearVelocityFromLocalPoint,
                   b2Body_GetLocalPointVelocity(body->id,
                                                physics_scaleDownVec((float)x, (float)y)), true)

static int fn_love_body_getPosition(struct fh_program *prog,
                                    struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Body *body = check_body(prog, args, n_args, 0);
    if (!body)
        return -1;
    b2Vec2 position = b2Body_GetPosition(body->id);
    return return_pair(prog, ret, physics_scaleUp(position.x), physics_scaleUp(position.y));
}

static int fn_love_body_setPosition(struct fh_program *prog,
                                    struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Body *body = check_body(prog, args, n_args, 0);
    double x, y;
    if (!body)
        return -1;
    if (check_number(prog, args, n_args, 1, &x) < 0 ||
        check_number(prog, args, n_args, 2, &y) < 0)
        return -1;

    b2Body_SetTransform(body->id, physics_scaleDownVec((float)x, (float)y),
                        b2Body_GetRotation(body->id));
    *ret = fh_new_null();
    return 0;
}

static int fn_love_body_setTransform(struct fh_program *prog,
                                     struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Body *body = check_body(prog, args, n_args, 0);
    double x, y;
    if (!body)
        return -1;
    if (check_number(prog, args, n_args, 1, &x) < 0 ||
        check_number(prog, args, n_args, 2, &y) < 0)
        return -1;
    double angle = fh_optnumber(args, n_args, 3, body_angle(body->id));

    b2Body_SetTransform(body->id, physics_scaleDownVec((float)x, (float)y),
                        b2MakeRot((float)angle));
    *ret = fh_new_null();
    return 0;
}

static int fn_love_body_getLinearVelocity(struct fh_program *prog,
                                          struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Body *body = check_body(prog, args, n_args, 0);
    if (!body)
        return -1;
    b2Vec2 velocity = b2Body_GetLinearVelocity(body->id);
    return return_pair(prog, ret, physics_scaleUp(velocity.x), physics_scaleUp(velocity.y));
}

static int fn_love_body_setLinearVelocity(struct fh_program *prog,
                                          struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Body *body = check_body(prog, args, n_args, 0);
    double x, y;
    if (!body)
        return -1;
    if (check_number(prog, args, n_args, 1, &x) < 0 ||
        check_number(prog, args, n_args, 2, &y) < 0)
        return -1;

    b2Body_SetLinearVelocity(body->id, physics_scaleDownVec((float)x, (float)y));
    *ret = fh_new_null();
    return 0;
}

/* applyForce(fx, fy) applies at the centre of mass; applyForce(fx, fy, x, y)
 * at a world point, as in LÖVE. Same shape for applyLinearImpulse. */
static int body_apply(struct fh_program *prog, struct fh_value *ret,
                      struct fh_value *args, int n_args, bool impulse) {
    physics_Body *body = check_body(prog, args, n_args, 0);
    double fx, fy;
    if (!body)
        return -1;
    if (check_number(prog, args, n_args, 1, &fx) < 0 ||
        check_number(prog, args, n_args, 2, &fy) < 0)
        return -1;

    b2Vec2 amount = physics_scaleDownVec((float)fx, (float)fy);

    if (n_args >= 5) {
        double x, y;
        if (check_number(prog, args, n_args, 3, &x) < 0 ||
            check_number(prog, args, n_args, 4, &y) < 0)
            return -1;
        b2Vec2 point = physics_scaleDownVec((float)x, (float)y);
        if (impulse)
            b2Body_ApplyLinearImpulse(body->id, amount, point, true);
        else
            b2Body_ApplyForce(body->id, amount, point, true);
    } else {
        if (impulse)
            b2Body_ApplyLinearImpulseToCenter(body->id, amount, true);
        else
            b2Body_ApplyForceToCenter(body->id, amount, true);
    }

    *ret = fh_new_null();
    return 0;
}

static int fn_love_body_applyForce(struct fh_program *prog,
                                   struct fh_value *ret, struct fh_value *args, int n_args) {
    return body_apply(prog, ret, args, n_args, false);
}

static int fn_love_body_applyLinearImpulse(struct fh_program *prog,
                                           struct fh_value *ret, struct fh_value *args, int n_args) {
    return body_apply(prog, ret, args, n_args, true);
}

static int fn_love_body_getWorldCenter(struct fh_program *prog,
                                       struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Body *body = check_body(prog, args, n_args, 0);
    if (!body)
        return -1;
    b2Vec2 center = b2Body_GetWorldCenterOfMass(body->id);
    return return_pair(prog, ret, physics_scaleUp(center.x), physics_scaleUp(center.y));
}

static int fn_love_body_getLocalCenter(struct fh_program *prog,
                                       struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Body *body = check_body(prog, args, n_args, 0);
    if (!body)
        return -1;
    b2Vec2 center = b2Body_GetLocalCenterOfMass(body->id);
    return return_pair(prog, ret, physics_scaleUp(center.x), physics_scaleUp(center.y));
}

/* getMassData() -> [x, y, mass, inertia] */
static int fn_love_body_getMassData(struct fh_program *prog,
                                    struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Body *body = check_body(prog, args, n_args, 0);
    if (!body)
        return -1;

    b2MassData data = b2Body_GetMassData(body->id);

    ARRAY_BEGIN(4);
    arr->items[0] = fh_new_number(physics_scaleUp(data.center.x));
    arr->items[1] = fh_new_number(physics_scaleUp(data.center.y));
    arr->items[2] = fh_new_number(data.mass);
    arr->items[3] = fh_new_number(physics_scaleUp(physics_scaleUp(data.rotationalInertia)));
    ARRAY_END();
    return 0;
}

static int fn_love_body_resetMassData(struct fh_program *prog,
                                      struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Body *body = check_body(prog, args, n_args, 0);
    if (!body)
        return -1;
    b2Body_ApplyMassFromShapes(body->id);
    *ret = fh_new_null();
    return 0;
}

/* getWorldPoints(x1, y1, x2, y2, ...) -> [X1, Y1, X2, Y2, ...] */
static int fn_love_body_getWorldPoints(struct fh_program *prog,
                                       struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Body *body = check_body(prog, args, n_args, 0);
    if (!body)
        return -1;

    int pointCount = 0;
    float *points = read_points(prog, args, n_args, 1, &pointCount);
    if (!points)
        return -1;

    int pin_state = fh_get_pin_state(prog);
    struct fh_array *arr = fh_make_array(prog, true);
    if (!arr || !fh_grow_array_object(prog, arr, (uint32_t)(pointCount * 2))) {
        fh_restore_pin_state(prog, pin_state);
        free(points);
        return fh_set_error(prog, "out of memory");
    }
    for (int i = 0; i < pointCount; i++) {
        b2Vec2 world = b2Body_GetWorldPoint(body->id,
                                            physics_scaleDownVec(points[2*i], points[2*i + 1]));
        arr->items[2*i]     = fh_new_number(physics_scaleUp(world.x));
        arr->items[2*i + 1] = fh_new_number(physics_scaleUp(world.y));
    }
    fh_restore_pin_state(prog, pin_state);
    free(points);

    *ret = (struct fh_value){ .data = { .obj = arr }, .type = FH_VAL_ARRAY };
    return 0;
}

static int fn_love_body_setType(struct fh_program *prog,
                                struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Body *body = check_body(prog, args, n_args, 0);
    b2BodyType type;
    if (!body)
        return -1;
    if (n_args < 2 || !fh_is_string(&args[1]))
        return type_error(prog, 1, "string");
    if (body_type_from_string(prog, fh_get_string(&args[1]), &type) < 0)
        return -1;

    b2Body_SetType(body->id, type);
    *ret = fh_new_null();
    return 0;
}

static int fn_love_body_setUserData(struct fh_program *prog,
                                    struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Body *body = check_body(prog, args, n_args, 0);
    if (!body)
        return -1;
    if (set_userdata(prog, &body->userData, args, n_args, 1) < 0)
        return -1;
    *ret = fh_new_null();
    return 0;
}

static int fn_love_body_getWorld(struct fh_program *prog,
                                 struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Body *body = check_body(prog, args, n_args, 0);
    if (!body)
        return -1;

    physics_World_retain(body->world);
    *ret = fh_new_c_obj(prog, body->world, world_gc, FH_PHYSICS_WORLD);
    if (fh_is_null(ret)) {
        physics_World_release(body->world);
        return fh_set_error(prog, "out of memory");
    }
    return 0;
}

static int fn_love_body_getFixtures(struct fh_program *prog,
                                    struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Body *body = check_body(prog, args, n_args, 0);
    if (!body)
        return -1;

    int count = b2Body_GetShapeCount(body->id);
    b2ShapeId *shapes = count > 0 ? malloc((size_t)count * sizeof(b2ShapeId)) : NULL;
    if (count > 0 && !shapes)
        return fh_set_error(prog, "out of memory");
    count = b2Body_GetShapes(body->id, shapes, count);

    int pin_state = fh_get_pin_state(prog);
    struct fh_array *arr = fh_make_array(prog, true);
    if (!arr || !fh_grow_array_object(prog, arr, (uint32_t)count)) {
        fh_restore_pin_state(prog, pin_state);
        free(shapes);
        return fh_set_error(prog, "out of memory");
    }
    for (int i = 0; i < count; i++)
        arr->items[i] = fixture_value(prog, physics_Fixture_fromShapeId(shapes[i]));
    fh_restore_pin_state(prog, pin_state);
    free(shapes);

    *ret = (struct fh_value){ .data = { .obj = arr }, .type = FH_VAL_ARRAY };
    return 0;
}

static int fn_love_body_getJoints(struct fh_program *prog,
                                  struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Body *body = check_body(prog, args, n_args, 0);
    if (!body)
        return -1;

    int count = b2Body_GetJointCount(body->id);
    b2JointId *joints = count > 0 ? malloc((size_t)count * sizeof(b2JointId)) : NULL;
    if (count > 0 && !joints)
        return fh_set_error(prog, "out of memory");
    count = b2Body_GetJoints(body->id, joints, count);

    int pin_state = fh_get_pin_state(prog);
    struct fh_array *arr = fh_make_array(prog, true);
    if (!arr || !fh_grow_array_object(prog, arr, (uint32_t)count)) {
        fh_restore_pin_state(prog, pin_state);
        free(joints);
        return fh_set_error(prog, "out of memory");
    }
    for (int i = 0; i < count; i++)
        arr->items[i] = joint_value(prog, physics_Joint_fromJointId(joints[i]));
    fh_restore_pin_state(prog, pin_state);
    free(joints);

    *ret = (struct fh_value){ .data = { .obj = arr }, .type = FH_VAL_ARRAY };
    return 0;
}

static int fn_love_body_destroy(struct fh_program *prog,
                                struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Body *body = raw_body(prog, args, n_args, 0);
    if (!body)
        return -1;
    physics_Body_destroy(body);
    *ret = fh_new_null();
    return 0;
}

static int fn_love_body_isDestroyed(struct fh_program *prog,
                                    struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Body *body = raw_body(prog, args, n_args, 0);
    if (!body)
        return -1;
    *ret = fh_new_bool(!physics_Body_isValid(body));
    return 0;
}

/* ------------------------------------------------------------------ */
/* Fixture                                                             */
/* ------------------------------------------------------------------ */

/*
 * A chain fixture is a b2Chain, not a b2Shape: it has friction and
 * restitution but no density, no filter and nothing to query against. The
 * methods that need a real shape go through here so a chain gets a clear
 * error instead of operating on a null shape id.
 */
static bool fixture_shape_id(struct fh_program *prog, physics_Fixture *fixture, b2ShapeId *out) {
    if (fixture->kind == physics_FixtureKind_chain) {
        fh_set_error(prog, "This Fixture holds a chain shape, which does not support "
                           "that operation");
        return false;
    }
    *out = fixture->shapeId;
    return true;
}

#define DEF_FIXTURE_GET(name, expr)                                              \
static int fn_love_fixture_##name(struct fh_program *prog, struct fh_value *ret, \
                                  struct fh_value *args, int n_args) {           \
    physics_Fixture *fixture = check_fixture(prog, args, n_args, 0);             \
    b2ShapeId shape;                                                             \
    if (!fixture)                                                                \
        return -1;                                                               \
    if (!fixture_shape_id(prog, fixture, &shape))                                \
        return -1;                                                               \
    (void)shape;                                                                 \
    *ret = (expr);                                                               \
    return 0;                                                                    \
}

#define DEF_FIXTURE_SET_NUMBER(name, stmt)                                       \
static int fn_love_fixture_##name(struct fh_program *prog, struct fh_value *ret, \
                                  struct fh_value *args, int n_args) {           \
    physics_Fixture *fixture = check_fixture(prog, args, n_args, 0);             \
    b2ShapeId shape;                                                             \
    double value;                                                                \
    if (!fixture)                                                                \
        return -1;                                                               \
    if (!fixture_shape_id(prog, fixture, &shape))                                \
        return -1;                                                               \
    if (check_number(prog, args, n_args, 1, &value) < 0)                          \
        return -1;                                                               \
    (void)shape;                                                                 \
    stmt;                                                                        \
    *ret = fh_new_null();                                                        \
    return 0;                                                                    \
}

DEF_FIXTURE_GET(getDensity, fh_new_number(b2Shape_GetDensity(shape)))
DEF_FIXTURE_GET(isSensor, fh_new_bool(b2Shape_IsSensor(shape)))
DEF_FIXTURE_SET_NUMBER(setDensity, b2Shape_SetDensity(shape, (float)value, true))

/* Friction and restitution are the two properties a chain does have, so they
 * are written out rather than generated. */
static int fn_love_fixture_getFriction(struct fh_program *prog,
                                       struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Fixture *fixture = check_fixture(prog, args, n_args, 0);
    if (!fixture)
        return -1;
    *ret = fh_new_number(fixture->kind == physics_FixtureKind_chain
                         ? b2Chain_GetFriction(fixture->chainId)
                         : b2Shape_GetFriction(fixture->shapeId));
    return 0;
}

static int fn_love_fixture_setFriction(struct fh_program *prog,
                                       struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Fixture *fixture = check_fixture(prog, args, n_args, 0);
    double value;
    if (!fixture)
        return -1;
    if (check_number(prog, args, n_args, 1, &value) < 0)
        return -1;

    if (fixture->kind == physics_FixtureKind_chain)
        b2Chain_SetFriction(fixture->chainId, (float)value);
    else
        b2Shape_SetFriction(fixture->shapeId, (float)value);
    *ret = fh_new_null();
    return 0;
}

static int fn_love_fixture_getRestitution(struct fh_program *prog,
                                          struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Fixture *fixture = check_fixture(prog, args, n_args, 0);
    if (!fixture)
        return -1;
    *ret = fh_new_number(fixture->kind == physics_FixtureKind_chain
                         ? b2Chain_GetRestitution(fixture->chainId)
                         : b2Shape_GetRestitution(fixture->shapeId));
    return 0;
}

static int fn_love_fixture_setRestitution(struct fh_program *prog,
                                          struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Fixture *fixture = check_fixture(prog, args, n_args, 0);
    double value;
    if (!fixture)
        return -1;
    if (check_number(prog, args, n_args, 1, &value) < 0)
        return -1;

    if (fixture->kind == physics_FixtureKind_chain)
        b2Chain_SetRestitution(fixture->chainId, (float)value);
    else
        b2Shape_SetRestitution(fixture->shapeId, (float)value);
    *ret = fh_new_null();
    return 0;
}

static int fn_love_fixture_setSensor(struct fh_program *prog,
                                     struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Fixture *fixture = check_fixture(prog, args, n_args, 0);
    b2ShapeId shape;
    if (!fixture)
        return -1;
    if (!fixture_shape_id(prog, fixture, &shape))
        return -1;
    if (n_args < 2 || !fh_is_bool(&args[1]))
        return type_error(prog, 1, "boolean");

    /* Box2D 3 fixes whether a shape is a sensor when it is created, so this
     * is the one LÖVE setter that cannot be honoured after the fact. */
    if (b2Shape_IsSensor(shape) != fh_get_bool(&args[1]))
        return fh_set_error(prog, "Box2D 3 cannot turn sensing on or off after a Fixture "
                                  "exists; create the Shape as a sensor instead");
    *ret = fh_new_null();
    return 0;
}

/* LÖVE addresses collision filters by category *index* (1..16), not by bit. */
#define PHYSICS_MAX_CATEGORY 16

static int categories_to_bits(struct fh_program *prog, struct fh_value *args, int n_args,
                              int first, uint64_t *out) {
    uint64_t bits = 0;
    for (int i = first; i < n_args; i++) {
        if (!fh_is_number(&args[i]))
            return type_error(prog, i, "category number");
        int index = (int)fh_get_number(&args[i]);
        if (index < 1 || index > PHYSICS_MAX_CATEGORY)
            return fh_set_error(prog, "Category %d is out of range (1..%d)",
                                index, PHYSICS_MAX_CATEGORY);
        bits |= (uint64_t)1 << (index - 1);
    }
    *out = bits;
    return 0;
}

static int return_categories(struct fh_program *prog, struct fh_value *ret, uint64_t bits) {
    int count = 0;
    for (int i = 0; i < PHYSICS_MAX_CATEGORY; i++)
        if (bits & ((uint64_t)1 << i))
            count++;

    ARRAY_BEGIN(count);
    int at = 0;
    for (int i = 0; i < PHYSICS_MAX_CATEGORY; i++)
        if (bits & ((uint64_t)1 << i))
            arr->items[at++] = fh_new_number(i + 1);
    ARRAY_END();
    return 0;
}

static int fn_love_fixture_setCategory(struct fh_program *prog,
                                       struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Fixture *fixture = check_fixture(prog, args, n_args, 0);
    b2ShapeId shape;
    uint64_t bits;
    if (!fixture)
        return -1;
    if (!fixture_shape_id(prog, fixture, &shape))
        return -1;
    if (categories_to_bits(prog, args, n_args, 1, &bits) < 0)
        return -1;

    b2Filter filter = b2Shape_GetFilter(shape);
    filter.categoryBits = bits;
    b2Shape_SetFilter(shape, filter);
    *ret = fh_new_null();
    return 0;
}

static int fn_love_fixture_getCategory(struct fh_program *prog,
                                       struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Fixture *fixture = check_fixture(prog, args, n_args, 0);
    b2ShapeId shape;
    if (!fixture)
        return -1;
    if (!fixture_shape_id(prog, fixture, &shape))
        return -1;
    return return_categories(prog, ret, b2Shape_GetFilter(shape).categoryBits);
}

static int fn_love_fixture_setMask(struct fh_program *prog,
                                   struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Fixture *fixture = check_fixture(prog, args, n_args, 0);
    b2ShapeId shape;
    uint64_t bits;
    if (!fixture)
        return -1;
    if (!fixture_shape_id(prog, fixture, &shape))
        return -1;
    if (categories_to_bits(prog, args, n_args, 1, &bits) < 0)
        return -1;

    /* LÖVE's setMask lists the categories NOT to collide with. */
    b2Filter filter = b2Shape_GetFilter(shape);
    filter.maskBits = ~bits;
    b2Shape_SetFilter(shape, filter);
    *ret = fh_new_null();
    return 0;
}

static int fn_love_fixture_getMask(struct fh_program *prog,
                                   struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Fixture *fixture = check_fixture(prog, args, n_args, 0);
    b2ShapeId shape;
    if (!fixture)
        return -1;
    if (!fixture_shape_id(prog, fixture, &shape))
        return -1;
    return return_categories(prog, ret, ~b2Shape_GetFilter(shape).maskBits);
}

static int fn_love_fixture_setGroupIndex(struct fh_program *prog,
                                         struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Fixture *fixture = check_fixture(prog, args, n_args, 0);
    b2ShapeId shape;
    double value;
    if (!fixture)
        return -1;
    if (!fixture_shape_id(prog, fixture, &shape))
        return -1;
    if (check_number(prog, args, n_args, 1, &value) < 0)
        return -1;

    b2Filter filter = b2Shape_GetFilter(shape);
    filter.groupIndex = (int)value;
    b2Shape_SetFilter(shape, filter);
    *ret = fh_new_null();
    return 0;
}

DEF_FIXTURE_GET(getGroupIndex, fh_new_number(b2Shape_GetFilter(shape).groupIndex))

/* getFilterData() -> [categoryBits, maskBits, groupIndex] */
static int fn_love_fixture_getFilterData(struct fh_program *prog,
                                         struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Fixture *fixture = check_fixture(prog, args, n_args, 0);
    b2ShapeId shape;
    if (!fixture)
        return -1;
    if (!fixture_shape_id(prog, fixture, &shape))
        return -1;

    b2Filter filter = b2Shape_GetFilter(shape);
    ARRAY_BEGIN(3);
    arr->items[0] = fh_new_number((double)filter.categoryBits);
    arr->items[1] = fh_new_number((double)filter.maskBits);
    arr->items[2] = fh_new_number(filter.groupIndex);
    ARRAY_END();
    return 0;
}

static int fn_love_fixture_setFilterData(struct fh_program *prog,
                                         struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Fixture *fixture = check_fixture(prog, args, n_args, 0);
    b2ShapeId shape;
    double category, mask, group;
    if (!fixture)
        return -1;
    if (!fixture_shape_id(prog, fixture, &shape))
        return -1;
    if (check_number(prog, args, n_args, 1, &category) < 0 ||
        check_number(prog, args, n_args, 2, &mask) < 0 ||
        check_number(prog, args, n_args, 3, &group) < 0)
        return -1;

    b2Filter filter;
    filter.categoryBits = (uint64_t)category;
    filter.maskBits = (uint64_t)mask;
    filter.groupIndex = (int)group;
    b2Shape_SetFilter(shape, filter);
    *ret = fh_new_null();
    return 0;
}

static int fn_love_fixture_testPoint(struct fh_program *prog,
                                     struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Fixture *fixture = check_fixture(prog, args, n_args, 0);
    b2ShapeId shape;
    double x, y;
    if (!fixture)
        return -1;
    if (!fixture_shape_id(prog, fixture, &shape))
        return -1;
    if (check_number(prog, args, n_args, 1, &x) < 0 ||
        check_number(prog, args, n_args, 2, &y) < 0)
        return -1;

    *ret = fh_new_bool(b2Shape_TestPoint(shape, physics_scaleDownVec((float)x, (float)y)));
    return 0;
}

/* rayCast(x1, y1, x2, y2 [, maxFraction]) -> [x, y, nx, ny, fraction] or null */
static int fn_love_fixture_rayCast(struct fh_program *prog,
                                   struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Fixture *fixture = check_fixture(prog, args, n_args, 0);
    b2ShapeId shape;
    double x1, y1, x2, y2;
    if (!fixture)
        return -1;
    if (!fixture_shape_id(prog, fixture, &shape))
        return -1;
    if (check_number(prog, args, n_args, 1, &x1) < 0 ||
        check_number(prog, args, n_args, 2, &y1) < 0 ||
        check_number(prog, args, n_args, 3, &x2) < 0 ||
        check_number(prog, args, n_args, 4, &y2) < 0)
        return -1;

    b2Vec2 origin = physics_scaleDownVec((float)x1, (float)y1);
    b2RayCastInput input;
    input.origin = origin;
    input.translation = b2Sub(physics_scaleDownVec((float)x2, (float)y2), origin);
    input.maxFraction = (float)fh_optnumber(args, n_args, 5, 1.0);

    b2CastOutput hit = b2Shape_RayCast(shape, &input);
    if (!hit.hit) {
        *ret = fh_new_null();
        return 0;
    }

    ARRAY_BEGIN(5);
    arr->items[0] = fh_new_number(physics_scaleUp(hit.point.x));
    arr->items[1] = fh_new_number(physics_scaleUp(hit.point.y));
    arr->items[2] = fh_new_number(hit.normal.x);
    arr->items[3] = fh_new_number(hit.normal.y);
    arr->items[4] = fh_new_number(hit.fraction);
    ARRAY_END();
    return 0;
}

/* getBoundingBox() -> [topLeftX, topLeftY, bottomRightX, bottomRightY] */
static int fn_love_fixture_getBoundingBox(struct fh_program *prog,
                                          struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Fixture *fixture = check_fixture(prog, args, n_args, 0);
    b2ShapeId shape;
    if (!fixture)
        return -1;
    if (!fixture_shape_id(prog, fixture, &shape))
        return -1;

    b2AABB aabb = b2Shape_GetAABB(shape);
    ARRAY_BEGIN(4);
    arr->items[0] = fh_new_number(physics_scaleUp(aabb.lowerBound.x));
    arr->items[1] = fh_new_number(physics_scaleUp(aabb.lowerBound.y));
    arr->items[2] = fh_new_number(physics_scaleUp(aabb.upperBound.x));
    arr->items[3] = fh_new_number(physics_scaleUp(aabb.upperBound.y));
    ARRAY_END();
    return 0;
}

/* getMassData() -> [x, y, mass, inertia] */
static int fn_love_fixture_getMassData(struct fh_program *prog,
                                       struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Fixture *fixture = check_fixture(prog, args, n_args, 0);
    b2ShapeId shape;
    if (!fixture)
        return -1;
    if (!fixture_shape_id(prog, fixture, &shape))
        return -1;

    b2MassData data = b2Shape_GetMassData(shape);
    ARRAY_BEGIN(4);
    arr->items[0] = fh_new_number(physics_scaleUp(data.center.x));
    arr->items[1] = fh_new_number(physics_scaleUp(data.center.y));
    arr->items[2] = fh_new_number(data.mass);
    arr->items[3] = fh_new_number(physics_scaleUp(physics_scaleUp(data.rotationalInertia)));
    ARRAY_END();
    return 0;
}

static int fn_love_fixture_getBody(struct fh_program *prog,
                                   struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Fixture *fixture = check_fixture(prog, args, n_args, 0);
    if (!fixture)
        return -1;
    *ret = body_value(prog, fixture->body);
    return 0;
}

static int fn_love_fixture_getShape(struct fh_program *prog,
                                    struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Fixture *fixture = raw_fixture(prog, args, n_args, 0);
    if (!fixture)
        return -1;
    if (!fixture->shape) {
        *ret = fh_new_null();
        return 0;
    }

    physics_Shape_retain(fixture->shape);
    *ret = fh_new_c_obj(prog, fixture->shape, shape_gc, FH_PHYSICS_SHAPE);
    if (fh_is_null(ret)) {
        physics_Shape_release(fixture->shape);
        return fh_set_error(prog, "out of memory");
    }
    return 0;
}

static int fn_love_fixture_getUserData(struct fh_program *prog,
                                       struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Fixture *fixture = raw_fixture(prog, args, n_args, 0);
    if (!fixture)
        return -1;
    *ret = userdata_value(prog, &fixture->userData);
    return 0;
}

static int fn_love_fixture_setUserData(struct fh_program *prog,
                                       struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Fixture *fixture = raw_fixture(prog, args, n_args, 0);
    if (!fixture)
        return -1;
    if (set_userdata(prog, &fixture->userData, args, n_args, 1) < 0)
        return -1;
    *ret = fh_new_null();
    return 0;
}

static int fn_love_fixture_destroy(struct fh_program *prog,
                                   struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Fixture *fixture = raw_fixture(prog, args, n_args, 0);
    if (!fixture)
        return -1;
    physics_Fixture_destroy(fixture);
    *ret = fh_new_null();
    return 0;
}

static int fn_love_fixture_isDestroyed(struct fh_program *prog,
                                       struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Fixture *fixture = raw_fixture(prog, args, n_args, 0);
    if (!fixture)
        return -1;
    *ret = fh_new_bool(!physics_Fixture_isValid(fixture));
    return 0;
}

/* ------------------------------------------------------------------ */
/* Shape                                                               */
/* ------------------------------------------------------------------ */

static int fn_love_shape_getType(struct fh_program *prog,
                                 struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Shape *shape = check_shape(prog, args, n_args, 0);
    if (!shape)
        return -1;

    const char *name;
    switch (shape->type) {
    case physics_ShapeType_circle:  name = "circle";  break;
    case physics_ShapeType_polygon: name = "polygon"; break;
    case physics_ShapeType_edge:    name = "edge";    break;
    default:                        name = "chain";   break;
    }
    *ret = fh_new_string(prog, name);
    return 0;
}

static int fn_love_shape_getRadius(struct fh_program *prog,
                                   struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Shape *shape = check_shape(prog, args, n_args, 0);
    if (!shape)
        return -1;
    *ret = fh_new_number(shape->type == physics_ShapeType_circle
                         ? physics_scaleUp(shape->circle.radius)
                         : 0.0);
    return 0;
}

/* getPoint() -> [x, y], the centre of a circle shape. */
static int fn_love_shape_getPoint(struct fh_program *prog,
                                  struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Shape *shape = check_shape(prog, args, n_args, 0);
    if (!shape)
        return -1;
    if (shape->type != physics_ShapeType_circle)
        return fh_set_error(prog, "Only a circle Shape has a centre point");
    return return_pair(prog, ret,
                       physics_scaleUp(shape->circle.center.x),
                       physics_scaleUp(shape->circle.center.y));
}

/* getPoints() -> [x1, y1, x2, y2, ...] in the shape's own coordinates. */
static int fn_love_shape_getPoints(struct fh_program *prog,
                                   struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Shape *shape = check_shape(prog, args, n_args, 0);
    if (!shape)
        return -1;
    if (shape->type == physics_ShapeType_circle)
        return fh_set_error(prog, "A circle Shape has no points; use love_shape_getPoint");

    if (shape->type == physics_ShapeType_edge) {
        ARRAY_BEGIN(4);
        arr->items[0] = fh_new_number(physics_scaleUp(shape->segment.point1.x));
        arr->items[1] = fh_new_number(physics_scaleUp(shape->segment.point1.y));
        arr->items[2] = fh_new_number(physics_scaleUp(shape->segment.point2.x));
        arr->items[3] = fh_new_number(physics_scaleUp(shape->segment.point2.y));
        ARRAY_END();
        return 0;
    }

    ARRAY_BEGIN(shape->pointCount * 2);
    for (int i = 0; i < shape->pointCount; i++) {
        arr->items[2*i]     = fh_new_number(physics_scaleUp(shape->points[i].x));
        arr->items[2*i + 1] = fh_new_number(physics_scaleUp(shape->points[i].y));
    }
    ARRAY_END();
    return 0;
}

static int fn_love_shape_getChildCount(struct fh_program *prog,
                                       struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Shape *shape = check_shape(prog, args, n_args, 0);
    if (!shape)
        return -1;

    int count = 1;
    if (shape->type == physics_ShapeType_chain)
        count = shape->loop ? shape->pointCount : shape->pointCount - 1;
    *ret = fh_new_number(count);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Joint — common                                                      */
/* ------------------------------------------------------------------ */

static const char *joint_type_to_string(physics_JointType type) {
    switch (type) {
    case physics_JointType_distance:  return "distance";
    case physics_JointType_revolute:  return "revolute";
    case physics_JointType_prismatic: return "prismatic";
    case physics_JointType_mouse:     return "mouse";
    case physics_JointType_weld:      return "weld";
    case physics_JointType_wheel:     return "wheel";
    case physics_JointType_motor:     return "motor";
    case physics_JointType_friction:  return "friction";
    default:                          return "rope";
    }
}

static int fn_love_joint_getType(struct fh_program *prog,
                                 struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Joint *joint = raw_joint(prog, args, n_args, 0);
    if (!joint)
        return -1;
    *ret = fh_new_string(prog, joint_type_to_string(joint->type));
    return 0;
}

/* getBodies() -> [bodyA, bodyB]. A mouse joint pulls against a body CLove
 * creates for it, so its first slot is null. */
static int fn_love_joint_getBodies(struct fh_program *prog,
                                   struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Joint *joint = check_joint(prog, args, n_args, 0);
    if (!joint)
        return -1;

    ARRAY_BEGIN(2);
    arr->items[0] = body_value(prog, joint->bodyA);
    arr->items[1] = body_value(prog, joint->bodyB);
    ARRAY_END();
    return 0;
}

/* getAnchors() -> [x1, y1, x2, y2], the two anchors in world coordinates. */
static int fn_love_joint_getAnchors(struct fh_program *prog,
                                    struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Joint *joint = check_joint(prog, args, n_args, 0);
    if (!joint)
        return -1;

    b2Vec2 a = b2Body_GetWorldPoint(b2Joint_GetBodyA(joint->id), b2Joint_GetLocalAnchorA(joint->id));
    b2Vec2 b = b2Body_GetWorldPoint(b2Joint_GetBodyB(joint->id), b2Joint_GetLocalAnchorB(joint->id));

    ARRAY_BEGIN(4);
    arr->items[0] = fh_new_number(physics_scaleUp(a.x));
    arr->items[1] = fh_new_number(physics_scaleUp(a.y));
    arr->items[2] = fh_new_number(physics_scaleUp(b.x));
    arr->items[3] = fh_new_number(physics_scaleUp(b.y));
    ARRAY_END();
    return 0;
}

/* Box2D 3 reports a force, not the impulse LÖVE divides by dt, so the
 * argument LÖVE takes here is accepted and ignored. */
static int fn_love_joint_getReactionForce(struct fh_program *prog,
                                          struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Joint *joint = check_joint(prog, args, n_args, 0);
    if (!joint)
        return -1;
    b2Vec2 force = b2Joint_GetConstraintForce(joint->id);
    return return_pair(prog, ret, physics_scaleUp(force.x), physics_scaleUp(force.y));
}

static int fn_love_joint_getReactionTorque(struct fh_program *prog,
                                           struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Joint *joint = check_joint(prog, args, n_args, 0);
    if (!joint)
        return -1;
    float torque = b2Joint_GetConstraintTorque(joint->id);
    *ret = fh_new_number(physics_scaleUp(physics_scaleUp(torque)));
    return 0;
}

static int fn_love_joint_getCollideConnected(struct fh_program *prog,
                                             struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Joint *joint = check_joint(prog, args, n_args, 0);
    if (!joint)
        return -1;
    *ret = fh_new_bool(b2Joint_GetCollideConnected(joint->id));
    return 0;
}

static int fn_love_joint_destroy(struct fh_program *prog,
                                 struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Joint *joint = raw_joint(prog, args, n_args, 0);
    if (!joint)
        return -1;
    physics_Joint_destroy(joint);
    *ret = fh_new_null();
    return 0;
}

static int fn_love_joint_isDestroyed(struct fh_program *prog,
                                     struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Joint *joint = raw_joint(prog, args, n_args, 0);
    if (!joint)
        return -1;
    *ret = fh_new_bool(!physics_Joint_isValid(joint));
    return 0;
}

/* ------------------------------------------------------------------ */
/* Joint — per type                                                    */
/* ------------------------------------------------------------------ */

/* `joint` is in scope in expr/stmt; setters also see `value` or `flag`. */
#define DEF_JOINT_GET(prefix, KIND, name, expr)                              \
static int fn_love_##prefix##_##name(struct fh_program *prog, struct fh_value *ret, \
                                     struct fh_value *args, int n_args) {           \
    physics_Joint *joint = check_joint_of(prog, args, n_args, 0, KIND);      \
    if (!joint)                                                                     \
        return -1;                                                                  \
    *ret = (expr);                                                                  \
    return 0;                                                                       \
}

#define DEF_JOINT_SET_NUMBER(prefix, KIND, name, stmt)                       \
static int fn_love_##prefix##_##name(struct fh_program *prog, struct fh_value *ret, \
                                     struct fh_value *args, int n_args) {           \
    physics_Joint *joint = check_joint_of(prog, args, n_args, 0, KIND);      \
    double value;                                                                   \
    if (!joint)                                                                     \
        return -1;                                                                  \
    if (check_number(prog, args, n_args, 1, &value) < 0)                             \
        return -1;                                                                  \
    stmt;                                                                           \
    *ret = fh_new_null();                                                           \
    return 0;                                                                       \
}

#define DEF_JOINT_SET_BOOL(prefix, KIND, name, stmt)                         \
static int fn_love_##prefix##_##name(struct fh_program *prog, struct fh_value *ret, \
                                     struct fh_value *args, int n_args) {           \
    physics_Joint *joint = check_joint_of(prog, args, n_args, 0, KIND);      \
    if (!joint)                                                                     \
        return -1;                                                                  \
    if (n_args < 2 || !fh_is_bool(&args[1]))                                        \
        return type_error(prog, 1, "boolean");                                      \
    bool flag = fh_get_bool(&args[1]);                                              \
    stmt;                                                                           \
    *ret = fh_new_null();                                                           \
    return 0;                                                                       \
}

#define DEF_JOINT_LIMITS(prefix, KIND, getLower, getUpper, setLimits, toBox2D, fromBox2D) \
static int fn_love_##prefix##_setLimits(struct fh_program *prog, struct fh_value *ret, \
                                        struct fh_value *args, int n_args) {        \
    physics_Joint *joint = check_joint_of(prog, args, n_args, 0, KIND);      \
    double lower, upper;                                                            \
    if (!joint)                                                                     \
        return -1;                                                                  \
    if (check_number(prog, args, n_args, 1, &lower) < 0 ||                           \
        check_number(prog, args, n_args, 2, &upper) < 0)                             \
        return -1;                                                                  \
    setLimits(joint->id, toBox2D((float)lower), toBox2D((float)upper));                 \
    *ret = fh_new_null();                                                           \
    return 0;                                                                       \
}                                                                                   \
static int fn_love_##prefix##_getLimits(struct fh_program *prog, struct fh_value *ret, \
                                        struct fh_value *args, int n_args) {        \
    physics_Joint *joint = check_joint_of(prog, args, n_args, 0, KIND);      \
    if (!joint)                                                                     \
        return -1;                                                                  \
    return return_pair(prog, ret, fromBox2D(getLower(joint->id)),                  \
                       fromBox2D(getUpper(joint->id)));                              \
}

/* The identity "scale" for angular limits, so the macro above can serve both. */
static float physics_identity(float value) { return value; }

/* --- distance joint --- */
#define JT_DISTANCE physics_JointType_distance
DEF_JOINT_GET(distancejoint, JT_DISTANCE, getLength,
              fh_new_number(physics_scaleUp(b2DistanceJoint_GetLength(joint->id))))
DEF_JOINT_SET_NUMBER(distancejoint, JT_DISTANCE, setLength,
                     b2DistanceJoint_SetLength(joint->id, physics_scaleDown((float)value)))
DEF_JOINT_GET(distancejoint, JT_DISTANCE, getFrequency,
              fh_new_number(b2DistanceJoint_IsSpringEnabled(joint->id)
                            ? b2DistanceJoint_GetSpringHertz(joint->id) : 0.0f))
/* LÖVE treats frequency 0 as "rigid", which is Box2D 3's spring turned off. */
DEF_JOINT_SET_NUMBER(distancejoint, JT_DISTANCE, setFrequency,
                     (value > 0.0
                      ? (b2DistanceJoint_EnableSpring(joint->id, true),
                         b2DistanceJoint_SetSpringHertz(joint->id, (float)value))
                      : b2DistanceJoint_EnableSpring(joint->id, false)))
DEF_JOINT_GET(distancejoint, JT_DISTANCE, getDampingRatio,
              fh_new_number(b2DistanceJoint_GetSpringDampingRatio(joint->id)))
DEF_JOINT_SET_NUMBER(distancejoint, JT_DISTANCE, setDampingRatio,
                     b2DistanceJoint_SetSpringDampingRatio(joint->id, (float)value))

/* --- rope joint --- */
#define JT_ROPE physics_JointType_rope
DEF_JOINT_GET(ropejoint, JT_ROPE, getMaxLength,
              fh_new_number(physics_scaleUp(b2DistanceJoint_GetMaxLength(joint->id))))
DEF_JOINT_SET_NUMBER(ropejoint, JT_ROPE, setMaxLength,
                     b2DistanceJoint_SetLengthRange(joint->id, 0.0f,
                                                    physics_scaleDown((float)value)))

/* --- revolute joint --- */
#define JT_REVOLUTE physics_JointType_revolute
DEF_JOINT_GET(revolutejoint, JT_REVOLUTE, getJointAngle,
              fh_new_number(b2RevoluteJoint_GetAngle(joint->id)))
DEF_JOINT_GET(revolutejoint, JT_REVOLUTE, getJointSpeed,
              fh_new_number(b2Body_GetAngularVelocity(b2Joint_GetBodyB(joint->id)) -
                            b2Body_GetAngularVelocity(b2Joint_GetBodyA(joint->id))))
DEF_JOINT_GET(revolutejoint, JT_REVOLUTE, isMotorEnabled,
              fh_new_bool(b2RevoluteJoint_IsMotorEnabled(joint->id)))
DEF_JOINT_SET_BOOL(revolutejoint, JT_REVOLUTE, setMotorEnabled,
                   b2RevoluteJoint_EnableMotor(joint->id, flag))
DEF_JOINT_GET(revolutejoint, JT_REVOLUTE, getMotorSpeed,
              fh_new_number(b2RevoluteJoint_GetMotorSpeed(joint->id)))
DEF_JOINT_SET_NUMBER(revolutejoint, JT_REVOLUTE, setMotorSpeed,
                     b2RevoluteJoint_SetMotorSpeed(joint->id, (float)value))
DEF_JOINT_GET(revolutejoint, JT_REVOLUTE, getMaxMotorTorque,
              fh_new_number(physics_scaleUp(physics_scaleUp(
                  b2RevoluteJoint_GetMaxMotorTorque(joint->id)))))
DEF_JOINT_SET_NUMBER(revolutejoint, JT_REVOLUTE, setMaxMotorTorque,
                     b2RevoluteJoint_SetMaxMotorTorque(joint->id,
                         physics_scaleDown(physics_scaleDown((float)value))))
DEF_JOINT_GET(revolutejoint, JT_REVOLUTE, getMotorTorque,
              fh_new_number(physics_scaleUp(physics_scaleUp(
                  b2RevoluteJoint_GetMotorTorque(joint->id)))))
DEF_JOINT_GET(revolutejoint, JT_REVOLUTE, areLimitsEnabled,
              fh_new_bool(b2RevoluteJoint_IsLimitEnabled(joint->id)))
DEF_JOINT_SET_BOOL(revolutejoint, JT_REVOLUTE, setLimitsEnabled,
                   b2RevoluteJoint_EnableLimit(joint->id, flag))
DEF_JOINT_GET(revolutejoint, JT_REVOLUTE, getLowerLimit,
              fh_new_number(b2RevoluteJoint_GetLowerLimit(joint->id)))
DEF_JOINT_GET(revolutejoint, JT_REVOLUTE, getUpperLimit,
              fh_new_number(b2RevoluteJoint_GetUpperLimit(joint->id)))
DEF_JOINT_SET_NUMBER(revolutejoint, JT_REVOLUTE, setLowerLimit,
                     b2RevoluteJoint_SetLimits(joint->id, (float)value,
                                               b2RevoluteJoint_GetUpperLimit(joint->id)))
DEF_JOINT_SET_NUMBER(revolutejoint, JT_REVOLUTE, setUpperLimit,
                     b2RevoluteJoint_SetLimits(joint->id,
                                               b2RevoluteJoint_GetLowerLimit(joint->id),
                                               (float)value))
DEF_JOINT_LIMITS(revolutejoint, JT_REVOLUTE, b2RevoluteJoint_GetLowerLimit,
                 b2RevoluteJoint_GetUpperLimit, b2RevoluteJoint_SetLimits,
                 physics_identity, physics_identity)

/* --- prismatic joint --- */
#define JT_PRISMATIC physics_JointType_prismatic
DEF_JOINT_GET(prismaticjoint, JT_PRISMATIC, getJointTranslation,
              fh_new_number(physics_scaleUp(b2PrismaticJoint_GetTranslation(joint->id))))
DEF_JOINT_GET(prismaticjoint, JT_PRISMATIC, getJointSpeed,
              fh_new_number(physics_scaleUp(b2PrismaticJoint_GetSpeed(joint->id))))
DEF_JOINT_GET(prismaticjoint, JT_PRISMATIC, isMotorEnabled,
              fh_new_bool(b2PrismaticJoint_IsMotorEnabled(joint->id)))
DEF_JOINT_SET_BOOL(prismaticjoint, JT_PRISMATIC, setMotorEnabled,
                   b2PrismaticJoint_EnableMotor(joint->id, flag))
DEF_JOINT_GET(prismaticjoint, JT_PRISMATIC, getMotorSpeed,
              fh_new_number(physics_scaleUp(b2PrismaticJoint_GetMotorSpeed(joint->id))))
DEF_JOINT_SET_NUMBER(prismaticjoint, JT_PRISMATIC, setMotorSpeed,
                     b2PrismaticJoint_SetMotorSpeed(joint->id, physics_scaleDown((float)value)))
DEF_JOINT_GET(prismaticjoint, JT_PRISMATIC, getMaxMotorForce,
              fh_new_number(physics_scaleUp(b2PrismaticJoint_GetMaxMotorForce(joint->id))))
DEF_JOINT_SET_NUMBER(prismaticjoint, JT_PRISMATIC, setMaxMotorForce,
                     b2PrismaticJoint_SetMaxMotorForce(joint->id, physics_scaleDown((float)value)))
DEF_JOINT_GET(prismaticjoint, JT_PRISMATIC, getMotorForce,
              fh_new_number(physics_scaleUp(b2PrismaticJoint_GetMotorForce(joint->id))))
DEF_JOINT_GET(prismaticjoint, JT_PRISMATIC, areLimitsEnabled,
              fh_new_bool(b2PrismaticJoint_IsLimitEnabled(joint->id)))
DEF_JOINT_SET_BOOL(prismaticjoint, JT_PRISMATIC, setLimitsEnabled,
                   b2PrismaticJoint_EnableLimit(joint->id, flag))
DEF_JOINT_GET(prismaticjoint, JT_PRISMATIC, getLowerLimit,
              fh_new_number(physics_scaleUp(b2PrismaticJoint_GetLowerLimit(joint->id))))
DEF_JOINT_GET(prismaticjoint, JT_PRISMATIC, getUpperLimit,
              fh_new_number(physics_scaleUp(b2PrismaticJoint_GetUpperLimit(joint->id))))
DEF_JOINT_LIMITS(prismaticjoint, JT_PRISMATIC, b2PrismaticJoint_GetLowerLimit,
                 b2PrismaticJoint_GetUpperLimit, b2PrismaticJoint_SetLimits,
                 physics_scaleDown, physics_scaleUp)

/* --- wheel joint --- */
#define JT_WHEEL physics_JointType_wheel
DEF_JOINT_GET(wheeljoint, JT_WHEEL, getSpringFrequency,
              fh_new_number(b2WheelJoint_IsSpringEnabled(joint->id)
                            ? b2WheelJoint_GetSpringHertz(joint->id) : 0.0f))
DEF_JOINT_SET_NUMBER(wheeljoint, JT_WHEEL, setSpringFrequency,
                     (value > 0.0
                      ? (b2WheelJoint_EnableSpring(joint->id, true),
                         b2WheelJoint_SetSpringHertz(joint->id, (float)value))
                      : b2WheelJoint_EnableSpring(joint->id, false)))
DEF_JOINT_GET(wheeljoint, JT_WHEEL, getSpringDampingRatio,
              fh_new_number(b2WheelJoint_GetSpringDampingRatio(joint->id)))
DEF_JOINT_SET_NUMBER(wheeljoint, JT_WHEEL, setSpringDampingRatio,
                     b2WheelJoint_SetSpringDampingRatio(joint->id, (float)value))
DEF_JOINT_GET(wheeljoint, JT_WHEEL, isMotorEnabled,
              fh_new_bool(b2WheelJoint_IsMotorEnabled(joint->id)))
DEF_JOINT_SET_BOOL(wheeljoint, JT_WHEEL, setMotorEnabled,
                   b2WheelJoint_EnableMotor(joint->id, flag))
DEF_JOINT_GET(wheeljoint, JT_WHEEL, getMotorSpeed,
              fh_new_number(b2WheelJoint_GetMotorSpeed(joint->id)))
DEF_JOINT_SET_NUMBER(wheeljoint, JT_WHEEL, setMotorSpeed,
                     b2WheelJoint_SetMotorSpeed(joint->id, (float)value))
DEF_JOINT_GET(wheeljoint, JT_WHEEL, getMaxMotorTorque,
              fh_new_number(physics_scaleUp(physics_scaleUp(
                  b2WheelJoint_GetMaxMotorTorque(joint->id)))))
DEF_JOINT_SET_NUMBER(wheeljoint, JT_WHEEL, setMaxMotorTorque,
                     b2WheelJoint_SetMaxMotorTorque(joint->id,
                         physics_scaleDown(physics_scaleDown((float)value))))
DEF_JOINT_GET(wheeljoint, JT_WHEEL, getMotorTorque,
              fh_new_number(physics_scaleUp(physics_scaleUp(
                  b2WheelJoint_GetMotorTorque(joint->id)))))

/* --- weld joint --- */
#define JT_WELD physics_JointType_weld
DEF_JOINT_GET(weldjoint, JT_WELD, getFrequency,
              fh_new_number(b2WeldJoint_GetAngularHertz(joint->id)))
DEF_JOINT_SET_NUMBER(weldjoint, JT_WELD, setFrequency,
                     b2WeldJoint_SetAngularHertz(joint->id, (float)value))
DEF_JOINT_GET(weldjoint, JT_WELD, getDampingRatio,
              fh_new_number(b2WeldJoint_GetAngularDampingRatio(joint->id)))
DEF_JOINT_SET_NUMBER(weldjoint, JT_WELD, setDampingRatio,
                     b2WeldJoint_SetAngularDampingRatio(joint->id, (float)value))
DEF_JOINT_GET(weldjoint, JT_WELD, getLinearFrequency,
              fh_new_number(b2WeldJoint_GetLinearHertz(joint->id)))
DEF_JOINT_SET_NUMBER(weldjoint, JT_WELD, setLinearFrequency,
                     b2WeldJoint_SetLinearHertz(joint->id, (float)value))
DEF_JOINT_GET(weldjoint, JT_WELD, getLinearDampingRatio,
              fh_new_number(b2WeldJoint_GetLinearDampingRatio(joint->id)))
DEF_JOINT_SET_NUMBER(weldjoint, JT_WELD, setLinearDampingRatio,
                     b2WeldJoint_SetLinearDampingRatio(joint->id, (float)value))

/* --- mouse joint --- */
#define JT_MOUSE physics_JointType_mouse
DEF_JOINT_GET(mousejoint, JT_MOUSE, getMaxForce,
              fh_new_number(physics_scaleUp(b2MouseJoint_GetMaxForce(joint->id))))
DEF_JOINT_SET_NUMBER(mousejoint, JT_MOUSE, setMaxForce,
                     b2MouseJoint_SetMaxForce(joint->id, physics_scaleDown((float)value)))
DEF_JOINT_GET(mousejoint, JT_MOUSE, getFrequency,
              fh_new_number(b2MouseJoint_GetSpringHertz(joint->id)))
DEF_JOINT_SET_NUMBER(mousejoint, JT_MOUSE, setFrequency,
                     b2MouseJoint_SetSpringHertz(joint->id, (float)value))
DEF_JOINT_GET(mousejoint, JT_MOUSE, getDampingRatio,
              fh_new_number(b2MouseJoint_GetSpringDampingRatio(joint->id)))
DEF_JOINT_SET_NUMBER(mousejoint, JT_MOUSE, setDampingRatio,
                     b2MouseJoint_SetSpringDampingRatio(joint->id, (float)value))

static int fn_love_mousejoint_setTarget(struct fh_program *prog,
                                        struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Joint *joint = check_joint_of(prog, args, n_args, 0, JT_MOUSE);
    double x, y;
    if (!joint)
        return -1;
    if (check_number(prog, args, n_args, 1, &x) < 0 ||
        check_number(prog, args, n_args, 2, &y) < 0)
        return -1;

    b2MouseJoint_SetTarget(joint->id, physics_scaleDownVec((float)x, (float)y));
    b2Joint_WakeBodies(joint->id);
    *ret = fh_new_null();
    return 0;
}

static int fn_love_mousejoint_getTarget(struct fh_program *prog,
                                        struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Joint *joint = check_joint_of(prog, args, n_args, 0, JT_MOUSE);
    if (!joint)
        return -1;
    b2Vec2 target = b2MouseJoint_GetTarget(joint->id);
    return return_pair(prog, ret, physics_scaleUp(target.x), physics_scaleUp(target.y));
}

/* --- motor joint --- */
#define JT_MOTOR physics_JointType_motor
DEF_JOINT_GET(motorjoint, JT_MOTOR, getAngularOffset,
              fh_new_number(b2MotorJoint_GetAngularOffset(joint->id)))
DEF_JOINT_SET_NUMBER(motorjoint, JT_MOTOR, setAngularOffset,
                     b2MotorJoint_SetAngularOffset(joint->id, (float)value))
DEF_JOINT_GET(motorjoint, JT_MOTOR, getMaxForce,
              fh_new_number(physics_scaleUp(b2MotorJoint_GetMaxForce(joint->id))))
DEF_JOINT_SET_NUMBER(motorjoint, JT_MOTOR, setMaxForce,
                     b2MotorJoint_SetMaxForce(joint->id, physics_scaleDown((float)value)))
DEF_JOINT_GET(motorjoint, JT_MOTOR, getMaxTorque,
              fh_new_number(physics_scaleUp(physics_scaleUp(
                  b2MotorJoint_GetMaxTorque(joint->id)))))
DEF_JOINT_SET_NUMBER(motorjoint, JT_MOTOR, setMaxTorque,
                     b2MotorJoint_SetMaxTorque(joint->id,
                         physics_scaleDown(physics_scaleDown((float)value))))
DEF_JOINT_GET(motorjoint, JT_MOTOR, getCorrectionFactor,
              fh_new_number(b2MotorJoint_GetCorrectionFactor(joint->id)))
DEF_JOINT_SET_NUMBER(motorjoint, JT_MOTOR, setCorrectionFactor,
                     b2MotorJoint_SetCorrectionFactor(joint->id, (float)value))

static int fn_love_motorjoint_setLinearOffset(struct fh_program *prog,
                                              struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Joint *joint = check_joint_of(prog, args, n_args, 0, JT_MOTOR);
    double x, y;
    if (!joint)
        return -1;
    if (check_number(prog, args, n_args, 1, &x) < 0 ||
        check_number(prog, args, n_args, 2, &y) < 0)
        return -1;

    b2MotorJoint_SetLinearOffset(joint->id, physics_scaleDownVec((float)x, (float)y));
    *ret = fh_new_null();
    return 0;
}

static int fn_love_motorjoint_getLinearOffset(struct fh_program *prog,
                                              struct fh_value *ret, struct fh_value *args, int n_args) {
    physics_Joint *joint = check_joint_of(prog, args, n_args, 0, JT_MOTOR);
    if (!joint)
        return -1;
    b2Vec2 offset = b2MotorJoint_GetLinearOffset(joint->id);
    return return_pair(prog, ret, physics_scaleUp(offset.x), physics_scaleUp(offset.y));
}

/* --- friction joint (a motor joint with no positional correction) --- */
#define JT_FRICTION physics_JointType_friction
DEF_JOINT_GET(frictionjoint, JT_FRICTION, getMaxForce,
              fh_new_number(physics_scaleUp(b2MotorJoint_GetMaxForce(joint->id))))
DEF_JOINT_SET_NUMBER(frictionjoint, JT_FRICTION, setMaxForce,
                     b2MotorJoint_SetMaxForce(joint->id, physics_scaleDown((float)value)))
DEF_JOINT_GET(frictionjoint, JT_FRICTION, getMaxTorque,
              fh_new_number(physics_scaleUp(physics_scaleUp(
                  b2MotorJoint_GetMaxTorque(joint->id)))))
DEF_JOINT_SET_NUMBER(frictionjoint, JT_FRICTION, setMaxTorque,
                     b2MotorJoint_SetMaxTorque(joint->id,
                         physics_scaleDown(physics_scaleDown((float)value))))

/* ------------------------------------------------------------------ */
/* Contact                                                             */
/* ------------------------------------------------------------------ */

static int fn_love_contact_getNormal(struct fh_program *prog,
                                     struct fh_value *ret, struct fh_value *args, int n_args) {
    PhysicsContact *contact = check_contact(prog, args, n_args, 0);
    if (!contact)
        return -1;
    return return_pair(prog, ret, contact->manifold.normal.x, contact->manifold.normal.y);
}

/* getPositions() -> [x1, y1] or [x1, y1, x2, y2] for a two-point manifold. */
static int fn_love_contact_getPositions(struct fh_program *prog,
                                        struct fh_value *ret, struct fh_value *args, int n_args) {
    PhysicsContact *contact = check_contact(prog, args, n_args, 0);
    if (!contact)
        return -1;

    int count = contact->manifold.pointCount;
    ARRAY_BEGIN(count * 2);
    for (int i = 0; i < count; i++) {
        arr->items[2*i]     = fh_new_number(physics_scaleUp(contact->manifold.points[i].point.x));
        arr->items[2*i + 1] = fh_new_number(physics_scaleUp(contact->manifold.points[i].point.y));
    }
    ARRAY_END();
    return 0;
}

static int fn_love_contact_isTouching(struct fh_program *prog,
                                      struct fh_value *ret, struct fh_value *args, int n_args) {
    PhysicsContact *contact = check_contact(prog, args, n_args, 0);
    if (!contact)
        return -1;
    *ret = fh_new_bool(contact->touching);
    return 0;
}

static int fn_love_contact_getFixtures(struct fh_program *prog,
                                       struct fh_value *ret, struct fh_value *args, int n_args) {
    PhysicsContact *contact = check_contact(prog, args, n_args, 0);
    if (!contact)
        return -1;

    ARRAY_BEGIN(2);
    arr->items[0] = fixture_value(prog, contact->a);
    arr->items[1] = fixture_value(prog, contact->b);
    ARRAY_END();
    return 0;
}

/* Box2D mixes friction as sqrt(a*b) and restitution as max(a, b); the contact
 * reports the mixed value the solver actually used. */
static int fn_love_contact_getFriction(struct fh_program *prog,
                                       struct fh_value *ret, struct fh_value *args, int n_args) {
    PhysicsContact *contact = check_contact(prog, args, n_args, 0);
    if (!contact)
        return -1;
    if (!physics_Fixture_isValid(contact->a) || !physics_Fixture_isValid(contact->b))
        return fh_set_error(prog, "This Contact refers to a destroyed Fixture");

    float a = b2Shape_GetFriction(contact->a->shapeId);
    float b = b2Shape_GetFriction(contact->b->shapeId);
    *ret = fh_new_number(sqrtf(a * b));
    return 0;
}

static int fn_love_contact_getRestitution(struct fh_program *prog,
                                          struct fh_value *ret, struct fh_value *args, int n_args) {
    PhysicsContact *contact = check_contact(prog, args, n_args, 0);
    if (!contact)
        return -1;
    if (!physics_Fixture_isValid(contact->a) || !physics_Fixture_isValid(contact->b))
        return fh_set_error(prog, "This Contact refers to a destroyed Fixture");

    float a = b2Shape_GetRestitution(contact->a->shapeId);
    float b = b2Shape_GetRestitution(contact->b->shapeId);
    *ret = fh_new_number(a > b ? a : b);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

#define DEF_FN(name) { #name, fn_##name }
static const struct fh_named_c_func c_funcs[] = {
    /* love.physics module */
    DEF_FN(love_physics_getMeter),
    DEF_FN(love_physics_newBody),
    DEF_FN(love_physics_newChainShape),
    DEF_FN(love_physics_newCircleShape),
    DEF_FN(love_physics_newDistanceJoint),
    DEF_FN(love_physics_newEdgeShape),
    DEF_FN(love_physics_newFixture),
    DEF_FN(love_physics_newFrictionJoint),
    DEF_FN(love_physics_newMotorJoint),
    DEF_FN(love_physics_newMouseJoint),
    DEF_FN(love_physics_newPolygonShape),
    DEF_FN(love_physics_newPrismaticJoint),
    DEF_FN(love_physics_newRectangleShape),
    DEF_FN(love_physics_newRevoluteJoint),
    DEF_FN(love_physics_newRopeJoint),
    DEF_FN(love_physics_newWeldJoint),
    DEF_FN(love_physics_newWheelJoint),
    DEF_FN(love_physics_newWorld),
    DEF_FN(love_physics_setMeter),
    /* World */
    DEF_FN(love_world_destroy),
    DEF_FN(love_world_getBodies),
    DEF_FN(love_world_getBodyCount),
    DEF_FN(love_world_getCallbacks),
    DEF_FN(love_world_getGravity),
    DEF_FN(love_world_getJointCount),
    DEF_FN(love_world_getJoints),
    DEF_FN(love_world_isDestroyed),
    DEF_FN(love_world_isSleepingAllowed),
    DEF_FN(love_world_queryBoundingBox),
    DEF_FN(love_world_rayCast),
    DEF_FN(love_world_rayCastClosest),
    DEF_FN(love_world_setCallbacks),
    DEF_FN(love_world_setGravity),
    DEF_FN(love_world_setSleepingAllowed),
    DEF_FN(love_world_update),
    /* Body */
    DEF_FN(love_body_applyAngularImpulse),
    DEF_FN(love_body_applyForce),
    DEF_FN(love_body_applyLinearImpulse),
    DEF_FN(love_body_applyTorque),
    DEF_FN(love_body_destroy),
    DEF_FN(love_body_getAngle),
    DEF_FN(love_body_getAngularDamping),
    DEF_FN(love_body_getAngularVelocity),
    DEF_FN(love_body_getFixtures),
    DEF_FN(love_body_getGravityScale),
    DEF_FN(love_body_getInertia),
    DEF_FN(love_body_getJoints),
    DEF_FN(love_body_getLinearDamping),
    DEF_FN(love_body_getLinearVelocity),
    DEF_FN(love_body_getLinearVelocityFromLocalPoint),
    DEF_FN(love_body_getLinearVelocityFromWorldPoint),
    DEF_FN(love_body_getLocalCenter),
    DEF_FN(love_body_getLocalPoint),
    DEF_FN(love_body_getLocalVector),
    DEF_FN(love_body_getMass),
    DEF_FN(love_body_getMassData),
    DEF_FN(love_body_getPosition),
    DEF_FN(love_body_getType),
    DEF_FN(love_body_getUserData),
    DEF_FN(love_body_getWorld),
    DEF_FN(love_body_getWorldCenter),
    DEF_FN(love_body_getWorldPoint),
    DEF_FN(love_body_getWorldPoints),
    DEF_FN(love_body_getWorldVector),
    DEF_FN(love_body_getX),
    DEF_FN(love_body_getY),
    DEF_FN(love_body_isActive),
    DEF_FN(love_body_isAwake),
    DEF_FN(love_body_isBullet),
    DEF_FN(love_body_isDestroyed),
    DEF_FN(love_body_isFixedRotation),
    DEF_FN(love_body_isSleepingAllowed),
    DEF_FN(love_body_resetMassData),
    DEF_FN(love_body_setActive),
    DEF_FN(love_body_setAngle),
    DEF_FN(love_body_setAngularDamping),
    DEF_FN(love_body_setAngularVelocity),
    DEF_FN(love_body_setAwake),
    DEF_FN(love_body_setBullet),
    DEF_FN(love_body_setFixedRotation),
    DEF_FN(love_body_setGravityScale),
    DEF_FN(love_body_setLinearDamping),
    DEF_FN(love_body_setLinearVelocity),
    DEF_FN(love_body_setPosition),
    DEF_FN(love_body_setSleepingAllowed),
    DEF_FN(love_body_setTransform),
    DEF_FN(love_body_setType),
    DEF_FN(love_body_setUserData),
    DEF_FN(love_body_setX),
    DEF_FN(love_body_setY),
    /* Fixture */
    DEF_FN(love_fixture_destroy),
    DEF_FN(love_fixture_getBody),
    DEF_FN(love_fixture_getBoundingBox),
    DEF_FN(love_fixture_getCategory),
    DEF_FN(love_fixture_getDensity),
    DEF_FN(love_fixture_getFilterData),
    DEF_FN(love_fixture_getFriction),
    DEF_FN(love_fixture_getGroupIndex),
    DEF_FN(love_fixture_getMask),
    DEF_FN(love_fixture_getMassData),
    DEF_FN(love_fixture_getRestitution),
    DEF_FN(love_fixture_getShape),
    DEF_FN(love_fixture_getUserData),
    DEF_FN(love_fixture_isDestroyed),
    DEF_FN(love_fixture_isSensor),
    DEF_FN(love_fixture_rayCast),
    DEF_FN(love_fixture_setCategory),
    DEF_FN(love_fixture_setDensity),
    DEF_FN(love_fixture_setFilterData),
    DEF_FN(love_fixture_setFriction),
    DEF_FN(love_fixture_setGroupIndex),
    DEF_FN(love_fixture_setMask),
    DEF_FN(love_fixture_setRestitution),
    DEF_FN(love_fixture_setSensor),
    DEF_FN(love_fixture_setUserData),
    DEF_FN(love_fixture_testPoint),
    /* Shape */
    DEF_FN(love_shape_getChildCount),
    DEF_FN(love_shape_getPoint),
    DEF_FN(love_shape_getPoints),
    DEF_FN(love_shape_getRadius),
    DEF_FN(love_shape_getType),
    /* Joint (common) */
    DEF_FN(love_joint_destroy),
    DEF_FN(love_joint_getAnchors),
    DEF_FN(love_joint_getBodies),
    DEF_FN(love_joint_getCollideConnected),
    DEF_FN(love_joint_getReactionForce),
    DEF_FN(love_joint_getReactionTorque),
    DEF_FN(love_joint_getType),
    DEF_FN(love_joint_isDestroyed),
    /* DistanceJoint */
    DEF_FN(love_distancejoint_getDampingRatio),
    DEF_FN(love_distancejoint_getFrequency),
    DEF_FN(love_distancejoint_getLength),
    DEF_FN(love_distancejoint_setDampingRatio),
    DEF_FN(love_distancejoint_setFrequency),
    DEF_FN(love_distancejoint_setLength),
    /* RopeJoint */
    DEF_FN(love_ropejoint_getMaxLength),
    DEF_FN(love_ropejoint_setMaxLength),
    /* RevoluteJoint */
    DEF_FN(love_revolutejoint_areLimitsEnabled),
    DEF_FN(love_revolutejoint_getJointAngle),
    DEF_FN(love_revolutejoint_getJointSpeed),
    DEF_FN(love_revolutejoint_getLimits),
    DEF_FN(love_revolutejoint_getLowerLimit),
    DEF_FN(love_revolutejoint_getMaxMotorTorque),
    DEF_FN(love_revolutejoint_getMotorSpeed),
    DEF_FN(love_revolutejoint_getMotorTorque),
    DEF_FN(love_revolutejoint_getUpperLimit),
    DEF_FN(love_revolutejoint_isMotorEnabled),
    DEF_FN(love_revolutejoint_setLimits),
    DEF_FN(love_revolutejoint_setLimitsEnabled),
    DEF_FN(love_revolutejoint_setLowerLimit),
    DEF_FN(love_revolutejoint_setMaxMotorTorque),
    DEF_FN(love_revolutejoint_setMotorEnabled),
    DEF_FN(love_revolutejoint_setMotorSpeed),
    DEF_FN(love_revolutejoint_setUpperLimit),
    /* PrismaticJoint */
    DEF_FN(love_prismaticjoint_areLimitsEnabled),
    DEF_FN(love_prismaticjoint_getJointSpeed),
    DEF_FN(love_prismaticjoint_getJointTranslation),
    DEF_FN(love_prismaticjoint_getLimits),
    DEF_FN(love_prismaticjoint_getLowerLimit),
    DEF_FN(love_prismaticjoint_getMaxMotorForce),
    DEF_FN(love_prismaticjoint_getMotorForce),
    DEF_FN(love_prismaticjoint_getMotorSpeed),
    DEF_FN(love_prismaticjoint_getUpperLimit),
    DEF_FN(love_prismaticjoint_isMotorEnabled),
    DEF_FN(love_prismaticjoint_setLimits),
    DEF_FN(love_prismaticjoint_setLimitsEnabled),
    DEF_FN(love_prismaticjoint_setMaxMotorForce),
    DEF_FN(love_prismaticjoint_setMotorEnabled),
    DEF_FN(love_prismaticjoint_setMotorSpeed),
    /* WheelJoint */
    DEF_FN(love_wheeljoint_getMaxMotorTorque),
    DEF_FN(love_wheeljoint_getMotorSpeed),
    DEF_FN(love_wheeljoint_getMotorTorque),
    DEF_FN(love_wheeljoint_getSpringDampingRatio),
    DEF_FN(love_wheeljoint_getSpringFrequency),
    DEF_FN(love_wheeljoint_isMotorEnabled),
    DEF_FN(love_wheeljoint_setMaxMotorTorque),
    DEF_FN(love_wheeljoint_setMotorEnabled),
    DEF_FN(love_wheeljoint_setMotorSpeed),
    DEF_FN(love_wheeljoint_setSpringDampingRatio),
    DEF_FN(love_wheeljoint_setSpringFrequency),
    /* WeldJoint */
    DEF_FN(love_weldjoint_getDampingRatio),
    DEF_FN(love_weldjoint_getFrequency),
    DEF_FN(love_weldjoint_getLinearDampingRatio),
    DEF_FN(love_weldjoint_getLinearFrequency),
    DEF_FN(love_weldjoint_setDampingRatio),
    DEF_FN(love_weldjoint_setFrequency),
    DEF_FN(love_weldjoint_setLinearDampingRatio),
    DEF_FN(love_weldjoint_setLinearFrequency),
    /* MouseJoint */
    DEF_FN(love_mousejoint_getDampingRatio),
    DEF_FN(love_mousejoint_getFrequency),
    DEF_FN(love_mousejoint_getMaxForce),
    DEF_FN(love_mousejoint_getTarget),
    DEF_FN(love_mousejoint_setDampingRatio),
    DEF_FN(love_mousejoint_setFrequency),
    DEF_FN(love_mousejoint_setMaxForce),
    DEF_FN(love_mousejoint_setTarget),
    /* MotorJoint */
    DEF_FN(love_motorjoint_getAngularOffset),
    DEF_FN(love_motorjoint_getCorrectionFactor),
    DEF_FN(love_motorjoint_getLinearOffset),
    DEF_FN(love_motorjoint_getMaxForce),
    DEF_FN(love_motorjoint_getMaxTorque),
    DEF_FN(love_motorjoint_setAngularOffset),
    DEF_FN(love_motorjoint_setCorrectionFactor),
    DEF_FN(love_motorjoint_setLinearOffset),
    DEF_FN(love_motorjoint_setMaxForce),
    DEF_FN(love_motorjoint_setMaxTorque),
    /* FrictionJoint */
    DEF_FN(love_frictionjoint_getMaxForce),
    DEF_FN(love_frictionjoint_getMaxTorque),
    DEF_FN(love_frictionjoint_setMaxForce),
    DEF_FN(love_frictionjoint_setMaxTorque),
    /* Contact */
    DEF_FN(love_contact_getFixtures),
    DEF_FN(love_contact_getFriction),
    DEF_FN(love_contact_getNormal),
    DEF_FN(love_contact_getPositions),
    DEF_FN(love_contact_getRestitution),
    DEF_FN(love_contact_isTouching)
};

void fh_physics_register(struct fh_program *prog) {
    fh_add_c_funcs(prog, c_funcs, sizeof(c_funcs)/sizeof(c_funcs[0]));
}
