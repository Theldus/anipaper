/*
 * MIT License
 *
 * Copyright (c) 2021 Davidson Francis <davidsondfgl@gmail.com>
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
#include <getopt.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/common.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavfilter/avfilter.h>
#include <libswscale/swscale.h>
#include <SDL.h>
#include <X11/Xlib.h>

#include "frame.h"

/* Logs. */
#define LOG_GOTO(log,lbl) \
	do { \
		fprintf(stderr, "INFO: " log); \
		goto lbl; \
	} while (0)

#define LOG(log) \
	fprintf(stderr, "INFO: " log)

/* Queues size. */
#define MAX_PACKET_QUEUE 128
#define MAX_PICTURE_QUEUE 8

/*
 * Multiple decode parameters, used during the decoding
 * and playing process.
 */
struct av_decode_params dp = {0};

/* Termination flags. */
int should_quit = 0;
int end_pkts = 0;
int end_pics = 0;

/*
 * Since AVPacketList is marked as 'deprecated', let's
 * use ours.
 */
struct packet_list
{
	AVPacket pkt;
	struct packet_list *next;
};

/* Packet queue definition. */
struct packet_queue
{
	struct packet_list *first_packet;
	struct packet_list *last_packet;
	int npkts;
	int size;
	SDL_mutex *mutex;
	SDL_cond *cond;
} packet_queue;

/* Picture list definition. */
struct picture_list
{
	double pts;
	SDL_Texture *picture;
	struct picture_list *next;
};

/* Picture queue definition. */
struct picture_queue
{
	struct picture_list *first_picture;
	struct picture_list *last_picture;
	int npics;
	SDL_mutex *mutex;
	SDL_cond *cond;
} picture_queue;

/* SDL global variables. */
static Display *x11dip;
static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_mutex *screen_mutex;

static SDL_Thread *enqueue_thread;
static SDL_Thread *decode_thread;

/* SDL Events. */
static int SDL_EVENT_REFRESH_SCREEN;

/* CMD Flags. */
#define CMD_LOOP              1
#define CMD_WINDOWED          2
#define CMD_RESOLUTION_KEEP   4
#define CMD_RESOLUTION_SCALE  8
#define CMD_RESOLUTION_FIT   16
static int cmd_flags = CMD_LOOP | CMD_RESOLUTION_FIT;
static int set_screen_width;
static int set_screen_height;

/**
 * @brief Initialize the packet queue.
 *
 * @param q Packet queue.
 *
 * @return Returns 0 if sucess, -1 otherwise.
 */
static int init_packet_queue(struct packet_queue *q)
{
	memset(q, 0, sizeof(*q));
	q->mutex = SDL_CreateMutex();
	q->cond  = SDL_CreateCond();
	if (!q->mutex || !q->cond)
		LOG_GOTO("Unable to create SDL mutexes/cond!\n", out);
	return (0);
out:
	return (-1);
}

/**
 * @brief Releases all resources related to the packet
 * queue, including the elements itself.
 *
 * @param q Packet queue to be freed.
 */
static void finish_packet_queue(struct packet_queue *q)
{
	struct packet_list *pkl;
	struct packet_list *pkl_next;

	if (!q)
		return;
	if (!q->mutex)
		SDL_DestroyMutex(q->mutex);
	if (!q->cond)
		SDL_DestroyCond(q->cond);

	/* Go through the queue and clear everything. */
	pkl = q->first_packet;
	while (pkl)
	{
		pkl_next = pkl->next;
			av_packet_unref(&pkl->pkt);
			av_free(pkl);
		pkl = pkl_next;
	}
}

/**
 * @brief Add a new packet @p src_pkt to the queue.
 *
 * It is important to note that this routine is blocking and if
 * there are no space left, the thread remains in blocking state
 * until there are room available.
 *
 * @param q Packet queue.
 * @param src_pkt Packet to be added.
 *
 * @return Returns 1 if success, -1 otherwise.
 */
static int packet_queue_put(struct packet_queue *q, AVPacket *src_pkt)
{
	struct packet_list *pkl;

	pkl = av_malloc(sizeof(*pkl));
	if (!pkl)
		return (-1);

	/* Fill our new node. */
	pkl->pkt  = *src_pkt;
	pkl->next = NULL;

	/* Add to our list. */
	SDL_LockMutex(q->mutex);
		while (1)
		{
			if (should_quit)
				break;

			/* Sleep until a new space or if we should quit. */
			if (q->npkts == MAX_PACKET_QUEUE)
			{
				SDL_CondWait(q->cond, q->mutex);
				continue;
			}

			if (!q->last_packet)
				q->first_packet = pkl;
			else
				q->last_packet->next = pkl;
			q->last_packet = pkl;

			q->npkts++;
			q->size += src_pkt->size;
			SDL_CondSignal(q->cond);
			break;
		}
	SDL_UnlockMutex(q->mutex);
	return (1);
}

