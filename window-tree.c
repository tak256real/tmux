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

#define SESSION_DEFAULT_COMMAND " switch-client -t '%%'"
#define WINDOW_DEFAULT_COMMAND " select-window -t '%%'"
#define PANE_DEFAULT_COMMAND " select-pane -t '%%'"

#define WINDOW_TREE_SESSION_TEMPLATE				\
	" #{session_name}: #{session_windows} windows"		\
	"#{?session_grouped, (group ,}"				\
	"#{session_group}#{?session_grouped,),}"		\
	"#{?session_attached, (attached),}"
#define WINDOW_TREE_WINDOW_TEMPLATE				\
	"#{window_index}: #{window_name}#{window_flags} "	\
	"\"#{pane_title}\" (#{window_panes} panes)"
#define WINDOW_TREE_PANE_TEMPLATE				\
	"#{pane_index}: #{pane_id}: - (#{pane_tty}) "

const struct window_mode window_tree_mode = {
	.init = window_tree_init,
	.free = window_tree_free,
	.resize = window_tree_resize,
	.key = window_tree_key,
};

struct window_tree_data;
struct window_tree_item {
	struct window_tree_data		*data;

	u_int				 number;
	u_int				 order;
	const char			*name;

	RB_ENTRY (window_tree_item)	 entry;
};

RB_HEAD(window_tree_tree, window_tree_item);
static int window_tree_cmp(const struct window_tree_item *,
    const struct window_tree_item *);
RB_GENERATE_STATIC(window_tree_tree, window_tree_item, entry,
    window_tree_cmp);

enum window_tree_order {
	WINDOW_TREE_BUFFER_BY_NAME,
	WINDOW_TREE_BUFFER_BY_TIME,
};

enum window_tree_type {
	WINDOW_TREE_SESSION,
	WINDOW_TREE_WINDOW,
	WINDOW_TREE_PANE,
};

struct window_tree_data {
	struct session			*s;
	struct winlink			*wl;
	struct window_pane		*wp;
	char				*command;
	struct screen			 screen;
	u_int				 offset, wl_count, wp_count;
	struct window_tree_item		*current;

	u_int				 width;
	u_int				 height;

	struct window_tree_tree 	 tree;
	u_int				 number;
	enum window_tree_order	 	 order;
	enum window_tree_type		 type;
};

static struct window_tree_item *window_tree_add_item(struct window_tree_data *,
			     struct format_tree *, enum window_tree_type);

static void	window_tree_up(struct window_tree_data *);
static void	window_tree_down(struct window_tree_data *);
static void	window_tree_run_command(struct client *, const char *,
		    const char *);
static void	window_tree_free_tree(struct window_tree_data *);
static void	window_tree_build_tree(struct window_tree_data *);
static void	window_tree_draw_screen(struct window_pane *);

static u_int	global_tree_order = 0;

static int
window_tree_cmp(const struct window_tree_item *a,
    const struct window_tree_item *b)
{
	if (a->order < b->order)
		return (-1);
	if (a->order > b->order)
		return (1);
	return (0);
}

static struct screen *
window_tree_init(struct window_pane *wp, __unused struct args *args)
{
	struct window_tree_data		*data;
	struct screen			*s;

	wp->modedata = data = xcalloc(1, sizeof *data);

	s = &data->screen;
	screen_init(s, screen_size_x(&wp->base), screen_size_y(&wp->base), 0);
	s->mode &= ~MODE_CURSOR;

	data->order = WINDOW_TREE_BUFFER_BY_NAME;
	RB_INIT(&data->tree);
	window_tree_build_tree(data);

	window_tree_draw_screen(wp);

	return (s);
}

static void
window_tree_free(struct window_pane *wp)
{
	struct window_tree_data	*data = wp->modedata;

	if (data == NULL)
		return;

	window_tree_free_tree(data);

	screen_free(&data->screen);

	free(data->command);
	free(data);
}

static void
window_tree_resize(struct window_pane *wp, u_int sx, u_int sy)
{
	struct window_tree_data	*data = wp->modedata;
	struct screen			*s = &data->screen;

	screen_resize(s, sx, sy, 0);
	window_tree_build_tree(data);
	window_tree_draw_screen(wp);
	wp->flags |= PANE_REDRAW;
}

