/* See LICENSE file for copyright and license details. */
#include <ctype.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif
#include <X11/Xft/Xft.h>

#include "drw.h"
#include "util.h"

/* macros */
#define INTERSECT(x,y,w,h,r)  (MAX(0, MIN((x)+(w),(r).x_org+(r).width)  - MAX((x),(r).x_org)) \
                             * MAX(0, MIN((y)+(h),(r).y_org+(r).height) - MAX((y),(r).y_org)))
#define LENGTH(X)             (sizeof X / sizeof X[0])
#define TEXTNW(X,N)           (drw_font_getexts_width(drw->fonts[0], (X), (N)))
#define TEXTW(X)              (drw_text(drw, 0, 0, 0, 0, (X), 0, CPU_THREADS) + drw->fonts[0]->h)

/* enums */
enum { SchemeFirst, SchemeNorm, SchemeSel, SchemeOut, SchemeLast }; /* color schemes */

struct item {
	char *text;
	struct item *left, *right;
	int out;
	int number;
	int distance;
};

static char text[BUFSIZ] = "";
static int bh, mw, mh;
static int sw, sh; /* X display screen geometry width, height */
static int inputw, promptw;
static size_t cursor;
static struct item *items = NULL;
static struct item *matches, *matchend;
static struct item *prev, *curr, *next, *sel;
static int mon = -1, screen;

static Atom clip, utf8;
static Display *dpy;
static Window root, win;
static XIC xic;

static ClrScheme scheme[SchemeLast];
static Drw *drw;

#include "config.h"

static int (*fstrncmp)(const char *, const char *, size_t) = strncmp;
static char *(*fstrstr)(const char *, const char *) = strstr;

static void
appenditem(struct item *item, struct item **list, struct item **last)
{
	int number = 0;
	if (*last) {
		(*last)->right = item;
		number = (*last)->number+1;
	} else
		*list = item;

	item->left = *last;
	item->right = NULL;
	item->number = number;
	*last = item;
}

static void
calcoffsets(void)
{
	int i, n;

	if (lines > 0)
		n = lines * bh;
	else
		n = mw - (promptw + inputw + TEXTW("<") + TEXTW(">"));
	/* calculate which items will begin the next page and previous page */
	for (i = 0, next = curr; next; next = next->right)
		if ((i += (lines > 0) ? bh : MIN(TEXTW(next->text), n)) > n)
			break;
	for (i = 0, prev = curr; prev && prev->left; prev = prev->left)
		if ((i += (lines > 0) ? bh : MIN(TEXTW(prev->left->text), n)) > n)
			break;
}

static void
cleanup(void)
{
	size_t i;

	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	for (i = 0; i < SchemeLast; i++) {
		drw_clr_free(scheme[i].bg);
		drw_clr_free(scheme[i].fg);
	}
	drw_free(drw);
	XSync(dpy, False);
	XCloseDisplay(dpy);
}

static char *
cistrstr(const char *s, const char *sub)
{
	size_t len;

	for (len = strlen(sub); *s; s++)
		if (!strncasecmp(s, sub, len))
			return (char *)s;
	return NULL;
}

