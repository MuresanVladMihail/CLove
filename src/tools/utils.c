/*
#   clove
#
#   Copyright (C) 2016-2021 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/

#include "../include/utils.h"

#include <stdarg.h>
#include <stdio.h>

/* The single definition of the main-loop flags declared in utils.h. */
bool clove_running = false;
bool clove_reload = false;

int clove_error(const char* format, ...)
{
  va_list argptr;
  va_start(argptr, format);
  vfprintf(stderr, format, argptr);
  va_end(argptr);
  return -1;
}
