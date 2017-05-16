/* $OpenBSD$ */

/*
 * Copyright (c) 2012 Thomas Adam <thomas@xteddy.org>
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

static enum cmd_retval	cmd_choose_tree_exec(struct cmd *, struct cmdq_item *);

const struct cmd_entry cmd_choose_tree_entry = {
	.name = "choose-tree",
	.alias = NULL,

<<<<<<< HEAD
	.args = { "ut:", 0, 1 },
	.usage = "[-u] " CMD_TARGET_WINDOW_USAGE,
=======
	.args = { "S:W:swub:c:t:", 0, 1 },
	.usage = "[-suw] [-b session-template] [-c window template] "
		 "[-S format] [-W format] " CMD_TARGET_WINDOW_USAGE,

	.target = { 't', CMD_FIND_WINDOW, 0 },

	.flags = 0,
	.exec = cmd_choose_tree_exec
};

const struct cmd_entry cmd_choose_session_entry = {
	.name = "choose-session",
	.alias = NULL,

	.args = { "F:t:", 0, 1 },
	.usage = CMD_TARGET_WINDOW_USAGE " [-F format] [template]",

	.target = { 't', CMD_FIND_WINDOW, 0 },

	.flags = 0,
	.exec = cmd_choose_tree_exec
};

const struct cmd_entry cmd_choose_window_entry = {
	.name = "choose-window",
	.alias = NULL,

	.args = { "F:t:", 0, 1 },
	.usage = CMD_TARGET_WINDOW_USAGE "[-F format] [template]",
>>>>>>> master

	.target = { 't', CMD_FIND_WINDOW, 0 },

	.flags = 0,
	.exec = cmd_choose_tree_exec
};

static enum cmd_retval
cmd_choose_tree_exec(struct cmd *self, struct cmdq_item *item)
{
	struct args			*args = self->args;
<<<<<<< HEAD
	struct client			*c = item->state.c;
	struct winlink			*wl = item->state.tflag.wl, *wm;
=======
	struct client			*c = cmd_find_client(item, NULL, 1);
	struct winlink			*wl = item->target.wl, *wm;
	struct session			*s = item->target.s, *s2;
	struct window_choose_data	*wcd = NULL;
	const char			*ses_template, *win_template;
	char				*final_win_action, *cur_win_template;
	char				*final_win_template_middle;
	char				*final_win_template_last;
	const char			*ses_action, *win_action;
	u_int				 cur_win, idx_ses, win_ses, win_max;
	u_int				 wflag, sflag;

	ses_template = win_template = NULL;
	ses_action = win_action = NULL;
>>>>>>> master

	if (c == NULL) {
		cmdq_error(item, "no client available");
		return (CMD_RETURN_ERROR);
	}

	if (window_pane_set_mode(wl->window->active, &choose_tree_mode,
	    args) != 0) {
		return (CMD_RETURN_NORMAL);
	}

	return (CMD_RETURN_NORMAL);
}