static void
window_tree_key(struct window_pane *wp, struct client *c,
    __unused struct session *s, key_code key, struct mouse_event *m)
{
	struct window_tree_data	*data = wp->modedata;
	struct window_tree_item	*item;
	u_int			 i, x, y;
	int			 finished, found;
	char			*command, *name;

	/*
	 * q = exit
	 * O = change sort order
	 * ENTER = paste buffer
	 */

	if (key == KEYC_MOUSEDOWN1_PANE) {
		if (cmd_mouse_at(wp, m, &x, &y, 0) != 0)
			return;
		if (x > data->width || y > data->height)
			return;
		found = 0;
		RB_FOREACH(item, window_tree_tree, &data->tree) {
			if (item->number == data->offset + y) {
				found = 1;
				data->current = item;
			}
		}
		if (found && key == KEYC_MOUSEDOWN1_PANE)
			key = '\r';
	}

	finished = 0;
	switch (key) {
	case KEYC_UP:
	case 'k':
	case KEYC_WHEELUP_PANE:
		window_tree_up(data);
		break;
	case KEYC_DOWN:
	case 'j':
	case KEYC_WHEELDOWN_PANE:
		window_tree_down(data);
		break;
	case KEYC_PPAGE:
	case '\002': /* C-b */
		for (i = 0; i < data->height; i++) {
			if (data->current->number == 0)
				break;
			window_tree_up(data);
		}
		break;
	case KEYC_NPAGE:
	case '\006': /* C-f */
		for (i = 0; i < data->height; i++) {
			if (data->current->number == data->number - 1)
				break;
			window_tree_down(data);
		}
		break;
	case KEYC_HOME:
		data->current = RB_MIN(window_tree_tree, &data->tree);
		data->offset = 0;
		break;
	case KEYC_END:
		data->current = RB_MAX(window_tree_tree, &data->tree);
		if (data->current->number > data->height - 1)
			data->offset = data->number - data->height;
		else
			data->offset = 0;
		break;
#if 0
	case 'O':

		if (data->order == WINDOW_BUFFER_BY_NAME)
			data->order = WINDOW_BUFFER_BY_TIME;
		else if (data->order == WINDOW_BUFFER_BY_TIME)
			data->order = WINDOW_BUFFER_BY_NAME;
		window_tree_build_tree(data);
		break;
#endif
	case '\r':
		command = xstrdup(data->command);
		name = xstrdup(data->current->name);
		window_pane_reset_mode(wp);
		window_tree_run_command(c, command, name);
		free(name);
		free(command);
		return;
	case 'q':
	case '\033': /* Escape */
		finished = 1;
		break;
	}
	if (finished)
		window_pane_reset_mode(wp);
	else {
		window_tree_draw_screen(wp);
		wp->flags |= PANE_REDRAW;
	}
}

static void
window_tree_up(struct window_tree_data *data)
{
	data->current = RB_PREV(window_tree_tree, &data->tree, data->current);
	if (data->current == NULL) {
		data->current = RB_MAX(window_tree_tree, &data->tree);
		if (data->current->number > data->height - 1)
			data->offset = data->number - data->height;
	} else if (data->current->number < data->offset)
		data->offset--;
}

static void
window_tree_down(struct window_tree_data *data)
{
	data->current = RB_NEXT(window_tree_tree, &data->tree, data->current);
	if (data->current == NULL) {
		data->current = RB_MIN(window_tree_tree, &data->tree);
		data->offset = 0;
	} else if (data->current->number > data->offset + data->height - 1)
		data->offset++;
}

static void
window_tree_run_command(struct client *c, const char *template,
    const char *name)
{
	struct cmdq_item	*new_item;
	struct cmd_list		*cmdlist;
	char			*command, *cause;

	command = cmd_template_replace(template, name, 1);
	if (command == NULL || *command == '\0')
		return;

	cmdlist = cmd_string_parse(command, NULL, 0, &cause);
	if (cmdlist == NULL) {
		if (cause != NULL && c != NULL) {
			*cause = toupper((u_char)*cause);
			status_message_set(c, "%s", cause);
		}
		free(cause);
	} else {
		new_item = cmdq_get_command(cmdlist, NULL, NULL, 0);
		cmdq_append(c, new_item);
		cmd_list_free(cmdlist);
	}

	free (command);
}

