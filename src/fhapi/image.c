/*
#   clove
#
#   Copyright (C) 2019 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/
#include "image.h"

#include <stdlib.h>

#include "../include/svg.h"

#include "../3rdparty/FH/src/value.h"

static fh_c_obj_gc_callback onFreeCallback(fh_image_t *data) {
    graphics_Image_free(data->img);
    // data->data is NULL after newImage() adopts an existing ImageData
    // c_obj (ownership moves here - see below); nothing left to free then.
    if (data->data) {
        image_ImageData_free(data->data);
        free(data->data);
    }
    free(data->img);
    free(data);
    return (fh_c_obj_gc_callback)1;
}

static fh_c_obj_gc_callback imageDataFreeCallback(image_ImageData *data) {
    // NULL after newImage() adopts this handle (see below) and transfers
    // ownership to the new Image's own free callback.
    if (data) {
        image_ImageData_free(data);
        free(data);
    }
    return (fh_c_obj_gc_callback)1;
}

static int fn_love_graphics_newImageData(struct fh_program *prog,
                                         struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args == 0)
        return fh_set_error(prog, "Expected width & height");

    if (!fh_is_number(&args[0]) || !fh_is_number(&args[1]))
        return fh_set_error(prog, "Expected width & height");

    image_ImageData* data = malloc(sizeof(image_ImageData));

    image_ImageData_new_with_size(data, (int) fh_get_number(&args[0]),
            (int) fh_get_number(&args[1]), 4);
    fh_c_obj_gc_callback *callback = imageDataFreeCallback;
    *ret = fh_new_c_obj(prog, data, callback, FH_IMAGE_DATA_TYPE);
    return 0;
}

static int fn_love_graphics_newImage(struct fh_program *prog,
                                     struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args == 0)
        return fh_set_error(prog, "Expected path");

    // Optional rasterization scale, only meaningful for vector art (.svg):
    // 1.0 is the size the drawing was authored at. Left out, vector art starts
    // at its authored size and is re-rasterized on its own as it is drawn
    // bigger; given here, it stays pinned at that scale.
    double scale = 0.0;
    if (n_args > 1) {
        if (!fh_is_number(&args[1]))
            return fh_set_error(prog, "love_graphics_newImage(): expected a number as second argument (vector scale)");
        scale = fh_get_number(&args[1]);
    }

    fh_image_t *img = malloc(sizeof(fh_image_t));
    img->img = malloc(sizeof(graphics_Image));

    if (fh_is_string(&args[0])) {
        const char *path = fh_get_string(&args[0]);

        img->data = malloc(sizeof(image_ImageData));
        if (svg_isVectorPath(path) && scale > 0.0)
            image_ImageData_new_with_svg(img->data, path, (float) scale);
        else
            image_ImageData_new_with_filename(img->data, path);
        if (!img->data->surface) {
            // Load failed (missing/corrupt file) - img->data->pixels is
            // NULL at this point; proceeding into
            // graphics_Image_new_with_ImageData() would dereference it.
            free(img->data);
            free(img->img);
            free(img);
            return fh_set_error(prog, "Could not load image: %s", path);
        }
        graphics_Image_new_with_ImageData(img->img, img->data);

        // Pins the scale so the image keeps the resolution that was asked for
        // instead of following what it is drawn at.
        if (scale > 0.0 && graphics_Image_isVector(img->img))
            graphics_Image_setVectorScale(img->img, (float) scale);
    } else if(fh_is_c_obj_of_type(&args[0], FH_IMAGE_DATA_TYPE)) {
        image_ImageData *data = fh_get_c_obj_value(&args[0]);
        img->data = data;
        graphics_Image_new_with_ImageData(img->img, data);
        // Ownership of `data` moves to the new Image (onFreeCallback above
        // frees img->data). Null out the source c_obj's pointer so its own
        // free callback (imageDataFreeCallback) becomes a no-op instead of
        // double-freeing the same image_ImageData when it's GC'd too.
        fh_get_c_obj(&args[0])->ptr = NULL;
    } else {
        // img->data was never set on this path - nothing to free but the
        // two allocations made above.
        free(img->img);
        free(img);
        return fh_set_error(prog, "Expected image data or path to image");
    }

    fh_c_obj_gc_callback *onFree = onFreeCallback;
    *ret = fh_new_c_obj(prog, img, onFree, FH_IMAGE_TYPE);
    return 0;
}

static int fn_love_image_getWidth(struct fh_program *prog,
                                  struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_image_getWidth(): expected 1 argument, got %d", n_args);

    if (fh_is_c_obj_of_type(&args[0], FH_IMAGE_TYPE)) {
        // img->img->width, not img->data->w: for vector art the two differ,
        // the texture being whatever resolution it was last rasterized at.
        fh_image_t *img = fh_get_c_obj_value(&args[0]);
        *ret = fh_new_number(img->img->width);
    } else if (fh_is_c_obj_of_type(&args[0], FH_IMAGE_DATA_TYPE)) {
        image_ImageData *data = fh_get_c_obj_value(&args[0]);
        *ret = fh_new_number(image_ImageData_getWidth(data));
    } else
        return fh_set_error(prog, "Expected image");

    return 0;
}

static int fn_love_image_getHeight(struct fh_program *prog,
                                   struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_image_getHeight(): expected 1 argument, got %d", n_args);

    if (fh_is_c_obj_of_type(&args[0], FH_IMAGE_TYPE)) {
        fh_image_t *img = fh_get_c_obj_value(&args[0]);
        *ret = fh_new_number(img->img->height);
    } else if (fh_is_c_obj_of_type(&args[0], FH_IMAGE_DATA_TYPE)) {
        image_ImageData *data = fh_get_c_obj_value(&args[0]);
        *ret = fh_new_number(image_ImageData_getHeight(data));
    } else
        return fh_set_error(prog, "Expected image");
    return 0;
}

static int fn_love_image_getDimensions(struct fh_program *prog,
                                       struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_image_getDimensions(): expected 1 argument, got %d", n_args);

    if (!fh_is_c_obj_of_type(&args[0], FH_IMAGE_TYPE))
        return fh_set_error(prog, "Expected image");

    fh_image_t *img = fh_get_c_obj_value(&args[0]);

    int pin_state = fh_get_pin_state(prog);
    struct fh_array *arr = fh_make_array(prog, true);
    if (!fh_grow_array_object(prog, arr, 2))
        return fh_set_error(prog, "out of memory");

    struct fh_value new_val = fh_new_array(prog);

    arr->items[0] = fh_new_number(img->img->width);
    arr->items[1] = fh_new_number(img->img->height);

    fh_restore_pin_state(prog, pin_state);
    new_val.data.obj = arr;;
    *ret = new_val;

    return 0;
}

static int fn_love_image_getPath(struct fh_program *prog,
                                 struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_image_getPath(): expected 1 argument, got %d", n_args);

    if (fh_is_c_obj_of_type(&args[0], FH_IMAGE_TYPE)) {
        fh_image_t *img = fh_get_c_obj_value(&args[0]);
        *ret = fh_new_string(prog, image_ImageData_getPath(img->data));
    } else if (fh_is_c_obj_of_type(&args[0], FH_IMAGE_DATA_TYPE)) {
        image_ImageData *data = fh_get_c_obj_value(&args[0]);
        *ret = fh_new_string(prog, image_ImageData_getPath(data));
    } else
        return fh_set_error(prog, "Expected image");

    return 0;
}

static int fn_love_image_getChannels(struct fh_program *prog,
                                     struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_image_getChannels(): expected 1 argument, got %d", n_args);

    if (fh_is_c_obj_of_type(&args[0], FH_IMAGE_TYPE)) {
        fh_image_t *img = fh_get_c_obj_value(&args[0]);
        *ret = fh_new_number(image_ImageData_getChannels(img->data));
    } else if (fh_is_c_obj_of_type(&args[0], FH_IMAGE_DATA_TYPE)) {
        image_ImageData *data = fh_get_c_obj_value(&args[0]);
        *ret = fh_new_number(image_ImageData_getChannels(data));
    } else
        return fh_set_error(prog, "Expected image");
    return 0;
}

static int fn_love_image_refresh(struct fh_program *prog,
                                 struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 2)
        return fh_set_error(prog, "love_image_refresh(): expected 2 arguments, got %d", n_args);

    if (!fh_is_c_obj_of_type(&args[0], FH_IMAGE_TYPE))
        return fh_set_error(prog, "Expected image");
    if (!fh_is_c_obj_of_type(&args[1], FH_IMAGE_DATA_TYPE))
        return fh_set_error(prog, "Expected image data as second argument");

    // FH_IMAGE_DATA_TYPE c_objs wrap a raw image_ImageData* directly (see
    // newImageData() above), not an fh_image_t* - reading it as one (as
    // this used to) reinterprets unrelated struct bytes as a pointer.
    fh_image_t *img = fh_get_c_obj_value(&args[0]);
    image_ImageData *imgd = fh_get_c_obj_value(&args[1]);

    graphics_Image_refresh(img->img, imgd);
    *ret = fh_new_null();
    return 0;
}

static int fn_love_image_setPixel(struct fh_program *prog,
                                  struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 7)
        return fh_set_error(prog, "Expected 7 arguments (image, x, y, r, g, b, a)");

    if (!fh_is_c_obj_of_type(&args[0], FH_IMAGE_DATA_TYPE) &&
        !fh_is_c_obj_of_type(&args[0], FH_IMAGE_TYPE))
        return fh_set_error(prog, "Expected image");

    for (int i = 1; i <= 6; i++) {
        if (!fh_is_number(&args[i]))
            return fh_set_error(prog, "Expected number at index: %d", i);
    }

	int x = (int) fh_get_number(&args[1]);
	int y = (int) fh_get_number(&args[2]);

	pixel p;
	p.r = (unsigned char) fh_get_number(&args[3]);
	p.g = (unsigned char) fh_get_number(&args[4]);
	p.b = (unsigned char) fh_get_number(&args[5]);
	p.a = (unsigned char) fh_get_number(&args[6]);


	if (fh_is_c_obj_of_type(&args[0], FH_IMAGE_DATA_TYPE)) {
		image_ImageData *data = fh_get_c_obj_value(&args[0]);

		if (x < 0 || x >= data->w || y < 0 || y >= data->h)
			return fh_set_error(prog, "Index out of bounds %d %d", x, y);

		image_ImageData_setPixel(data, x, y, p);
	} else if (fh_is_c_obj_of_type(&args[0], FH_IMAGE_TYPE)) {
		fh_image_t *img = fh_get_c_obj_value(&args[0]);
		image_ImageData *data = img->data;

		if (x < 0 || x >= data->w || y < 0 || y >= data->h)
			return fh_set_error(prog, "Index out of bounds %d %d", x, y);

		image_ImageData_setPixel(data, x, y, p);
	}  else {
        return fh_set_error(prog, "Expected image");
	}

    *ret = fh_new_null();
    return 0;
}

static int fn_love_image_getPixel(struct fh_program *prog,
                                  struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 3)
        return fh_set_error(prog, "Expected 3 arguments (image, x, y)");

    for (int i = 1; i <= 2; i++) {
        if (!fh_is_number(&args[i]))
            return fh_set_error(prog, "Expected number at index: %d", i);
    }

	int x = (int) fh_get_number(&args[1]);
	int y = (int) fh_get_number(&args[2]);

	if (fh_is_c_obj_of_type(&args[0], FH_IMAGE_DATA_TYPE)) {
		image_ImageData *data = fh_get_c_obj_value(&args[0]);

		if (x < 0 || x >= data->w || y < 0 || y >= data->h)
			return fh_set_error(prog, "Index out of bounds %d %d", x, y);

		pixel p = image_ImageData_getPixel(data, x, y);

		int pin_state = fh_get_pin_state(prog);
		struct fh_array *arr= fh_make_array(prog, true);
		if (!fh_grow_array_object(prog, arr, 4))
			return fh_set_error(prog, "out of memory");

		struct fh_value new_val = fh_new_array(prog);

		arr->items[0] = fh_new_number(p.r);
		arr->items[1] = fh_new_number(p.g);
		arr->items[2] = fh_new_number(p.b);
		arr->items[3] = fh_new_number(p.a);

		new_val.data.obj = arr;

		*ret = new_val;
		fh_restore_pin_state(prog, pin_state);
	} else if (fh_is_c_obj_of_type(&args[0], FH_IMAGE_TYPE)) {
		fh_image_t *img = fh_get_c_obj_value(&args[0]);
		image_ImageData *data = img->data;

		if (x < 0 || x >= data->w || y < 0 || y >= data->h)
			return fh_set_error(prog, "Index out of bounds %d %d", x, y);

		pixel p = image_ImageData_getPixel(data, x, y);

		int pin_state = fh_get_pin_state(prog);
		struct fh_array *arr= fh_make_array(prog, true);
		if (!fh_grow_array_object(prog, arr, 4))
			return fh_set_error(prog, "out of memory");

		struct fh_value new_val = fh_new_array(prog);

		arr->items[0] = fh_new_number(p.r);
		arr->items[1] = fh_new_number(p.g);
		arr->items[2] = fh_new_number(p.b);
		arr->items[3] = fh_new_number(p.a);

		new_val.data.obj = arr;

		*ret = new_val;
		fh_restore_pin_state(prog, pin_state);
	} else {
		return fh_set_error(prog, "Expected image");
	}

    return 0;
}

static int fn_love_image_getWrap(struct fh_program *prog,
                                 struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_image_getWrap(): expected 1 argument, got %d", n_args);

    if (!fh_is_c_obj_of_type(&args[0], FH_IMAGE_TYPE))
        return fh_set_error(prog, "Expected image");

    fh_image_t *img = fh_get_c_obj_value(&args[0]);

    graphics_Wrap wrap;
    graphics_Image_getWrap(img->img, &wrap);

    int pin_state = fh_get_pin_state(prog);
    struct fh_array *arr= fh_make_array(prog, true);
    if (!fh_grow_array_object(prog, arr, 2))
        return fh_set_error(prog, "out of memory");

    struct fh_value new_val = fh_new_array(prog);

    if (wrap.horMode == graphics_WrapMode_clamp)
        arr->items[0] = fh_new_string(prog, "clamp");
    else if (wrap.horMode == graphics_WrapMode_repeat) {
        arr->items[0] = fh_new_string(prog, "repeat");
    }

    if (wrap.verMode == graphics_WrapMode_clamp)
        arr->items[1] = fh_new_string(prog, "clamp");
    else if (wrap.verMode == graphics_WrapMode_repeat) {
        arr->items[1] = fh_new_string(prog, "repeat");
    }

    fh_restore_pin_state(prog, pin_state);
    new_val.data.obj = arr;
    *ret = new_val;

    return 0;
}

static int fn_love_image_setWrap(struct fh_program *prog,
                                 struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 3)
        return fh_set_error(prog, "love_image_setWrap(): expected 3 arguments, got %d", n_args);

    if (!fh_is_c_obj_of_type(&args[0], FH_IMAGE_TYPE) || !fh_is_string(&args[1]) || !fh_is_string(&args[2]))
        return fh_set_error(prog, "Expected image, horizontal and veritical string mode");

    fh_image_t *img = fh_get_c_obj_value(&args[0]);
    const char *horMode = fh_get_string(&args[1]);
    const char *verMode = fh_get_string(&args[2]);

    graphics_Wrap wrap;

    if (strcmp(horMode, "clamp") == 0) {
        wrap.horMode = graphics_WrapMode_clamp;
    } else if (strcmp(horMode, "repeat") == 0) {
        wrap.horMode = graphics_WrapMode_repeat;
    }

    if (strcmp(verMode, "clamp") == 0) {
        wrap.verMode = graphics_WrapMode_clamp;
    } else if (strcmp(verMode, "repeat") == 0) {
        wrap.verMode = graphics_WrapMode_repeat;
    }

    graphics_Image_setWrap(img->img, &wrap);

    *ret = fh_new_null();
    return 0;
}

static int fn_love_image_getFilter(struct fh_program *prog,
                                   struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_image_getFilter(): expected 1 argument, got %d", n_args);

    if (!fh_is_c_obj_of_type(&args[0], FH_IMAGE_TYPE))
        return fh_set_error(prog, "Expected image");

    fh_image_t *img = fh_get_c_obj_value(&args[0]);

    graphics_Filter filter;
    graphics_Image_getFilter(img->img, &filter);

    int pin_state = fh_get_pin_state(prog);
    struct fh_array *arr = fh_make_array(prog, true);
    if (!fh_grow_array_object(prog, arr, 2))
        return fh_set_error(prog, "out of memory");

    struct fh_value new_val = fh_new_array(prog);

    if (filter.minMode == graphics_FilterMode_none)
        arr->items[0] = fh_new_string(prog, "none");
    else if (filter.minMode == graphics_FilterMode_linear)
        arr->items[0] = fh_new_string(prog, "linear");
    else if (filter.minMode == graphics_FilterMode_nearest)
        arr->items[0] = fh_new_string(prog, "nearest");

    if (filter.magMode == graphics_FilterMode_none)
        arr->items[1] = fh_new_string(prog, "none");
    else if (filter.magMode == graphics_FilterMode_linear)
        arr->items[1] = fh_new_string(prog, "linear");
    else if (filter.magMode == graphics_FilterMode_nearest)
        arr->items[1] = fh_new_string(prog, "nearest");

    arr->items[2] = fh_new_number(filter.maxAnisotropy);

    fh_restore_pin_state(prog, pin_state);
    new_val.data.obj = arr;
    *ret = new_val;

    return 0;
}

static int fn_love_image_setFilter(struct fh_program *prog,
                                   struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args < 3)
        return fh_set_error(prog, "love_image_setFilter(): expected at least 3 arguments, got %d", n_args);

    if (!fh_is_c_obj_of_type(&args[0], FH_IMAGE_TYPE) || !fh_is_string(&args[1]) || !fh_is_string(&args[2]))
        return fh_set_error(prog, "Expected image, min and mag string mode");

    fh_image_t *img = fh_get_c_obj_value(&args[0]);
    const char *min = fh_get_string(&args[1]);
    const char *mag = fh_get_string(&args[2]);

    graphics_Filter newFilter;
    graphics_Image_getFilter(img->img, &newFilter);

    newFilter.maxAnisotropy = (float)fh_optnumber(args, n_args, 3, 1.0);

    if (strcmp(min, "none") == 0) {
        newFilter.minMode = graphics_FilterMode_none;
    } else if (strcmp(min, "linear") == 0) {
        newFilter.minMode = graphics_FilterMode_linear;
    } else if (strcmp(min, "nearest") == 0) {
        newFilter.minMode = graphics_FilterMode_nearest;
    }

    if (strcmp(mag, "none") == 0) {
        newFilter.magMode = graphics_FilterMode_none;
    } else if (strcmp(mag, "linear") == 0) {
        newFilter.magMode = graphics_FilterMode_linear;
    } else if (strcmp(mag, "nearest") == 0) {
        newFilter.magMode = graphics_FilterMode_nearest;
    }

    graphics_Image_setFilter(img->img, &newFilter);

    *ret = fh_new_null();
    return 0;
}

static int fn_love_image_setMipmapFilter(struct fh_program *prog,
                                         struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args < 2)
        return fh_set_error(prog, "love_image_setMipmapFilter(): expected at least 2 arguments, got %d", n_args);

    if (!fh_is_c_obj_of_type(&args[0], FH_IMAGE_TYPE))
        return fh_set_error(prog, "Expected image, min and mag string mode");

    fh_image_t *img = fh_get_c_obj_value(&args[0]);

    graphics_Filter newFilter;
    graphics_Image_getFilter(img->img, &newFilter);

    if (fh_is_null(&args[1])) {
        newFilter.mipmapMode = graphics_FilterMode_none;
        newFilter.mipmapLodBias = 0.0f;
    } else {
        if (!fh_is_string(&args[1]) || !fh_is_number(&args[2]))
            return fh_set_error(prog, "Illegal parameters, expected string and number");
        const char *mipmapMode = fh_get_string(&args[1]);
        if (strcmp(mipmapMode, "none") == 0) {
            newFilter.mipmapMode = graphics_FilterMode_none;
        } else if (strcmp(mipmapMode, "linear") == 0) {
            newFilter.mipmapMode = graphics_FilterMode_linear;
        } else if (strcmp(mipmapMode, "nearest") == 0) {
            newFilter.mipmapMode = graphics_FilterMode_nearest;
        }
        /* param 2 is supposed to be "sharpness", which is exactly opposite to LOD,
         * therefore we use the negative value */
        double sharpness = fh_get_number(&args[2]);
        newFilter.mipmapLodBias = -sharpness;
    }

    graphics_Image_setFilter(img->img, &newFilter);

    *ret = fh_new_null();
    return 0;
}