static void
drawmenu(void)
{
	int curpos;
	struct item *item;
	int x = 0, y = 0, h = bh, w;

	drw_setscheme(drw, &scheme[SchemeNorm]);
	drw_rect(drw, 0, 0, mw, mh, 1, 1, 1, CPU_THREADS);

	if (prompt && *prompt) {
		drw_setscheme(drw, &scheme[SchemeSel]);
		drw_text(drw, x, 0, promptw, bh, prompt, 0, CPU_THREADS);
		x += promptw;
	}
	/* draw input field */
	w = (lines > 0 || !matches) ? mw - x : inputw;
	drw_setscheme(drw, &scheme[SchemeFirst]);
	drw_text(drw, x, 0, w, bh, text, 0, CPU_THREADS);

	if ((curpos = TEXTNW(text, cursor) + bh / 2 - 2) < w) {
		drw_setscheme(drw, &scheme[SchemeNorm]);
		drw_rect(drw, x + curpos + 2, 2, 1, bh - 4, 1, 1, 0, CPU_THREADS);
	}

	if (lines > 0) {
		/* draw vertical list */
		w = mw - x;
		for (item = curr; item != next; item = item->right) {
			y += h;
			if (item == sel)
				drw_setscheme(drw, &scheme[SchemeSel]);
			else if (item->out)
				drw_setscheme(drw, &scheme[SchemeOut]);
			else
				drw_setscheme(drw, &scheme[SchemeNorm]);

			drw_text(drw, x, y, w, bh, item->text, 0, CPU_THREADS);
		}
	} else if (matches) {
		/* draw horizontal list */
		x += inputw;
		w = TEXTW("<");
		if (curr->left) {
			drw_setscheme(drw, &scheme[SchemeNorm]);
			drw_text(drw, x, 0, w, bh, "<", 0, CPU_THREADS);
		}
		for (item = curr; item != next; item = item->right) {
			x += w;
			w = MIN(TEXTW(item->text), mw - x - TEXTW(">"));

			if (item == sel)
				drw_setscheme(drw, &scheme[SchemeSel]);
			else if (item->out)
				drw_setscheme(drw, &scheme[SchemeOut]);
			else
				drw_setscheme(drw, &scheme[SchemeNorm]);
			drw_text(drw, x, 0, w, bh, item->text, 0, CPU_THREADS);
		}
		w = TEXTW(">");
		x = mw - w;
		if (next) {
			drw_setscheme(drw, &scheme[SchemeNorm]);
			drw_text(drw, x, 0, w, bh, ">", 0, CPU_THREADS);
		}
	}
	drw_map(drw, win, 0, 0, mw, mh);
}

static void
grabkeyboard(void)
{
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000  };
	int i;

	/* try to grab keyboard, we may have to wait for another process to ungrab */
	for (i = 0; i < 1000; i++) {
		if (XGrabKeyboard(dpy, DefaultRootWindow(dpy), True,
		                 GrabModeAsync, GrabModeAsync, CurrentTime) == GrabSuccess)
			return;
		nanosleep(&ts, NULL);
	}
	die("cannot grab keyboard\n");
}

static int
compare_distance(const void *a, const void *b)
{
	struct item const *da = *(struct item **) a;
	struct item const *db = *(struct item **) b;

	if (!db)
		return 1;
	if (!da)
		return -1;
	return da->distance - db->distance;
}

static void
fuzzymatch(void)
{
	struct item *item;
	struct item **fuzzymatches = NULL;
	char c;
	int number_of_matches = 0, i, pidx, sidx, eidx;
	int text_len = strlen(text), itext_len;

	matches = matchend = NULL;

	/* walk through all items */
	for (item = items; item && item->text; item++) {
		if (text_len) {
			itext_len = strlen(item->text);
			pidx = 0;
			sidx = eidx = -1;
			/* walk through item text */
			for (i = 0; i < itext_len && (c = item->text[i]); i++) {
				/* case-insensitive fuzzy match pattern */
				if (tolower(text[pidx]) == c || toupper(text[pidx]) == c) {
					if (sidx == -1)
						sidx = i;
					pidx++;
					if (pidx == text_len) {
						eidx = i;
						break;
					}
				}
			}
			/* build list of matches */
			if (eidx != -1) {
				/* compute distance */
				/* factor in 30% of sidx and distance between eidx and total
				 * text length .. let's see how it works */
				item->distance = eidx - sidx + (itext_len - eidx + sidx) / 3;
				appenditem(item, &matches, &matchend);
				number_of_matches++;
			}
		}
		else
			appenditem(item, &matches, &matchend);
	}

	if (number_of_matches) {
		/* initialize array with matches */
		if (!(fuzzymatches = realloc(fuzzymatches, number_of_matches * sizeof(struct item*))))
			die("cannot realloc %u bytes:", number_of_matches * sizeof(struct item *));
		for (i = 0, item = matches; item && i < number_of_matches; i++, item = item->right)
			fuzzymatches[i] = item;

		/* sort matches according to distance */
		qsort(fuzzymatches, number_of_matches, sizeof(struct item *), compare_distance);
		/* rebuild list of matches */
		matches = matchend = NULL;
		for (i = 0, item = fuzzymatches[0]; i < number_of_matches && item && \
				item->text; item = fuzzymatches[i], i++)
			appenditem(item, &matches, &matchend);

		free(fuzzymatches);
	}
	curr = sel = matches;
	calcoffsets();
}

