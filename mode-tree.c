/* $OpenBSD$ */

/*
 * Copyright (c) 2017 Nicholas Marriott <nicholas.marriott@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include "tmux.h"

struct mode_tree_item;
TAILQ_HEAD(mode_tree_list, mode_tree_item);

struct mode_tree_data {
	struct window_pane	 *wp;
	void			 *modedata;

	const char		**sort_list;
	u_int			  sort_size;
	u_int			  sort_type;

	void			 (*buildcb)(void *, u_int);
	struct screen		*(*drawcb)(void *, void *, u_int, u_int);

	struct mode_tree_list	  children;
	struct mode_tree_list	  saved;

	struct mode_tree_line	 *line_list;
	u_int			  line_size;

	u_int			  width;
	u_int			  height;

	u_int			  offset;
	u_int			  current;

	struct screen		  screen;
};

struct mode_tree_item {
	void				*itemdata;

	uint64_t			 tag;
	const char			*name;
	const char			*text;

	int				 expanded;
	int				 tagged;

	struct mode_tree_list		 children;
	TAILQ_ENTRY(mode_tree_item)	 entry;
};

struct mode_tree_line {
	struct mode_tree_item		*item;
	u_int				 number;
};

static struct mode_tree_item *
mode_tree_find_item(struct mode_tree_list *mtl, uint64_t tag)
{
	struct mode_tree_item	*mti, *child;

	TAILQ_FOREACH(mti, mtl, entry) {
		if (mti->tag == tag)
			return (mti);
		child = mode_tree_find_item(&mti->children, tag);
		if (child != NULL)
			return (child);
	}
	return (NULL);
}

static void
mode_tree_free_items(struct mode_tree_list *mtl)
{
	struct mode_tree_item	*mti, *mti1;

	TAILQ_FOREACH_SAFE(mti, mtl, entry, mti1) {
		mode_tree_free_items(&mti->children);

		free((void *)mti->name);
		free((void *)mti->text);

		TAILQ_REMOVE(mtl, mti, entry);
		free(mti);
	}
}

static void
mode_tree_clear_lines(struct mode_tree_data *mtd)
{
	free(mtd->line_list);
	mtd->line_list = NULL;
	mtd->line_size = 0;
}

static void
mode_tree_build_lines(struct mode_tree_data *mtd,
    struct mode_tree_list *mtl)
{
	struct mode_tree_item	*mti;
	struct mode_tree_line	*line;

	TAILQ_FOREACH(mti, mtl, entry) {
		mtd->line_list = xreallocarray(mtd->line_list,
		    mtd->line_size + 1, sizeof *mtd->line_list);

		line = &mtd->line_list[mtd->line_size++];
		line->item = mti;
		line->number = mtd->line_size - 1;

		if (mti->expanded)
			mode_tree_build_lines(mtd, &mti->children);
	}
}

static void
mode_tree_up(struct mode_tree_data *mtd)
{
	if (mtd->current == 0) {
		mtd->current = mtd->line_size - 1;
		if (mtd->line_size >= mtd->height)
			mtd->offset = mtd->line_size - mtd->height;
	} else {
		mtd->current--;
		if (mtd->current < mtd->offset)
			mtd->offset--;
	}
}

static void
mode_tree_down(struct mode_tree_data *mtd)
{
	if (mtd->current == mtd->line_size - 1) {
		mtd->current = 0;
		mtd->offset = 0;
	} else {
		mtd->current++;
		if (mtd->current > mtd->offset + mtd->height - 1)
			mtd->offset++;
	}
}

void *
mode_tree_get_current(struct mode_tree_data *mtd)
{
	return (mtd->line_list[mtd->current].item->itemdata);
}

void
mode_tree_each_tagged(struct mode_tree_data *mtd, void (*cb)(void *))
{
	u_int	i;

	for (i = 0; i < mtd->line_size; i++) {
		if (mtd->line_list[i].item->tagged)
			cb(mtd->line_list[i].item->itemdata);
	}
}

struct mode_tree_data *
mode_tree_start(struct window_pane *wp, void (*buildcb)(void *, u_int),
    struct screen *(*drawcb)(void *, void *, u_int, u_int), void *modedata,
    const char **sort_list, u_int sort_size, struct screen **s)
{
	struct mode_tree_data	*mtd;

	mtd = xcalloc(1, sizeof *mtd);
	mtd->wp = wp;
	mtd->modedata = modedata;

	mtd->sort_list = sort_list;
	mtd->sort_size = sort_size;
	mtd->sort_type = 0;

	mtd->buildcb = buildcb;
	mtd->drawcb = drawcb;

	TAILQ_INIT(&mtd->children);

	*s = &mtd->screen;
	screen_init(*s, screen_size_x(&wp->base), screen_size_y(&wp->base), 0);
	(*s)->mode &= ~MODE_CURSOR;

	return (mtd);
}

void
mode_tree_build(struct mode_tree_data *mtd)
{
	struct screen	*s = &mtd->screen;

	memcpy(&mtd->saved, &mtd->children, sizeof mtd->saved);
	TAILQ_INIT(&mtd->children);

	mtd->buildcb(mtd->modedata, mtd->sort_type);

	//XXX check current line. by tag?

	mode_tree_free_items(&mtd->saved);
	TAILQ_INIT(&mtd->saved);

	mode_tree_clear_lines(mtd);
	mode_tree_build_lines(mtd, &mtd->children);

	mtd->width = screen_size_x(s);
	mtd->height = (screen_size_y(s) / 3) * 2;
	if (mtd->height > mtd->line_size)
		mtd->height = screen_size_y(s) / 2;
	if (mtd->height < 10)
		mtd->height = screen_size_y(s);
	if (screen_size_y(s) - mtd->height < 2)
		mtd->height = screen_size_y(s);
}

void
mode_tree_free(struct mode_tree_data *mtd)
{
	mode_tree_free_items(&mtd->children);
	mode_tree_clear_lines(mtd);
	screen_free(&mtd->screen);
	free(mtd);
}

void
mode_tree_resize(struct mode_tree_data *mtd, u_int sx, u_int sy)
{
	struct screen	*s = &mtd->screen;

	screen_resize(s, sx, sy, 0);

	mode_tree_build(mtd);
	mode_tree_draw(mtd);

	mtd->wp->flags |= PANE_REDRAW;
}

struct mode_tree_item *
mode_tree_add(struct mode_tree_data *mtd, struct mode_tree_item *parent,
    void *itemdata, uint64_t tag, const char *name, const char *text)
{
	struct mode_tree_item	*mti, *saved;

	mti = xcalloc(1, sizeof *mti);
	mti->itemdata = itemdata;

	mti->tag = tag;
	mti->name = xstrdup(name);
	mti->text = xstrdup(text);

	saved = mode_tree_find_item(&mtd->saved, tag);
	if (saved != NULL)
		mti->expanded = saved->expanded;

	TAILQ_INIT (&mti->children);

	if (parent != NULL)
		TAILQ_INSERT_TAIL(&parent->children, mti, entry);
	else
		TAILQ_INSERT_TAIL(&mtd->children, mti, entry);

	return (mti);
}

void
mode_tree_draw(struct mode_tree_data *mtd)
{
	struct window_pane	*wp = mtd->wp;
	struct screen		*s = &mtd->screen, *box;
	struct mode_tree_line	*line;
	struct mode_tree_item	*mti;
	struct options		*oo = wp->window->options;
	struct screen_write_ctx	 ctx;
	struct grid_cell	 gc0, gc;
	u_int			 w, h, i, sy, box_x, box_y;
	char			*text, *tmp;
	const char		*tag;

	memcpy(&gc0, &grid_default_cell, sizeof gc0);
	memcpy(&gc, &grid_default_cell, sizeof gc);
	style_apply(&gc, oo, "mode-style");

	w = mtd->width;
	h = mtd->height;

	screen_write_start(&ctx, NULL, s);
	screen_write_clearscreen(&ctx, 8);

	for (i = 0; i < mtd->line_size; i++) {
		if (i < mtd->offset)
			continue;
		if (i > mtd->offset + h - 1)
			break;

		line = &mtd->line_list[i];
		mti = line->item;

		screen_write_cursormove(&ctx, 0, i - mtd->offset);

		if (mti->tagged)
			tag = "*";
		else
			tag = "";
		xasprintf(&tmp, "%s%s:", mti->name, tag);
		xasprintf(&text, "%-16s%s", tmp, mti->text);
		free(tmp);

		//XXX children, expanded etc

		if (i != mtd->current) {
			screen_write_puts(&ctx, &gc0, "%.*s", w, text);
			screen_write_clearendofline(&ctx, 8);
		} else
			screen_write_puts(&ctx, &gc, "%-*.*s", w, w, text);
		free(text);
	}

	sy = screen_size_y(s);
	if (sy <= 4 || h <= 4 || sy - h <= 4 || w <= 4) {
		screen_write_stop(&ctx);
		return;
	}

	line = &mtd->line_list[mtd->current];
	mti = line->item;

	screen_write_cursormove(&ctx, 0, h);
	screen_write_box(&ctx, w, sy - h);

	xasprintf(&text, " %s (sort: %s) ", mti->name,
	    mtd->sort_list[mtd->sort_type]);
	if (w - 2 >= strlen(text)) {
		screen_write_cursormove(&ctx, 1, h);
		screen_write_puts(&ctx, &gc0, "%s", text);
	}
	free(text);

	box_x = w - 4;
	box_y = sy - h - 2;

	box = mtd->drawcb(mtd->modedata, mti->itemdata, box_x, box_y);
	if (box != NULL) {
		screen_write_cursormove(&ctx, 2, h + 1);
		screen_write_copy(&ctx, box, 0, 0, box_x, box_y, NULL, NULL);
	}

	screen_free(box);
	screen_write_stop(&ctx);
}

int
mode_tree_key(struct mode_tree_data *mtd, key_code *key, struct mouse_event *m)
{
	u_int	x, y;

	if (*key == KEYC_MOUSEDOWN1_PANE) {
		if (cmd_mouse_at(mtd->wp, m, &x, &y, 0) != 0) {
			*key = KEYC_NONE;
			return 0;
		}
		if (x > mtd->width || y > mtd->height) {
			*key = KEYC_NONE;
			return 0;
		}
		if (mtd->offset + y < mtd->line_size) {
			mtd->current = mtd->offset + y;
			*key = '\r';
			return 0;
		}
	}

	switch (*key) {
	case 'q':
	case '\033': /* Escape */
		return 1;
	case KEYC_UP:
	case 'k':
	case KEYC_WHEELUP_PANE:
		mode_tree_up(mtd);
		break;
	case KEYC_DOWN:
	case 'j':
	case KEYC_WHEELDOWN_PANE:
		mode_tree_down(mtd);
		break;
