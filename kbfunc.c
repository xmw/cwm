/*
 * calmwm - the calm window manager
 *
 * Copyright (c) 2004 Martin Murray <mmurray@monkey.org>
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
 *
 * $OpenBSD$
 */

#include <sys/param.h>
#include "queue.h"

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <paths.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "calmwm.h"

#define HASH_MARKER	"|1|"

extern sig_atomic_t	 cwm_status;

void
kbfunc_client_lower(struct client_ctx *cc, union arg *arg)
{
	client_ptrsave(cc);
	client_lower(cc);
}

void
kbfunc_client_raise(struct client_ctx *cc, union arg *arg)
{
	client_raise(cc);
}

#define TYPEMASK	(CWM_MOVE | CWM_RESIZE | CWM_PTRMOVE)
#define MOVEMASK	(CWM_UP | CWM_DOWN | CWM_LEFT | CWM_RIGHT)
void
kbfunc_client_moveresize(struct client_ctx *cc, union arg *arg)
{
	struct screen_ctx	*sc = cc->sc;
	struct geom		 xine;
	int			 x, y, flags, amt;
	unsigned int		 mx, my;

	if (cc->flags & (CLIENT_FREEZE|CLIENT_STICKY))
		return;

	mx = my = 0;

	flags = arg->i;
	amt = Conf.mamount;

	if (flags & CWM_BIGMOVE) {
		flags -= CWM_BIGMOVE;
		amt = amt * 10;
	}

	switch (flags & MOVEMASK) {
	case CWM_UP:
		my -= amt;
		break;
	case CWM_DOWN:
		my += amt;
		break;
	case CWM_RIGHT:
		mx += amt;
		break;
	case CWM_LEFT:
		mx -= amt;
		break;
	}
	switch (flags & TYPEMASK) {
	case CWM_MOVE:
		cc->geom.x += mx;
		if (cc->geom.x + cc->geom.w < 0)
			cc->geom.x = -cc->geom.w;
		if (cc->geom.x > sc->view.w - 1)
			cc->geom.x = sc->view.w - 1;
		cc->geom.y += my;
		if (cc->geom.y + cc->geom.h < 0)
			cc->geom.y = -cc->geom.h;
		if (cc->geom.y > sc->view.h - 1)
			cc->geom.y = sc->view.h - 1;

		xine = screen_find_xinerama(sc,
		    cc->geom.x + cc->geom.w / 2,
		    cc->geom.y + cc->geom.h / 2, CWM_GAP);
		cc->geom.x += client_snapcalc(cc->geom.x,
		    cc->geom.x + cc->geom.w + (cc->bwidth * 2),
		    xine.x, xine.x + xine.w, sc->snapdist);
		cc->geom.y += client_snapcalc(cc->geom.y,
		    cc->geom.y + cc->geom.h + (cc->bwidth * 2),
		    xine.y, xine.y + xine.h, sc->snapdist);

		client_move(cc);
		xu_ptr_getpos(cc->win, &x, &y);
		cc->ptr.x = x + mx;
		cc->ptr.y = y + my;
		client_ptrwarp(cc);
		break;
	case CWM_RESIZE:
		if ((cc->geom.w += mx) < 1)
			cc->geom.w = 1;
		if ((cc->geom.h += my) < 1)
			cc->geom.h = 1;
		client_resize(cc, 1);

		/* Make sure the pointer stays within the window. */
		xu_ptr_getpos(cc->win, &cc->ptr.x, &cc->ptr.y);
		if (cc->ptr.x > cc->geom.w)
			cc->ptr.x = cc->geom.w - cc->bwidth;
		if (cc->ptr.y > cc->geom.h)
			cc->ptr.y = cc->geom.h - cc->bwidth;
		client_ptrwarp(cc);
		break;
	case CWM_PTRMOVE:
		xu_ptr_getpos(sc->rootwin, &x, &y);
		xu_ptr_setpos(sc->rootwin, x + mx, y + my);
		break;
	default:
		warnx("invalid flags passed to kbfunc_client_moveresize");
	}
}