/**
 * @brief Removes a packet from the queue and returns it
 * as @p pk.
 *
 * It is important to note that this routine is blocking and if
 * there are no new packets, the thread remains in blocking state
 * until there are new packets.
 *
 * @param q Packet queue.
 * @param pk Returned packet.
 *
 * @return Returns 1 if success, -1 otherwise.
 */
static int packet_queue_get(struct packet_queue *q, AVPacket *pk)
{
	int ret;
	struct packet_list *pkl;

	ret = -1;

	SDL_LockMutex(q->mutex);
		while (1)
		{
			/* Should we abort? */
			if (should_quit || (end_pkts && !q->npkts))
				break;

			pkl = q->first_packet;

			/* If empty, lets wait for something. */
			if (!pkl)
			{
				SDL_CondWait(q->cond, q->mutex);
				continue;
			}

			/* If something, remove head node and return. */
			q->first_packet = pkl->next;
			if (!q->first_packet)
				q->last_packet = NULL;

			q->npkts--;
			q->size -= pkl->pkt.size;
			*pk = pkl->pkt;

			/* Release our node. */
			av_free(pkl);
			ret = 1;
			break;
		}
		SDL_CondSignal(q->cond);
	SDL_UnlockMutex(q->mutex);

	return (ret);
}

/**
 * @brief Initialize the picture queue.
 *
 * @param q Picture queue.
 *
 * @return Returns 0 if sucess, -1 otherwise.
 */
static int init_picture_queue(struct picture_queue *q)
{
	memset(q, 0, sizeof(*q));
	q->mutex = SDL_CreateMutex();
	q->cond  = SDL_CreateCond();
	if (!q->mutex || !q->cond)
		LOG_GOTO("Unable to create SDL mutexes/cond in picture_queue!\n",
			out);
	return (0);
out:
	return (-1);
}

/**
 * @brief Releases all resources related to the picture
 * queue, including the elements itself.
 *
 * @param q Picture queue to be freed.
 */
static void finish_picture_queue(struct picture_queue *q)
{
	struct picture_list *pl;
	struct picture_list *pl_next;

	if (!q)
		return;
	if (!q->mutex)
		SDL_DestroyMutex(q->mutex);
	if (!q->cond)
		SDL_DestroyCond(q->cond);

	/* Go through the queue and clear everything. */
	pl = q->first_picture;
	while (pl)
	{
		pl_next = pl->next;
			SDL_DestroyTexture(pl->picture);
			av_free(pl);
		pl = pl_next;
	}
}

/**
 * @brief Add a complete frame @p src_frm to the queue.
 *
 * It is important to note that this routine is blocking and if
 * there are no space left, the thread remains in blocking state
 * until there are room available.
 *
 * @param dp av_decode_params structure.
 * @param q Picture queue.
 * @param src_frm Frame to be added.
 *
 * @return Returns 1 if success, -1 otherwise.
 */
static int picture_queue_put(struct av_decode_params *dp,
	struct picture_queue *q, AVFrame *src_frm)
{
	int ret;
	struct picture_list *pl;
	SDL_Texture *picture;

	ret = -1;

	/* Allocate a new node and put in the list. */
	pl = av_malloc(sizeof(*pl));
	if (!pl)
		return (-1);

	/*
	 * Create a SDL_Texture.
	 *
	 * Yes... I'm assuming all frames will be YUV420...
	 * maybe emmit an error or abort if the case.
	 *
	 * TODO: Handle non-YUV frames.
	 */
	SDL_LockMutex(screen_mutex);
		picture = SDL_CreateTexture(renderer,
			SDL_PIXELFORMAT_YV12,
			SDL_TEXTUREACCESS_STREAMING,
			dp->codec_context->width, dp->codec_context->height);

		if (!picture)
		{
			SDL_UnlockMutex(screen_mutex);
			av_free(pl);
			return (-1);
		}

