/*
 * MIT License
 *
 * Copyright (c) 2021-2022 Davidson Francis <davidsondfgl@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <sys/time.h>
#include <X11/Xlib.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/time.h>

#include "anipaper.h"
#include "khash.h"

/* Line sweep states. */
#define OPENING  1
#define CLOSING -1

/* Event for the sweep algorithm. */
struct event { int y, offset, x1, x2; };

/* Hashmaps/sets used in sweep line algorithm. */
KHASH_SET_INIT_INT(rec)
KHASH_MAP_INIT_INT(map, int)

/* Window rectangle. */
struct rect
{
	int x1; /* Top left corner X.     */
	int y1; /* Top left corner Y.     */
	int x2; /* Bottom right corner X. */
	int y2; /* Bottom right corner Y. */
};

/**
 * @brief Save the frame @p frame as a PPM file.
 * Made for debug purposes only.
 *
 * @param frame Frame to be saved.
 * @param dp av_decode_params structure.
 */
#ifdef DECODE_TO_FILE
void save_frame_ppm(AVFrame *frame, struct av_decode_params *dp)
{
	int i;
	FILE *f;
	char filename[64];

	/* Convert to RGB. */
	sws_scale(dp->sws_ctx, (const uint8_t * const*)frame->data,
			frame->linesize, 0, frame->height,
			dp->dst_img, dp->dst_linesize);

	/* Save file. */
	snprintf(filename, sizeof(filename), "out/frame_%04d.ppm",
		dp->codec_context->frame_number);

	f = fopen(filename, "wb");
	fprintf(f, "P6\n%d %d\n255\n", frame->width, frame->height);

	/* Write. */
	for (i = 0; i < frame->height; i++)
	{
		fwrite(dp->dst_img[0] + i * dp->dst_linesize[0], 1,
			frame->width * 3, f);
	}
	fclose(f);
}
#endif

/**
 * @brief Get the current time, in seconds.
 *
 * @return Returns the current time.
 */
double time_secs(void)
{
	return ((double)av_gettime_relative() / 1000000.0);
}

/**
 * @brief Comparison routine to order an array of ints.
 *
 * @param i1 First int.
 * @param i2 Second int.
 *
 * @return Returns a number less than, equal to or greater
 * than 0 if @p i1 is considered to be less than, equal to,
 * or greater than the @p i2.
 */
static int cmp_int(const void *i1, const void *i2)
{
	int int1 = *(int *)i1;
	int int2 = *(int *)i2;
	return (int1 - int2);
}

/**
 * @brief Comparison routine to order the event list.
 *
 * @param e1 First event.
 * @param e2 Second event.
 *
 * @return Returns a number less than, equal to or greater
 * than 0 if @p e1 is considered to be less than, equal to,
 * or greater than the @p e2.
 */
static int cmp_event(const void *e1, const void *e2)
{
	const struct event *ev1 = e1;
	const struct event *ev2 = e2;

	if (ev1->y != ev2->y)
		return (ev1->y - ev2->y);
	if (ev1->offset != ev2->offset)
		return (ev1->offset - ev2->offset);
	if (ev1->x1 != ev2->x1)
		return (ev1->x1 - ev2->x1);
	if (ev1->x2 != ev2->x2)
		return (ev1->x2 - ev2->x2);
	return (0);
}

/**
 * @brief Line sweep algorithm to calculate the total area of all
 * overlapping (or not) windows on the screen.
 *
 * @param rects Windows (as rectangles) list.
 * @param nrects Number of windows.
 *
 * @return Returns the area, 0 if error.
 *
 * @note Code based on the Python version available here:
 * https://tryalgo.org/en/geometry/2016/06/25/union-of-rectangles/
 *
 * @note Although this implementation is O(n^2), it is decently fast,
 * calculating 10k rectangles (much more than we need) in about ~6ms.
 */
