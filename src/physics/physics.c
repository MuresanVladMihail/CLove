/*
#   clove
#
#   Copyright (C) 2016-2025 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/

#include "../include/physics.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ------------------------------------------------------------------ */
/* Pixels <-> meters                                                   */
/* ------------------------------------------------------------------ */

/* LÖVE's default: 30 pixels to the meter. */
static float moduleMeter = 30.0f;

void physics_setMeter(float meter) {
    if (meter > 0.0f)
        moduleMeter = meter;
}

float physics_getMeter(void) {
    return moduleMeter;
}

float physics_scaleDown(float pixels) {
    return pixels / moduleMeter;
}

float physics_scaleUp(float meters) {
    return meters * moduleMeter;
}

b2Vec2 physics_scaleDownVec(float x, float y) {
    b2Vec2 v = { x / moduleMeter, y / moduleMeter };
    return v;
}

/* ------------------------------------------------------------------ */
/* User data                                                           */
/* ------------------------------------------------------------------ */

void physics_UserData_clear(physics_UserData *ud) {
    free(ud->string);
    ud->string = NULL;
    ud->number = 0.0;
    ud->type = physics_UserData_none;
}

void physics_UserData_setNumber(physics_UserData *ud, double value) {
    physics_UserData_clear(ud);
    ud->type = physics_UserData_number;
    ud->number = value;
}

bool physics_UserData_setString(physics_UserData *ud, const char *value) {
    size_t len = strlen(value);
    char *copy = malloc(len + 1);
    if (!copy)
        return false;
    memcpy(copy, value, len + 1);

    physics_UserData_clear(ud);
    ud->type = physics_UserData_string;
    ud->string = copy;
    return true;
}

/* ------------------------------------------------------------------ */
/* Small pointer-list helpers (the world's weak child lists)           */
/* ------------------------------------------------------------------ */

static bool list_add(void ***items, int *count, int *capacity, void *item) {
    if (*count == *capacity) {
        int newCapacity = *capacity ? *capacity * 2 : 8;
        void **grown = realloc(*items, (size_t)newCapacity * sizeof(void*));
        if (!grown)
            return false;
        *items = grown;
        *capacity = newCapacity;
    }
    (*items)[(*count)++] = item;
    return true;
}

/* Cuts a fixture wrapper loose from Box2D without touching it - for when the
 * shape is already gone, because the body took it down. */
static void fixture_detach(physics_Fixture *fixture);