static void
window_tree_free_tree(struct window_tree_data *data)
{
	struct window_tree_item	*item, *item1;

	RB_FOREACH_SAFE(item, window_tree_tree, &data->tree, item1) {
		free((void *)item->name);

		RB_REMOVE(window_tree_tree, &data->tree, item);
		free(item);
	}
}

static struct window_tree_item *
window_tree_add_item(struct window_tree_data *data, struct format_tree *ft,
    enum window_tree_type ctt)
{
	struct window_tree_item	*item;
	struct session		*s = data->s;
	struct winlink		*wl = data->wl;
	char			*fmt = NULL;
	u_int			 no_of_wl = 0, no_of_wp = 0;
	int			 has_panes = 0;

	item = xcalloc(1, sizeof *item);
	item->data = data;

	switch (ctt) {
	case WINDOW_TREE_SESSION:
		/* XXX: What about expand/collapse of sessions? */
		item->name = format_expand(ft, WINDOW_TREE_SESSION_TEMPLATE);
		data->type = WINDOW_TREE_SESSION;
		break;
	case WINDOW_TREE_WINDOW:
		no_of_wl = winlink_count(&s->windows);
		has_panes = window_count_panes(wl->window) > 1;
		if (data->wl_count < no_of_wl && !has_panes) {
			xasprintf(&fmt, " \001tq\001> %s",
			    WINDOW_TREE_WINDOW_TEMPLATE);
		} else {
			if (data->wl_count == no_of_wl) {
				xasprintf(&fmt, " \001mq\001> %s",
				   WINDOW_TREE_WINDOW_TEMPLATE);
			} else {
				xasprintf(&fmt, " \001tq\001> %s",
				    WINDOW_TREE_WINDOW_TEMPLATE);
			}
		}
		item->name = format_expand(ft, fmt);
		data->type = WINDOW_TREE_WINDOW;
		break;
	case WINDOW_TREE_PANE:
		no_of_wp = window_count_panes(wl->window);
		if (data->wp_count < no_of_wp) {
			xasprintf(&fmt, " \001x   tq\001> %s",
			    WINDOW_TREE_PANE_TEMPLATE);
		} else {
			if (data->wp_count == no_of_wp) {
				xasprintf(&fmt, " \001x   mq\001> %s",
				    WINDOW_TREE_PANE_TEMPLATE);
			} else {
				xasprintf(&fmt, " \001x   tq\001> %s",
				    WINDOW_TREE_PANE_TEMPLATE);
			}
		}
		item->name = format_expand(ft, fmt);
		data->type = WINDOW_TREE_PANE;
		break;
	}

	item->order = global_tree_order++;
	RB_INSERT(window_tree_tree, &data->tree, item);

	free(fmt);

	return (item);
}

static void
window_tree_build_tree(struct window_tree_data *data)
{
	struct screen			*s = &data->screen;
	struct session			*sess;
	struct winlink			*wl;
	struct window_pane		*wp;
	struct window_tree_item		*item;
	struct format_tree		*ft;
	char				*name;
	struct window_tree_item		*current;

	if (data->current != NULL)
		name = xstrdup(data->current->name);
	else
		name = NULL;
	current = NULL;

	window_tree_free_tree(data);

	RB_FOREACH(sess, sessions, &sessions) {
		data->s = sess;
		data->wl_count = data->wp_count = 0;
		ft = format_create(NULL, NULL, 0, 0);
		format_defaults(ft, NULL, sess, NULL, NULL);
		item = window_tree_add_item(data, ft, WINDOW_TREE_SESSION);
		format_free(ft);

		RB_FOREACH(wl, winlinks, &sess->windows) {
			data->wl = wl;
			data->wl_count++;
			ft = format_create(NULL, NULL, 0, 0);
			format_defaults(ft, NULL, sess, wl, NULL);
			item = window_tree_add_item(data, ft,
			    WINDOW_TREE_WINDOW);
			format_free(ft);

			if (window_count_panes(wl->window) == 1)
				continue;

			TAILQ_FOREACH(wp, &wl->window->panes, entry) {
				data->wp = wp;
				data->wp_count++;
				ft = format_create(NULL, NULL, 0, 0);
				format_defaults(ft, NULL, sess, wl, wp);
				item = window_tree_add_item(data, ft,
				    WINDOW_TREE_PANE);
				format_free(ft);
			}
		}
		if (name != NULL && strcmp(name, item->name) == 0)
			current = item;
	}

	data->number = 0;
	RB_FOREACH(item, window_tree_tree, &data->tree) {
		item->number = data->number++;
	}

	if (current != NULL)
		data->current = current;
	else
		data->current = RB_MIN(window_tree_tree, &data->tree);
	free(name);

	data->width = screen_size_x(s);
	data->height = (screen_size_y(s) / 3) * 2;
	if (data->height > data->number)
		data->height = screen_size_y(s) / 2;
	if (data->height < 10)
		data->height = screen_size_y(s);
	if (screen_size_y(s) - data->height < 2)
		data->height = screen_size_y(s);

	if (data->current == NULL)
		return;
	current = data->current;
	if (current->number < data->offset) {
		if (current->number > data->height - 1)
			data->offset = current->number - (data->height - 1);
		else
			data->offset = 0;
	}
	if (current->number > data->offset + data->height - 1) {
		if (current->number > data->height - 1)
			data->offset = current->number - (data->height - 1);
		else
			data->offset = 0;
	}
}