#if 0
	case KEYC_PPAGE:
	case '\002': /* C-b */
		for (i = 0; i < mtd->height; i++) {
			if (mtd->current->number == 0)
				break;
			mode_tree_up(mtd);
		}
		break;
	case KEYC_NPAGE:
	case '\006': /* C-f */
		for (i = 0; i < mtd->height; i++) {
			if (mtd->current->number == mtd->number - 1)
				break;
			mode_tree_down(mtd);
		}
		break;
#endif
	case KEYC_HOME:
		mtd->current = 0;
		mtd->offset = 0;
		break;
	case KEYC_END:
		mtd->current = mtd->line_size - 1;
		if (mtd->current > mtd->height - 1)
			mtd->offset = mtd->current - mtd->height;
		else
			mtd->offset = 0;
		break;
#if 0
	case 't':
		mtd->current->tagged = !mtd->current->tagged;
		mode_tree_down(mtd);
		break;
	case 'T':
		RB_FOREACH(item, mode_tree_tree, &mtd->tree)
			item->tagged = 0;
		break;
	case '\024': /* C-t */
		RB_FOREACH(item, mode_tree_tree, &mtd->tree)
			item->tagged = 1;
		break;
#endif
	case 'O':
		mtd->sort_type++;
		if (mtd->sort_type == mtd->sort_size)
			mtd->sort_type = 0;
		mode_tree_build(mtd);
		break;
	}
	return (0);
}
