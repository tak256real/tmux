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

// XXX XXX
// TODO:
// tree mode only commands (what?)
//               K = kill tagged (needs to go on cmdq in case killing cur pane)
//               s = swap cur with first tagged?
//               j = join first tagged to cur?
//               apply command to all tagged (need to use command prompt)
// find-window
// man page
// do we want to use send -X or not?
// choose-tree flags to start with sessions, windows, panes, expanded
//

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

#define WINDOW_TREE_DEFAULT_COMMAND "switch-client -t '%%'"

const struct window_mode window_tree_mode = {
	.init = window_tree_init,
	.free = window_tree_free,
	.resize = window_tree_resize,
	.key = window_tree_key,
};

enum window_tree_sort_type {
	WINDOW_TREE_BY_INDEX,
	WINDOW_TREE_BY_NAME,
	WINDOW_TREE_BY_TIME,
};
static const char *window_tree_sort_list[] = {
	"index",
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
	struct window_pane		 *wp;
	int				  dead;
	int				  references;

	struct mode_tree_data		 *data;
	char				 *command;

	struct window_tree_itemdata	**item_list;
	u_int				  item_size;

	struct client			 *client;
	const char			 *entered;
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

static struct window_tree_itemdata *
window_tree_add_item(struct window_tree_modedata *data)
{
	struct window_tree_itemdata	*item;

	data->item_list = xreallocarray(data->item_list, data->item_size + 1,
	    sizeof *data->item_list);
	item = data->item_list[data->item_size++] = xcalloc(1, sizeof *item);
	return (item);
}

static void
window_tree_free_item(struct window_tree_itemdata *item)
{
	free(item);
}

static int
window_tree_cmp_session_name(const void *a0, const void *b0)
{
	const struct session *const *a = a0;
	const struct session *const *b = b0;

	return (strcmp((*a)->name, (*b)->name));
}

static int
window_tree_cmp_session_time(const void *a0, const void *b0)
{
	const struct session *const *a = a0;
	const struct session *const *b = b0;

	if (timercmp(&(*a)->activity_time, &(*b)->activity_time, >))
		return (-1);
	if (timercmp(&(*a)->activity_time, &(*b)->activity_time, <))
		return (1);
	return (strcmp((*a)->name, (*b)->name));
}

static int
window_tree_cmp_window_name(const void *a0, const void *b0)
{
	const struct winlink *const *a = a0;
	const struct winlink *const *b = b0;

	return (strcmp((*a)->window->name, (*b)->window->name));
}

static int
window_tree_cmp_window_time(const void *a0, const void *b0)
{
	const struct winlink *const *a = a0;
	const struct winlink *const *b = b0;

	if (timercmp(&(*a)->window->activity_time,
	    &(*b)->window->activity_time, >))
		return (-1);
	if (timercmp(&(*a)->window->activity_time,
	    &(*b)->window->activity_time, <))
		return (1);
	return (strcmp((*a)->window->name, (*b)->window->name));
}

static int
window_tree_cmp_pane_time(const void *a0, const void *b0)
{
	const struct window_pane *const *a = a0;
	const struct window_pane *const *b = b0;

	if ((*a)->active_point < (*b)->active_point)
		return (-1);
	if ((*a)->active_point > (*b)->active_point)
		return (1);
	return (0);
}

static void
window_tree_build(void *modedata, u_int sort_type)
{
	struct window_tree_modedata	 *data = modedata;
	struct window_tree_itemdata	 *item;
	struct session			 *s;
	struct winlink			 *wl;
	struct window_pane		 *wp;
	struct mode_tree_item		 *mti, *mti2;
	char				 *name, *text;
	u_int				  i, j, k, ssn, wwn, ppn;
	struct session			**ss;
	struct winlink			**ww;
	struct window_pane		**pp;

	for (i = 0; i < data->item_size; i++)
		window_tree_free_item(data->item_list[i]);
	free(data->item_list);
	data->item_list = NULL;
	data->item_size = 0;

	ss = NULL;
	ssn = 0;
	RB_FOREACH(s, sessions, &sessions) {
		ss = xreallocarray(ss, ssn + 1, sizeof *ss);
		ss[ssn++] = s;
	}
	switch (sort_type) {
	case WINDOW_TREE_BY_INDEX:
		break;
	case WINDOW_TREE_BY_NAME:
		qsort(ss, ssn, sizeof *ss, window_tree_cmp_session_name);
		break;
	case WINDOW_TREE_BY_TIME:
		qsort(ss, ssn, sizeof *ss, window_tree_cmp_session_time);
		break;
	}

	for (i = 0; i < ssn; i++) {
		s = ss[i];

		item = window_tree_add_item(data);
		item->type = WINDOW_TREE_SESSION;
		item->session = s->id;
		item->winlink = -1;
		item->pane = -1;

		text = format_single(NULL,
		    "#{session_windows} windows"
		    "#{?session_grouped, (group ,}"
		    "#{session_group}#{?session_grouped,),}"
		    "#{?session_attached, (attached),}",
		    NULL, s, NULL, NULL);

		mti = mode_tree_add(data->data, NULL, item, (uint64_t)s,
		    s->name, text);

		free(text);

		ww = NULL;
		wwn = 0;
		RB_FOREACH(wl, winlinks, &s->windows) {
			ww = xreallocarray(ww, wwn + 1, sizeof *ww);
			ww[wwn++] = wl;
		}
		switch (sort_type) {
		case WINDOW_TREE_BY_INDEX:
			break;
		case WINDOW_TREE_BY_NAME:
			qsort(ww, wwn, sizeof *ww, window_tree_cmp_window_name);
			break;
		case WINDOW_TREE_BY_TIME:
			qsort(ww, wwn, sizeof *ww, window_tree_cmp_window_time);
			break;
		}

		for (j = 0; j < wwn; j++) {
			wl = ww[j];

			item = window_tree_add_item(data);
			item->type = WINDOW_TREE_WINDOW;
			item->session = s->id;
			item->winlink = wl->idx;
			item->pane = -1;

			text = format_single(NULL,
			    "#{window_name}#{window_flags} "
			    "(#{window_panes} panes)",
			    NULL, s, wl, NULL);
			xasprintf(&name, "%u", wl->idx);

			mti2 = mode_tree_add(data->data, mti, item,
			    (uint64_t)wl, name, text);

			free(text);
			free(name);

			pp = NULL;
			ppn = 0;
			TAILQ_FOREACH(wp, &wl->window->panes, entry) {
				pp = xreallocarray(pp, ppn + 1, sizeof *pp);
				pp[ppn++] = wp;
			}
			switch (sort_type) {
			case WINDOW_TREE_BY_INDEX:
				break;
			case WINDOW_TREE_BY_NAME:
				/*
				 * Panes don't have names, so leave in number
				 * order.
				 */
				break;
			case WINDOW_TREE_BY_TIME:
				qsort(pp, ppn, sizeof *pp,
				    window_tree_cmp_pane_time);
				break;
			}

			for (k = 0; k < ppn; k++) {
				wp = pp[k];

				item = window_tree_add_item(data);
				item->type = WINDOW_TREE_PANE;
				item->session = s->id;
				item->winlink = wl->idx;
				item->pane = wp->id;

				text = format_single(NULL,
				    "#{pane_current_command} (#{pane_tty}) "
				    "\"#{pane_title}\"",
				    NULL, s, wl, wp);
				xasprintf(&name, "%u", k);

				mode_tree_add(data->data, mti2, item,
				    (uint64_t)wp, name, text);

				free(text);
				free(name);
			}
			free(pp);
		}
		free(ww);
	}
	free(ss);
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

	data->wp = wp;
	data->references = 1;

	if (args == NULL || args->argc == 0)
		data->command = xstrdup(WINDOW_TREE_DEFAULT_COMMAND);
	else
		data->command = xstrdup(args->argv[0]);

	data->data = mode_tree_start(wp, window_tree_build,
	    window_tree_draw, data, window_tree_sort_list,
	    nitems(window_tree_sort_list), &s);

	mode_tree_build(data->data);
	mode_tree_draw(data->data);

	return (s);
}

static void
window_tree_destroy(struct window_tree_modedata *data)
{
	u_int	i;

	if (--data->references != 0)
		return;

	mode_tree_free(data->data);

	for (i = 0; i < data->item_size; i++)
		window_tree_free_item(data->item_list[i]);
	free(data->item_list);

	free(data->command);
	free(data);
}

static void
window_tree_free(struct window_pane *wp)
{
	struct window_tree_modedata *data = wp->modedata;

	if (data == NULL)
		return;

	data->dead = 1;
	window_tree_destroy(data);
}

static void
window_tree_resize(struct window_pane *wp, u_int sx, u_int sy)
{
	struct window_tree_modedata	*data = wp->modedata;

	mode_tree_resize(data->data, sx, sy);
}

static char *
window_tree_get_target(struct window_tree_itemdata *item,
    struct cmd_find_state *fs)
{
	struct session		*s;
	struct winlink		*wl;
	struct window_pane	*wp;
	char			*target;

	window_tree_pull_item(item, &s, &wl, &wp);

	target = NULL;
	switch (item->type) {
	case WINDOW_TREE_SESSION:
		if (s == NULL)
			break;
		xasprintf(&target, "=%s:", s->name);
		break;
	case WINDOW_TREE_WINDOW:
		if (s == NULL || wl == NULL)
			break;
		xasprintf(&target, "=%s:%u.", s->name, wl->idx);
		break;
	case WINDOW_TREE_PANE:
		if (s == NULL || wl == NULL || wp == NULL)
			break;
		xasprintf(&target, "=%s:%u.%%%u", s->name, wl->idx, wp->id);
		break;
	}
	if (target == NULL)
		cmd_find_clear_state(fs, 0);
	else
		cmd_find_from_winlink_pane(fs, wl, wp);
	return (target);
}

static void
window_tree_command_each(void* modedata, void* itemdata, __unused key_code key)
{
	struct window_tree_modedata	*data = modedata;
	struct window_tree_itemdata	*item = itemdata;
	char				*name;
	struct cmd_find_state		 fs;

	name = window_tree_get_target(item, &fs);
	if (name != NULL)
		mode_tree_run_command(data->client, &fs, data->entered, name);
	free(name);
}

static enum cmd_retval
window_tree_command_done(__unused struct cmdq_item *item, void *modedata)
{
	struct window_tree_modedata	*data = modedata;

	if (!data->dead) {
		mode_tree_build(data->data);
		mode_tree_draw(data->data);
		data->wp->flags |= PANE_REDRAW;
	}
	window_tree_destroy(data);
	return (CMD_RETURN_NORMAL);
}

static int
window_tree_command_callback(struct client *c, void *modedata, const char *s,
    __unused int done)
{
	struct window_tree_modedata	*data = modedata;

	if (data->dead)
		return (0);

	data->client = c;
	data->entered = s;

	mode_tree_each_tagged(data->data, window_tree_command_each, KEYC_NONE);

	data->client = NULL;
	data->entered = NULL;

	data->references++;
	cmdq_append(c, cmdq_get_callback(window_tree_command_done, data));

	return (0);
}

static void
window_tree_command_free(void *modedata)
{
	struct window_tree_modedata	*data = modedata;

	window_tree_destroy(data);
}

static void
window_tree_key(struct window_pane *wp, struct client *c,
    __unused struct session *s, key_code key, struct mouse_event *m)
{
	struct window_tree_modedata	*data = wp->modedata;
	struct window_tree_itemdata	*item;
	char				*command, *name, *prompt;
	struct cmd_find_state		 fs;
	int				 finished;
	u_int				 tagged;

	/*
	 * t = toggle tag
	 * T = tag none
	 * C-t = tag all
	 * q = exit
	 * O = change sort order
	 *
	 * Enter = select item
	 * Space = enter command
	 */

	finished = mode_tree_key(data->data, &key, m);
	switch (key) {
	case ' ':
		tagged = mode_tree_count_tagged(data->data);
		if (tagged == 0)
			break;
		data->references++;
		xasprintf(&prompt, "(%u tagged) ", tagged);
		status_prompt_set(c, prompt, "", window_tree_command_callback,
		    window_tree_command_free, data, 0);
		free(prompt);
		break;
	case '\r':
		item = mode_tree_get_current(data->data);
		command = xstrdup(data->command);
		name = window_tree_get_target(item, &fs);
		window_pane_reset_mode(wp);
		if (name != NULL)
			mode_tree_run_command(c, &fs, command, name);
		free(name);
		free(command);
		return;
	}
	if (finished)
		window_pane_reset_mode(wp);
	else {
		mode_tree_draw(data->data);
		wp->flags |= PANE_REDRAW;
	}
}