static void
insert(const char *str, ssize_t n)
{
	if (strlen(text) + n > sizeof text - 1)
		return;
	/* move existing text out of the way, insert new text, and update cursor */
	memmove(&text[cursor + n], &text[cursor], sizeof text - cursor - MAX(n, 0));
	if (n > 0)
		memcpy(&text[cursor], str, n);
	cursor += n;
	fuzzymatch();
}

static size_t
nextrune(int inc)
{
	ssize_t n;

	/* return location of next utf8 rune in the given direction (+1 or -1) */
	for (n = cursor + inc; n + inc >= 0 && (text[n] & 0xc0) == 0x80; n += inc)
		;
	return n;
}

static void
goup(unsigned int state)
{
	if ((!sel) || (!sel->left))
		return;
	if ((sel = sel->left)->right == curr) {
		curr = prev;
		calcoffsets();
	}
	if (output_on_move) {
		if (output_number) {
			if (!(state & ShiftMask))
				printf("%d\n", sel->number);
		} else
			puts((!(state & ShiftMask)) ? sel->text : text);
	}
}

static void
godown(unsigned int state)
{
	if ((!sel) || (!sel->right))
		return;
	if (sel && (sel = sel->right) == next) {
		curr = next;
		calcoffsets();
	}
	if (output_on_move) {
		if (output_number) {
			if (!(state & ShiftMask))
				printf("%d\n", sel->number);
		} else
			puts((!(state & ShiftMask)) ? sel->text : text);
	}
}

static void
choose(unsigned int state)
{
	if (output_number) {
		if (sel && !(state & ShiftMask))
			printf("%d\n", sel->number);
	} else
		puts((sel && !(state & ShiftMask)) ? sel->text : text);
	if (!stay_after_select) {
		if (!(state & ControlMask)) {
			cleanup();
			exit(0);
		}
		if (sel)
			sel->out = 1;
	}
}

