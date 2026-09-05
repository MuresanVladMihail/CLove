/*
#   clove
#
#   Copyright (C) 2016-2025 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/
#pragma once

#include "../3rdparty/FH/src/fh.h"

#define FH_PHYSICS_WORLD   12
#define FH_PHYSICS_BODY    13
#define FH_PHYSICS_SHAPE   14
#define FH_PHYSICS_FIXTURE 15
#define FH_PHYSICS_JOINT   16
#define FH_PHYSICS_CONTACT 17

void fh_physics_register(struct fh_program *prog);
