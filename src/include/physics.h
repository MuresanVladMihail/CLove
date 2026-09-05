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

#include <box2d/box2d.h>

/*
 * CLove's physics module: the object model LÖVE's love.physics exposes,
 * implemented on top of Box2D 3.x (which is already plain C, so there is no
 * per-call engine wrapper here — the bindings talk to b2* directly and use
 * this file for object lifetime, pixel/meter scaling and enumeration).
 *
 * Ownership follows LÖVE: a child retains its parent (fixture -> body ->
 * world, joint -> world + both bodies), and a parent keeps a *weak* list of
 * its live children purely so world:getBodies() and friends can enumerate
 * them. A wrapper whose refcount reaches zero destroys its Box2D object and
 * unlinks itself from the parent list. As in LÖVE this means a body the
 * script no longer references is eventually collected and leaves the world.
 *
 * Every wrapper can also be destroyed explicitly (body:destroy()). After that
 * the Box2D id is stale; physics_*_isValid() reports it and the bindings turn
 * a call on a destroyed object into a script error instead of a crash.
 */

/* ------------------------------------------------------------------ */
/* Pixels <-> meters                                                   */
/* ------------------------------------------------------------------ */

/* Number of pixels in one Box2D meter. LÖVE's default is 30. */
void  physics_setMeter(float meter);
float physics_getMeter(void);

float   physics_scaleDown(float pixels);
float   physics_scaleUp(float meters);
b2Vec2  physics_scaleDownVec(float x, float y);

/* ------------------------------------------------------------------ */
/* User data (numbers and strings only — FH has no way to hold an      */
/* arbitrary script value alive across a GC cycle from C)              */
/* ------------------------------------------------------------------ */

typedef enum {
    physics_UserData_none,
    physics_UserData_number,
    physics_UserData_string
} physics_UserDataType;

typedef struct {
    physics_UserDataType type;
    double number;
    char  *string;
} physics_UserData;

void physics_UserData_clear(physics_UserData *ud);
void physics_UserData_setNumber(physics_UserData *ud, double value);
/* Copies the string; returns false when out of memory. */
bool physics_UserData_setString(physics_UserData *ud, const char *value);

/* ------------------------------------------------------------------ */
/* Shapes (geometry templates, not attached to anything)               */
/* ------------------------------------------------------------------ */

typedef enum {
    physics_ShapeType_circle,
    physics_ShapeType_polygon,
    physics_ShapeType_edge,
    physics_ShapeType_chain
} physics_ShapeType;

/* Box2D's polygon vertex ceiling (B2_MAX_POLYGON_VERTICES), the same limit
 * LÖVE documents for love.physics.newPolygonShape. */
#define PHYSICS_MAX_POLYGON_VERTICES B2_MAX_POLYGON_VERTICES

typedef struct physics_Shape {
    int refcount;
    physics_ShapeType type;

    b2Circle  circle;   /* physics_ShapeType_circle, meters */
    b2Segment segment;  /* physics_ShapeType_edge,   meters */

    b2Vec2 *points;     /* polygon / chain vertices, meters */
    int     pointCount;
    bool    loop;       /* chain only */
} physics_Shape;

physics_Shape *physics_Shape_newCircle(float x, float y, float radius);
physics_Shape *physics_Shape_newRectangle(float x, float y, float w, float h, float angle);
/* points are pixel pairs: x1, y1, x2, y2, ... */
physics_Shape *physics_Shape_newPolygon(const float *points, int pointCount);
physics_Shape *physics_Shape_newEdge(float x1, float y1, float x2, float y2);
physics_Shape *physics_Shape_newChain(bool loop, const float *points, int pointCount);

void physics_Shape_retain(physics_Shape *shape);
void physics_Shape_release(physics_Shape *shape);

/* ------------------------------------------------------------------ */
/* World / Body / Fixture / Joint                                      */
/* ------------------------------------------------------------------ */

typedef struct physics_World   physics_World;
typedef struct physics_Body    physics_Body;
typedef struct physics_Fixture physics_Fixture;
typedef struct physics_Joint   physics_Joint;

struct physics_World {
    int refcount;
    b2WorldId id;
    bool destroyed;

    /* Weak lists of live wrappers, for enumeration. */
    physics_Body **bodies;
    int bodyCount, bodyCapacity;
    physics_Joint **joints;
    int jointCount, jointCapacity;

    /* Names of the script functions set with world:setCallbacks(). Box2D 3
     * reports contacts as events collected during the step rather than as
     * callbacks, so these are dispatched right after b2World_Step. */
    char *beginContact;
    char *endContact;