static int calculate_area(struct rect *rects, int nrects)
{
	int ret;                  /* Return code.                          */
	int i, j;                 /* Loop indexes.                         */
	int area;                 /* Total area.                           */
	int i1, i2;               /* X range in sweep.                     */
	khiter_t k;               /* Hash table iterator.                  */
	int *i_to_x;              /* Sorted set of X coordinates.          */
	int previous_y;           /* Previous y in the last iteration.     */
	int len_interval;         /* Current length interval.              */
	struct event *events;     /* Line sweep events.                    */
	khash_t(map) *x_to_i;     /* Map of X'es and its indexes.          */
	khash_t(rec) *hash_xs;    /* Temp set to hold the unique X'es.     */
	int *nb_current_rects;    /* Number of current rects in the sweep. */
	int len_union_intervals;  /* Length of the intervals.              */

	area   = 0;
	events = malloc(nrects * 2 * sizeof(*events));
	if (!events)
		return (0);

	hash_xs = kh_init(rec);
	if (!hash_xs)
		goto out0;

	/* Initialize our set. */
	for (i = 0, j = 0; i < nrects; i++, j += 2)
	{
		/* Add to our X'es set. */
		kh_put(rec, hash_xs, rects[i].x1, &ret);
		if (ret < 0)
			goto out1;
		kh_put(rec, hash_xs, rects[i].x2, &ret);
		if (ret < 0)
			goto out1;

		/* Add to our event list. */
		events[j] = (struct event)
			{rects[i].y1, OPENING, rects[i].x1, rects[i].x2};
		events[j + 1] = (struct event)
			{rects[i].y2, CLOSING, rects[i].x1, rects[i].x2};
	}

	/* Copy our set to array and sort. */
	i_to_x = malloc(kh_size(hash_xs) * sizeof(int));
	if (!i_to_x)
		goto out1;

	for (k = 0, i = 0; k < kh_end(hash_xs); k++)
		if (kh_exist(hash_xs, k))
			i_to_x[i++] = kh_key(hash_xs, k);

	qsort(i_to_x, kh_size(hash_xs), sizeof(int), cmp_int);

	/*
	 * Create our 'dictionary' that maps the X coordinate
	 * to its rank.
	 */
	x_to_i = kh_init(map);
	if (!x_to_i)
		goto out2;

	for (i = 0; i < (int)kh_size(hash_xs); i++)
	{
		k = kh_put(map, x_to_i, i_to_x[i], &ret);
		if (ret < 0)
			goto out3;
		kh_value(x_to_i, k) = i;
	}

	nb_current_rects = calloc(kh_size(hash_xs), sizeof(int));
	if (!nb_current_rects)
		goto out3;

	/* Sort our event list. */
	qsort(events, nrects * 2, sizeof(struct event), cmp_event);

	previous_y = 0;
	len_interval = 0;
	len_union_intervals = 0;

	/* Sweep algorithm. */
	for (i = 0; i < nrects * 2; i++)
	{
		area += (events[i].y - previous_y) * len_union_intervals;
		i1 = kh_value(x_to_i, kh_get(map, x_to_i, events[i].x1));
		i2 = kh_value(x_to_i, kh_get(map, x_to_i, events[i].x2));

		for (j = i1; j < i2; j++)
		{
			len_interval = i_to_x[j + 1] - i_to_x[j];

			if (!nb_current_rects[j])
				len_union_intervals += len_interval;

			nb_current_rects[j] += events[i].offset;

			if (!nb_current_rects[j])
				len_union_intervals -= len_interval;
		}
		previous_y = events[i].y;
	}

	free(nb_current_rects);
out3:
	kh_destroy(map, x_to_i);
out2:
	free(i_to_x);
out1:
	kh_destroy(rec, hash_xs);
out0:
	free(events);

	return (area);
}

/**
 * @brief For a given window attribute @p attr and screen
 * dimensions, decide if the current window is visible
 * or not.
 *
 * @param attr Window attributes.
 * @param screen_width Screen width.
 * @param screen_height Screen height.
 *
 * @return Returns 1 if visible, 0 otherwise.
 *
 * @note It's important to note that this routine _may_ not
 * work for all types of Window Managers/DEs, but it worked
 * fine for all those I tested, as long as there isn't a
 * compositor running.
 */
static int is_visible(XWindowAttributes *attr, int screen_width,
	int screen_height)
{
	if (attr->map_state != IsViewable)
		return (0);

	/* Check if too far right. */
	if (attr->x + attr->width > screen_width)
	{
		if (attr->x > screen_width)
			return (0);
		attr->width = screen_width - attr->x;
	}

	/* Too far down. */
	if (attr->y + attr->height > screen_height)
	{
		if (attr->y > screen_height)
			return (0);
		attr->height = screen_height - attr->y;
	}

	/* Check if too far left. */
	if (attr->x < 0)
	{
		attr->width += attr->x;
		if (attr->width < 0)
			return (0);
		attr->x = 0;
	}

	/* Check if too far up. */
	if (attr->y < 0)
	{
		attr->height += attr->y;
		if (attr->height < 0)
			return (0);
		attr->y = 0;
	}

	return (1);
}

/**
 * @brief Gets the percentage of screen area used by all
 * visible windows (with or without overlay) at the moment.
 *
 * @param disp X11 Display.
 * @param screen_width Screen width.
 * @param screen_height Screen height.
 *
 * @return Returns the area used or -1 if error.
 */
int screen_area_used(Display *disp, int screen_width, int screen_height)
{
	int i;               /* Loop index.                        */
	int area;            /* Total window area used.            */
	int rl_idx;          /* Rectangle list size.               */
	int perc_used;       /* Total screen % used.               */
	int screen_area;     /* Screen area.                       */
	unsigned nchildren;  /* Number of children of root window. */

	XWindowAttributes attr;         /* X11 Window attributes. */
	struct rect *rectangle_list;    /* Rectangles list.       */
	Window root, parent, *children; /* Windows.               */

	perc_used = -1;

	if (!XQueryTree(disp, DefaultRootWindow(disp), &root, &parent,
		&children, &nchildren))
	{
		LOG_GOTO("Unable to get root children!\n", out0);
    }

	rectangle_list = calloc(nchildren, sizeof(*rectangle_list));
	if (!rectangle_list)
		LOG_GOTO("Unable to allocate room for window list!\n", out1);

	/* Add all visible windows to the window list. */
	for (i = 0, rl_idx = 0; i < (int)nchildren; i++)
	{
		if (!XGetWindowAttributes(disp, children[i], &attr))
			continue;

		if (!is_visible(&attr, screen_width, screen_height))
			continue;

		rectangle_list[rl_idx].x1 = attr.x;
		rectangle_list[rl_idx].y1 = attr.y;
		rectangle_list[rl_idx].x2 = attr.width  + attr.x;
		rectangle_list[rl_idx].y2 = attr.height + attr.y;
		rl_idx++;
	}

	/* Calculate area. */
	area = calculate_area(rectangle_list, rl_idx);
	screen_area = (screen_width * screen_height);
	perc_used = (area * 100) / screen_area;

out1:
	XFree(children);
out0:
	return (perc_used);
}