#define max(a,b) (a > b ? a : b)
#define min(a,b) (a < b ? a : b)
void
kbfunc_client_snap(struct client_ctx *cc, union arg *arg)
{
	struct gap		*gap = &cc->sc->gap;
	int 			flags = arg->i;
	struct region_ctx	*rc;
	struct client_ctx 	*oc;
	int			cw, ch, oh, ow, i, c, *h = NULL, *v = NULL;
	int			left, right, up, down, move;
	
	cw = cc->geom.w + 2*cc->bwidth;
	ch = cc->geom.h + 2*cc->bwidth;
	
	i = 0; // one additional for initgeom
	TAILQ_FOREACH(rc, &cc->sc->regionq, entry)
		i++;
	c = 0;
        TAILQ_FOREACH(oc, &cc->sc->clientq, entry) {
		if ((oc->flags & (CLIENT_HIDDEN | CLIENT_IGNORE)) ||
			(oc == cc)) continue;
		c++;
	}
	switch (flags & (CWM_MOVE |  CWM_RESIZE)) {
	case CWM_MOVE:
		c = i * 7 + c * 4; i = 0;
		h = malloc(sizeof(int) * c);
		v = malloc(sizeof(int) * c);
		TAILQ_FOREACH(rc, &cc->sc->regionq, entry) {
			ow = rc->area.w - gap->left - gap->right;
			h[i + 0] = rc->area.x + gap->left - cw / 2;
			h[i + 1] = rc->area.x + gap->left;
			h[i + 2] = rc->area.x + (ow - 2 * cw) / 2;
			h[i + 3] = rc->area.x + (ow -     cw) / 2;
			h[i + 4] = rc->area.x + (ow         ) / 2;
			h[i + 5] = rc->area.x + gap->left + ow - cw;
			h[i + 6] = rc->area.x + gap->left + ow - cw / 2;
			oh = rc->area.h - gap->top - gap->bottom;
			v[i + 0] = rc->area.y + gap->top - ch / 2;
			v[i + 1] = rc->area.y + gap->top;
			v[i + 2] = rc->area.y + (oh - 2 * ch) / 2;
			v[i + 3] = rc->area.y + (oh -     ch) / 2;
			v[i + 4] = rc->area.y + (oh         ) / 2;
			v[i + 5] = rc->area.y + gap->left + oh - ch;
			v[i + 6] = rc->area.y + gap->left + oh - ch / 2;
			i += 7;
		}
	        TAILQ_FOREACH(oc, &cc->sc->clientq, entry) {
			if ((oc->flags & (CLIENT_HIDDEN | CLIENT_IGNORE)) ||
				(oc == cc)) continue;
			ow = oc->geom.w + 2 * oc->bwidth;
			h[i + 0] = oc->geom.x      - cw;
			h[i + 1] = oc->geom.x;
			h[i + 2] = oc->geom.x + ow - cw;
			h[i + 3] = oc->geom.x + ow;
			oh = oc->geom.h + 2 * oc->bwidth;
			v[i + 0] = oc->geom.y      - ch;
			v[i + 1] = oc->geom.y;
			v[i + 2] = oc->geom.y + oh - ch;
			v[i + 3] = oc->geom.y + oh;
			i += 4;
		}
		left = up = INT_MIN; right = down = INT_MAX;
		while (i--) {
			if (h[i] < cc->geom.x && h[i] > left ) left  = h[i];
			if (h[i] > cc->geom.x && h[i] < right) right = h[i];
			if (v[i] < cc->geom.y && v[i] > up   ) up    = v[i];
			if (v[i] > cc->geom.y && v[i] < down ) down  = v[i];
		}
		move = 0;
		switch (flags & (CWM_UP|CWM_DOWN|CWM_LEFT|CWM_RIGHT)) {
			case CWM_LEFT:
				if (left  < cc->geom.x && left  > INT_MIN) {
					move = 1; cc->geom.x = left;  }
				break;
			case CWM_RIGHT:
				if (right > cc->geom.x && right < INT_MAX) {
					move = 1; cc->geom.x = right; }
				break;
			case CWM_UP:
				if (up    < cc->geom.y && up    > INT_MIN) {
					move = 1; cc->geom.y = up;    }
				break;
			case CWM_DOWN:
				if (down  > cc->geom.y && down  < INT_MAX) {
					move = 1; cc->geom.y = down;  }
				break;
		}
		if (move) {
			client_ptrsave(cc);
			client_move(cc);
			client_ptrwarp(cc);
		}
		break;
	case CWM_RESIZE:
		c = i * 5 + c * 2 + 1; i = 0;
		h = malloc(sizeof(int) * c);
		v = malloc(sizeof(int) * c);
		// h and v in absolute coordinates
		TAILQ_FOREACH(rc, &cc->sc->regionq, entry) {
			ow = rc->area.w - gap->left - gap->right;
			h[i + 0] = rc->area.x;
			h[i + 1] = rc->area.x + gap->left;
			h[i + 2] = rc->area.x + gap->left + ow / 2;
			h[i + 3] = rc->area.x + gap->left + ow;
			h[i + 4] = rc->area.x + rc->area.w;
			oh = rc->area.h - gap->top - gap->bottom;
			v[i + 0] = rc->area.y;
			v[i + 1] = rc->area.y + gap->top;
			v[i + 2] = rc->area.y + gap->top + oh / 2;
			v[i + 3] = rc->area.y + gap->top + oh;
			v[i + 4] = rc->area.y + rc->area.h;
			i += 5;
		}
	        TAILQ_FOREACH(oc, &cc->sc->clientq, entry) {
			if ((oc->flags & (CLIENT_HIDDEN | CLIENT_IGNORE)) ||
				(oc == cc)) continue;
			ow = oc->geom.w + 2 * oc->bwidth;
			h[i + 0] = oc->geom.x;
			h[i + 1] = oc->geom.x + ow;
			oh = oc->geom.h + 2 * oc->bwidth;
			v[i + 0] = oc->geom.y;
			v[i + 1] = oc->geom.y + oh;
			i += 2;
		}
		h[i] = cc->geom.x + cc->initgeom.w + 2 * cc->bwidth;
		v[i] = cc->geom.y + cc->initgeom.h + 2 * cc->bwidth;
		i++;
		left = up = INT_MIN; right = down = INT_MAX;
		while (i--) {
			// back to relative width and height
			h[i] -= cc->geom.x + 2 * cc->bwidth;
			v[i] -= cc->geom.y + 2 * cc->bwidth;
			if (h[i] < cc->geom.w && h[i] > left ) left  = h[i];
			if (h[i] > cc->geom.w && h[i] < right) right = h[i];
			if (v[i] < cc->geom.h && v[i] > up   ) up    = v[i];
			if (v[i] > cc->geom.h && v[i] < down ) down  = v[i];
		}
		move = 0;
		switch (flags & (CWM_UP|CWM_DOWN|CWM_LEFT|CWM_RIGHT)) {
			case CWM_LEFT:
				if (left  < cc->geom.w && left  > 0) {
					move = 1; cc->geom.w = left;  }
				break;
			case CWM_RIGHT:
				if (right > cc->geom.w && right < INT_MAX) {
					move = 1; cc->geom.w = right; }
				break;
			case CWM_UP:
				if (up    < cc->geom.h && up    > 0) {
					move = 1; cc->geom.h = up;    }
				break;
			case CWM_DOWN:
				if (down  > cc->geom.h && down  < INT_MAX) {
					move = 1; cc->geom.h = down;  }
				break;
		}
		if (move) {
			client_resize(cc, 1);
			// Make sure the pointer stays within the window.
			xu_ptr_getpos(cc->win, &cc->ptr.x, &cc->ptr.y);
			if (cc->ptr.x > cc->geom.w)
				cc->ptr.x = cc->geom.w - cc->bwidth;
			if (cc->ptr.y > cc->geom.h)
				cc->ptr.y = cc->geom.h - cc->bwidth;
			client_ptrwarp(cc);
		}
		break;
	}
	free(h);
	free(v);
}