		/* Fill our new node with a new SDL_Surface. */
		SDL_UpdateYUVTexture(picture, NULL,
			src_frm->data[0], src_frm->linesize[0],
			src_frm->data[1], src_frm->linesize[1],
			src_frm->data[2], src_frm->linesize[2]);
	SDL_UnlockMutex(screen_mutex);

	pl->pts = (double)src_frm->best_effort_timestamp * dp->time_base;
	pl->picture = picture;
	pl->next = NULL;

	/* Add to our list. */
	SDL_LockMutex(q->mutex);
		while (1)
		{
			if (should_quit)
			{
				ret = -1;
				break;
			}

			/* Sleep until a new space or if we should quit. */
			if (q->npics == MAX_PICTURE_QUEUE)
			{
				SDL_CondWait(q->cond, q->mutex);
				continue;
			}

			if (!q->last_picture)
				q->first_picture = pl;
			else
				q->last_picture->next = pl;
			q->last_picture = pl;

			ret = 1;
			q->npics++;
			SDL_CondSignal(q->cond);
			break;
		}
	SDL_UnlockMutex(q->mutex);
	return (ret);
}

/**
 * @brief Removes a full frame from the queue and returns it
 * as @p sdl_pic and @p pts.
 *
 * It is important to note that this routine is blocking and if
 * there are no new frames, the thread remains in blocking until
 * there are new frames.
 *
 * @param q Picture queue.
 * @param sdl_pic Returned frame to be drawn.
 * @param pts Returned frame pts.
 *
 * @return Returns 1 if success, -1 otherwise.
 */
static int picture_queue_get(struct picture_queue *q, SDL_Texture **sdl_pic,
	double *pts)
{
	int ret;
	struct picture_list *pl;

	ret = -1;

	SDL_LockMutex(q->mutex);
		while (1)
		{
			if (should_quit || (end_pics && !q->npics))
				break;

			pl = q->first_picture;

			/* If empty, lets wait for something. */
			if (!pl)
			{
				SDL_CondWait(q->cond, q->mutex);
				continue;
			}

			/* If something, remove head node and return. */
			q->first_picture = pl->next;
			if (!q->first_picture)
				q->last_picture = NULL;

			q->npics--;
			*sdl_pic = pl->picture;
			*pts = pl->pts;

			/* Release our node. */
			av_free(pl);
			ret = 1;
			break;
		}
		SDL_CondSignal(q->cond);
	SDL_UnlockMutex(q->mutex);

	return (ret);
}

/**
 * @brief Screen refresh timer callback, this function triggers
 * a refresh screen event, where the main thread must refresh the
 * screen.
 *
 * @param interval Interval time (in ms).
 * @param data av_decode_params structure.
 */
static Uint32 refresh_screen_callback(Uint32 interval, void *data)
{
	((void)interval);
	SDL_Event event;
	event.type = SDL_EVENT_REFRESH_SCREEN;
	event.user.data1 = data;
	SDL_PushEvent(&event);
	return (0);
}

/**
 * @brief Schedule a new time for the main thread to sleep.
 *
 * @param dp av_decode_params structure.
 * @param delay Delay (in ms).
 */
static void schedule_refresh(struct av_decode_params *dp, int delay)
{
	SDL_AddTimer(delay, refresh_screen_callback, dp);
}

/**
 * @brief Draws a new frame on the screen, taking
 * command line parameters into account.
 *
 * @param texture_frame Frame to be drawn.
 * @param dp av_decode_params structure.
 */
static void draw_frame(SDL_Texture *texture_frame,
	struct av_decode_params *dp)
{
	SDL_Rect dst = {0};
	SDL_Rect *dst_ptr;
	double w_ratio;
	double h_ratio;
	double b_ratio;

	dst_ptr = NULL;

	/* Update screen. */
	SDL_QueryTexture(texture_frame, NULL, NULL, &dst.w, &dst.h);

	/* Adjust sizes. */
	if (cmd_flags & CMD_RESOLUTION_FIT)
	{
		if (dp->screen_width && dp->screen_height)
		{
			dst_ptr = &dst;
			w_ratio = (double)dp->screen_width  / (double)dst.w;
			h_ratio = (double)dp->screen_height / (double)dst.h;
			b_ratio = fmin(w_ratio, h_ratio);

			dst.w = (double)dst.w * b_ratio;
			dst.h = (double)dst.h * b_ratio;

			dst.x = dp->screen_width / 2 - dst.w / 2;
			dst.y = dp->screen_height / 2 - dst.h / 2;
		}
	}

