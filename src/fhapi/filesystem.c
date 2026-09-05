/*
#   clove
#
#   Copyright (C) 2019 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/
#include "filesystem.h"

#include "../3rdparty/FH/src/value.h"

#include "../include/filesystem.h"

static int fn_love_filesystem_read(struct fh_program *prog,
                                   struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_filesystem_read(): expected 1 argument, got %d", n_args);
    if (!fh_is_string(&args[0]))
        return fh_set_error(prog, "Illegal parameter, expected filename:string");

    const char *filename = fh_get_string(&args[0]);
    char *data = NULL;
    int len = filesystem_read(filename, &data);
    if (len < -1) {
        return fh_set_error(prog, "Could not open file %s\n", filename);
    }

    *ret = fh_new_string(prog, data);
    free(data);
    return 0;
}

static int fn_love_filesystem_write(struct fh_program *prog,
                                    struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 2)
        return fh_set_error(prog, "love_filesystem_write(): expected 2 arguments, got %d", n_args);
    if (!fh_is_string(&args[0]) || !fh_is_string(&args[1]))
        return fh_set_error(prog, "Illegal parameter, expected filename:string and data:string");

    const char *filename = fh_get_string(&args[0]);
    const char *data = fh_get_string(&args[1]);

    int wrote_data = filesystem_write(filename, data);
    if (wrote_data < 0) {
        //Note: error will be issued in filesystem_write, no need to respecify it here.
        return -1;
    }

    *ret = fh_new_number(wrote_data);
    return 0;
}

static int fn_love_filesystem_append(struct fh_program *prog,
                                     struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 2)
        return fh_set_error(prog, "love_filesystem_append(): expected 2 arguments, got %d", n_args);
    if (!fh_is_string(&args[0]) || !fh_is_string(&args[1]))
        return fh_set_error(prog, "Illegal parameter, expected filename:string and data:string");

    const char *filename = fh_get_string(&args[0]);
    const char *data = fh_get_string(&args[1]);
    int appended_data = filesystem_append(filename, data);
    if (appended_data < 0) {
        //Note: error will be issued in filesystem_write, no need to respecify it here.
        return -1;
    }

    *ret = fh_new_number(appended_data);
    return 0;
}

static int fn_love_filesystem_getCurrentDirectory(struct fh_program *prog, struct fh_value *ret, struct fh_value *args, int n_args) {
	*ret = fh_new_string(prog,filesystem_getCurrentDirectory());
	return 0;
}

static int fn_love_filesystem_getSaveDirectory(struct fh_program *prog, struct fh_value *ret, struct fh_value *args, int n_args) {
    const char *company = fh_optstring(args, n_args, 0, "CLove");
    const char *projName = fh_optstring(args, n_args, 1, "myGame");

    const char *path = filesystem_getSaveDirectory(company, projName);
    *ret = fh_new_string(prog, path);
    SDL_free((char*)path);
    return 0;
}

static int fn_love_filesystem_getInfo(struct fh_program *prog,
                                     struct fh_value *ret, struct fh_value *args, int n_args) {

	if (n_args != 1)
		return fh_set_error(prog, "love_filesystem_getInfo(): expected 1 argument, got %d", n_args);
	if (!fh_is_string(&args[0])) {
		return fh_set_error(prog, "Expected path");
	}

	const char* path = fh_get_string(&args[0]);
	struct FileInfo info;

	bool rez = filesystem_getInfo(path, &info);

	if (!rez) {
		*ret = fh_new_bool(false);
		return 0;
	}

	int pin_state = fh_get_pin_state(prog);
	struct fh_array *ret_arr = fh_make_array(prog, true);
	fh_grow_array_object(prog, ret_arr, 5);

	struct fh_value new_val = fh_new_array(prog);

	const char* type = "other";
	if (info.type == FileType_REGULAR) {
		type = "file";
	} else if (info.type == FileType_DIRECTORY) {
		type = "directory";
	} else if (info.type == PHYSFS_FILETYPE_SYMLINK) {
		type = "symlink";
	}

	ret_arr->items[0] = fh_new_string(prog, type);
	ret_arr->items[1] = fh_new_number(info.size);
	ret_arr->items[2] = fh_new_number(info.modtime);
	ret_arr->items[3] = fh_new_number(info.accesstime);
	ret_arr->items[4] = fh_new_number(info.createtime);

    new_val.data.obj = ret_arr;
    fh_restore_pin_state(prog, pin_state);
    *ret = new_val;

	return 0;
}

static int fn_love_filesystem_exists(struct fh_program *prog,
                                     struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_filesystem_exists(): expected 1 argument, got %d", n_args);
    if (!fh_is_string(&args[0]))
        return fh_set_error(prog, "Illegal parameter, expected filename:string");
    const char *filename = fh_get_string(&args[0]);

    *ret = fh_new_bool(filesystem_exists(filename));
    return 0;
}

static int fn_love_filesystem_setSource(struct fh_program *prog,
                                        struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_filesystem_setSource(): expected 1 argument, got %d", n_args);
    if (!fh_is_string(&args[0]))
        return fh_set_error(prog, "Illegal parameter, expected source:string");
    const char *source = fh_get_string(&args[0]);
    filesystem_setSource(source);
    *ret = fh_make_null();
    return 0;
}

static int fn_love_filesystem_getSource(struct fh_program *prog,
                                        struct fh_value *ret, struct fh_value *args, int n_args) {
    const char *source = filesystem_getSource();
    *ret = fh_new_string(prog, source);
    free((char*)source);
    return 0;
}

static int fn_love_filesystem_remove(struct fh_program *prog,
                                     struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_filesystem_remove(): expected 1 argument, got %d", n_args);
    if (!fh_is_string(&args[0]))
        return fh_set_error(prog, "Illegal parameter, expected filename:string");
    const char *filename = fh_get_string(&args[0]);

    *ret = fh_new_bool(filesystem_remove(filename));
    return 0;
}

static int fn_love_filesystem_rename(struct fh_program *prog,
                                     struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 2)
        return fh_set_error(prog, "love_filesystem_rename(): expected 2 arguments, got %d", n_args);
    if (!fh_is_string(&args[0]) || !fh_is_string(&args[1]))
        return fh_set_error(prog, "Illegal parameter, expected old_filename:string and new_filename:string");
    const char *old_filename = fh_get_string(&args[0]);
    const char *new_filename = fh_get_string(&args[1]);

    *ret = fh_new_bool(filesystem_rename(old_filename, new_filename));
    return 0;
}

static int fn_love_filesystem_state(struct fh_program *prog,
                                    struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args < 1)
        return fh_set_error(prog, "love_filesystem_state(): expected at least 1 argument, got %d", n_args);
    if (!fh_is_string(&args[0]))
        return fh_set_error(prog, "Illegal parameter, expected filename:string");
    const char *filename = fh_get_string(&args[0]);
    const char *smode = fh_optstring(args, n_args, 1, "e");

    int mode = 0;

    if (strcmp(smode, "e") == 0) {
        mode = 0;
    } else if (strcmp(smode, "x") == 0) {
        mode = 1;
    } else if (strcmp(smode, "w") == 0) {
        mode = 2;
    } else if (strcmp(smode, "r") == 0) {
        mode = 4;
    } else if (strcmp(smode, "wr") == 0 || strcmp(smode, "rw") == 0) {
        mode = 6;
    }

    *ret = fh_new_bool(filesystem_state(filename, mode));
    return 0;
}

static int fn_love_filesystem_isSymLink(struct fh_program *prog,
                                        struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_filesystem_isSymLink(): expected 1 argument, got %d", n_args);
    if (!fh_is_string(&args[0]))
        return fh_set_error(prog, "Illegal parameter, expected filename:string");
    const char *filename = fh_get_string(&args[0]);

    *ret = fh_new_bool(filesystem_isSymLink(filename));
    return 0;
}

static int fn_love_filesystem_mkDir(struct fh_program *prog,
                                    struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_filesystem_mkDir(): expected 1 argument, got %d", n_args);
    if (!fh_is_string(&args[0]))
        return fh_set_error(prog, "Illegal parameter, expected name:string");
    const char *name = fh_get_string(&args[0]);

    *ret = fh_new_bool(filesystem_mkDir(name));
    return 0;
}

static int fn_love_filesystem_isDir(struct fh_program *prog,
                                    struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_filesystem_isDir(): expected 1 argument, got %d", n_args);
    if (!fh_is_string(&args[0]))
        return fh_set_error(prog, "Illegal parameter, expected name:string");
    const char *name = fh_get_string(&args[0]);

    *ret = fh_new_bool(filesystem_isDir(name));
    return 0;
}

static int fn_love_filesystem_getUsrDir(struct fh_program *prog,
                                        struct fh_value *ret, struct fh_value *args, int n_args) {

    *ret = fh_new_string(prog, filesystem_getUsrDir());
    return 0;
}

static int fn_love_filesystem_setIdentity(struct fh_program *prog,
                                          struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_filesystem_setIdentity(): expected 1 argument, got %d", n_args);
    if (!fh_is_string(&args[0]))
        return fh_set_error(prog, "Illegal parameter, expected name:string");
    const char *name = fh_get_string(&args[0]);

    *ret = fh_new_bool(filesystem_setIdentity(name));
    return 0;
}

static int fn_love_filesystem_enumerate(struct fh_program *prog,
                                        struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_filesystem_enumerate(): expected 1 argument, got %d", n_args);
    if (!fh_is_string(&args[0]))
        return fh_set_error(prog, "Illegal parameter, expected name:string");
    const char *path = fh_get_string(&args[0]);

    char **rez = filesystem_enumerate(path);
    if (rez == NULL)
        return fh_set_error(prog, "Couldn't get files in directory %s, perhaps you didn't call setIdentity first?\n", path);

    /* PhysFS hands back a NULL-terminated list of names. The count used to be
     * taken as strlen() of the *first name*, which then indexed past the end
     * of the list; and the list was never given back to PhysFS. */
    int rez_size = 0;
    while (rez[rez_size] != NULL) { rez_size++; }

    int pin_state = fh_get_pin_state(prog);
    struct fh_array *ret_arr = fh_make_array(prog, true);
    fh_grow_array_object(prog, ret_arr, rez_size);

    struct fh_value new_val = fh_new_array(prog);
    for (int i = 0; i < rez_size; i++) {
        ret_arr->items[i] = fh_new_string(prog, rez[i]);
    }

    new_val.data.obj = ret_arr;
    *ret = new_val;
    fh_restore_pin_state(prog, pin_state);
    filesystem_freeEnumerate(rez);
    return 0;
}

