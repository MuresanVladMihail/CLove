#
#   clove
#
#   Copyright (C) 2016-2025 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
#

cmake_minimum_required(VERSION 3.16)

project(clove C CXX)

# ----------------------------------------------------------------------------
# Options
# ----------------------------------------------------------------------------
option(USE_FH      "Build CLove with FH as the scripting language"  ON)
option(USE_LUA     "Build CLove with Lua as the scripting language" OFF)
option(STATIC_LIB  "Also build CLove as a static library"           ON)
option(DYNAMIC_LIB "Also build CLove as a shared library"           OFF)

if(USE_FH)
    message(STATUS "CLove scripting language: FH")
endif()
if(USE_LUA)
    message(STATUS "CLove scripting language: Lua")
endif()

# Defines consumed by the engine sources (e.g. main.c: #ifdef USE_FH).
# Applied directory-wide so every CLove target (executable + libraries) agrees.
if(USE_FH)
    add_compile_definitions(USE_FH=ON)
endif()
if(USE_LUA)
    add_compile_definitions(USE_LUA=ON)
endif()
add_compile_definitions(SDL2 FT2_BUILD_LIBRARY)

# ----------------------------------------------------------------------------
# Platform-specific compiler / linker setup
#
# OS_SPECIFIC_LIBRARIES: extra system libs linked into the clove executable.
# SOURCE_PHYSFS_FILES:   PhysFS sources (its platform set differs per OS).
# ----------------------------------------------------------------------------
if(WIN32)
    find_package(OpenGL REQUIRED)

    set(CMAKE_C_FLAGS_DEBUG     "${CMAKE_C_FLAGS_DEBUG} /Z7 /W4 /Od /MDd /D_DEBUG /D__Debug__")
    set(CMAKE_C_FLAGS_RELEASE   "${CMAKE_C_FLAGS_RELEASE} /W1 /Ox /Ot /MD /DNDEBUG")
    set(CMAKE_CXX_FLAGS         "${CMAKE_CXX_FLAGS} /EHsc /GR /FC /DWIN32 /D_MBCS")
    set(CMAKE_CXX_FLAGS_DEBUG   "${CMAKE_CXX_FLAGS_DEBUG} /Z7 /W4 /Od /MDd /D_DEBUG /D__Debug__")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /W1 /Ox /Ot /MD /DNDEBUG")

    set(OS_SPECIFIC_LIBRARIES ws2_32)

    file(GLOB SOURCE_PHYSFS_FILES CONFIGURE_DEPENDS
        src/3rdparty/physfs/*.c
        src/3rdparty/physfs/*.h)

elseif(APPLE)
    find_package(OpenGL REQUIRED)
    find_library(COCOA_LIB         Cocoa)
    find_library(CORE_SERVICES_LIB CoreServices)
    find_library(FOUNDATION_LIB    Foundation)
    find_library(IOKIT_LIB         IOKit)

    # SDL 2.32+ requires a deployment target of at least 10.7; 10.6 fails its
    # SDL_platform.h guard. 10.11 is a safe modern floor for current toolchains.
    set(MTCL_BUILD_FLAGS "-mmacosx-version-min=10.11")
    set(CMAKE_C_FLAGS         "${CMAKE_C_FLAGS} ${MTCL_BUILD_FLAGS} -Wall -Wvla -O3 -DOSX")
    set(CMAKE_C_FLAGS_DEBUG   "${CMAKE_C_FLAGS_DEBUG} ${MTCL_BUILD_FLAGS} -D__Debug__ -g3")
    set(CMAKE_CXX_FLAGS       "${CMAKE_CXX_FLAGS} ${MTCL_BUILD_FLAGS} -O3 -DOSX")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${MTCL_BUILD_FLAGS} -D__Debug__")

    set(OS_SPECIFIC_LIBRARIES ${COCOA_LIB} ${CORE_SERVICES_LIB} ${FOUNDATION_LIB} ${IOKIT_LIB})
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
    set(CMAKE_INSTALL_RPATH "install")

    file(GLOB SOURCE_PHYSFS_FILES CONFIGURE_DEPENDS
        src/3rdparty/physfs/*.c
        src/3rdparty/physfs/*.h
        src/3rdparty/physfs/*.m)

elseif(UNIX)
    find_package(OpenGL REQUIRED)

    set(CMAKE_C_FLAGS         "${CMAKE_C_FLAGS} -std=c99 -g -Wall -Wvla -static-libgcc -fPIC -DLINUX")
    set(CMAKE_C_FLAGS_DEBUG   "${CMAKE_C_FLAGS_DEBUG} -D__Debug__")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3")
    set(CMAKE_CXX_FLAGS         "${CMAKE_CXX_FLAGS} -fPIC -DLINUX")
    set(CMAKE_CXX_FLAGS_DEBUG   "${CMAKE_CXX_FLAGS_DEBUG} -D__Debug__")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
    set(CMAKE_SHARED_LINKER_FLAGS
        "-Wl,-rpath=$ORIGIN,--as-needed,--allow-multiple-definition,--build-id")

    # dl is linked as a library (CMAKE_DL_LIBS) below, not as a compile flag.
    set(OS_SPECIFIC_LIBRARIES pthread Xxf86vm X11 Xrandr Xi ${CMAKE_DL_LIBS})
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
    set(CMAKE_INSTALL_RPATH "install")

    file(GLOB SOURCE_PHYSFS_FILES CONFIGURE_DEPENDS
        src/3rdparty/physfs/*.c
        src/3rdparty/physfs/*.h)
endif()

# ----------------------------------------------------------------------------
# Source files
#
# Globs use CONFIGURE_DEPENDS so newly added files are picked up on the next
# build without re-running cmake by hand.
# ----------------------------------------------------------------------------
file(GLOB SOURCE_FILES CONFIGURE_DEPENDS
    src/*.c               src/*.h
    src/audio/*.c         src/audio/*.h
    src/filesystem/*.c    src/filesystem/*.h
    src/graphics/*.c      src/graphics/*.h
    src/image/*.c         src/image/*.h
    src/svg/*.c           src/svg/*.h
    src/tools/*.c         src/tools/*.h
    src/math/*.c          src/math/*.h
    src/timer/*.c         src/timer/*.h
    src/net/*.c           src/net/*.h
    src/physics/*.c       src/physics/*.h
    src/ui/*.c            src/ui/*.h
    src/include/*.c       src/include/*.h

    # custom native plugins
    native/*.c* native/*.h

    # small, single/dual-file vendored libraries
    src/3rdparty/microui/src/*.c  src/3rdparty/microui/src/*.inl  src/3rdparty/microui/src/*.h
    src/3rdparty/microtar/*.c     src/3rdparty/microtar/*.h
    src/3rdparty/noise/*.c        src/3rdparty/noise/*.h
    src/3rdparty/slre/*.c         src/3rdparty/slre/*.h
    src/3rdparty/CMath/*.c        src/3rdparty/CMath/*.h
    src/3rdparty/mojoAL/mojoal.c  src/3rdparty/mojoAL/AL/*.h)

if(USE_FH)
    file(GLOB SOURCE_FH_FILES CONFIGURE_DEPENDS
        src/fhapi/*.c                 src/fhapi/*.h
        src/3rdparty/FH/src/*.c        src/3rdparty/FH/src/*.h
        src/3rdparty/FH/src/map/*.c    src/3rdparty/FH/src/map/*.h
        src/3rdparty/FH/src/vec/*.c    src/3rdparty/FH/src/vec/*.h
        src/3rdparty/FH/src/regex/*.c  src/3rdparty/FH/src/regex/*.h
        src/3rdparty/FH/src/crypto/*.c src/3rdparty/FH/src/crypto/*.h)
endif()

if(USE_LUA)
    file(GLOB SOURCE_LUA_FILES CONFIGURE_DEPENDS
        src/luaapi/*.c src/luaapi/*.h)
endif()

# ----------------------------------------------------------------------------
# Vendored dependencies (built from source via their own CMake)
# ----------------------------------------------------------------------------

# freetype — its target propagates its include dirs, but the static `love`
# library compiles font code without linking it, so add the include globally.
add_subdirectory(src/3rdparty/freetype)
include_directories(src/3rdparty/freetype/include)
set(FREETYPE_LIBRARIES freetype)

# glew (static)
add_subdirectory(src/3rdparty/glew/build/cmake)
set(GLEW_LIBRARIES glew_s)

# SDL2 — link statically; force the static target on and skip the shared lib
# and tests so SDL exposes the SDL2-static / SDL2main targets.
set(SDL_SHARED        OFF CACHE BOOL "" FORCE)
set(SDL_STATIC        ON  CACHE BOOL "" FORCE)
set(SDL_TEST          OFF CACHE BOOL "" FORCE)
set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
add_subdirectory(src/3rdparty/SDL2)
set(SDL_LIBRARIES SDL2-static SDL2main)

if(USE_LUA)
    add_subdirectory(src/3rdparty/lua)
    set(LUA_LIBRARIES lua)
endif()

# Currently unused vendored libraries, kept for reference:
#   add_subdirectory(src/3rdparty/Chipmunk2D)   # physics
#   add_subdirectory(src/3rdparty/zlib)          # compression

# ----------------------------------------------------------------------------
# Targets
# ----------------------------------------------------------------------------
set(CLOVE_LINK_LIBRARIES
    ${SDL_LIBRARIES}
    ${GLEW_LIBRARIES}
    ${OPENGL_LIBRARIES}
    ${FREETYPE_LIBRARIES}
    ${OS_SPECIFIC_LIBRARIES})
if(USE_LUA)
    list(APPEND CLOVE_LINK_LIBRARIES ${LUA_LIBRARIES})
endif()

# The clove executable: engine + scripting bindings + PhysFS.
add_executable(clove
    ${SOURCE_FILES}
    ${SOURCE_FH_FILES}
    ${SOURCE_LUA_FILES}
    ${SOURCE_PHYSFS_FILES})
target_link_libraries(clove ${CLOVE_LINK_LIBRARIES})

# Optional reusable libraries built from the engine sources.
if(STATIC_LIB)
    add_library(love STATIC ${SOURCE_FILES})
endif()
if(DYNAMIC_LIB)
    add_library(love_shared SHARED ${SOURCE_FILES})
endif()

message(STATUS "Copyright (c) 2015-2025, Mureşan Vlad-Mihail.")