int
client_geom_dist(struct client_ctx *cc, struct geom *area)
{
	int			left, right, top, bottom;

	left   = area->x - (cc->geom.x + cc->geom.w + 2 * cc->bwidth);
	right  = cc->geom.x - (area->x + area->w);
	top    = area->y - (cc->geom.y + cc->geom.h + 2 * cc->bwidth);
	bottom = cc->geom.y - (area->y + area->h);

	return max(left, max(right, max(top, bottom)));
}


void
kbfunc_client_box(struct client_ctx *cc, union arg *arg)
{
	struct region_ctx	*rc, *closest;
	int			dist, min_dist;
	int			x, y, move = 0;

	closest = rc = TAILQ_FIRST(&cc->sc->regionq);
	min_dist = client_geom_dist(cc, &rc->area);

	while ((rc = TAILQ_NEXT(rc, entry))) {
		dist = client_geom_dist(cc, &rc->area);
		if (dist > min_dist) continue;
	
		min_dist = dist;
		closest = rc;
	}
	debug("dist(%s, region_%i) = %i\n", 
		cc->ch.res_name, closest->num, min_dist);

	x = closest->area.x + closest->area.w - cc->geom.w / 2 - cc->bwidth;
	if (cc->geom.x > x) { cc->geom.x = x; move = 1; }
	x = closest->area.x - cc->geom.w / 2 - cc->bwidth;
	if (cc->geom.x < x) { cc->geom.x = x; move = 1; }
	y = closest->area.y + closest->area.h - cc->geom.h / 2 - cc->bwidth;
	if (cc->geom.y > y) { cc->geom.y = y; move = 1; }
	y = closest->area.y - cc->geom.h / 2 - cc->bwidth;
	if (cc->geom.y < y) { cc->geom.y = y; move = 1; }
	
	if (move) client_move(cc);
}