	else if (cmd_flags & CMD_RESOLUTION_SCALE)
	{
		if (cmd_flags & CMD_WINDOWED)
		{
			if (dp->screen_width && dp->screen_height)
			{
				dst.w = dp->screen_width;
				dst.h = dp->screen_height;
				dst.x = dp->screen_width / 2  - dst.w / 2;
				dst.y = dp->screen_height / 2 - dst.h / 2;
			}
		}
	}

	/* CMD_RESOLUTION_KEEP. */
	else
	{
		if (!(cmd_flags & CMD_WINDOWED))
		{
			if (dp->screen_width && dp->screen_height)
			{
				dst_ptr = &dst;
				dst.x = dp->screen_width / 2  - dst.w / 2;
				dst.y = dp->screen_height / 2 - dst.h / 2;
			}
		}
	}

	SDL_LockMutex(screen_mutex);
		SDL_RenderClear(renderer);
		SDL_RenderCopy(renderer, texture_frame, NULL, dst_ptr);
		SDL_RenderPresent(renderer);
	SDL_UnlockMutex(screen_mutex);
}

/**
 * @brief Adjust the current video timers.
 *
 * This routine is the 'magic' point that synchronizes the video:
 * the times of each frame are adjusted (according to the pts, and
 * the current time), and the new sleep time is calculated. If
 * the program is too late, the frame is discarded and a new
 * frame is read, this is repeated as many times as necessary.
 *
 * @param pts Presentation Time Stamp for the current frame.
 * @param dp av_decode_params structure.
 *
 * @return Returns the amount of time the thread should sleep.
 */
static double adjust_timers(double pts, struct av_decode_params *dp)
{
	double delay;
	double true_delay;

	delay = pts - dp->frame_last_pts;

	/*
	 * If delay is negative: pts no set
	 * If greater than 1.0: too big
	 */
	if (delay <= 0 || delay >= 1.0)
		delay = dp->frame_last_delay;

	/* Backup our values. */
	dp->frame_last_delay = delay;
	dp->frame_last_pts = pts;

	/*
	 * Calculate our true delay:
	 *
	 * Since we may be behind or ahead of the next frame's wait
	 * time, we need to calculate the real delay, using the
	 * program's current execution time.
	 *
	 * If we are too late, we ignore the frame.
	 */
	dp->frame_timer += delay;
	true_delay = dp->frame_timer - (time_microsecs() / 1000000.0);
	return (true_delay);
}

/**
 * @brief Updates the screen periodically, until
 * there is no more data to be processed.
 *
 * @param data av_decode_params structure.
 */
static void refresh_screen(void *data)
{
	SDL_Event event;

	struct av_decode_params *dp;
	SDL_Texture *texture_frame = NULL;

	double true_delay;
	double pts;

	dp = (struct av_decode_params *)data;

again:
	/*
	 * If error, do nothing.
	 *
	 * It is worth to note that the routine below
	 * is blocking, meaning that we will wait until
	 * get a valid frame to update.
	 *
	 * Although seems better to polling, (IMO) it is better
	 * to just wait instead of sleeping/waking up
	 * every time, wasting resources. The timer should
	 * always be adjusted anyway.
	 */
	if (picture_queue_get(&picture_queue, &texture_frame, &pts) < 0)
	{
		/*
		 * If everything is over, send an event to myself signaling
		 * to quit.
		 */
		event.type = SDL_QUIT;
    	SDL_PushEvent(&event);
		return;
	}

	/* === Adjust timers === */
	true_delay = adjust_timers(pts, dp);

	/* If less than 10ms, skip the frame and read the next. */
	if (true_delay < 0.010)
	{
		SDL_DestroyTexture(texture_frame);
		goto again;
	}

	/* Update screen. */
	draw_frame(texture_frame, dp);

	/* Release resources. */
	SDL_DestroyTexture(texture_frame);

	/*
	 * Set our new timer, with the adjusted delay.
	 *
	 * Since SDL_AddTimer is accurate to milliseconds, we
	 * need to convert first and round the result.
	 */
	schedule_refresh(dp, (int)((true_delay * 1000) + 0.5));
}

/**
 * @brief Given a @p packet, a @p frame pointer and a
 * @p dp decode context, decode the packet and saves
 * the resulting frame in the picture queue.
 *
 * @param packet Packet to be decoded.
 * @param frame Destination frame.
 * @param dp av_decode_params structure.
 *
 * @return Returns 0 if success, -1 otherwise.
 */
