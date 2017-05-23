/* $OpenBSD$ */

/*
 * Copyright (c) 2010 Nicholas Marriott <nicholas.marriott@gmail.com>
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

#include "tmux.h"

static int	tty_acs_cmp(const void *, const void *);

/* Table mapping ACS entries to UTF-8. */
struct tty_acs_entry {
	u_char	 	 key;
	const char	*string;
};
static const struct tty_acs_entry tty_acs_table[] = {
	{ '+', ">" },	/* arrow pointing right */
	{ ',', "<" },	/* arrow pointing left */
	{ '-', "^" },	/* arrow pointing up */
	{ '.', "." },	/* arrow pointing down */
	{ '0', "#" },	/* solid square block */
	{ '`', "#" },	/* diamond */
	{ 'a', "#" },	/* checker board (stipple) */
	{ 'f', "#" },	/* degree symbol */
	{ 'g', "#" },	/* plus/minus */
	{ 'h', "#" },	/* board of squares */
	{ 'i', "#" },	/* lantern symbol */
	{ 'j', "+" },	/* lower right corner */
	{ 'k', "+" },	/* upper right corner */
	{ 'l', "+" },	/* upper left corner */
	{ 'm', "+" },	/* lower left corner */
	{ 'n', "+" },	/* large plus or crossover */
	{ 'o', "_" },	/* scan line 1 */
	{ 'p', "_" },	/* scan line 3 */
	{ 'q', "-" },	/* horizontal line */
	{ 'r', "_" },	/* scan line 7 */
	{ 's', "_" },	/* scan line 9 */
	{ 't', "+" },	/* tee pointing right */
	{ 'u', "+" },	/* tee pointing left */
	{ 'v', "+" },	/* tee pointing up */
	{ 'w', "+" },	/* tee pointing down */
	{ 'x', "|" },	/* vertical line */
	{ 'y', "#" },	/* less-than-or-equal-to */
	{ 'z', "#" },	/* greater-than-or-equal-to */
	{ '{', "#" },	/* greek pi */
	{ '|', "#" },	/* not-equal */
	{ '}', "#" },	/* UK pound sign */
	{ '~', "*" }	/* bullet */
};

static int
tty_acs_cmp(const void *key, const void *value)
{
	const struct tty_acs_entry	*entry = value;
	u_char				 ch;

	ch = *(u_char *) key;
	return (ch - entry->key);
}

/* Retrieve ACS to output as a string. */
const char *
tty_acs_get(struct tty *tty, u_char ch)
{
	struct tty_acs_entry *entry;

	/* If pane-border-ascii */
	if ( options_get_number(global_s_options, "pane-border-ascii") != 0 ) {
		if (tty->term->acs[ch][0] == '\0')
			return (NULL);
		return (&tty->term->acs[ch][0]);
	}

	/* Otherwise look up the UTF-8 translation. */
	entry = bsearch(&ch,
	    tty_acs_table, nitems(tty_acs_table), sizeof tty_acs_table[0],
	    tty_acs_cmp);
	if (entry == NULL)
		return (NULL);
	return (entry->string);
}
