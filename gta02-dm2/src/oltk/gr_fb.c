/*
 * gr_fb.c
 *
 * Linux framebuffer implementation of gr backend.  Inspired by tslib.
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
 * 02110-1301 USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>

#include <linux/vt.h>
#include <linux/kd.h>
#include <linux/fb.h>

#include <tslib.h>

#include "font.h"
#include "gr.h"
#include "gr_impl.h"

#define GR ((struct gr_fb *) (gr))

#define GR_DEPTH 8
#define N_COLORS (1 << GR_DEPTH)

struct gr_fb {
	struct gr gr;
	char *ts_dev;
	struct tsdev *ts;
	int nonblock;

	int con_fd;
	int last_vt;

	char *fb_dev;
	int fb_fd;
	char *fb_base;
	unsigned long fb_size;

	struct fb_fix_screeninfo fix;
	struct fb_var_screeninfo var;

	unsigned colormap [N_COLORS];
};

static int open_console(struct gr *gr)
{
	char vtname[128];
	struct vt_stat vts;
	int fd, nr;

	GR->con_fd = -1;

	fd = open("/dev/tty0", O_WRONLY);
	if (fd < 0)
	{
		perror("Failed to open /dev/tty0");

		return 0;
	}

	if (ioctl(fd, VT_OPENQRY, &nr) < 0)
	{
		perror("ioctl VT_OPENQRY");
		close(fd);

		return 0;
	}
	close(fd);

	snprintf(vtname, 128, "/dev/tty%d", nr);

	fd = open(vtname, O_RDWR | O_NDELAY);
	if (fd < 0)
	{
		fprintf(stderr, "Failed to open %s: %s\n", vtname, strerror(errno));

		return 0;
	}

	if (ioctl(fd, VT_GETSTATE, &vts) == 0)
		GR->last_vt = vts.v_active;
	else
		GR->last_vt = -1;

	if (ioctl(fd, VT_ACTIVATE, nr) < 0)
	{
		perror("ioctl VT_ACTIVATE");
		close(fd);

		return 0;
	}

	if (ioctl(fd, VT_WAITACTIVE, nr) < 0)
	{
		perror("ioctl VT_WAITACTIVE");
		close(fd);

		return 0;
	}


	if (ioctl(fd, KDSETMODE, KD_GRAPHICS) < 0)
	{
		perror("ioctl KDSETMODE");
		close(fd);

		return 0;
	}

	GR->con_fd = fd;

	return 1;
}

static void close_console(struct gr *gr)
{
	if (GR->con_fd == -1)
		return;

	ioctl(GR->con_fd, KDSETMODE, KD_TEXT);

	if (GR->last_vt >= 0)
		ioctl(GR->con_fd, VT_ACTIVATE, GR->last_vt);

	close(GR->con_fd);
	GR->con_fd = -1;
}

static int open_framebuffer(struct gr *gr, int width, int height)
{
	unsigned long offset;

	GR->fb_fd = open(GR->fb_dev, O_RDWR);
	if (GR->fb_fd < 0)
	{
		fprintf(stderr, "Failed to open %s: %s\n", GR->fb_dev, strerror(errno));

		return 0;
	}

	if (ioctl(GR->fb_fd, FBIOGET_FSCREENINFO, &GR->fix) < 0)
	{
		perror("ioctl FBIOGET_FSCREENINFO");
		close(GR->fb_fd);

		return 0;
	}

	if (ioctl(GR->fb_fd, FBIOGET_VSCREENINFO, &GR->var) < 0)
	{
		perror("ioctl FBIOGET_VSCREENINFO");
		close(GR->fb_fd);

		return 0;
	}

	offset = (unsigned long) GR->fix.smem_start % getpagesize();
	GR->fb_size = GR->fix.smem_len + offset;
	GR->fb_base = mmap(NULL, GR->fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, GR->fb_fd, 0);
	if (GR->fb_base == (char *) -1)
	{
		fprintf(stderr, "Failed to mmap %s: %s\n", GR->fb_dev, strerror(errno));
		close(GR->fb_fd);

		return 0;
	}

	gr->fb = GR->fb_base + offset;
	memset(gr->fb, 0, GR->fix.smem_len);

	return 1;
}

static void close_framebuffer(struct gr *gr)
{
	munmap(GR->fb_base, GR->fb_size);
	close(GR->fb_fd);
}

static int open_touchscreen(struct gr *gr, int nonblock)
{
	GR->ts = ts_open(GR->ts_dev, nonblock);
	if (!GR->ts)
	{
		fprintf(stderr, "ts_open failed\n");

		return 0;
	}

	if (ts_config(GR->ts))
	{
		fprintf(stderr, "ts_config failed\n");
		ts_close(GR->ts);

		return 0;
	}

	gr->fd = ts_fd(GR->ts);
	GR->nonblock = nonblock;

	return 1;
}

static void close_touchscreen(struct gr *gr)
{
	ts_close(GR->ts);
}

static void gr_fb_close(struct gr *gr)
{
	close_framebuffer(gr);
	close_console(gr);

	close_touchscreen(gr);

	free(GR->fb_dev);
}

static int gr_fb_sample(struct gr* gr, struct gr_sample *sample)
{
	int has_sample = 0;

	while (!has_sample)
	{
		struct ts_sample tss;
		int ret;

		ret = ts_read(GR->ts, &tss, 1);

		if (ret < 0)
			return ret;

		if (ret == 1)
		{
			sample->x = tss.x;
			sample->y = tss.y;
			sample->pressure = tss.pressure;
			has_sample = 1;
		}
		else if (GR->nonblock)
			break;
	}

	return has_sample;
}

static void gr_fb_set_color(struct gr *gr, unsigned int index, struct gr_rgb *rgb, unsigned int n)
{
	unsigned short red, green, blue;
	struct fb_cmap cmap;
	int i;

	if (index >= N_COLORS)
		return;

	if (index + n >= N_COLORS)
		n = N_COLORS - index;

	switch (gr->bytes_per_pixel)
	{
	default:
	case 1:
		for (i = 0; i < n; i++, index++)
		{
			__u16 r, g, b;

			r = rgb[i].red;
			g = rgb[i].green;
			b = rgb[i].blue;

			cmap.start = index;
			cmap.len = 1;
			cmap.red = &r;
			cmap.green = &g;
			cmap.blue = &b;
			cmap.transp = NULL;

			if (ioctl (GR->fb_fd, FBIOPUTCMAP, &cmap) < 0)
				perror("ioctl FBIOPUTCMAP");

			GR->colormap[index] = index;
		}
		break;
	case 2:
	case 4:
		for (i = 0; i < n; i++)
		{
			red   = (rgb[i].red   + 0x80) >> 8;
			green = (rgb[i].green + 0x80) >> 8;
			blue  = (rgb[i].blue  + 0x80) >> 8;

			GR->colormap[index + i] =
				((red >> (8 - GR->var.red.length)) << GR->var.red.offset) |
				((green >> (8 - GR->var.green.length)) << GR->var.green.offset) |
				((blue >> (8 - GR->var.blue.length)) << GR->var.blue.offset);
		}
		break;
	}
}

static int gr_fb_get_color(struct gr *gr, unsigned int index)
{
	if (index >= N_COLORS)
		return 0;

	return GR->colormap[index];
}

static void gr_fb_update(struct gr *gr, struct gr_rectangle *rects, int n_rects)
{
}

static struct gr *gr_fb_open(const char *dev, int width, int height, int nonblock)
{
	struct gr *gr;

	printf("gr_fb_open: failed\n");

	gr = malloc(sizeof(struct gr_fb));
	if (!gr) {
		printf("gr_fb_open: OOM\n");
		return NULL;
	}
	if (!(GR->ts_dev = getenv("TSLIB_TSDEVICE")))
		GR->ts_dev = "/dev/input/touchscreen0";
	if (!open_touchscreen(gr, nonblock))
	{
		printf("gr_fb_open: opening touchscreen failed\n");
		free(gr);

		return NULL;
	}

	if (!open_console(gr))
	{
		printf("gr_fb_open: open_console failed\n");
		close_touchscreen(gr);
		free(gr);

		return NULL;
	}


	if (dev)
		GR->fb_dev = strdup(dev);
	else
		GR->fb_dev = strdup("/dev/fb0");

	if (!GR->fb_dev || !open_framebuffer(gr, width, height))
	{
		printf("gr_fb_open: open_framebuffer failed\n");
		close_console(gr);
		close_touchscreen(gr);
		free(gr);

		if (GR->fb_dev)
			free(GR->fb_dev);

		return NULL;
	}

	gr->pitch = GR->fix.line_length;
	gr->width = GR->var.xres;
	gr->height = GR->var.yres;
	gr->depth = GR_DEPTH;
	gr->bytes_per_pixel = (GR->var.bits_per_pixel + 7) / 8;

	gr->close = gr_fb_close;
	gr->update = gr_fb_update;
	gr->set_color = gr_fb_set_color;
	gr->get_color = gr_fb_get_color;
	gr->sample = gr_fb_sample;


	return gr;
}

struct gr_backend gr_fb_backend = {
	"linux fb",
	gr_fb_open,
};
