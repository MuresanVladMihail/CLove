/*
#   clove
#
#   Copyright (C) 2016-2020 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/

#include "../include/filesystem.h"
#include "../include/utils.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <ctype.h>

#ifdef CLOVE_WINDOWS
#include <direct.h>
#define getcwd _getcwd // apparently getcwd is dreprecated on windows
#include <io.h>
#define access _access
#include <windows.h>
#else
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif


static struct {
    const char* os;
    /*
     * used when use_physfs is 'true' to
     * determine the mouting point
     */
    const char* source;
    /**
     * @brief hasIdentitySet most physfs features do not work
     * until you set the identity folder, we have to check for that
     * before executing a function
     */
    bool hasIdentitySet;
} moduleData;

void filesystem_init(char* argv0, bool stats) {
#ifdef CLOVE_MACOSX
    moduleData.os = "osx";
#elif CLOVE_LINUX
    moduleData.os = "linux";
#elif CLOVE_WINDOWS
    moduleData.os = "windows";
#elif CLOVE_ANDROID
    moduleData.os = "android";
#elif CLOVE_WEB
    moduleData.os = "web";
#else
    moduleData.os = "This OS is not supported";
#endif

    moduleData.source = NULL;
#ifdef USE_PHYSFS
    if (!PHYSFS_init(argv0))
        clove_error(PHYSFS_getLastError());
	PHYSFS_mount("./", NULL, 1);

#endif
    moduleData.hasIdentitySet = false;
}

void filesystem_free()
{
#ifdef USE_PHYSFS
    if (PHYSFS_isInit())
        PHYSFS_deinit();
#endif
}

const char* filesystem_getOS() {
    return moduleData.os;
}

const char* filesystem_getSaveDirectory(const char* company, const char* projName) {
    return SDL_GetPrefPath(company, projName);
}

bool filesystem_contain(const char* a, const char* b) {
    return strstr(a,b) != NULL;
}
/*
 * This functions checks to see if two strings are the same
 * If you pass a number to argument 'l' then clove will
 * dheck for equality untill the point of 'l' value
 */
bool filesystem_equals(const char* a, const char* b, int l) {
    if (l > 0) {
        if(strncmp(a, b, l) == 0)
            return true;
    } else {
        if (strcmp(a, b) == 0)
            return true;
    }
    return false;
}

bool filesystem_exists(const char* name)
{
#ifndef USE_PHYSFS
    FILE* file = fopen(name,"r");
    if(!file){
        return 0;
    }

    fclose(file);
    return 1;

#else
    return PHYSFS_exists(name);
#endif

}

bool filesystem_getInfo(const char* path, struct FileInfo *info)
{
	#ifndef USE_PHYSFS
	clove_error("filesystem:isFile is supported just with PHYSFS");
	return 0;
	#endif
    if (!PHYSFS_isInit()) {
        clove_error("PHYSFS is not initialized");
        return false;
    }
	PHYSFS_Stat stat = {};
	if (!PHYSFS_stat(path, &stat)) {
		clove_error("Couldn't fetch information about path %s\n", path);
        return false;
	}
	info->modtime = stat.modtime;
	info->accesstime = stat.accesstime;
	info->createtime = stat.createtime;
	info->size = stat.filesize;

	if (stat.filetype == PHYSFS_FILETYPE_REGULAR) {
		info->type = FileType_REGULAR;
	} else if (stat.filetype == PHYSFS_FILETYPE_SYMLINK) {
		info->type = FileType_DIRECTORY;
	} else if (stat.filetype == PHYSFS_FILETYPE_DIRECTORY) {
		info->type = FileType_DIRECTORY;
	} else {
		info->type = FileType_OTHER;
	}
    return true;
}