static void
keypress(XKeyEvent *ev)
{
	char buf[32];
	int len;
	KeySym ksym = NoSymbol;
	Status status;

	len = XmbLookupString(xic, ev, buf, sizeof buf, &ksym, &status);
	if (status == XBufferOverflow)
		return;

	//Swap forward and backward keys on single line menu
	if (reverse_updown) {
		if (ksym == XK_Down)
			ksym = XK_Up;
		else if (ksym == XK_Up)
			ksym = XK_Down;
	}

	if (ev->state & ControlMask)
		switch(ksym) {
		case XK_a: ksym = XK_Home;      break;
		case XK_b: ksym = XK_Left;      break;
		case XK_c: ksym = XK_Escape;    break;
		case XK_d: ksym = XK_Delete;    break;
		case XK_e: ksym = XK_End;       break;
		case XK_f: ksym = XK_Right;     break;
		case XK_g: ksym = XK_Escape;    break;
		case XK_h: ksym = XK_BackSpace; break;
		case XK_i: ksym = XK_Tab;       break;
		case XK_j: /* fallthrough */
		case XK_J: /* fallthrough */
		case XK_m: /* fallthrough */
		case XK_M: ksym = XK_Return; ev->state &= ~ControlMask; break;
		case XK_n: ksym = XK_Down;      break;
		case XK_p: ksym = XK_Up;        break;

		case XK_k: /* delete right */
			text[cursor] = '\0';
			fuzzymatch();
			break;
		case XK_u: /* delete left */
			insert(NULL, 0 - cursor);
			break;
		case XK_w: /* delete word */
			while (cursor > 0 && strchr(worddelimiters,
			       text[nextrune(-1)]))
				insert(NULL, nextrune(-1) - cursor);
			while (cursor > 0 && !strchr(worddelimiters,
			       text[nextrune(-1)]))
				insert(NULL, nextrune(-1) - cursor);
			break;
		case XK_y: /* paste selection */
		case XK_Y:
			XConvertSelection(dpy, (ev->state & ShiftMask) ? clip : XA_PRIMARY,
			                  utf8, utf8, win, CurrentTime);
			return;
		case XK_Return:
		case XK_KP_Enter:
			break;
		case XK_bracketleft:
			cleanup();
			exit(1);
		default:
			return;
		}
	else if (ev->state & Mod1Mask)
		switch(ksym) {
		case XK_g: ksym = XK_Home;  break;
		case XK_G: ksym = XK_End;   break;
		case XK_h: ksym = XK_Up;    break;
		case XK_j: ksym = XK_Next;  break;
		case XK_k: ksym = XK_Prior; break;
		case XK_l: ksym = XK_Down;  break;
		default:
			return;
		}
	switch(ksym) {
	default:
		if (!iscntrl(*buf))
			insert(buf, len);
		break;
	case XK_Delete:
		if (text[cursor] == '\0')
			return;
		cursor = nextrune(+1);
		/* fallthrough */
	case XK_BackSpace:
		if (cursor == 0)
			return;
		insert(NULL, nextrune(-1) - cursor);
		break;
	case XK_End:
		if (text[cursor] != '\0') {
			cursor = strlen(text);
			break;
		}
		if (next) {
			/* jump to end of list and position items in reverse */
			curr = matchend;
			calcoffsets();
			curr = prev;
			calcoffsets();
			while (next && (curr = curr->right))
				calcoffsets();
		}
		sel = matchend;
		break;
	case XK_Super_L:
	case XK_Escape:
		cleanup();
		exit(1);
	case XK_Home:
		if (sel == matches) {
			cursor = 0;
			break;
		}
		sel = curr = matches;
		calcoffsets();
		break;
	case XK_Left:
		if (cursor > 0 && (!sel || !sel->left || lines > 0)) {
			cursor = nextrune(-1);
			break;
		}
		if (lines > 0)
			return;
		/* fallthrough */
	case XK_Up:
		goup(ev->state);
		break;
	case XK_Next:
		if (!next)
			return;
		sel = curr = next;
		calcoffsets();
		break;
	case XK_Prior:
		if (!prev)
			return;
		sel = curr = prev;
		calcoffsets();
		break;
	case XK_Return:
	case XK_KP_Enter:
		choose(ev->state);
		break;
	case XK_Right:
		if (text[cursor] != '\0') {
			cursor = nextrune(+1);
			break;
		}
		if (lines > 0)
			return;
		/* fallthrough */
	case XK_Down:
		godown(ev->state);
		break;
	case XK_Tab:
		if (!sel)
			return;
		strncpy(text, sel->text, sizeof text - 1);
		text[sizeof text - 1] = '\0';
		cursor = strlen(text);
		fuzzymatch();
		break;
	}
	drawmenu();
}

