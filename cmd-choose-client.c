/* $OpenBSD$ */

/*
 * Copyright (c) 2009 Nicholas Marriott <nicholas.marriott@gmail.com>
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

#include "tmux.h"

/*
 * Enter client mode.
 */

<<<<<<< HEAD
=======
#define CHOOSE_CLIENT_TEMPLATE					\
	"#{client_name}: #{session_name} "			\
	"[#{client_width}x#{client_height} #{client_termname}]"	\
	"#{?client_utf8, (utf8),}#{?client_readonly, (ro),} "	\
	"(last used #{t:client_activity})"

>>>>>>> master
static enum cmd_retval	cmd_choose_client_exec(struct cmd *,
			    struct cmdq_item *);

const struct cmd_entry cmd_choose_client_entry = {
	.name = "choose-client",
	.alias = NULL,

	.args = { "t:", 0, 1 },
	.usage = CMD_TARGET_PANE_USAGE " [template]",

<<<<<<< HEAD
	.tflag = CMD_PANE,
=======
	.target = { 't', CMD_FIND_WINDOW, 0 },
>>>>>>> master

	.flags = 0,
	.exec = cmd_choose_client_exec
};

static enum cmd_retval
cmd_choose_client_exec(struct cmd *self, struct cmdq_item *item)
{
<<<<<<< HEAD
	struct args		*args = self->args;
	struct window_pane	*wp = item->state.tflag.wp;

	if (server_client_how_many() != 0)
		window_pane_set_mode(wp, &window_client_mode, args);
=======
	struct args			*args = self->args;
	struct client			*c = cmd_find_client(item, NULL, 1);
	struct client			*c1;
	struct window_choose_data	*cdata;
	struct winlink			*wl = item->target.wl;
	const char			*template;
	char				*action;
	u_int			 	 idx, cur;

	if (c == NULL) {
		cmdq_error(item, "no client available");
		return (CMD_RETURN_ERROR);
	}

	if (window_pane_set_mode(wl->window->active, &window_choose_mode) != 0)
		return (CMD_RETURN_NORMAL);

	if ((template = args_get(args, 'F')) == NULL)
		template = CHOOSE_CLIENT_TEMPLATE;

	if (args->argc != 0)
		action = xstrdup(args->argv[0]);
	else
		action = xstrdup("detach-client -t '%%'");

	cur = idx = 0;
	TAILQ_FOREACH(c1, &clients, entry) {
		if (c1->session == NULL)
			continue;
		if (c1 == item->client)
			cur = idx;

		cdata = window_choose_data_create(TREE_OTHER, c, c->session);
		cdata->idx = idx;

		cdata->ft_template = xstrdup(template);
		format_add(cdata->ft, "line", "%u", idx);
		format_defaults(cdata->ft, c1, NULL, NULL, NULL);

		cdata->command = cmd_template_replace(action, c1->name, 1);

		window_choose_add(wl->window->active, cdata);

		idx++;
	}
	free(action);

	window_choose_ready(wl->window->active, cur,
	    cmd_choose_client_callback);
>>>>>>> master

	return (CMD_RETURN_NORMAL);
}