int filesystem_write(const char* name, const char* data)
{

#ifdef USE_PHYSFS
    PHYSFS_File* file;
    file = PHYSFS_openWrite(name);
    if(! file) {
        clove_error(PHYSFS_getLastError());
        return -1;
    }

    int write_size = PHYSFS_write(file, data, sizeof(char), strlen(data));
    if (write_size == -1) {
        clove_error(PHYSFS_getLastError());
        return -2;
    }

    PHYSFS_close(file);

    return write_size;

#else
    FILE* file = fopen(name, "w");
    if(!file){
        clove_error("Couldn't open filename %s\n", name);
        return -1;
    }

    fseek(file,0,SEEK_END);
    long size = ftell(file);
    rewind(file);

    int wrote = fprintf(file, data);
    if (wrote < 0) {
        fclose(file);
        clove_error("Couldn't write to filename %s\n", name);
        return -2;
    }
    fclose(file);

    return size;
#endif
}

int filesystem_append(const char* name, const char* data) {

#ifdef USE_PHYSFS

    PHYSFS_File* file;
    file = PHYSFS_openAppend(name);
    if(! file) {
        clove_error(PHYSFS_getLastError());
        return -1;
    }
    int append_size = PHYSFS_write(file, data, sizeof(char), strlen(data));
    if (append_size == -1) {
        PHYSFS_close(file);
        clove_error(PHYSFS_getLastError());
        return -2;
    }
    PHYSFS_close(file);

    return append_size;

#else

    FILE* file = fopen(name, "a");
    if (!file) {
        clove_error("Couldn't open file %s for appending\n", name);
        return -1;
    }

    fseek(file,0,SEEK_END);
    long size = ftell(file);
    rewind(file);

    int appended = fprintf(file, data);
    if (appended < 0) {
        clove_error("Couldn't write to file %s for appending\n", name);
        fclose(file);
        return -2;
    }
    fclose(file);

    return size;
#endif

}

const char* filesystem_getWorkingDirectory(void) {
    /* The process's own cwd -- which is where CLove loaded main.fh from, and
     * so where a game's relative paths resolve. Note that this is *not* what
     * filesystem_getCurrentDirectory() below returns: that one has always
     * answered PhysFS's base directory, the directory the executable lives in,
     * despite the name. */
    static char buffer[4096];
    if (getcwd(buffer, sizeof(buffer)) == NULL) { return NULL; }
    return buffer;
}

const char* filesystem_getCurrentDirectory() {
/*
#ifndef CLOVE_WEB
    char buffer[1024];
    const char* dir = getcwd(buffer, sizeof(buffer));
    if (dir != NULL)
        return dir;
#endif
    clove_error("Error, Could not get the current directory \n");
*/
    return PHYSFS_getBaseDir();
}

/**
 * mode can be:
 * 0, wether or not the file exist
 * 2, write only
 * 4, read only
 * 6, read and write
 * default is 0
 */
bool filesystem_state(const char* file, int mode) {
    //TODO Look into this
#ifndef CLOVE_WEB
    if (access(file, mode) != -1)
        return true;
#endif
    return false;
}

int filesystem_read(char const* filename, char** output) {

#ifdef USE_PHYSFS

    PHYSFS_File* file;
    file = PHYSFS_openRead(filename);
    if (!file)
        return -1;
    PHYSFS_sint64 size = PHYSFS_fileLength(file);

    *output = malloc(size + 1);

    /*int len_read = */PHYSFS_read(file, *output, 1, size);
    (*output)[size] = '\n';

    PHYSFS_close(file);

    return size;

#else
    FILE* infile = fopen(filename, "r");
    if(!infile) {
        return -1;
    }

    fseek(infile, 0, SEEK_END);
    long size = ftell(infile);
    rewind(infile);

    *output = malloc(size+1);
    fread(*output, size, 1, infile);
    fclose(infile);
    (*output)[size] = 0;

    return size;
#endif
}


bool filesystem_mkDir(const char* path)
{
#ifdef USE_PHYSFS
    return PHYSFS_mkdir(path) != 0;
#else
    /*
#ifdef CLOVE_UNIX
    int result = mkdir(path, 0755);
    if (result == 0) return true;
    if (result == -1) {
        clove_error("Couldn't make new directory %s\n", path);
        return false;
    }
#endif
#ifdef CLOVE_WINDOWS
    bool rez = CreateDirectory(path, NULL);
    if (!rez) {
        clove_error("Couldn't make new directory %s\n", path);
    }
    return rez;
#endif*/
    return false;
#endif
}