static void
buttonpress(XEvent *e)
{
	struct item *item;
	XButtonPressedEvent *ev = &e->xbutton;
	int x = 0, y = 0, h = bh, w;

	if (ev->window != win)
		return;

	/* right-click: exit */
	if (ev->button == Button3)
		exit(1);

	//Swap forward and backward keys on single line menu
	if (reverse_updown) {
		if (ev->button == Button4)
			ev->button = Button5;
		else if (ev->button == Button5)
			ev->button = Button4;
	}

	if (prompt && *prompt)
		x += promptw;

	/* input field */
	w = (lines > 0 || !matches) ? mw - x : inputw;

	/* left-click on input: clear input,
	 * NOTE: if there is no left-arrow the space for < is reserved so
	 *       add that to the input width */
	if (ev->button == Button1 &&
	   ((lines <= 0 && ev->x >= 0 && ev->x <= x + w +
	   ((!prev || !curr->left) ? TEXTW("<") : 0)) ||
	   (lines > 0 && ev->y >= y && ev->y <= y + h))) {
		insert(NULL, -cursor);
		drawmenu();
		return;
	}
	/* middle-mouse click: paste selection */
	if (ev->button == Button2) {
		XConvertSelection(dpy, (ev->state & ShiftMask) ? clip : XA_PRIMARY,
		                  utf8, utf8, win, CurrentTime);
		drawmenu();
		return;
	}
	/* scroll up */
	if (ev->button == Button4) {
		goup(ev->state);
		drawmenu();
		return;
	}
	/* scroll down */
	if (ev->button == Button5) {
		godown(ev->state);
		drawmenu();
		return;
	}
	if (ev->button != Button1)
		return;
	if (lines > 0) {
		/* vertical list: (ctrl)left-click on item */
		w = mw - x;
		for (item = curr; item != next; item = item->right) {
			y += h;
			if (ev->y >= y && ev->y <= (y + h)) {
				sel = item;
				choose(ev->state);
				drawmenu();
				return;
			}
		}
	} else if (matches) {
		/* left-click on left arrow */
		x += inputw;
		w = TEXTW("<");
		if (prev && curr->left) {
			if (ev->x >= x && ev->x <= x + w) {
				sel = curr = prev;
				calcoffsets();
				drawmenu();
				return;
			}
		}
		/* horizontal list: (ctrl)left-click on item */
		for (item = curr; item != next; item = item->right) {
			x += w;
			w = MIN(TEXTW(item->text), mw - x - TEXTW(">"));
			if (ev->x >= x && ev->x <= x + w) {
				sel = item;
				choose(ev->state);
				drawmenu();
				return;
			}
		}
		/* left-click on right arrow */
		w = TEXTW(">");
		x = mw - w;
		if (next && ev->x >= x && ev->x <= x + w) {
			sel = curr = next;
			calcoffsets();
			drawmenu();
			return;
		}
	}
}

static void
paste(void)
{
	char *p, *q;
	int di;
	unsigned long dl;
	Atom da;

	/* we have been given the current selection, now insert it into input */
	XGetWindowProperty(dpy, win, utf8, 0, (sizeof text / 4) + 1, False,
	                   utf8, &da, &di, &dl, &dl, (unsigned char **)&p);
	insert(p, (q = strchr(p, '\n')) ? q - p : (ssize_t)strlen(p));
	XFree(p);
	drawmenu();
}

static void
readstdin(void)
{
	char buf[sizeof text], *p, *maxstr = NULL;
	size_t i, max = 0, size = 0;

	/* read each line from stdin and add it to the item list */
	for (i = 0; fgets(buf, sizeof buf, stdin); i++) {
		if (i + 1 >= size / sizeof *items)
			if (!(items = realloc(items, (size += BUFSIZ))))
				die("cannot realloc %u bytes:", size);
		if ((p = strchr(buf, '\n')))
			*p = '\0';
		if (!(items[i].text = strdup(buf)))
			die("cannot strdup %u bytes:", strlen(buf) + 1);
		items[i].out = 0;
		if (strlen(items[i].text) > max)
			max = strlen(maxstr = items[i].text);
	}
	if (items)
		items[i].text = NULL;
	inputw = maxstr ? TEXTW(maxstr) : 0;
	lines = MIN(lines, i);
}

