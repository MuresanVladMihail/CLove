/*
#   clove
#
#   Copyright (C) 2019-2020 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/
#include "../include/ui.h"

#include "atlas.inl"

#include "../include/image.h"
#include "../include/imagedata.h"
#include "../include/geometry.h"
#include "../include/graphics.h"
#include "../include/font.h"

#include <string.h>

static struct {
    mu_Context *ctx;
    graphics_Font *font;

    graphics_Image *img;
} moduleData;

static void draw_rect(mu_Rect rect, mu_Color color) {
  graphics_setColor(color.r / 255.0f, color.g / 255.0f,
                    color.b / 255.0f, color.a / 255.0f);
  graphics_geometry_rectangle(true, rect.x, rect.y,
                              rect.w, rect.h,
                              0, 1, 1, 0, 0);
  graphics_setColor(1, 1, 1, 1);
}

static void draw_icon(int id, mu_Rect rect, mu_Color color) {
    mu_Rect src = atlas[id];

  int x = rect.x + (rect.w - src.w) / 2;
  int y = rect.y + (rect.h - src.h) / 2;
  graphics_setColor(color.r / 255.0f, color.g / 255.0f,
                    color.b / 255.0f, color.a / 255.0f);
  graphics_Quad quad;
  graphics_Quad_newWithRef(&quad, src.x, src.y, src.w, src.h, ATLAS_WIDTH, ATLAS_HEIGHT);

  graphics_Image_draw(moduleData.img, &quad, x, y, 0, 1, 1, 0, 0, 0, 0);

  graphics_setColor(1, 1, 1, 1);
}

/*
 * `text` has to stay a pointer: mu_TextCommand ends in a `char str[1]` whose
 * real length lives past the end of the struct, inside microui's command
 * buffer. Taking the command by value copied only sizeof(mu_TextCommand),
 * which truncated every string to the 4 bytes that fit in the struct's
 * trailing padding.
 */
static void draw_font(const mu_TextCommand *text, mu_Color color) {
    graphics_setColor(color.r / 255.0f, color.g / 255.0f,
                      color.b / 255.0f, color.a / 255.0f);
    graphics_Font_render(moduleData.font, text->str,
                         text->pos.x, text->pos.y,
                         0, 1, 1, 0, 0, 0, 0);
    graphics_setColor(1, 1, 1, 1);
}

static int text_width(mu_Font font, const char *text, int len) {
	if (len > 0) {
		/* microui hands us a length, not a terminated string. The old code
		   copied `len` bytes into a `len`-byte VLA and measured that: with no
		   NUL, graphics_Font_getWidth() ran off the end of the buffer. */
		char *t = malloc((size_t) len + 1);
		if (!t)
			return 0;
		memcpy(t, text, (size_t) len);
		t[len] = '\0';
		int w = graphics_Font_getWidth(moduleData.font, t);
		free(t);
		return w;
	}
	return graphics_Font_getWidth(moduleData.font, text);
}

static int text_height(mu_Font font) {
    return graphics_Font_getHeight(moduleData.font);
}

void ui_init(void) {
    moduleData.ctx = malloc(sizeof (mu_Context));
    mu_init(moduleData.ctx);

    moduleData.font = malloc(sizeof (graphics_Font));
    graphics_Font_new(moduleData.font, NULL, 14);

    // For font alignment and clipping to work correctly
    moduleData.ctx->text_width = text_width;
    moduleData.ctx->text_height = text_height;

    image_ImageData *data = malloc(sizeof(image_ImageData));
    image_ImageData_new_with_surface(data, atlas_texture, ATLAS_WIDTH, ATLAS_HEIGHT, 1);

    graphics_Filter filter;

    moduleData.img = malloc(sizeof(graphics_Image));
    graphics_Image_new_with_ImageData(moduleData.img, data);

    graphics_Image_getFilter(moduleData.img, &filter);
    filter.magMode = graphics_FilterMode_nearest;
    filter.minMode = graphics_FilterMode_nearest;
    filter.maxAnisotropy = 1;

    graphics_Image_setFilter(moduleData.img, &filter);

    graphics_Font_getFilter(moduleData.font, &filter);
    filter.magMode = graphics_FilterMode_nearest;
    filter.minMode = graphics_FilterMode_nearest;
    filter.maxAnisotropy = 1;

    graphics_Font_setFilter(moduleData.font, &filter);

}

void ui_deinit(void) {
    free(moduleData.ctx);
    free(moduleData.font);
    graphics_Image_free(moduleData.img);
    free(moduleData.img);
}

mu_Context *ui_get_context(void) {
    return moduleData.ctx;
}

/* microui's layout, clip and container stacks are only filled between
 * mu_begin_window()/mu_end_window() (or a panel or a popup). Every widget and
 * every draw command reads the top of one of them through expect(), which is
 * an abort() -- so a script drawing outside a container takes the whole
 * process down. The bindings ask this first and raise an ordinary script
 * error instead. */