static int decode_packet(AVPacket *packet, AVFrame *frame,
	struct av_decode_params *dp)
{
	int ret;

	/* Send packet data as input to a decoder. */
	ret = avcodec_send_packet(dp->codec_context, packet);
	if (ret < 0)
		LOG_GOTO("Error while sending packet data to a decoder!\n", out);

	while (ret >= 0)
	{
		/* Get decoded output (i.e: frame) from the decoder. */
		ret = avcodec_receive_frame(dp->codec_context, frame);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
			break;
		else if (ret < 0)
			LOG_GOTO("Error while getting a frame from the decoder!\n", out);

#ifndef DECODE_TO_FILE
		/* We have the complete frame, enqueue it */
		if (picture_queue_put(dp, &picture_queue, frame) < 0)
		{
			ret = -1;
			goto out;
		}
#else
		save_frame_ppm(frame, dp);
#endif
	}
	ret = 0;
out:
	return (ret);
}

/**
 * @brief Read each packet from the packet queue,
 * decode them, and save the resulting frame
 * in the picture queue.
 *
 * This executes in another thread.
 *
 * @param arg av_decode_params structure.
 *
 * @return Always returns 0.
 */
static int decode_packets_thread(void *arg)
{
	int ret;
	AVFrame *frame;
	AVPacket packet;
	struct av_decode_params *dp;

	ret = 0;
	dp = (struct av_decode_params *)arg;

	frame = av_frame_alloc();
	if (!frame)
		LOG_GOTO("Unable to allocate an AVFrame!\n", out0);

	while (1)
	{
		/* Should quit?. */
		if (packet_queue_get(&packet_queue, &packet) < 0)
		{
			/* Signal the end of pictures and wake up threads. */
			end_pics = 1;
			SDL_CondSignal(picture_queue.cond);
			break;
		}

		if (decode_packet(&packet, frame, dp) < 0)
			break;

		av_packet_unref(&packet);
	}
out0:
	av_frame_free(&frame);
	return (ret);
}

/**
 * @brief Read each video packet from the video and
 * enqueue them for later processing.
 *
 * This executes in another thread.
 *
 * @param arg av_decode_params structure.
 *
 * @return Always returns 0.
 */
static int enqueue_packets_thread(void *arg)
{
	AVFrame *frame;
	AVPacket *packet;
	struct av_decode_params *dp;

	dp = (struct av_decode_params *)arg;

	/* Allocate memory for AVFrame and AVPacket. */
	frame = av_frame_alloc();
	if (!frame)
		LOG_GOTO("Unable to allocate an AVFrame!\n", out2);
	packet = av_packet_alloc();
	if (!packet)
		LOG_GOTO("Unable to allocate an AVPacket!\n", out3);

start:
	while (1)
	{
		if (should_quit)
			goto out2;

		/* Error/EOF. */
		if (av_read_frame(dp->format_context, packet) < 0)
		{
			/* Signal the end of packets and wake up threads. */
			end_pkts = 1;
			SDL_CondSignal(packet_queue.cond);
			break;
		}

		/* Check packet type and enqueue it. */
		if (packet->stream_index == dp->video_idx)
			packet_queue_put(&packet_queue, packet);
		else
			av_packet_unref(packet);
	}

	/* Loop again. */
	if (cmd_flags & CMD_LOOP)
	{
		av_seek_frame(dp->format_context, dp->video_idx, 0,
			AVSEEK_FLAG_BACKWARD);
		goto start;
	}

out2:
	av_packet_free(&packet);
out3:
	av_frame_free(&frame);
	return (0);
}

/**
 * @brief Initializes all resources related to video decoding,
 * most of them related to libavcodec. Leaves the program in a
 * state ready to decode the video.
 *
 * @param dp av_decode_params structure.
 * @param file Video to be played.
 *
 * @return Returns 0 if success, -1 otherwise.
 */