bool filesystem_isDir(const char* dir)
{
#ifdef USE_PHYSFS
    return PHYSFS_isDirectory(dir);
#else

    clove_error("isDir feature is supported by enabling physfs.");
    return false;
#endif
}

bool filesystem_isSymLink(const char* name)
{
#ifdef USE_PHYSFS
    return PHYSFS_isSymbolicLink(name) != 0;
#else

    clove_error("isDir feature is supported by enabling physfs.");
    return false;
#endif
}


void filesystem_setSource(const char* source)
{
#ifdef USE_PHYSFS
    if (! PHYSFS_mount(moduleData.source != NULL ? moduleData.source : SDL_GetBasePath(), source, 1))
    {
        clove_error("couldn't mount file:");
        clove_error(source);

    }
#endif
    moduleData.source = source;
}

const char* filesystem_getSource() {
    return moduleData.source != NULL ? moduleData.source : SDL_GetBasePath();
}

bool filesystem_setIdentity(const char* name)
{
#ifdef USE_PHYSFS
    const char* save_dir = filesystem_getUsrDir();

    if (! PHYSFS_setWriteDir(save_dir)) {
        clove_error("Error in fileystem set identity %s", PHYSFS_getLastError());
        return false;
    }

    if (! filesystem_mkDir(name)) {
        PHYSFS_setWriteDir(NULL);
        clove_error("Error in fileystem set identity, cannot mkdir, %s",PHYSFS_getLastError());
        return false;
    }

    if (! PHYSFS_setWriteDir(name)) {
        clove_error("Error in fileystem set identity, cannot set write dir, %s",PHYSFS_getLastError());
        return false;
    }

    if (! PHYSFS_mount(name, NULL, 0)) {
        PHYSFS_setWriteDir(NULL);
        clove_error("Error in fileystem set identity,cannot mount, %s", PHYSFS_getLastError());
        return false;
    }
    moduleData.hasIdentitySet = true;
    return true;
#else
    clove_error("identity feature is supported by enabling physfs.");
    return false;
#endif
}

bool filesystem_mount(const char* path, const char* mountPoint, int appendToPath)
{

#ifdef USE_PHYSFS
    return PHYSFS_mount(path, mountPoint, appendToPath) != 0;
#else
    clove_error("mouting feature is supported by enabling physfs.");
    return false;
#endif
}

bool filesystem_unmount(const char* path)
{
#ifdef USE_PHYSFS
    const char* getMountPoint = PHYSFS_getMountPoint(path);
    if (!getMountPoint) {
        clove_error("no mouting point for: %s\n", path);
        return false;
    }
    return PHYSFS_removeFromSearchPath(path) != 0;
#else
    clove_error("unmouting feature is supported by enabling physfs.");
    return false;
#endif
}

char** filesystem_enumerate(const char* path)
{
#ifdef USE_PHYSFS
    if (!moduleData.hasIdentitySet)
        return NULL;
    return PHYSFS_enumerateFiles(path);
#else
    clove_error("enumerate feature is supported by enabling physfs.");
    return NULL;
#endif
}

void filesystem_freeEnumerate(char** list)
{
    if (!list) { return; }
#ifdef USE_PHYSFS
    PHYSFS_freeList(list);
#endif
}

/* --- real directory listing ------------------------------------------------
 *
 * PhysFS only enumerates what has been mounted, so a file picker that is meant
 * to browse the machine cannot use filesystem_enumerate(). This walks the
 * actual filesystem with readdir()/FindFirstFile().
 */

static int dir_entry_cmp(const void *a, const void *b) {
    const struct DirEntry *x = a;
    const struct DirEntry *y = b;
    /* directories first, so a picker's list reads the way a file manager does */
    if (x->is_dir != y->is_dir) { return x->is_dir ? -1 : 1; }
    {
        const char *p = x->name;
        const char *q = y->name;
        while (*p && *q) {
            int cp = tolower((unsigned char) *p);
            int cq = tolower((unsigned char) *q);
            if (cp != cq) { return cp < cq ? -1 : 1; }
            p++;
            q++;
        }
        if (*p == *q) { return 0; }
        return *p ? 1 : -1;
    }
}