int ui_in_container(void) {
    return moduleData.ctx != NULL && moduleData.ctx->container_stack.idx > 0;
}

/* Is the named popup on screen right now?
 *
 * mu_begin_popup() returns MU_RES_ACTIVE on the very frame it closes -- the
 * "elsewhere was clicked" test runs after the body has been begun -- so a
 * script cannot tell "still open" from "closing" by its return value alone.
 * This reads the container's own flag, which by then is already 0.
 *
 * The id is hashed against the top of the id stack exactly the way
 * mu_open_popup() and mu_begin_popup() hash it, so call this from the same
 * window that opened the popup. A popup that has never been opened has no
 * container in the pool yet, and mu_get_container() would create one *open* --
 * hence the pool lookup rather than mu_get_container(). */
/* Is the mouse over one of microui's containers?
 *
 * ctx->hover_root is the root container the pointer was inside at the end of
 * the last frame, so a game that draws its own scene under a floating window
 * can ask this before acting on a click and let the UI have it. NULL means the
 * pointer is over bare scene. */
/* Re-open (or force closed) a named window.
 *
 * microui's title-bar close button sets the container's open flag to 0, and
 * mu_begin_window_ex() then returns 0 for good -- a script that shows the
 * window from a check box has no way back in without this. The name is hashed
 * against the top of the id stack, the same way mu_begin_window_ex() hashes
 * it, so call this from the same scope the window is begun in. */
void ui_set_window_open(const char *name, int open) {
    if (moduleData.ctx == NULL) return;
    mu_Container *cnt = mu_get_container(moduleData.ctx, name);
    if (cnt != NULL) { cnt->open = open; }
}

int ui_mouse_over(void) {
    return moduleData.ctx != NULL && moduleData.ctx->hover_root != NULL;
}

int ui_popup_open(const char *name) {
    mu_Context *ctx = moduleData.ctx;
    if (ctx == NULL) return 0;

    mu_Id id = mu_get_id(ctx, name, (int) strlen(name));
    int idx = mu_pool_get(ctx, ctx->container_pool, MU_CONTAINERPOOL_SIZE, id);
    if (idx < 0) return 0;
    return ctx->containers[idx].open;
}

void ui_layout_row(int no_items, int widths[], int height) {
    mu_layout_row(moduleData.ctx, no_items, widths, height);
}

void ui_layout_begin_column(void) {
    mu_layout_begin_column(moduleData.ctx);
}

void ui_layout_end_column(void) {
    mu_layout_end_column(moduleData.ctx);
}

void ui_layout_set_next(int x, int y,
                        int w, int h,
                        int relative) {
    mu_layout_set_next(moduleData.ctx,
                       mu_rect(x, y, w, h), relative);
}

void ui_layout_width(int width) {
    mu_layout_width(moduleData.ctx, width);
}

mu_Container* ui_get_container(const char *name) {
    return mu_get_container(moduleData.ctx, name);
}

int ui_begin_window(const char* title, mu_Rect rect, int opt) {
    return mu_begin_window_ex(moduleData.ctx, title, rect, opt);
}

void ui_draw_control_text(const char *str, mu_Rect rect, int colorid, int opt) {
    mu_draw_control_text(moduleData.ctx, str, rect, colorid, opt);
}

mu_Rect ui_layout_next(void) {
    return mu_layout_next(moduleData.ctx);
}

void ui_rect(mu_Rect rect, mu_Color color) {
    mu_draw_rect(moduleData.ctx, rect, color);
}

int ui_button(const char* label, int opt) {
    return mu_button_ex(moduleData.ctx, label, 0, opt);
}

/* mu_checkbox() takes its widget id from the address of the int* it is handed,
 * which here is always the same stack slot -- so every check box in a program
 * shared one id, one hover slot and one focus slot. What that looked like: a
 * box only responded when it happened to be the last one drawn, because each
 * later box with the same id cleared the hover the earlier one had just set.
 * The caller passes an id, exactly as for ui_slider() and ui_number(). */
int ui_checkbox(const char *label, int state, int id) {
    mu_push_id(moduleData.ctx, &id, sizeof(id));
    int res = mu_checkbox(moduleData.ctx, label, &state);
    mu_pop_id(moduleData.ctx);
    return res;
}

void ui_text(const char *text) {
    mu_text(moduleData.ctx, text);
}

int ui_textbox(char* label, int len, int opt) {
    return mu_textbox_ex(moduleData.ctx, label, len, opt);
}

int ui_header(const char *label, int opt) {
    return mu_header_ex(moduleData.ctx, label, opt);
}

int ui_begin_tree(const char *label, int opt) {
    return mu_begin_treenode_ex(moduleData.ctx, label, opt);
}

void ui_end_tree(void) {
    mu_end_treenode(moduleData.ctx);
}