static void list_remove(void **items, int *count, void *item) {
    for (int i = 0; i < *count; i++) {
        if (items[i] == item) {
            items[i] = items[*count - 1];
            (*count)--;
            return;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Shapes                                                              */
/* ------------------------------------------------------------------ */

static physics_Shape *shape_alloc(physics_ShapeType type) {
    physics_Shape *shape = calloc(1, sizeof(physics_Shape));
    if (!shape)
        return NULL;
    shape->refcount = 1;
    shape->type = type;
    return shape;
}

/* Copies pointCount pixel pairs into a freshly allocated meter-space array. */
static bool shape_copyPoints(physics_Shape *shape, const float *points, int pointCount) {
    shape->points = malloc((size_t)pointCount * sizeof(b2Vec2));
    if (!shape->points)
        return false;
    for (int i = 0; i < pointCount; i++)
        shape->points[i] = physics_scaleDownVec(points[2*i], points[2*i + 1]);
    shape->pointCount = pointCount;
    return true;
}

physics_Shape *physics_Shape_newCircle(float x, float y, float radius) {
    physics_Shape *shape = shape_alloc(physics_ShapeType_circle);
    if (!shape)
        return NULL;
    shape->circle.center = physics_scaleDownVec(x, y);
    shape->circle.radius = physics_scaleDown(radius);
    return shape;
}

physics_Shape *physics_Shape_newRectangle(float x, float y, float w, float h, float angle) {
    float hw = w * 0.5f;
    float hh = h * 0.5f;
    float corners[8] = {
        -hw, -hh,
         hw, -hh,
         hw,  hh,
        -hw,  hh
    };

    float c = cosf(angle);
    float s = sinf(angle);
    for (int i = 0; i < 4; i++) {
        float cx = corners[2*i];
        float cy = corners[2*i + 1];
        corners[2*i]     = x + cx * c - cy * s;
        corners[2*i + 1] = y + cx * s + cy * c;
    }
    return physics_Shape_newPolygon(corners, 4);
}

physics_Shape *physics_Shape_newPolygon(const float *points, int pointCount) {
    if (pointCount < 3 || pointCount > PHYSICS_MAX_POLYGON_VERTICES)
        return NULL;

    physics_Shape *shape = shape_alloc(physics_ShapeType_polygon);
    if (!shape)
        return NULL;
    if (!shape_copyPoints(shape, points, pointCount)) {
        free(shape);
        return NULL;
    }

    /* Reject degenerate input here rather than at fixture creation time, so
     * the script gets the error where the bad vertices were passed. */
    b2Hull hull = b2ComputeHull(shape->points, shape->pointCount);
    if (hull.count < 3) {
        physics_Shape_release(shape);
        return NULL;
    }
    return shape;
}

physics_Shape *physics_Shape_newEdge(float x1, float y1, float x2, float y2) {
    physics_Shape *shape = shape_alloc(physics_ShapeType_edge);
    if (!shape)
        return NULL;
    shape->segment.point1 = physics_scaleDownVec(x1, y1);
    shape->segment.point2 = physics_scaleDownVec(x2, y2);
    return shape;
}

physics_Shape *physics_Shape_newChain(bool loop, const float *points, int pointCount) {
    if (pointCount < 2)
        return NULL;

    physics_Shape *shape = shape_alloc(physics_ShapeType_chain);
    if (!shape)
        return NULL;
    if (!shape_copyPoints(shape, points, pointCount)) {
        free(shape);
        return NULL;
    }
    shape->loop = loop;
    return shape;
}

void physics_Shape_retain(physics_Shape *shape) {
    if (shape)
        shape->refcount++;
}

void physics_Shape_release(physics_Shape *shape) {
    if (!shape || --shape->refcount > 0)
        return;
    free(shape->points);
    free(shape);
}

/* ------------------------------------------------------------------ */
/* World                                                               */
/* ------------------------------------------------------------------ */

physics_World *physics_World_new(float gravityX, float gravityY, bool allowSleep) {
    physics_World *world = calloc(1, sizeof(physics_World));
    if (!world)
        return NULL;

    b2WorldDef def = b2DefaultWorldDef();
    def.gravity = physics_scaleDownVec(gravityX, gravityY);
    def.enableSleep = allowSleep;

    world->refcount = 1;
    world->id = b2CreateWorld(&def);
    b2World_SetUserData(world->id, world);
    return world;
}

void physics_World_retain(physics_World *world) {
    if (world)
        world->refcount++;
}

bool physics_World_isValid(const physics_World *world) {
    return world && !world->destroyed && b2World_IsValid(world->id);
}

void physics_World_destroy(physics_World *world) {
    if (!world || world->destroyed)
        return;

    /* The Box2D world takes every body, shape and joint with it, so the
     * children only have to forget their now-stale ids. Their wrappers stay
     * alive as long as the script holds them and report isDestroyed(). */
    for (int i = 0; i < world->bodyCount; i++)
        world->bodies[i]->id = b2_nullBodyId;
    for (int i = 0; i < world->jointCount; i++)
        world->joints[i]->id = b2_nullJointId;

    b2DestroyWorld(world->id);
    world->id = b2_nullWorldId;
    world->destroyed = true;
}

void physics_World_release(physics_World *world) {
    if (!world || --world->refcount > 0)
        return;

    physics_World_destroy(world);
    free(world->bodies);
    free(world->joints);
    free(world->beginContact);
    free(world->endContact);
    free(world);
}

void physics_World_update(physics_World *world, float dt, int subSteps) {
    if (!physics_World_isValid(world))
        return;
    b2World_Step(world->id, dt, subSteps);
}

static bool world_setCallbackName(char **slot, const char *name) {
    char *copy = NULL;
    if (name) {
        size_t len = strlen(name);
        copy = malloc(len + 1);
        if (!copy)
            return false;
        memcpy(copy, name, len + 1);
    }
    free(*slot);
    *slot = copy;
    return true;
}

bool physics_World_setBeginContact(physics_World *world, const char *name) {
    return world_setCallbackName(&world->beginContact, name);
}

bool physics_World_setEndContact(physics_World *world, const char *name) {
    return world_setCallbackName(&world->endContact, name);
}

b2BodyId physics_World_mouseGround(physics_World *world) {
    if (!world->hasMouseGround) {
        b2BodyDef def = b2DefaultBodyDef();
        def.type = b2_staticBody;
        world->mouseGround = b2CreateBody(world->id, &def);
        world->hasMouseGround = true;
    }
    return world->mouseGround;
}

/* ------------------------------------------------------------------ */
/* Box2D id -> CLove wrapper                                           */
/* ------------------------------------------------------------------ */

physics_Fixture *physics_Fixture_fromShapeId(b2ShapeId shapeId) {
    if (!b2Shape_IsValid(shapeId))
        return NULL;
    return b2Shape_GetUserData(shapeId);
}

physics_Body *physics_Body_fromBodyId(b2BodyId bodyId) {
    if (!b2Body_IsValid(bodyId))
        return NULL;
    return b2Body_GetUserData(bodyId);
}

physics_Joint *physics_Joint_fromJointId(b2JointId jointId) {
    if (!b2Joint_IsValid(jointId))
        return NULL;
    return b2Joint_GetUserData(jointId);
}

/* ------------------------------------------------------------------ */
/* Body                                                                */
/* ------------------------------------------------------------------ */

physics_Body *physics_Body_new(physics_World *world, float x, float y, b2BodyType type) {
    if (!physics_World_isValid(world))
        return NULL;

    physics_Body *body = calloc(1, sizeof(physics_Body));
    if (!body)
        return NULL;

    if (!list_add((void***)&world->bodies, &world->bodyCount, &world->bodyCapacity, body)) {
        free(body);
        return NULL;
    }

    b2BodyDef def = b2DefaultBodyDef();
    def.type = type;
    def.position = physics_scaleDownVec(x, y);
    def.userData = body;

    body->refcount = 1;
    body->id = b2CreateBody(world->id, &def);
    body->world = world;
    physics_World_retain(world);
    return body;
}

void physics_Body_retain(physics_Body *body) {
    if (body)
        body->refcount++;
}

bool physics_Body_isValid(const physics_Body *body) {
    return body && body->world && !body->world->destroyed && b2Body_IsValid(body->id);
}

void physics_Body_destroy(physics_Body *body) {
    if (!body || !body->world)
        return;

    if (b2Body_IsValid(body->id) && !body->world->destroyed)
        b2DestroyBody(body->id);    /* takes its shapes with it */
    body->id = b2_nullBodyId;

    /* Those shapes are gone now, so let go of the wrappers that were only being
     * held for them. A wrapper the script still holds survives, detached, and
     * reports itself destroyed. */
    for (int i = body->fixtureCount - 1; i >= 0; i--) {
        physics_Fixture *fixture = body->fixtures[i];
        fixture_detach(fixture);
        physics_Fixture_release(fixture);
    }
    free(body->fixtures);
    body->fixtures = NULL;
    body->fixtureCount = 0;
    body->fixtureCapacity = 0;

    list_remove((void**)body->world->bodies, &body->world->bodyCount, body);
    physics_World_release(body->world);
    body->world = NULL;
}

void physics_Body_release(physics_Body *body) {
    if (!body || --body->refcount > 0)
        return;
    physics_Body_destroy(body);
    physics_UserData_clear(&body->userData);
    free(body);
}

/* ------------------------------------------------------------------ */
/* Fixture                                                             */
/* ------------------------------------------------------------------ */

physics_Fixture *physics_Fixture_new(physics_Body *body, physics_Shape *shape, float density) {
    if (!physics_Body_isValid(body) || !shape)
        return NULL;

    physics_Fixture *fixture = calloc(1, sizeof(physics_Fixture));
    if (!fixture)
        return NULL;
    fixture->refcount = 1;

    if (shape->type == physics_ShapeType_chain) {
        /* Chains are their own Box2D object: they have no density and are
         * only useful on static geometry, exactly as in LÖVE. */
        b2ChainDef def = b2DefaultChainDef();
        def.points = shape->points;
        def.count = shape->pointCount;
        def.isLoop = shape->loop;
        def.userData = fixture;

        fixture->kind = physics_FixtureKind_chain;
        fixture->chainId = b2CreateChain(body->id, &def);
        fixture->shapeId = b2_nullShapeId;
    } else {
        b2ShapeDef def = b2DefaultShapeDef();
        def.density = density;
        def.userData = fixture;
        def.enableContactEvents = true;

        fixture->kind = physics_FixtureKind_shape;
        fixture->chainId = b2_nullChainId;

        switch (shape->type) {
        case physics_ShapeType_circle:
            fixture->shapeId = b2CreateCircleShape(body->id, &def, &shape->circle);
            break;
        case physics_ShapeType_edge:
            fixture->shapeId = b2CreateSegmentShape(body->id, &def, &shape->segment);
            break;
        case physics_ShapeType_polygon:
        default: {
            b2Hull hull = b2ComputeHull(shape->points, shape->pointCount);
            if (hull.count < 3) {
                free(fixture);
                return NULL;
            }
            b2Polygon polygon = b2MakePolygon(&hull, 0.0f);
            fixture->shapeId = b2CreatePolygonShape(body->id, &def, &polygon);
            break;
        }
        }
    }

    fixture->body = body;
    fixture->shape = shape;
    physics_Shape_retain(shape);

    /* The body takes a reference: dropping the script's handle to a fixture
     * must not take the collision shape down with it. */
    if (!list_add((void***)&body->fixtures, &body->fixtureCount,
                  &body->fixtureCapacity, fixture)) {
        physics_Fixture_destroy(fixture);
        physics_Shape_release(shape);
        free(fixture);
        return NULL;
    }
    physics_Fixture_retain(fixture);
    return fixture;
}

void physics_Fixture_retain(physics_Fixture *fixture) {
    if (fixture)
        fixture->refcount++;
}

bool physics_Fixture_isValid(const physics_Fixture *fixture) {
    if (!fixture || !physics_Body_isValid(fixture->body))
        return false;
    return fixture->kind == physics_FixtureKind_chain
        ? b2Chain_IsValid(fixture->chainId)
        : b2Shape_IsValid(fixture->shapeId);
}

static void fixture_detach(physics_Fixture *fixture) {
    fixture->shapeId = b2_nullShapeId;
    fixture->chainId = b2_nullChainId;
    fixture->body = NULL;
}

void physics_Fixture_destroy(physics_Fixture *fixture) {
    physics_Body *body;

    if (!fixture || !fixture->body)
        return;

    body = fixture->body;

    if (physics_Body_isValid(body)) {
        if (fixture->kind == physics_FixtureKind_chain) {
            if (b2Chain_IsValid(fixture->chainId))
                b2DestroyChain(fixture->chainId);
        } else if (b2Shape_IsValid(fixture->shapeId)) {
            b2DestroyShape(fixture->shapeId, true);
        }
    }
    fixture_detach(fixture);

    /* The body's reference goes last: it may well be the only one left. */
    list_remove((void**)body->fixtures, &body->fixtureCount, fixture);
    physics_Fixture_release(fixture);
}

void physics_Fixture_release(physics_Fixture *fixture) {
    if (!fixture || --fixture->refcount > 0)
        return;
    /* Whoever let the last reference go was not the owner of the shape: by now
     * either fixture:destroy() or the body's teardown has already detached it,
     * so there is nothing left to tell Box2D. */
    physics_Shape_release(fixture->shape);
    physics_UserData_clear(&fixture->userData);
    free(fixture);
}

/* ------------------------------------------------------------------ */
/* Joint                                                               */
/* ------------------------------------------------------------------ */

physics_Joint *physics_Joint_new(physics_World *world, physics_JointType type, b2JointId id,
                                 physics_Body *bodyA, physics_Body *bodyB) {
    physics_Joint *joint = calloc(1, sizeof(physics_Joint));
    if (!joint) {
        if (b2Joint_IsValid(id))
            b2DestroyJoint(id);
        return NULL;
    }

    if (!list_add((void***)&world->joints, &world->jointCount, &world->jointCapacity, joint)) {
        if (b2Joint_IsValid(id))
            b2DestroyJoint(id);
        free(joint);
        return NULL;
    }

    joint->refcount = 1;
    joint->type = type;
    joint->id = id;
    joint->world = world;
    joint->bodyA = bodyA;
    joint->bodyB = bodyB;
    physics_World_retain(world);
    physics_Body_retain(bodyA);
    physics_Body_retain(bodyB);
    b2Joint_SetUserData(id, joint);
    return joint;
}

void physics_Joint_retain(physics_Joint *joint) {
    if (joint)
        joint->refcount++;
}

bool physics_Joint_isValid(const physics_Joint *joint) {
    return joint && joint->world && !joint->world->destroyed && b2Joint_IsValid(joint->id);
}

void physics_Joint_destroy(physics_Joint *joint) {
    if (!joint || !joint->world)
        return;

    if (!joint->world->destroyed && b2Joint_IsValid(joint->id))
        b2DestroyJoint(joint->id);
    joint->id = b2_nullJointId;

    list_remove((void**)joint->world->joints, &joint->world->jointCount, joint);

    physics_Body_release(joint->bodyA);
    physics_Body_release(joint->bodyB);
    joint->bodyA = NULL;
    joint->bodyB = NULL;

    physics_World_release(joint->world);
    joint->world = NULL;
}

void physics_Joint_release(physics_Joint *joint) {
    if (!joint || --joint->refcount > 0)
        return;
    physics_Joint_destroy(joint);
    free(joint);
}