void
kbfunc_client_box_all(struct client_ctx *cc, union arg *arg)
{
	TAILQ_FOREACH(cc, &cc->sc->clientq, entry)
		kbfunc_client_box(cc, arg);
}

int
sign3(int a, int b, int c) {
	if ((a < 0) || (a == 0 && b < 0) ||
		(a == 0 && b == 0 && c < 0)) return -1;
	if ((a > 0) || (a == 0 && b > 0) ||
		(a == 0 && b == 0 && c > 0)) return 1;
	return 0;
}

void
kbfunc_client_focus(struct client_ctx *cc, union arg *arg)
{
	struct client_ctx	*oc, *best = NULL;

	TAILQ_FOREACH(oc, &cc->sc->clientq, entry) {
		if ((oc->flags & (CLIENT_HIDDEN | CLIENT_IGNORE)) ||
			(oc == cc)) continue;

		switch (arg->i) {
		case CWM_LEFT:
			if (sign3(oc->geom.x - cc->geom.x,
				oc->geom.y - cc->geom.y,
				oc->seq - cc->seq) >= 0) break;
			if (best == NULL) { best = oc; break; }
			if (sign3(oc->geom.x - best->geom.x,
				oc->geom.y - best->geom.y,
				oc->seq - best->seq) > 0) best = oc;
			break;
		case CWM_RIGHT:
			if (sign3(oc->geom.x - cc->geom.x,
				oc->geom.y - cc->geom.y,
				oc->seq - cc->seq) <= 0) break;
			if (best == NULL) { best = oc; break; }
			if (sign3(oc->geom.x - best->geom.x,
				oc->geom.y - best->geom.y,
				oc->seq - best->seq) < 0) best = oc;
			break;
		case CWM_UP:
			if (sign3(oc->geom.y - cc->geom.y,
				oc->geom.x - cc->geom.x,
				oc->seq - cc->seq) >= 0) break;
			if (best == NULL) { best = oc; break; }
			if (sign3(oc->geom.y - best->geom.y,
				oc->geom.x - best->geom.x,
				oc->seq - best->seq) > 0) best = oc;
			break;
		case CWM_DOWN:
			if (sign3(oc->geom.y - cc->geom.y,
				oc->geom.x - cc->geom.x,
				oc->seq - cc->seq) <= 0) break;
			if (best == NULL) { best = oc; break; }
			if (sign3(oc->geom.y - best->geom.y,
				oc->geom.x - best->geom.x,
				oc->seq - best->seq) < 0) best = oc;
			break;
		default:
			debug("not implemented\n");
		}
	}
	if (best != NULL && best != cc) {
		client_ptrsave(cc);
		client_ptrwarp(best);
	}
}


