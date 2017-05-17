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

#include "tmux.h"

static struct screen	*window_tree_init(struct window_pane *, struct args *);
static void		 window_tree_free(struct window_pane *);
static void		 window_tree_resize(struct window_pane *, u_int, u_int);
static void		 window_tree_key(struct window_pane *,
			     struct client *, struct session *, key_code,
			     struct mouse_event *);

#define WINDOW_TREE_SESSION_DEFAULT_COMMAND "switch-client -t '%%'"
#define WINDOW_TREE_WINDOW_DEFAULT_COMMAND  "select-window -t '%%'"
#define WINDOW_TREE_PANE_DEFAULT_COMMAND    "select-pane -t '%%'"

const struct window_mode window_tree_mode = {
	.init = window_tree_init,
	.free = window_tree_free,
	.resize = window_tree_resize,
	.key = window_tree_key,
};

enum window_tree_sort_type {
	WINDOW_TREE_BY_NAME,
	WINDOW_TREE_BY_TIME,
};
static const char *window_tree_sort_list[] = {
	"name",
	"time"
};

enum window_tree_type {
	WINDOW_TREE_SESSION,
	WINDOW_TREE_WINDOW,
	WINDOW_TREE_PANE,
};

struct window_tree_itemdata {
	enum window_tree_type	type;
	int			session;
	int			winlink;
	int			pane;
};

struct window_tree_modedata {
	struct mode_tree_data		*data;

	struct window_tree_itemdata	*item_list;
	u_int				 item_size;
};

static void
window_tree_pull_item(struct window_tree_itemdata *item, struct session **sp,
    struct winlink **wlp, struct window_pane **wp)
{
	*wp = NULL;
	*wlp = NULL;
	*sp = session_find_by_id(item->session);
	if (*sp == NULL)
		return;
	if (item->type == WINDOW_TREE_SESSION) {
		*wlp = (*sp)->curw;
		*wp = (*wlp)->window->active;
		return;
	}

	*wlp = winlink_find_by_index(&(*sp)->windows, item->winlink);
	if (*wlp == NULL) {
		*sp = NULL;
		return;
	}
	if (item->type == WINDOW_TREE_WINDOW) {
		*wp = (*wlp)->window->active;
		return;
	}

	*wp = window_pane_find_by_id(item->pane);
	if (!window_has_pane((*wlp)->window, *wp))
		*wp = NULL;
	if (*wp == NULL) {
		*sp = NULL;
		*wlp = NULL;
		return;
	}
}

static void
window_tree_free_item(struct window_tree_itemdata *item)
{
}

static void
window_tree_build(void *modedata, u_int sort_type)
{
	struct window_tree_modedata	*data = modedata;
	struct window_tree_itemdata	*item;
	struct session			*s;
	struct winlink			*wl;
	struct window_pane		*wp;
	struct mode_tree_item		*mti, *mti2;
	char				*name, *text;
	u_int				 i, n;

	for (i = 0; i < data->item_size; i++)
		window_tree_free_item(&data->item_list[i]);
	free(data->item_list);
	data->item_list = NULL;
	data->item_size = 0;

	/* XXX sort */
	RB_FOREACH(s, sessions, &sessions) {
		data->item_list = xreallocarray(data->item_list,
		    data->item_size + 1, sizeof *data->item_list);
		item = &data->item_list[data->item_size++];

		item->type = WINDOW_TREE_SESSION;
		item->session = s->id;
		item->winlink = -1;
		item->pane = -1;

		text = xstrdup("XXX SESSION"); //XXX

		mti = mode_tree_add(data->data, NULL, item, (uint64_t)s,
		    s->name, text);

		free(text);

		RB_FOREACH(wl, winlinks, &s->windows) {
			data->item_list = xreallocarray(data->item_list,
			    data->item_size + 1, sizeof *data->item_list);
			item = &data->item_list[data->item_size++];

			item->type = WINDOW_TREE_WINDOW;
			item->session = s->id;
			item->winlink = wl->idx;
			item->pane = -1;

			xasprintf(&name, "%u:%s", wl->idx, wl->window->name);
			text = xstrdup("XXX WINDOW"); //XXX

			mti2 = mode_tree_add(data->data, mti, item,
			    (uint64_t)wl, name, text);

			free(text);
			free(name);

			if (window_count_panes(wl->window) == 1)
				continue;

			n = 0;
			TAILQ_FOREACH(wp, &wl->window->panes, entry) {
				data->item_list = xreallocarray(data->item_list,
				    data->item_size + 1,
				    sizeof *data->item_list);
				item = &data->item_list[data->item_size++];

				item->type = WINDOW_TREE_PANE;
				item->session = s->id;
				item->winlink = wl->idx;
				item->pane = wp->id;

				xasprintf(&name, "%u", n);
				text = xstrdup("XXX PANE"); //XXX

				mode_tree_add(data->data, mti2, item,
				    (uint64_t)wp, name, text);

				free(text);
				free(name);

				n++;
			}
		}
	}
}

static struct screen *
window_tree_draw(__unused void *modedata, void *itemdata, u_int sx, u_int sy)
{
	struct window_tree_itemdata	*item = itemdata;
	struct session			*sp;
	struct winlink			*wlp;
	struct window_pane		*wp;
	static struct screen		 s;
	struct screen_write_ctx		 ctx;

	window_tree_pull_item(item, &sp, &wlp, &wp);
	if (wp == NULL)
		return (NULL);

	screen_init(&s, sx, sy, 0);

	screen_write_start(&ctx, NULL, &s);

	screen_write_preview(&ctx, &wp->base, sx, sy);

	screen_write_stop(&ctx);
	return (&s);
}

static struct screen *
window_tree_init(struct window_pane *wp, __unused struct args *args)
{
	struct window_tree_modedata	*data;
	struct screen			*s;

	wp->modedata = data = xcalloc(1, sizeof *data);

	data->data = mode_tree_start(wp, window_tree_build,
	    window_tree_draw, data, window_tree_sort_list,
	    nitems(window_tree_sort_list), &s);

	mode_tree_build(data->data);
	mode_tree_draw(data->data);

	return (s);
}

static void
window_tree_free(struct window_pane *wp)
{
	struct window_tree_modedata	*data = wp->modedata;
	u_int				 i;

	if (data == NULL)
		return;

	mode_tree_free(data->data);

	for (i = 0; i < data->item_size; i++)
		window_tree_free_item(&data->item_list[i]);
	free(data->item_list);

	free(data);
}

static void
window_tree_resize(struct window_pane *wp, u_int sx, u_int sy)
{
	struct window_tree_modedata	*data = wp->modedata;

	mode_tree_resize(data->data, sx, sy);
}

static void
window_tree_key(struct window_pane *wp, __unused struct client *c,
    __unused struct session *s, key_code key, struct mouse_event *m)
{
	struct window_tree_modedata	*data = wp->modedata;
	int				 finished;

	/*
	 * t = toggle tag
	 * T = tag none
	 * C-t = tag all
	 * q = exit
	 * O = change sort order
	 *
	 * XXX
	 */

	finished = mode_tree_key(data->data, &key, m);
#if 0
	switch (key) {
	}
#endif
	if (finished)
		window_pane_reset_mode(wp);
	else {
		mode_tree_draw(data->data);
		wp->flags |= PANE_REDRAW;
	}
}