static bool dir_entry_push(struct DirEntry **list, int *count, int *cap,
                           const char *name, bool is_dir) {
    if (*count == *cap) {
        int grown = *cap ? *cap * 2 : 32;
        struct DirEntry *bigger = realloc(*list, (size_t) grown * sizeof(struct DirEntry));
        if (!bigger) { return false; }
        *list = bigger;
        *cap = grown;
    }

    size_t len = strlen(name);
    char *copy = malloc(len + 1);
    if (!copy) { return false; }
    memcpy(copy, name, len + 1);

    (*list)[*count].name = copy;
    (*list)[*count].is_dir = is_dir;
    (*count)++;
    return true;
}

void filesystem_freeDirectoryList(struct DirEntry* entries, int count) {
    if (!entries) { return; }
    for (int i = 0; i < count; i++) { free(entries[i].name); }
    free(entries);
}

struct DirEntry* filesystem_listDirectory(const char* path, int* count) {
    if (count) { *count = 0; }
    if (!path || !count) { return NULL; }

    struct DirEntry *list = NULL;
    int n = 0;
    int cap = 0;
    bool ok = true;

#ifdef CLOVE_WINDOWS
    size_t len = strlen(path);
    char *pattern = malloc(len + 3);
    if (!pattern) { return NULL; }
    memcpy(pattern, path, len);
    /* "C:\\dir" -> "C:\\dir\\*", "C:\\" -> "C:\\*" */
    if (len > 0 && path[len - 1] != '\\' && path[len - 1] != '/') {
        pattern[len++] = '\\';
    }
    pattern[len++] = '*';
    pattern[len] = '\0';

    WIN32_FIND_DATAA found;
    HANDLE handle = FindFirstFileA(pattern, &found);
    free(pattern);
    if (handle == INVALID_HANDLE_VALUE) { return NULL; }

    do {
        if (strcmp(found.cFileName, ".") == 0 || strcmp(found.cFileName, "..") == 0) {
            continue;
        }
        bool is_dir = (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (!dir_entry_push(&list, &n, &cap, found.cFileName, is_dir)) {
            ok = false;
            break;
        }
    } while (FindNextFileA(handle, &found));
    FindClose(handle);
#else
    DIR *dir = opendir(path);
    if (!dir) { return NULL; }

    size_t base_len = strlen(path);
    bool needs_slash = base_len > 0 && path[base_len - 1] != '/';

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        /* d_type is not filled in on every filesystem, and it says "symlink"
         * rather than what the link points at -- stat() settles both. */
        bool is_dir = false;
        size_t name_len = strlen(entry->d_name);
        char *full = malloc(base_len + needs_slash + name_len + 1);
        if (!full) { ok = false; break; }
        memcpy(full, path, base_len);
        if (needs_slash) { full[base_len] = '/'; }
        memcpy(full + base_len + needs_slash, entry->d_name, name_len + 1);

        struct stat st;
        if (stat(full, &st) == 0) { is_dir = S_ISDIR(st.st_mode); }
        free(full);

        if (!dir_entry_push(&list, &n, &cap, entry->d_name, is_dir)) {
            ok = false;
            break;
        }
    }
    closedir(dir);
#endif

    if (!ok) {
        filesystem_freeDirectoryList(list, n);
        return NULL;
    }

    if (n > 1) { qsort(list, (size_t) n, sizeof(struct DirEntry), dir_entry_cmp); }
    *count = n;
    return list;
}

const char* filesystem_getHomeDirectory(void) {
#ifdef CLOVE_WINDOWS
    const char *home = getenv("USERPROFILE");
    if (home) { return home; }
    return getenv("HOMEPATH");
#else
    return getenv("HOME");
#endif
}

const char* filesystem_getUsrDir()
{
#ifdef USE_PHYSFS
    return PHYSFS_getUserDir();
#else
    clove_error("getUsrDir feature is supported by enabling physfs.");
    return NULL;
#endif
}

bool filesystem_remove(const char* name) {
    return remove(name);
}

bool filesystem_rename(const char *old_name, const char *new_name) {
    return rename(old_name, new_name);
}