void
kbfunc_client_search(struct client_ctx *cc, union arg *arg)
{
	struct screen_ctx	*sc = cc->sc;
	struct client_ctx	*old_cc;
	struct menu		*mi;
	struct menu_q		 menuq;

	old_cc = client_current();

	TAILQ_INIT(&menuq);
	TAILQ_FOREACH(cc, &sc->clientq, entry)
		menuq_add(&menuq, cc, "%s", cc->name);

	if ((mi = menu_filter(sc, &menuq, "window", NULL, 0,
	    search_match_client, search_print_client)) != NULL) {
		cc = (struct client_ctx *)mi->ctx;
		if (cc->flags & CLIENT_HIDDEN)
			client_unhide(cc);
		if (old_cc)
			client_ptrsave(old_cc);
		client_ptrwarp(cc);
	}

	menuq_clear(&menuq);
}

void
kbfunc_menu_cmd(struct client_ctx *cc, union arg *arg)
{
	struct screen_ctx	*sc = cc->sc;
	struct cmd		*cmd;
	struct menu		*mi;
	struct menu_q		 menuq;

	TAILQ_INIT(&menuq);
	TAILQ_FOREACH(cmd, &Conf.cmdq, entry)
		menuq_add(&menuq, cmd, "%s", cmd->name);

	if ((mi = menu_filter(sc, &menuq, "application", NULL, 0,
	    search_match_text, NULL)) != NULL)
		u_spawn(((struct cmd *)mi->ctx)->path);

	menuq_clear(&menuq);
}

void
kbfunc_client_cycle(struct client_ctx *cc, union arg *arg)
{
	struct screen_ctx	*sc = cc->sc;

	/* XXX for X apps that ignore events */
	XGrabKeyboard(X_Dpy, sc->rootwin, True,
	    GrabModeAsync, GrabModeAsync, CurrentTime);

	client_cycle(sc, arg->i);
}

void
kbfunc_client_hide(struct client_ctx *cc, union arg *arg)
{
	client_hide(cc);
}

void
kbfunc_cmdexec(struct client_ctx *cc, union arg *arg)
{
	u_spawn(arg->c);
}

void
kbfunc_term(struct client_ctx *cc, union arg *arg)
{
	struct cmd *cmd;

	TAILQ_FOREACH(cmd, &Conf.cmdq, entry) {
		if (strcmp(cmd->name, "term") == 0)
			u_spawn(cmd->path);
	}
}

void
kbfunc_lock(struct client_ctx *cc, union arg *arg)
{
	struct cmd *cmd;

	TAILQ_FOREACH(cmd, &Conf.cmdq, entry) {
		if (strcmp(cmd->name, "lock") == 0)
			u_spawn(cmd->path);
	}
}