static void
window_tree_draw_screen(struct window_pane *wp)
{
	struct window_tree_data	*data = wp->modedata;
	struct screen			*s = &data->screen;
	struct window_tree_item		*item;
	struct options			*oo = wp->window->options;
	struct screen_write_ctx	 	 ctx;
	struct grid_cell		 gc0;
	struct grid_cell		 gc;
	u_int				 width, height, i, at, needed;
	char				 line[1024], *cp;
	const char			*pdata, *end, *label, *name;
	size_t				 psize = 0;

	memcpy(&gc0, &grid_default_cell, sizeof gc0);
	memcpy(&gc, &grid_default_cell, sizeof gc);
	style_apply(&gc, oo, "mode-style");

	width = data->width;
	if (width >= sizeof line)
		width = (sizeof line) - 1;
	height = data->height;

	screen_write_start(&ctx, NULL, s);
	screen_write_clearscreen(&ctx, 8);

	i = 0;
	RB_FOREACH(item, window_tree_tree, &data->tree) {
		i++;

		if (i <= data->offset)
			continue;
		if (i - 1 > data->offset + height - 1)
			break;

		name = strdup(item->name);

		screen_write_cursormove(&ctx, 0, i - 1 - data->offset);

		snprintf(line, sizeof line, "%-16s ", name);
		free((void *)name);

		if (item != data->current) {
			screen_write_puts(&ctx, &gc0, "%.*s", width, line);
			screen_write_clearendofline(&ctx, 8);
			continue;
		}
		screen_write_puts(&ctx, &gc, "%-*.*s", width, width, line);
	}

	if (height == screen_size_y(s)) {
		screen_write_stop(&ctx);
		return;
	}

	screen_write_cursormove(&ctx, 0, height);
	screen_write_box(&ctx, width, screen_size_y(s) - height);

	label = "sort: name";
	needed = strlen(data->current->name) + strlen (label) + 5;
	if (width - 2 >= needed) {
		screen_write_cursormove(&ctx, 1, height);
		screen_write_puts(&ctx, &gc0, " %s (%s) ", data->current->name,
		    label);
	}

	pdata = NULL;
	psize = 0;
	end = pdata;
	for (i = height + 1; i < screen_size_y(s) - 1; i++) {
		gc0.attr |= GRID_ATTR_CHARSET;
		screen_write_cursormove(&ctx, 0, i);
		screen_write_putc(&ctx, &gc0, 'x');
		gc0.attr &= ~GRID_ATTR_CHARSET;

		at = 0;
		while (end != pdata + psize && *end != '\n') {
			if ((sizeof line) - at > 5) {
				cp = vis(line + at, *end, VIS_TAB|VIS_OCTAL, 0);
				at = cp - line;
			}
			end++;
		}
		if (at > width - 2)
			at = width - 2;
		line[at] = '\0';

		if (*line != '\0')
			screen_write_puts(&ctx, &gc0, "%s", line);
		while (s->cx != width - 1)
			screen_write_putc(&ctx, &grid_default_cell, ' ');

		if (end == pdata + psize)
			break;
		end++;
	}

	screen_write_stop(&ctx);
}