static int fn_love_image_getMipmapFilter(struct fh_program *prog,
                                         struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_image_getMipmapFilter(): expected 1 argument, got %d", n_args);

    if (!fh_is_c_obj_of_type(&args[0], FH_IMAGE_TYPE))
        return fh_set_error(prog, "Expected image");

    fh_image_t *img = fh_get_c_obj_value(&args[0]);

    graphics_Filter filter;
    graphics_Image_getFilter(img->img, &filter);

    int pin_state = fh_get_pin_state(prog);
    struct fh_array *arr = fh_make_array(prog, true);
    if (!fh_grow_array_object(prog, arr, 2))
        return fh_set_error(prog, "out of memory");

    struct fh_value new_val = fh_new_array(prog);

    if (filter.mipmapMode == graphics_FilterMode_none)
        arr->items[0] = fh_new_string(prog, "none");
    else if (filter.mipmapMode == graphics_FilterMode_linear)
        arr->items[0] = fh_new_string(prog, "linear");
    else if (filter.mipmapMode == graphics_FilterMode_nearest)
        arr->items[0] = fh_new_string(prog, "nearest");

    arr->items[1] = fh_new_number(filter.mipmapLodBias);

    fh_restore_pin_state(prog, pin_state);
    new_val.data.obj = arr;
    *ret = new_val;

    return 0;
}