void
kbfunc_exec(struct client_ctx *cc, union arg *arg)
{
#define NPATHS 256
	struct screen_ctx	*sc = cc->sc;
	char			**ap, *paths[NPATHS], *path, *pathcpy;
	char			 tpath[MAXPATHLEN];
	const char		*label;
	DIR			*dirp;
	struct dirent		*dp;
	struct menu		*mi;
	struct menu_q		 menuq;
	int			 l, i, cmd = arg->i;

	switch (cmd) {
	case CWM_EXEC_PROGRAM:
		label = "exec";
		break;
	case CWM_EXEC_WM:
		label = "wm";
		break;
	default:
		errx(1, "kbfunc_exec: invalid cmd %d", cmd);
		/*NOTREACHED*/
	}

	TAILQ_INIT(&menuq);

	if ((path = getenv("PATH")) == NULL)
		path = _PATH_DEFPATH;
	pathcpy = path = xstrdup(path);

	for (ap = paths; ap < &paths[NPATHS - 1] &&
	    (*ap = strsep(&pathcpy, ":")) != NULL;) {
		if (**ap != '\0')
			ap++;
	}
	*ap = NULL;
	for (i = 0; i < NPATHS && paths[i] != NULL; i++) {
		if ((dirp = opendir(paths[i])) == NULL)
			continue;

		while ((dp = readdir(dirp)) != NULL) {
			/* skip everything but regular files and symlinks */
			if (dp->d_type != DT_REG && dp->d_type != DT_LNK)
				continue;
			(void)memset(tpath, '\0', sizeof(tpath));
			l = snprintf(tpath, sizeof(tpath), "%s/%s", paths[i],
			    dp->d_name);
			if (l == -1 || l >= sizeof(tpath))
				continue;
			if (access(tpath, X_OK) == 0)
				menuq_add(&menuq, NULL, "%s", dp->d_name);
		}
		(void)closedir(dirp);
	}
	free(path);

	if ((mi = menu_filter(sc, &menuq, label, NULL,
	    CWM_MENU_DUMMY | CWM_MENU_FILE,
	    search_match_exec_path, NULL)) != NULL) {
		if (mi->text[0] == '\0')
			goto out;
		switch (cmd) {
		case CWM_EXEC_PROGRAM:
			u_spawn(mi->text);
			break;
		case CWM_EXEC_WM:
			u_exec(mi->text);
			warn("%s", mi->text);
			break;
		default:
			errx(1, "kb_func: egad, cmd changed value!");
			break;
		}
	}
out:
	if (mi != NULL && mi->dummy)
		free(mi);
	menuq_clear(&menuq);
}

void
kbfunc_ssh(struct client_ctx *cc, union arg *arg)
{
	struct screen_ctx	*sc = cc->sc;
	struct cmd		*cmd;
	struct menu		*mi;
	struct menu_q		 menuq;
	FILE			*fp;
	char			*buf, *lbuf, *p;
	char			 hostbuf[MAXHOSTNAMELEN];
	char			 path[MAXPATHLEN];
	int			 l;
	size_t			 len;

	if ((fp = fopen(Conf.known_hosts, "r")) == NULL) {
		warn("kbfunc_ssh: %s", Conf.known_hosts);
		return;
	}

	TAILQ_FOREACH(cmd, &Conf.cmdq, entry) {
		if (strcmp(cmd->name, "term") == 0)
			break;
	}

	TAILQ_INIT(&menuq);

	lbuf = NULL;
	while ((buf = fgetln(fp, &len))) {
		if (buf[len - 1] == '\n')
			buf[len - 1] = '\0';
		else {
			/* EOF without EOL, copy and add the NUL */
			lbuf = xmalloc(len + 1);
			(void)memcpy(lbuf, buf, len);
			lbuf[len] = '\0';
			buf = lbuf;
		}
		/* skip hashed hosts */
		if (strncmp(buf, HASH_MARKER, strlen(HASH_MARKER)) == 0)
			continue;
		for (p = buf; *p != ',' && *p != ' ' && p != buf + len; p++) {
			/* do nothing */
		}
		/* ignore badness */
		if (p - buf + 1 > sizeof(hostbuf))
			continue;
		(void)strlcpy(hostbuf, buf, p - buf + 1);
		menuq_add(&menuq, NULL, hostbuf);
	}
	free(lbuf);
	(void)fclose(fp);

	if ((mi = menu_filter(sc, &menuq, "ssh", NULL, CWM_MENU_DUMMY,
	    search_match_exec, NULL)) != NULL) {
		if (mi->text[0] == '\0')
			goto out;
		l = snprintf(path, sizeof(path), "%s -T '[ssh] %s' -e ssh %s",
		    cmd->path, mi->text, mi->text);
		if (l == -1 || l >= sizeof(path))
			goto out;
		u_spawn(path);
	}
out:
	if (mi != NULL && mi->dummy)
		free(mi);
	menuq_clear(&menuq);
}