static int init_av(struct av_decode_params *dp, const char *file)
{
	int i;
	const AVCodec *codec;
	AVCodecParameters *codec_parameters;

	/* Initialize context. */
	dp->format_context = avformat_alloc_context();
	if (!dp->format_context)
		LOG_GOTO("Unable to allocate a format context\n", out0);

	/* Open the media file and read its header. */
	if (avformat_open_input(&dp->format_context, file, NULL, NULL) != 0)
		LOG_GOTO("Unable to open input file\n", out1);

	/* Read stream information. */
	if (avformat_find_stream_info(dp->format_context, NULL) < 0)
		LOG_GOTO("Unable to get stream info\n", out1);

	codec = NULL;
	codec_parameters = NULL;
	dp->video_idx = -1;

	/* Loop through all streams until find a valid/compatible video stream. */
	for (i = 0; i < (int)dp->format_context->nb_streams; i++)
	{
		codec_parameters = dp->format_context->streams[i]->codecpar;

		/* Skip not video. */
		if (codec_parameters->codec_type != AVMEDIA_TYPE_VIDEO)
			continue;

		/* Try to find a codec, if not found, skip. */
		codec = avcodec_find_decoder(codec_parameters->codec_id);
		if (!codec)
			continue;

		dp->time_base = av_q2d(dp->format_context->streams[i]->time_base);
		dp->video_idx = i;
		break;
	}
	if (dp->video_idx == -1)
		LOG_GOTO("Unable to find any compatible video stream!\n", out1);

	/* Allocate a context and fill it. */
	dp->codec_context = avcodec_alloc_context3(codec);
	if (!dp->codec_context)
		LOG_GOTO("Unable to create a codec context!\n", out1);
	if (avcodec_parameters_to_context(dp->codec_context, codec_parameters) < 0)
	{
		LOG_GOTO("Unable to fill codec context with the codec "
			"parameters!\n", out2);
	}
	if (avcodec_open2(dp->codec_context, codec, NULL) < 0)
		LOG_GOTO("Unable to initialize a codec context!\n", out2);

#ifdef DECODE_TO_FILE
	/* Prepare our scale context and temporary buffer. */
	dp->sws_ctx = sws_getContext(dp->codec_context->width,
		dp->codec_context->height,
		dp->codec_context->pix_fmt,
		dp->codec_context->width,
		dp->codec_context->height,
		AV_PIX_FMT_RGB24,
		SWS_BILINEAR,
		NULL,
		NULL,
		NULL
	);
	if (!dp->sws_ctx)
		LOG_GOTO("Unable to create a scale context!\n", out2);

	if (av_image_alloc(dp->dst_img, dp->dst_linesize,
		dp->codec_context->width, dp->codec_context->height,
		AV_PIX_FMT_RGB24, 16) < 0)
	{
		LOG_GOTO("Unable to allocate destination image!\n", out3);
	}
#endif

	/* Initial time (in seconds). */
	dp->frame_timer = (double)time_microsecs() / 1000000.0;
	/* Frame last delay (in seconds). */
	dp->frame_last_delay = 0.04; /* 40ms (or 25 fps). */

	return (0);
#ifdef DECODE_TO_FILE
out3:
	sws_freeContext(dp->sws_ctx);
#endif
out2:
	avcodec_free_context(&dp->codec_context);
out1:
	avformat_close_input(&dp->format_context);
out0:
	return (-1);
}

/**
 * @brief Finishes all resources related to SDL and X11.
 *
 * @param dp av_decode_params structure.
 */
static void finish_av(struct av_decode_params *dp)
{
	avcodec_free_context(&dp->codec_context);
	avformat_close_input(&dp->format_context);
	sws_freeContext(dp->sws_ctx);
#if DECODE_TO_FILE
	av_freep(&dp->dst_img[0]);
#endif
}

/**
 * @brief Initializes all resources related to the
 * SDL, such as window, renderer and threads.
 *
 * @param dp av_decode_params structure.
 *
 * @return Returns 0 if success, -1 otherwise.
 */