    /* Box2D 3's mouse joint needs a second body to pull against; LÖVE's
     * love.physics.newMouseJoint() only takes one. This static, shapeless
     * body is created on first use and plays the "ground" LÖVE hides. */
    b2BodyId mouseGround;
    bool hasMouseGround;
};

struct physics_Body {
    int refcount;
    b2BodyId id;
    physics_World *world;   /* retained; NULL once the body is destroyed */
    physics_UserData userData;

    /* Strong references to this body's fixtures. Every b2 shape carries its
     * wrapper as user data, which the contact callbacks and getFixtures() read
     * back, so a wrapper has to outlive its shape even after the script drops
     * its handle to it. The fixture's own pointer back here is weak in return,
     * or the two would keep each other alive for good. */
    physics_Fixture **fixtures;
    int fixtureCount, fixtureCapacity;
};

typedef enum {
    physics_FixtureKind_shape,  /* one b2Shape */
    physics_FixtureKind_chain   /* a b2Chain (chain shapes only) */
} physics_FixtureKind;

struct physics_Fixture {
    int refcount;
    physics_FixtureKind kind;
    b2ShapeId shapeId;
    b2ChainId chainId;

    physics_Body  *body;   /* weak; NULL once the fixture or its body is destroyed */
    physics_Shape *shape;  /* retained */

    physics_UserData userData;
};

/* LÖVE's joint taxonomy. Box2D 3 dropped the friction and rope joints, so
 * CLove builds those two out of the joints it does have (a motor joint with
 * no offset, and a limited spring-less distance joint) and remembers here
 * which one the script asked for, so joint:getType() stays LÖVE-shaped. */
typedef enum {
    physics_JointType_distance,
    physics_JointType_revolute,
    physics_JointType_prismatic,
    physics_JointType_mouse,
    physics_JointType_weld,
    physics_JointType_wheel,
    physics_JointType_motor,
    physics_JointType_friction,
    physics_JointType_rope
} physics_JointType;

struct physics_Joint {
    int refcount;
    physics_JointType type;
    b2JointId id;
    physics_World *world;  /* retained; NULL once the joint is destroyed */
    physics_Body  *bodyA;  /* retained */
    physics_Body  *bodyB;  /* retained */
};

/* --- world --- */
physics_World *physics_World_new(float gravityX, float gravityY, bool allowSleep);
void physics_World_retain(physics_World *world);
void physics_World_release(physics_World *world);
bool physics_World_isValid(const physics_World *world);
/* Steps the simulation. dt is in seconds, subSteps is Box2D's solver sub-step
 * count (4 is Box2D's own default). */
void physics_World_update(physics_World *world, float dt, int subSteps);
/* Explicit teardown; the wrapper stays alive until its references go. */
void physics_World_destroy(physics_World *world);
/* Copies the function name (NULL clears the callback); false on OOM. */
bool physics_World_setBeginContact(physics_World *world, const char *name);
bool physics_World_setEndContact(physics_World *world, const char *name);
/* Every Box2D object carries its CLove wrapper in b2*_GetUserData, so the
 * collision callbacks and the getFixtures()/getJoints() enumerations can go
 * from a Box2D id back to the script-visible object. NULL if there is none. */
physics_Fixture *physics_Fixture_fromShapeId(b2ShapeId shapeId);
/* The world's hidden anchor body for mouse joints, created on first call. */
b2BodyId physics_World_mouseGround(physics_World *world);
physics_Body    *physics_Body_fromBodyId(b2BodyId bodyId);
physics_Joint   *physics_Joint_fromJointId(b2JointId jointId);

/* --- body --- */
physics_Body *physics_Body_new(physics_World *world, float x, float y, b2BodyType type);
void physics_Body_retain(physics_Body *body);
void physics_Body_release(physics_Body *body);
bool physics_Body_isValid(const physics_Body *body);
void physics_Body_destroy(physics_Body *body);

/* --- fixture --- */
/* density is in kg/m^2 as in LÖVE; returns NULL on failure (bad geometry). */
physics_Fixture *physics_Fixture_new(physics_Body *body, physics_Shape *shape, float density);
void physics_Fixture_retain(physics_Fixture *fixture);
void physics_Fixture_release(physics_Fixture *fixture);
bool physics_Fixture_isValid(const physics_Fixture *fixture);
void physics_Fixture_destroy(physics_Fixture *fixture);

/* --- joint --- */
/* Retains the world and both bodies; the b2 joint must already exist. */
physics_Joint *physics_Joint_new(physics_World *world, physics_JointType type, b2JointId id,
                                 physics_Body *bodyA, physics_Body *bodyB);
void physics_Joint_retain(physics_Joint *joint);
void physics_Joint_release(physics_Joint *joint);
bool physics_Joint_isValid(const physics_Joint *joint);
void physics_Joint_destroy(physics_Joint *joint);