/* love_filesystem_list(path) -> [{ "name": string, "dir": bool }, ...]
 *
 * A listing of a real directory, sorted with the directories first. Unlike
 * love_filesystem_enumerate() this does not go through PhysFS, so it sees the
 * whole machine and takes absolute paths -- it is what a file picker needs.
 * "." and ".." are left out. Errors (no such directory, no permission) come
 * back as null rather than as a script error, so a browser can just show the
 * folder as empty and let the user walk back out. */
static int fn_love_filesystem_list(struct fh_program *prog,
                                   struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_filesystem_list(): expected 1 argument, got %d", n_args);
    if (!fh_is_string(&args[0]))
        return fh_set_error(prog, "love_filesystem_list(): expected the directory path as a string");

    int count = 0;
    struct DirEntry *entries = filesystem_listDirectory(fh_get_string(&args[0]), &count);
    if (entries == NULL && count == 0) {
        *ret = fh_new_null();
        return 0;
    }

    int pin_state = fh_get_pin_state(prog);
    struct fh_array *arr = fh_make_array(prog, true);
    fh_grow_array_object(prog, arr, count);

    for (int i = 0; i < count; i++) {
        struct fh_value entry = fh_new_map(prog);
        struct fh_value key_name = fh_new_string(prog, "name");
        struct fh_value key_dir = fh_new_string(prog, "dir");
        struct fh_value name = fh_new_string(prog, entries[i].name);
        struct fh_value is_dir = fh_new_bool(entries[i].is_dir);
        fh_add_map_entry(prog, &entry, &key_name, &name);
        fh_add_map_entry(prog, &entry, &key_dir, &is_dir);
        arr->items[i] = entry;
    }

    struct fh_value out = fh_new_array(prog);
    out.data.obj = arr;
    *ret = out;
    fh_restore_pin_state(prog, pin_state);

    filesystem_freeDirectoryList(entries, count);
    return 0;
}

