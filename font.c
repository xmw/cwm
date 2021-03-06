/*
 * font.c - cwm font abstraction
 *
 * Copyright (c) 2005 Marius Eriksen <marius@monkey.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/queue.h>

#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "calmwm.h"

int
font_ascent(struct screen_ctx *sc)
{
	return (sc->font->ascent);
}

int
font_descent(struct screen_ctx *sc)
{
	return (sc->font->descent);
}

u_int
font_height(struct screen_ctx *sc)
{
	return (sc->fontheight);
}

void
font_init(struct screen_ctx *sc)
{
	sc->xftdraw = XftDrawCreate(X_Dpy, sc->rootwin,
	    DefaultVisual(X_Dpy, sc->which), DefaultColormap(X_Dpy, sc->which));
	if (sc->xftdraw == NULL)
		errx(1, "XftDrawCreate");

	if (!XftColorAllocName(X_Dpy, DefaultVisual(X_Dpy, sc->which),
	    DefaultColormap(X_Dpy, sc->which), "black", &sc->xftcolor))
		errx(1, "XftColorAllocName");
}

int
font_width(struct screen_ctx *sc, const char *text, int len)
{
	XGlyphInfo	 extents;

	XftTextExtents8(X_Dpy, sc->font, (const XftChar8*)text,
	    len, &extents);

	return (extents.xOff);
}

void
font_draw(struct screen_ctx *sc, const char *text, int len,
    Drawable d, int x, int y)
{
	XftDrawChange(sc->xftdraw, d);
	/* Really needs to be UTF8'd. */
	XftDrawString8(sc->xftdraw, &sc->xftcolor, sc->font, x, y,
	    (const FcChar8*)text, len);
}

XftFont *
font_make(struct screen_ctx *sc, const char *name)
{
	XftFont		*fn = NULL;
	FcPattern	*pat, *patx;
	XftResult	 res;

	if ((pat = FcNameParse((const FcChar8*)name)) == NULL)
		return (NULL);

	if ((patx = XftFontMatch(X_Dpy, sc->which, pat, &res)) != NULL)
		fn = XftFontOpenPattern(X_Dpy, patx);

	FcPatternDestroy(pat);

	return (fn);
}
