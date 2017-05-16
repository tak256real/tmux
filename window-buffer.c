/* $OpenBSD$ */

/*
 * Copyright (c) 2016 Nicholas Marriott <nicholas.marriott@gmail.com>
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

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tmux.h"

static struct screen	*window_buffer_init(struct window_pane *,
			     struct args *);
static void		 window_buffer_free(struct window_pane *);
static void		 window_buffer_resize(struct window_pane *, u_int,
			     u_int);
static void		 window_buffer_key(struct window_pane *,
			     struct client *, struct session *, key_code,
			     struct mouse_event *);

#define WINDOW_BUFFER_DEFAULT_COMMAND "paste-buffer -b '%%'"

const struct window_mode window_buffer_mode = {
	.init = window_buffer_init,
	.free = window_buffer_free,
	.resize = window_buffer_resize,
	.key = window_buffer_key,
};

enum window_buffer_sort_type {
	WINDOW_BUFFER_BY_NAME,
	WINDOW_BUFFER_BY_TIME,
	WINDOW_BUFFER_BY_SIZE,
};
static const char *window_buffer_sort_list[] = {
	"name",
	"time",
	"size"
};

struct window_buffer_itemdata {
	const char	*name;
	time_t		 created;
	u_int		 order;
	size_t		 size;
};


struct window_buffer_modedata {
	struct mode_tree_data		*data;
	char				*command;

	struct window_buffer_itemdata	*item_list;
	u_int				 item_size;
};

static void
window_buffer_free_item(struct window_buffer_itemdata *item)
{
	free((void *)item->name);
}

static int
window_buffer_compare_name(const void *a0, const void *b0)
{
	const struct window_buffer_itemdata	*a = a0;
	const struct window_buffer_itemdata	*b = b0;

	return (strcmp(a->name, b->name));
}

static int
window_buffer_compare_time(const void *a0, const void *b0)
{
	const struct window_buffer_itemdata	*a = a0;
	const struct window_buffer_itemdata	*b = b0;

	if (a->order > b->order)
		return (-1);
	if (a->order < b->order)
		return (1);
	return (strcmp(a->name, b->name));
}

static int
window_buffer_compare_size(const void *a0, const void *b0)
{
	const struct window_buffer_itemdata	*a = a0;
	const struct window_buffer_itemdata	*b = b0;

	if (a->size > b->size)
		return (-1);
	if (a->size < b->size)
		return (1);
	return (strcmp(a->name, b->name));
}

static void
window_buffer_build(void *modedata, u_int sort_type)
{
	struct window_buffer_modedata	*data = modedata;
	struct window_buffer_itemdata	*item;
	u_int				 i;
	struct paste_buffer		*pb;
	char				*tim;
	char				*text;

	for (i = 0; i < data->item_size; i++)
		window_buffer_free_item(&data->item_list[i]);
	free(data->item_list);
	data->item_list = NULL;
	data->item_size = 0;

	pb = NULL;
	while ((pb = paste_walk(pb)) != NULL) {
		data->item_list = xreallocarray(data->item_list,
		    data->item_size + 1, sizeof *data->item_list);
		item = &data->item_list[data->item_size++];

		item->name = xstrdup(paste_buffer_name(pb));
		item->created = paste_buffer_created(pb);
		paste_buffer_data(pb, &item->size);
		item->order = paste_buffer_order(pb);
	}

		switch (sort_type) {
	case WINDOW_BUFFER_BY_NAME:
		qsort(data->item_list, data->item_size, sizeof *data->item_list,
		    window_buffer_compare_name);
		break;
	case WINDOW_BUFFER_BY_TIME:
		qsort(data->item_list, data->item_size, sizeof *data->item_list,
		    window_buffer_compare_time);
		break;
	case WINDOW_BUFFER_BY_SIZE:
		qsort(data->item_list, data->item_size, sizeof *data->item_list,
		    window_buffer_compare_size);
		break;
	}

	for (i = 0; i < data->item_size; i++) {
		item = &data->item_list[i];

		tim = ctime(&item->created);
		*strchr(tim, '\n') = '\0';

		xasprintf(&text, "%zu bytes (%s)", item->size, tim);
		mode_tree_add(data->data, NULL, item, item->order, item->name,
		    text);
		free(text);
	}

}

static struct screen *
window_buffer_draw(__unused void *modedata, void *itemdata, u_int sx, u_int sy)
{
	struct window_buffer_itemdata	*item = itemdata;
	struct paste_buffer		*pb;
	static struct screen		 s;
	struct screen_write_ctx		 ctx;
	char				 line[1024];
	const char			*pdata, *end, *cp;
	size_t				 psize, at;
	u_int				 i;

	pb = paste_get_name(item->name);
	if (pb == NULL)
		return (NULL);

	screen_init(&s, sx, sy, 0);

	screen_write_start(&ctx, NULL, &s);
	screen_write_clearscreen(&ctx, 8);

	pdata = end = paste_buffer_data (pb, &psize);
	for (i = 0; i < sy; i++) {
		at = 0;
		while (end != pdata + psize && *end != '\n') {
			if ((sizeof line) - at > 5) {
				cp = vis(line + at, *end, VIS_TAB|VIS_OCTAL, 0);
				at = cp - line;
			}
			end++;
		}
		if (at > sx)
			at = sx;
		line[at] = '\0';

		if (*line != '\0') {
			screen_write_cursormove(&ctx, 0, i);
			screen_write_puts(&ctx, &grid_default_cell, "%s", line);
		}

		if (end == pdata + psize)
			break;
		end++;
	}

	return (&s);
}

static struct screen *
window_buffer_init(struct window_pane *wp, struct args *args)
{
	struct window_buffer_modedata	*data;
	struct screen			*s;

	wp->modedata = data = xcalloc(1, sizeof *data);

	if (args == NULL || args->argc == 0)
		data->command = xstrdup(WINDOW_BUFFER_DEFAULT_COMMAND);
	else
		data->command = xstrdup(args->argv[0]);

	data->data = mode_tree_start(wp, window_buffer_build,
	    window_buffer_draw, data, window_buffer_sort_list,
	    nitems(window_buffer_sort_list), &s);

	mode_tree_build(data->data);
	mode_tree_draw(data->data);

	return (s);
}

static void
window_buffer_free(struct window_pane *wp)
{
	struct window_buffer_modedata	*data = wp->modedata;
	u_int				 i;

	if (data == NULL)
		return;

	mode_tree_free(data->data);

	for (i = 0; i < data->item_size; i++)
		window_buffer_free_item(&data->item_list[i]);
	free(data->item_list);

	free(data->command);
	free(data);
}

static void
window_buffer_resize(struct window_pane *wp, u_int sx, u_int sy)
{
	struct window_buffer_modedata	*data = wp->modedata;

	mode_tree_resize(data->data, sx, sy);
}

static void
window_buffer_key(struct window_pane *wp, __unused struct client *c,
    __unused struct session *s, key_code key, struct mouse_event *m)
{
	struct window_buffer_modedata	*data = wp->modedata;
	int				 finished;

	/*
	 * t = toggle buffer tag
	 * T = tag no buffers
	 * C-t = tag all buffers
	 * d = delete buffer
	 * D = delete tagged buffers
	 * q = exit
	 * O = change sort order
	 * ENTER = paste buffer
	 */

	finished = mode_tree_key(data->data, &key, m);
#if 0
	switch (key) {
	case 'd':
		item = data->current;
		window_buffer_down(data);
		if ((pb = paste_get_name(item->name)) != NULL)
			paste_free(pb);
		window_buffer_build_tree(data);
		break;
	case 'D':
		RB_FOREACH(item, window_buffer_tree, &data->tree) {
			if (!item->tagged)
				continue;
			if (item == data->current)
				window_buffer_down(data);
			if ((pb = paste_get_name(item->name)) != NULL)
				paste_free(pb);
		}
		window_buffer_build_tree(data);
		break;
	case '\r':
		command = xstrdup(data->command);
		name = xstrdup(data->current->name);
		window_pane_reset_mode(wp);
		window_buffer_run_command(c, command, name);
		free(name);
		free(command);
		return;
	}
#endif
	if (finished || paste_get_top(NULL) == NULL)
		window_pane_reset_mode(wp);
	else {
		mode_tree_draw(data->data);
		wp->flags |= PANE_REDRAW;
	}
}