static void
run(void)
{
	XEvent ev;

	while (!XNextEvent(dpy, &ev)) {
		if (XFilterEvent(&ev, win))
			continue;
		switch(ev.type) {
		case ButtonPress:
			buttonpress(&ev);
			break;
		case Expose:
			if (ev.xexpose.count == 0)
				drw_map(drw, win, 0, 0, mw, mh);
			break;
		case KeyPress:
			keypress(&ev.xkey);
			break;
		case SelectionNotify:
			if (ev.xselection.property == utf8)
				paste();
			break;
		case VisibilityNotify:
			if (ev.xvisibility.state != VisibilityUnobscured)
				XRaiseWindow(dpy, win);
			break;
		}
	}
}

static void
setup(void)
{
	int x, y;
	XSetWindowAttributes swa;
	XIM xim;
#ifdef XINERAMA
	XineramaScreenInfo *info;
	Window w, pw, dw, *dws;
	XWindowAttributes wa;
	int a, j, di, n, i = 0, area = 0;
	unsigned int du;
#endif

	/* init appearance */
	scheme[SchemeFirst].bg = drw_clr_create(drw, firstbgcolor);
	scheme[SchemeFirst].fg = drw_clr_create(drw, firstfgcolor);
	scheme[SchemeNorm].bg = drw_clr_create(drw, normbgcolor);
	scheme[SchemeNorm].fg = drw_clr_create(drw, normfgcolor);
	scheme[SchemeSel].bg = drw_clr_create(drw, selbgcolor);
	scheme[SchemeSel].fg = drw_clr_create(drw, selfgcolor);
	scheme[SchemeOut].bg = drw_clr_create(drw, outbgcolor);
	scheme[SchemeOut].fg = drw_clr_create(drw, outfgcolor);

	clip = XInternAtom(dpy, "CLIPBOARD",   False);
	utf8 = XInternAtom(dpy, "UTF8_STRING", False);

	/* calculate menu geometry */
	bh = drw->fonts[0]->h + 2;
	lines = MAX(lines, 0);
	mh = (lines + 1) * bh;
#ifdef XINERAMA
	if ((info = XineramaQueryScreens(dpy, &n))) {
		XGetInputFocus(dpy, &w, &di);
		if (mon != -1 && mon < n)
			i = mon;
		else if (w != root && w != PointerRoot && w != None) {
			/* find top-level window containing current input focus */
			do {
				if (XQueryTree(dpy, (pw = w), &dw, &w, &dws, &du) && dws)
					XFree(dws);
			} while (w != root && w != pw);
			/* find xinerama screen with which the window intersects most */
			if (XGetWindowAttributes(dpy, pw, &wa))
				for (j = 0; j < n; j++)
					if ((a = INTERSECT(wa.x, wa.y, wa.width, wa.height, info[j])) > area) {
						area = a;
						i = j;
					}
		}
		/* no focused window is on screen, so use pointer location instead */
		if (mon == -1 && !area && XQueryPointer(dpy, root, &dw, &dw, &x, &y, &di, &di, &du))
			for (i = 0; i < n; i++)
				if (INTERSECT(x, y, 1, 1, info[i]))
					break;

		x = info[i].x_org;
		y = info[i].y_org + (topbar ? 0 : info[i].height - mh);
		mw = info[i].width;
		XFree(info);
	} else
#endif
	{
		x = 0;
		y = topbar ? 0 : sh - mh;
		mw = sw;
	}
	promptw = (prompt && *prompt) ? TEXTW(prompt) : 0;
	inputw = MIN(inputw, mw/3);
	fuzzymatch();
	if (default_number)
		for (i = 0; i < default_number; i++) {
			if (!sel->right)
				break;
			if (sel && (sel = sel->right) == next) {
				printf("A\n");
				curr = next;
				calcoffsets();
			}
		}

	/* create menu window */
	swa.override_redirect = True;
	swa.background_pixel = scheme[SchemeNorm].bg->pix;
	swa.event_mask = ExposureMask | KeyPressMask | VisibilityChangeMask |
	                 ButtonPressMask;
	drw_takesblurcreenshot(drw, x, y, mw, mh, blurlevel, CPU_THREADS);
	win = XCreateWindow(dpy, root, x, y, mw, mh, 0,
	                    DefaultDepth(dpy, screen), CopyFromParent,
	                    DefaultVisual(dpy, screen),
	                    CWOverrideRedirect | CWBackPixel | CWEventMask, &swa);

	/* open input methods */
	xim = XOpenIM(dpy, NULL, NULL, NULL);
	xic = XCreateIC(xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
	                XNClientWindow, win, XNFocusWindow, win, NULL);

	//take the screenshot and blur it before displaying the menu window
	
	XMapRaised(dpy, win);
	drw_resize(drw, mw, mh);
	drawmenu();
}

