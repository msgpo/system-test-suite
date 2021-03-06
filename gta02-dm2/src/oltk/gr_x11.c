/* gr_x11.c
 *
 * (C) 2007 by OpenMoko, Inc.
 * Written by Chia-I Wu <olv@openmoko.com>
 * All Rights Reserved
 *
 * This file is part of oltk.
 *
 * oltk is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * oltk is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with oltk; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 */ 

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "gr_impl.h"

#define GR ((struct gr_x11 *)(gr))

#define GR_DEPTH 8
#define N_COLORS (1 << GR_DEPTH)

struct gr_x11 {
	struct gr gr;

	Display *dpy;
	int scr;
	Window win;
	Visual *vis;
	XImage *img;
	Cursor xcursor;
	Atom wm_protocols;
	GC gc;

	int deleted;
	int nonblock;

	unsigned long colormap[N_COLORS];
};

static void gr_x11_update(struct gr *gr, struct gr_rectangle *rects, int n_rects)
{
	struct gr_rectangle screen;
	int i;

	if (!rects)
	{
		screen.x = 0;
		screen.y = 0;
		screen.width = gr->width;
		screen.height = gr->height;

		rects = &screen;
		n_rects = 1;
	}

	for (i = 0; i < n_rects; i++)
	{
		if (!rects[i].width || !rects[i].height)
			continue;

		XPutImage(GR->dpy, GR->win, GR->gc, GR->img, rects[i].x, rects[i].y,
			  rects[i].x, rects[i].y, rects[i].width, rects[i].height);
	}
	XSync(GR->dpy, False);
}

static void gr_x11_set_color(struct gr *gr, unsigned int index, struct gr_rgb *rgb, unsigned int n)
{
	XColor xc;
	int i;
	
	if (index >= N_COLORS)
		return;

	if (index + n >= N_COLORS)
		n = N_COLORS - index;

	for (i = 0; i < n; i++, index++)
	{
		xc.red   = rgb[i].red;
		xc.green = rgb[i].green;
		xc.blue  = rgb[i].blue;

		XAllocColor(GR->dpy, DefaultColormap(GR->dpy, GR->scr), &xc);
		GR->colormap[index] = xc.pixel;
	}
}

static int gr_x11_get_color(struct gr *gr, unsigned int index)
{
	if (index >= N_COLORS)
		return BlackPixel(GR->dpy, GR->scr);

	return GR->colormap[index];
}

static void gr_x11_close(struct gr *gr)
{
	XCloseDisplay(GR->dpy);
}

static int on_motion(struct gr *gr, XMotionEvent *e, struct gr_sample *samp)
{
	samp->x = e->x;
	samp->y = e->y;
	samp->pressure = 5;
	samp->tv.tv_sec = e->time / 1000;
	samp->tv.tv_usec = (e->time % 1000) * 1000;

	return 1;
}

static int on_button(struct gr *gr, XButtonEvent *e, struct gr_sample *samp)
{
	samp->x = e->x;
	samp->y = e->y;
	samp->pressure = (e->type == ButtonPress) ? 5 : 0;
	samp->tv.tv_sec = e->time / 1000;
	samp->tv.tv_usec = (e->time % 1000) * 1000;

	return 1;
}

static int gr_x11_sample(struct gr* gr, struct gr_sample *samp)
{
	XEvent e;
	int has_sample = 0;

	while (!has_sample)
	{
		if (GR->deleted)
			return -1;

		if (GR->nonblock)
		{
			if (!XPending(GR->dpy))
				break;
		}

		XNextEvent(GR->dpy, &e);

		switch (e.type)
		{
		case ButtonPress:
		case ButtonRelease:
			has_sample = on_button(gr, &e.xbutton, samp);
			break;
		case MotionNotify:
			has_sample = on_motion(gr, &e.xmotion, samp);
			break;
		case Expose:
			if (e.xexpose.count == 0)
				gr_x11_update(gr, NULL, 1);
			break;
		case ClientMessage:
			if (e.xclient.message_type == GR->wm_protocols)
				GR->deleted = 1;
			break;
		default:
			break;
		}
	}

	return has_sample;
}

static Cursor create_point_cursor(struct gr *gr)
{
	Cursor cur;
	Pixmap pix;
	GC gc;
	XGCValues gv;
	XColor color;

	pix = XCreatePixmap(GR->dpy, GR->win, 1, 1, 1);

	gv.function = GXclear;
	gc = XCreateGC(GR->dpy, pix, GCFunction, &gv);

	XFillRectangle(GR->dpy, pix, gc, 0, 0, 1, 1);

	color.red = 0xffff;
	color.green = 0xffff;
	color.blue = 0xffff;

	cur = XCreatePixmapCursor(GR->dpy, pix, None, &color, &color, 0, 0);

	XFreePixmap(GR->dpy, pix);
	XFreeGC(GR->dpy, gc);

	return cur;
}

static struct gr *gr_x11_open(const char *dev, int width, int height, int nonblock)
{
	struct gr *gr = malloc(sizeof(struct gr_x11));
	Atom wm;

	printf("gr_x11_open\n");

	if (!gr)
		return NULL;

	GR->dpy = XOpenDisplay(dev);
	if (!GR->dpy)
	{
		free(gr);

		return NULL;
	}

	gr->fd = -1;

	GR->scr = DefaultScreen(GR->dpy);
	GR->vis = DefaultVisual(GR->dpy, GR->scr);
	GR->win = XCreateSimpleWindow(GR->dpy, DefaultRootWindow(GR->dpy),
			0, 0, width, height, 0, 0, BlackPixel(GR->dpy, GR->scr));

	XSelectInput(GR->dpy, GR->win, ExposureMask | Button1MotionMask | ButtonPressMask | ButtonReleaseMask);

	GR->xcursor = create_point_cursor(gr);
	XDefineCursor(GR->dpy, GR->win, GR->xcursor);

	GR->gc = XCreateGC(GR->dpy, GR->win, 0, NULL);

	GR->wm_protocols = XInternAtom(GR->dpy, "WM_PROTOCOLS", False);
	wm = XInternAtom(GR->dpy, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(GR->dpy, GR->win, &wm, 1);

	GR->deleted = 0;
	GR->nonblock = nonblock;

	gr->width = width;
	gr->height = height;

	gr->depth = DefaultDepth(GR->dpy, GR->scr);
	gr->bytes_per_pixel = (gr->depth > 16) ? 4 : gr->depth / 8;

	gr->depth = GR_DEPTH;

	gr->pitch = (width * gr->bytes_per_pixel + 3) & ~3;
	gr->fb = malloc(gr->height * gr->pitch);

	if (!gr->fb)
	{
		XCloseDisplay(GR->dpy);
		free(gr);

		return NULL;
	}

	GR->img = XCreateImage(GR->dpy, GR->vis, DefaultDepth(GR->dpy, GR->scr), ZPixmap, 0,
			gr->fb, gr->width, gr->height, 32, gr->pitch);

	gr->close = gr_x11_close;
	gr->update = gr_x11_update;
	gr->set_color = gr_x11_set_color;
	gr->get_color = gr_x11_get_color;
	gr->sample = gr_x11_sample;

	XMapWindow(GR->dpy, GR->win);
	XSync(GR->dpy, False);

	return gr;
}

struct gr_backend gr_x11_backend = {
	"x11",
	gr_x11_open,
};
