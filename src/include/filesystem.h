/*
#   clove
#
#   Copyright (C) 2016-2021 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/
#pragma once

#include <stdbool.h>

#define USE_PHYSFS 1

#ifdef USE_PHYSFS
#include "../3rdparty/physfs/physfs.h"
#endif

#include "../3rdparty/SDL2/include/SDL.h"

enum FileType {
	FileType_REGULAR,
	FileType_DIRECTORY,
	FileType_SYMLIN,
	FileType_OTHER
};

struct FileInfo {
	int64_t size;
	int64_t modtime;
	int64_t accesstime;
	int64_t createtime;
	enum FileType type;
};

void filesystem_init(char* argv0, bool stats);
void filesystem_free(void);

const char* filesystem_getOS(void);
const char* filesystem_getSaveDirectory(const char* company, const char* projName);
int filesystem_read(char const* filename, char** output);
int filesystem_write(const char* name, const char* data);
int filesystem_append(const char* name, const char* data);
bool filesystem_exists(const char* name);
bool filesystem_getInfo(const char* path, struct FileInfo *info);
bool filesystem_equals(const char* a,const char* b,int l);
bool filesystem_contain(const char* a, const char* b);
bool filesystem_remove(const char* name);
bool filesystem_rename(const char *old_name, const char *new_name);
bool filesystem_state(const char* file, int mode);
bool filesystem_isSymLink(const char* dir);
bool filesystem_isDir(const char* dir);
bool filesystem_mkDir(const char* path);
/* the process's working directory, where a game's relative paths resolve --
 * filesystem_getCurrentDirectory() answers PhysFS's base directory instead,
 * which is where the executable lives */
const char* filesystem_getWorkingDirectory(void);
const char* filesystem_getCurrentDirectory(void);
char** filesystem_enumerate(const char* path);
/* gives the list from filesystem_enumerate() back to PhysFS */
void filesystem_freeEnumerate(char** list);

/*
 * One entry of a real directory listing.
 *
 * filesystem_enumerate() above is PhysFS's, so it only ever sees what has been
 * mounted into the virtual filesystem -- fine for a game reading its own
 * assets, useless for a tool that has to let someone browse the machine. This
 * pair walks the actual filesystem instead, and takes absolute paths.
 *
 * "." and ".." are left out: a caller that wants a parent entry knows where it
 * is. The list is sorted with directories first, then by name, case
 * insensitively, which is the order a file picker wants to show.
 */
struct DirEntry {
	char *name;
	bool is_dir;
};

struct DirEntry* filesystem_listDirectory(const char* path, int* count);
void filesystem_freeDirectoryList(struct DirEntry* entries, int count);

/* The user's home directory, straight from the environment -- unlike
 * filesystem_getUsrDir() this needs no PhysFS identity. NULL if unset. */
const char* filesystem_getHomeDirectory(void);
const char* filesystem_getUsrDir(void);
bool filesystem_setIdentity(const char* path);

bool filesystem_mount(const char* path, const char* mountPoint, int appendToPath);
bool filesystem_unmount(const char* path);

void filesystem_setSource(const char* source);
const char* filesystem_getSource(void);