void ui_label(const char *label, int opt) {
    mu_label(moduleData.ctx, label);
}

void ui_draw_rect(int x, int y, int w, int h,
                  int r, int g, int b, int a) {
    mu_draw_rect(moduleData.ctx, mu_rect(x, y, w, h),
                 mu_color(r, g, b, a));
}

void ui_begin_panel(mu_Container *cnt, const char *name, int opt) {
    mu_begin_panel_ex(moduleData.ctx, name, opt);
}

void ui_open_popup(const char *name) {
    mu_open_popup(moduleData.ctx, name);
}

void ui_end_panel(void) {
    mu_end_panel(moduleData.ctx);
}

int ui_begin_popup(const char *name) {
    return mu_begin_popup(moduleData.ctx, name);
}

void ui_end_popup(void) {
    mu_end_popup(moduleData.ctx);
}

/*
 * mu_slider_ex()/mu_number_ex() derive the widget id from the *address* of the
 * value pointer they are handed. `value` is a parameter, so every call from
 * here shares one stack slot and every slider in a frame would collide on the
 * same id -- none of them editable. The caller-supplied id goes on microui's
 * id stack to keep them apart.
 *
 * They also return a MU_RES_* code and write the new value through the
 * pointer; callers want the value, so that is what comes back.
 */
/* microui formats a slider's and a number field's value with printf, and a
 * format string coming from a script is a footgun -- "%s" there reads whatever
 * happens to be next on the stack. The caller picks a number of decimals
 * instead and the format comes from this table. */
static const char *ui_decimals_fmt(int decimals) {
  static const char *fmts[] = {
    "%.0f", "%.1f", "%.2f", "%.3f", "%.4f", "%.5f", "%.6f"
  };
  if (decimals < 0) { decimals = 0; }
  if (decimals > 6) { decimals = 6; }
  return fmts[decimals];
}

mu_Real ui_slider(mu_Real value, int low, int high, int step, int id, int opt,
                  int decimals) {
  mu_push_id(moduleData.ctx, &id, sizeof(id));
  mu_slider_ex(moduleData.ctx, &value, low, high, step,
               ui_decimals_fmt(decimals), opt);
  mu_pop_id(moduleData.ctx);
  return value;
}

/* Same id and return-value handling as ui_slider(). */
mu_Real ui_number(mu_Real value, mu_Real step, int id, int opt, int decimals) {
  mu_push_id(moduleData.ctx, &id, sizeof(id));
  mu_number_ex(moduleData.ctx, &value, step, ui_decimals_fmt(decimals), opt);
  mu_pop_id(moduleData.ctx);
  return value;
}

/*
 * microui derives a widget's id from its label, so two rows showing the same
 * text share one id and steal each other's clicks. Wrapping a list row in
 * ui_push_id()/ui_pop_id() mixes a caller-chosen key into those ids.
 */
void ui_push_id(const void *data, int size) {
    mu_push_id(moduleData.ctx, data, size);
}

void ui_pop_id(void) {
    mu_pop_id(moduleData.ctx);
}

void ui_end_window(void) {
    mu_end_window(moduleData.ctx);
}

void ui_begin(void) {
    mu_begin(moduleData.ctx);
}

void ui_end(void) {
    mu_end(moduleData.ctx);
}

void ui_draw(void) {
	graphics_clearScissor();
	mu_Command *cmd = NULL;
	while (mu_next_command(moduleData.ctx, &cmd)) {
		switch (cmd->type) {
			case MU_COMMAND_TEXT: draw_font(&cmd->text, cmd->text.color); break;
			case MU_COMMAND_RECT: draw_rect(cmd->rect.rect, cmd->rect.color); break;;
			case MU_COMMAND_ICON: draw_icon(cmd->icon.id, cmd->icon.rect, cmd->icon.color); break;
			case MU_COMMAND_CLIP: graphics_setScissor(cmd->clip.rect.x,
										  graphics_getHeight() - (cmd->clip.rect.y + cmd->clip.rect.h),
										  cmd->clip.rect.w, cmd->clip.rect.h); break;

		}

	}
}

void ui_input_mouse_move(int x, int y) {
    mu_input_mousemove(moduleData.ctx, x, y);
}

void ui_input_mouse_down(int btn, int x, int y) {
    mu_input_mousedown(moduleData.ctx, x, y, btn);
}

void ui_input_mouse_up(int btn, int x, int y) {
    mu_input_mouseup(moduleData.ctx, x, y, btn);
}

void ui_input_text(const char *txt) {
    mu_input_text(moduleData.ctx, txt);
}

void ui_input_scroll(int x, int y) {
    mu_input_scroll(moduleData.ctx, x, y);
}

void ui_input_keydown(int key) {
    mu_input_keydown(moduleData.ctx, key);
}

void ui_input_keyup(int key) {
    mu_input_keyup(moduleData.ctx, key);
}