static void
usage(void)
{
	fputs("usage: dmenu [-b] [-f] [-i] [-l lines] [-p prompt] [-fn font] [-m monitor]\n"
	      "             [-nb color] [-nf color] [-sb color] [-sf color] [-v] [-n]\n"
	      "             [-d default item number] [-k] [-r] [-s]\n", stderr);
	exit(1);
}

int
main(int argc, char *argv[])
{
	int i, fast = 0;

	for (i = 1; i < argc; i++)
		/* these options take no arguments */
		if (!strcmp(argv[i], "-v")) {      /* prints version information */
			puts("dmenu-"VERSION);
			exit(0);
		} else if (!strcmp(argv[i], "-b")) /* appears at the bottom of the screen */
			topbar = 0;
		else if (!strcmp(argv[i], "-f"))   /* grabs keyboard before reading stdin */
			fast = 1;
		else if (!strcmp(argv[i], "-i")) { /* case-insensitive item matching */
			fstrncmp = strncasecmp;
			fstrstr = cistrstr;
		} else if (!strcmp(argv[i], "-n")) /* output selected number instead of text */
			output_number = 1;
		else if (!strcmp(argv[i], "-k"))   /* keep sending selection to output on move */
			output_on_move = 1;
		else if (!strcmp(argv[i], "-r"))   /* Reverse up down keys */
			reverse_updown = 1;
		else if (!strcmp(argv[i], "-s"))   /* Stay until Escape key pressed */
			stay_after_select = 1;
		else if (i + 1 == argc)
			usage();
		/* these options take one argument */
		else if (!strcmp(argv[i], "-l"))   /* number of lines in vertical list */
			lines = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-m"))
			mon = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-p"))   /* adds prompt to left of input field */
			prompt = argv[++i];
		else if (!strcmp(argv[i], "-fn"))  /* font or font set */
			fonts[0] = argv[++i];
		else if (!strcmp(argv[i], "-fb"))  /* normal background color */
			firstbgcolor = argv[++i];
		else if (!strcmp(argv[i], "-ff"))  /* normal foreground color */
			firstfgcolor = argv[++i];
		else if (!strcmp(argv[i], "-nb"))  /* normal background color */
			normbgcolor = argv[++i];
		else if (!strcmp(argv[i], "-nf"))  /* normal foreground color */
			normfgcolor = argv[++i];
		else if (!strcmp(argv[i], "-sb"))  /* selected background color */
			selbgcolor = argv[++i];
		else if (!strcmp(argv[i], "-sf"))  /* selected foreground color */
			selfgcolor = argv[++i];
		else if (!strcmp(argv[i], "-d")) /* Default selected item number */
			default_number = atoi(argv[++i]);
		else
			usage();

	if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
		fputs("warning: no locale support\n", stderr);
	if (!(dpy = XOpenDisplay(NULL)))
		die("cannot open display\n");
	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);
	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);
	drw = drw_create(dpy, screen, root, sw, sh);
	drw_load_fonts(drw, fonts, LENGTH(fonts));
	if (!drw->fontcount)
		die("no fonts could be loaded.\n");
	drw_setscheme(drw, &scheme[SchemeNorm]);

	if (fast) {
		grabkeyboard();
		readstdin();
	} else {
		readstdin();
		grabkeyboard();
	}
	setup();
	run();

	return 1; /* unreachable */
}