static int init_sdl(struct av_decode_params *dp)
{
	Window x11w;
	SDL_DisplayMode dm = {0};
	int width;
	int height;

	/* Initialize. */
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0)
		LOG_GOTO("Unable to initialize SDL!\n", out0);

	/* Get screen dimensions. */
	if (!set_screen_width || !set_screen_height)
	{
		if (SDL_GetCurrentDisplayMode(0, &dm) != 0)
		{
			dp->screen_width = dm.w;
			dp->screen_height = dm.h;
		}
		else
		{
			LOG("Unable to get screen resolution, please set manually "
				"with -r!\n");
		}
	}
	else
	{
		dp->screen_width = set_screen_width;
		dp->screen_height = set_screen_height;
	}

	/*
	 * Create Window through X11's RootWindow or a new SDL
	 * window.
	 */
	if (cmd_flags & CMD_WINDOWED)
	{
		if (cmd_flags & (CMD_RESOLUTION_SCALE|CMD_RESOLUTION_FIT))
		{
			if (dp->screen_width && dp->screen_height)
			{
				width = dp->screen_width;
				height = dp->screen_height;
			}

			/* Resolution not set, use video res. */
			else
			{
				width = dp->codec_context->width;
				height = dp->codec_context->height;
			}
		}

		/* If KEEP. */
		else
		{
			width = dp->codec_context->width;
			height = dp->codec_context->height;
		}

		/* Create window. */
		window = SDL_CreateWindow("video",
			SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
			width, height, SDL_WINDOW_SHOWN);
		if (!window)
			LOG_GOTO("Unable to create a new SDL Window!\n", out1);
	}

	/* X11. */
	else
	{
		x11dip = XOpenDisplay(NULL);
		if (!x11dip)
			LOG_GOTO("Unable to open X11 display\n", out1);

		x11w = RootWindow(x11dip, DefaultScreen(x11dip));

		window = SDL_CreateWindowFrom((void*)x11w);
		if (!window)
			LOG_GOTO("Unable to create a new SDL Window through X11!\n", out2);
	}

	/* Create renderer. */
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED |
		SDL_RENDERER_PRESENTVSYNC);
	if (!renderer)
		LOG_GOTO("Unable to create an SDL Renderer!\n", out2);

	/* Create threads. */
	enqueue_thread = SDL_CreateThread(enqueue_packets_thread,
		"enqueue_pkts", dp);
	if (!enqueue_thread)
		LOG_GOTO("Unable to create the enqueue_packets thread!\n", out3);

	decode_thread = SDL_CreateThread(decode_packets_thread,
		"decode_pkts", dp);
	if (!decode_thread)
		LOG_GOTO("Unable to create the decode_packets thread!\n", out3);

	/* Allocate SDL Events. */
	SDL_EVENT_REFRESH_SCREEN = SDL_RegisterEvents(2);
	if (SDL_EVENT_REFRESH_SCREEN < 0)
		LOG_GOTO("Unable to register SDL events!\n", out3);

	/* Renderer mutex. */
	screen_mutex = SDL_CreateMutex();
	if (!screen_mutex)
		LOG_GOTO("Unable to create screen mutex!\n", out3);

	return (0);
out3:
	SDL_DestroyRenderer(renderer);
out2:
	if (!(cmd_flags & CMD_WINDOWED))
		XCloseDisplay(x11dip);
	else
		SDL_DestroyWindow(window);
out1:
	SDL_Quit();
out0:
	return (-1);
}

/**
 * @brief Releases all resources related to SDL.
 */
static void finish_sdl(void)
{
	/*
	 * Ideally we should join the threads here, to make sense with
	 * 'init_sdl' (where the threads are created). But it makes
	 * more sense to join right after the main loop, before
	 * finishing the queues and the SDL.
	 */

	/* Release resources. */
	if (renderer)
		SDL_DestroyRenderer(renderer);
	if (window)
		SDL_DestroyWindow(window);
	SDL_Quit();
	if (!(cmd_flags & CMD_WINDOWED))
		XCloseDisplay(x11dip);
}

/**
 * @brief Show program usage.
 * @param prgname Program name.
 */
static void usage(const char *prgname)
{
	fprintf(stderr, "Usage: %s <input-file>\n", prgname);
	fprintf(stderr,
		"  -o Execute only once, without loop (loop enabled by default)\n"
		"  -w Enable windowed mode (do not set wallpaper)\n\n"
		"Resolution options:\n"
		"  -k (Keep) resolution, may appears smaller or bigger\n"
		"     than the screen, preserve aspect ratio\n\n"
		"  -s (Scale to) screen resolution, occupies the entire screen\n"
		"     regardless of the aspect ratio!\n\n"
		"  -f (Fit) to screen. Make the video fit into the screen (default)\n\n"
		"  -r Set screen resolution, in format: WIDTHxHEIGHT\n\n"
		"  -h This help\n\n"
		"Note:\n"
		"  Please note that some options depends on the screen resolution.\n"
		"  If I'm unable to get the resolution and the -r parameter is not\n"
		"  set:\n"
		"  - If X11 (wallpaper) mode: The video will always fill the screen area\n"
		"  - If Windowed mode: Window will be the same size as the video\n"
		);
	exit(EXIT_FAILURE);
}