static int fn_love_image_isVector(struct fh_program *prog,
                                  struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_image_isVector(): expected 1 argument, got %d", n_args);

    if (fh_is_c_obj_of_type(&args[0], FH_IMAGE_TYPE)) {
        fh_image_t *img = fh_get_c_obj_value(&args[0]);
        *ret = fh_new_bool(graphics_Image_isVector(img->img));
    } else if (fh_is_c_obj_of_type(&args[0], FH_IMAGE_DATA_TYPE)) {
        image_ImageData *data = fh_get_c_obj_value(&args[0]);
        *ret = fh_new_bool(image_ImageData_isVector(data));
    } else
        return fh_set_error(prog, "Expected image");

    return 0;
}

static int fn_love_image_getVectorScale(struct fh_program *prog,
                                        struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 1)
        return fh_set_error(prog, "love_image_getVectorScale(): expected 1 argument, got %d", n_args);

    if (fh_is_c_obj_of_type(&args[0], FH_IMAGE_TYPE)) {
        fh_image_t *img = fh_get_c_obj_value(&args[0]);
        *ret = fh_new_number(graphics_Image_getVectorScale(img->img));
    } else if (fh_is_c_obj_of_type(&args[0], FH_IMAGE_DATA_TYPE)) {
        image_ImageData *data = fh_get_c_obj_value(&args[0]);
        *ret = fh_new_number(image_ImageData_getVectorScale(data));
    } else
        return fh_set_error(prog, "Expected image");

    return 0;
}