void
kbfunc_client_label(struct client_ctx *cc, union arg *arg)
{
	struct menu	*mi;
	struct menu_q	 menuq;

	TAILQ_INIT(&menuq);

	/* dummy is set, so this will always return */
	mi = menu_filter(cc->sc, &menuq, "label", cc->label, CWM_MENU_DUMMY,
	    search_match_text, NULL);

	if (!mi->abort) {
		free(cc->label);
		cc->label = xstrdup(mi->text);
	}
	free(mi);
}

void
kbfunc_client_delete(struct client_ctx *cc, union arg *arg)
{
	client_send_delete(cc);
}

void
kbfunc_client_group(struct client_ctx *cc, union arg *arg)
{
	group_hidetoggle(cc->sc, arg->i);
}

void
kbfunc_client_grouponly(struct client_ctx *cc, union arg *arg)
{
	group_only(cc->sc, arg->i);
}

void
kbfunc_client_cyclegroup(struct client_ctx *cc, union arg *arg)
{
	group_cycle(cc->sc, arg->i);
}

void
kbfunc_client_nogroup(struct client_ctx *cc, union arg *arg)
{
	group_alltoggle(cc->sc);
}

void
kbfunc_client_grouptoggle(struct client_ctx *cc, union arg *arg)
{
	/* XXX for stupid X apps like xpdf and gvim */
	XGrabKeyboard(X_Dpy, cc->win, True,
	    GrabModeAsync, GrabModeAsync, CurrentTime);

	group_toggle_membership_enter(cc);
}

void
kbfunc_client_movetogroup(struct client_ctx *cc, union arg *arg)
{
	group_movetogroup(cc, arg->i);
}

void
kbfunc_client_toggle_sticky(struct client_ctx *cc, union arg *arg)
{
	client_toggle_sticky(cc);
}

void
kbfunc_client_toggle_fullscreen(struct client_ctx *cc, union arg *arg)
{
	client_toggle_fullscreen(cc);
}

void
kbfunc_client_toggle_maximize(struct client_ctx *cc, union arg *arg)
{
	client_toggle_maximize(cc);
}

void
kbfunc_client_toggle_vmaximize(struct client_ctx *cc, union arg *arg)
{
	client_toggle_vmaximize(cc);
}

void
kbfunc_client_toggle_hmaximize(struct client_ctx *cc, union arg *arg)
{
	client_toggle_hmaximize(cc);
}

void
kbfunc_client_toggle_freeze(struct client_ctx *cc, union arg *arg)
{
	client_toggle_freeze(cc);
}

void
kbfunc_cwm_status(struct client_ctx *cc, union arg *arg)
{
	cwm_status = arg->i;
}

void
kbfunc_tile(struct client_ctx *cc, union arg *arg)
{
	switch (arg->i) {
	case CWM_TILE_HORIZ:
		client_htile(cc);
		break;
	case CWM_TILE_VERT:
		client_vtile(cc);
		break;
	}
}