/* love_filesystem_getWorkingDirectory() -> string or null
 *
 * The directory the process is running in, which for CLove is the one main.fh
 * was loaded from -- the base a game's relative paths are resolved against.
 * love_filesystem_getCurrentDirectory() is a different thing despite the name:
 * it answers PhysFS's base directory, where the executable lives. */
static int fn_love_filesystem_getWorkingDirectory(struct fh_program *prog,
                                                  struct fh_value *ret, struct fh_value *args, int n_args) {
    (void) args;
    if (n_args != 0)
        return fh_set_error(prog, "love_filesystem_getWorkingDirectory(): expected no arguments, got %d", n_args);

    const char *cwd = filesystem_getWorkingDirectory();
    *ret = cwd ? fh_new_string(prog, cwd) : fh_new_null();
    return 0;
}

/* love_filesystem_getHomeDirectory() -> string or null */
static int fn_love_filesystem_getHomeDirectory(struct fh_program *prog,
                                               struct fh_value *ret, struct fh_value *args, int n_args) {
    (void) args;
    if (n_args != 0)
        return fh_set_error(prog, "love_filesystem_getHomeDirectory(): expected no arguments, got %d", n_args);

    const char *home = filesystem_getHomeDirectory();
    *ret = home ? fh_new_string(prog, home) : fh_new_null();
    return 0;
}

#define DEF_FN(name) { #name, fn_##name }
static const struct fh_named_c_func c_funcs[] = {
    DEF_FN(love_filesystem_read),
    DEF_FN(love_filesystem_write),
    DEF_FN(love_filesystem_append),
    DEF_FN(love_filesystem_getSaveDirectory),
	DEF_FN(love_filesystem_getInfo),
    DEF_FN(love_filesystem_exists),
    DEF_FN(love_filesystem_setSource),
    DEF_FN(love_filesystem_getSource),
    DEF_FN(love_filesystem_remove),
    DEF_FN(love_filesystem_rename),
    DEF_FN(love_filesystem_state),
    DEF_FN(love_filesystem_isSymLink),
    DEF_FN(love_filesystem_mkDir),
    DEF_FN(love_filesystem_isDir),
    DEF_FN(love_filesystem_getUsrDir),
    DEF_FN(love_filesystem_setIdentity),
    DEF_FN(love_filesystem_enumerate),
    DEF_FN(love_filesystem_list),
    DEF_FN(love_filesystem_getHomeDirectory),
    DEF_FN(love_filesystem_getWorkingDirectory),
	DEF_FN(love_filesystem_getCurrentDirectory),
};

void fh_filesystem_register(struct fh_program *prog) {
    fh_add_c_funcs(prog, c_funcs, sizeof(c_funcs)/sizeof(c_funcs[0]));
}
