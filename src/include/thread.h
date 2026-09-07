/*
#   clove
#
#   Copyright (C) 2019-2026 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/
#ifndef __clove_thread_
#define __clove_thread_

#include "../3rdparty/SDL3/include/SDL3/SDL.h"

SDL_Thread* createThread(int (SDLCALL * fn) (void *), const char* name, void *data);



#endif