static int fn_love_image_setVectorScale(struct fh_program *prog,
                                        struct fh_value *ret, struct fh_value *args, int n_args) {
    if (n_args != 2)
        return fh_set_error(prog, "love_image_setVectorScale(): expected 2 arguments, got %d", n_args);

    if (!fh_is_c_obj_of_type(&args[0], FH_IMAGE_TYPE))
        return fh_set_error(prog, "Expected image");
    if (!fh_is_number(&args[1]))
        return fh_set_error(prog, "Expected a number as second argument (vector scale)");

    fh_image_t *img = fh_get_c_obj_value(&args[0]);

    // false for raster images (nothing to rasterize) and when the
    // rasterization itself failed; the image is left as it was either way.
    *ret = fh_new_bool(graphics_Image_setVectorScale(img->img, (float) fh_get_number(&args[1])));
    return 0;
}

#define DEF_FN(name) { #name, fn_##name }
static const struct fh_named_c_func c_funcs[] = {
    DEF_FN(love_graphics_newImageData),
    DEF_FN(love_graphics_newImage),
    DEF_FN(love_image_getWidth),
    DEF_FN(love_image_getHeight),
    DEF_FN(love_image_getPath),
    DEF_FN(love_image_getChannels),
    DEF_FN(love_image_refresh),
    DEF_FN(love_image_getDimensions),
    DEF_FN(love_image_getFilter),
    DEF_FN(love_image_setFilter),
    DEF_FN(love_image_getWrap),
    DEF_FN(love_image_setWrap),
    DEF_FN(love_image_setMipmapFilter),
    DEF_FN(love_image_getMipmapFilter),
    DEF_FN(love_image_getPixel),
    DEF_FN(love_image_setPixel),
    DEF_FN(love_image_isVector),
    DEF_FN(love_image_getVectorScale),
    DEF_FN(love_image_setVectorScale),
};

void fh_image_register(struct fh_program *prog) {
    fh_add_c_funcs(prog, c_funcs, sizeof(c_funcs)/sizeof(c_funcs[0]));
}