/**
 * @brief For a given string WIDTHxHEIGHT, parses it
 * and returns the width and height.
 *
 * @param res Resolution string.
 * @param w Returned width pointer.
 * @param h Returned height pointer.
 *
 * @return Returns 0 if success, -1 otherwise.
 */
static int get_resolution(const char *res, int *w, int *h)
{
	const char *p = res;

	/* Read width. */
	*w = 0;
	while (*p && isdigit(*p))
	{
		*w *= 10;
		*w += *p - '0';
		p++;
	}

	/* Skip delimiter, single char. */
	p++;
	if (!*p)
		return (-1);

	/* Read height. */
	*h = 0;
	while (*p && isdigit(*p))
	{
		*h *= 10;
		*h += *p - '0';
		p++;
	}

	/* Validate remaining. */
	while (*p)
	{
		if (!isspace(*p))
			return (-1);
		p++;
	}

	if (!*w || !*h)
		return (-1);

	return (0);
}

/**
 * Parse the command-line arguments.
 *
 * @param argc Argument count.
 * @param argv Argument list.
 *
 * @return Returns 1 if success or abort otherwise.
 */
static char* parse_args(int argc, char **argv)
{
	int c; /* Current arg. */
	while ((c = getopt(argc, argv, "howksfr:")) != -1)
	{
		switch (c)
		{
			case 'h':
				usage(argv[0]);
				break;
			case 'o':
				cmd_flags &= ~CMD_LOOP;
				break;
			case 'w':
				cmd_flags |= CMD_WINDOWED;
				break;
			case 'k':
				cmd_flags &= ~(CMD_RESOLUTION_SCALE|CMD_RESOLUTION_FIT);
				cmd_flags |= CMD_RESOLUTION_KEEP;
				break;
			case 's':
				cmd_flags &= ~(CMD_RESOLUTION_FIT|CMD_RESOLUTION_KEEP);
				cmd_flags |= CMD_RESOLUTION_SCALE;
				break;
			case 'f':
				cmd_flags &= ~(CMD_RESOLUTION_SCALE|CMD_RESOLUTION_KEEP);
				cmd_flags |= CMD_RESOLUTION_FIT;
				break;
			case 'r':
				if (get_resolution(optarg, &set_screen_width,
					&set_screen_height) < 0)
				{
					fprintf(stderr, "Invalid resolution (%s)\n", optarg);
					usage(argv[0]);
				}
				break;
			default:
				usage(argv[0]);
				break;
		}
	}

	/* If not input file available. */
	if (optind >= argc)
	{
		fprintf(stderr, "Expected <input-file> after options!\n");
		usage(argv[0]);
	}

	return (argv[optind]);
}

/* Main =). */
int main(int argc, char **argv)
{
	int ret;
	SDL_Event event;
	char *input_file;

	ret = EXIT_FAILURE;

	/* Parse arguments. */
	input_file = parse_args(argc, argv);

	/* Initialize AV stuff. */
	if (init_av(&dp, input_file) < 0)
		LOG_GOTO("Unable to process input file, aborting!\n", out0);

	/* Initialize queues. */
	if (init_packet_queue(&packet_queue) < 0)
		LOG_GOTO("Unable to initialize packet queue!\n", out1);
	if (init_picture_queue(&picture_queue) < 0)
		LOG_GOTO("Unable to initialize picture queue!\n", out2);

	/* Initialize SDL and start enqueue & decode packet threads. */
	if (init_sdl(&dp) < 0)
		LOG_GOTO("Unable to initialize SDL, aborting!\n", out3);

	/* Start our refresh timer. */
	schedule_refresh(&dp, 40);

	/* SDL/Event loop. */
	while (1)
	{
		SDL_WaitEvent(&event);

		if (event.type == SDL_QUIT)
		{
			should_quit = 1;
			SDL_CondSignal(picture_queue.cond);
			SDL_CondSignal(packet_queue.cond);
			break;
		}

		else if (event.type == (Uint32)SDL_EVENT_REFRESH_SCREEN)
			refresh_screen(event.user.data1);
	}

	SDL_WaitThread(enqueue_thread, NULL);
	SDL_WaitThread(decode_thread, NULL);

	ret = EXIT_SUCCESS;
out3:
	finish_picture_queue(&picture_queue);
	finish_sdl();
out2:
	finish_packet_queue(&packet_queue);
out1:
	finish_av(&dp);
out0:
	return (ret);
}
