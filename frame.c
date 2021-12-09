#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/common.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavfilter/avfilter.h>
#include <libswscale/swscale.h>
#include <SDL.h>

#include "frame.h"

#define LOG_GOTO(log,lbl) \
	do { \
		fprintf(stderr, log); \
		goto lbl; \
	} while (0)

/**/
#define MAX_PACKET_QUEUE 128

/**/
#define MAX_PICTURE_QUEUE 8

/**/
int should_quit = 0;

/*
 * Since AVPacketList is marked as 'deprecated', let's
 * use ours.
 */
struct packet_list
{
	AVPacket pkt;
	struct packet_list *next;
};

/**/
struct packet_queue
{
	struct packet_list *first_packet;
	struct packet_list *last_packet;
	int npkts;
	int size;
	SDL_mutex *mutex;
	SDL_cond *cond;
} packet_queue;

/*
 *
 */
struct picture_list
{
	SDL_Texture *picture;
	struct picture_list *next;
};

/**/
struct picture_queue
{
	struct picture_list *first_picture;
	struct picture_list *last_picture;
	int npics;
	SDL_mutex *mutex;
	SDL_cond *cond;
} picture_queue;

/**/
static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;

static SDL_Thread *enqueue_thread;
static SDL_Thread *decode_thread;

/* SDL Events. */
static int SDL_EVENT_QUIT;
static int SDL_EVENT_REFRESH_SCREEN;

/**/
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

/**/
static void finish_packet_queue(struct packet_queue *q)
{
	if (!q)
		return;
	if (!q->mutex)
		SDL_DestroyMutex(q->mutex);
	if (!q->cond)
		SDL_DestroyCond(q->cond);
}

/**/
static int packet_queue_put(struct packet_queue *q, AVPacket *src_pkt)
{
	struct packet_list *pkl;
	AVPacket *dst_pkt;

	/* Clone a new packet and release the older. */
	if (!(dst_pkt = av_packet_clone(src_pkt)))
		return (-1);

	pkl = av_malloc(sizeof(*pkl));
	if (!pkl)
		return (-1);

	/* Fill our new node. */
	pkl->pkt  = *dst_pkt;
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

			q->npkts++;
			q->size += dst_pkt->size;
			SDL_CondSignal(q->cond);
		}
	SDL_UnlockMutex(q->mutex);
	return (0);
}

/**/
static int packet_queue_get(struct packet_queue *q, AVPacket *pk)
{
	int ret;
	struct packet_list *pkl;

	ret = -1;

	SDL_LockMutex(q->mutex);
		while (1)
		{
			if (should_quit)
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

/**/
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

/**/
static void finish_picture_queue(struct picture_queue *q)
{
	if (!q)
		return;
	if (!q->mutex)
		SDL_DestroyMutex(q->mutex);
	if (!q->cond)
		SDL_DestroyCond(q->cond);
}

/**/
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
	picture = SDL_CreateTexture(renderer,
		SDL_PIXELFORMAT_YV12,
		SDL_TEXTUREACCESS_STREAMING,
		dp->codec_context->width, dp->codec_context->height);

	if (!picture)
		return (-1);

	/* Fill our new node with a new SDL_Surface. */
	SDL_UpdateYUVTexture(texture, NULL,
		src_frm->data[0], src_frm->linesize[0],
		src_frm->data[1], src_frm->linesize[1],
		src_frm->data[2], src_frm->linesize[2]);

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

			ret = 1;
			q->npics++;
			SDL_CondSignal(q->cond);
		}
	SDL_UnlockMutex(q->mutex);
	return (ret);
}

/**/
static int picture_queue_get(struct picture_queue *q, SDL_Texture **sdl_pic)
{
	int ret;
	struct picture_list *pl;

	ret = -1;

	SDL_LockMutex(q->mutex);
		while (1)
		{
			if (should_quit)
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
 *
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
	for (i = 0; i < dp->format_context->nb_streams; i++)
	{
		codec_parameters = dp->format_context->streams[i]->codecpar;

		/* Skip not video. */
		if (codec_parameters->codec_type != AVMEDIA_TYPE_VIDEO)
			continue;

		printf("== time_base: %f ==\n", av_q2d(dp->format_context->streams[i]->time_base));

		/* Try to find a codec, if not found, skip. */
		codec = avcodec_find_decoder(codec_parameters->codec_id);
		if (!codec)
			continue;

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

	return (0);
out3:
	sws_freeContext(dp->sws_ctx);
out2:
	avcodec_free_context(&dp->codec_context);
out1:
	avformat_close_input(&dp->format_context);
out0:
	return (-1);
}

/**
 *
 */
static void finish_av(struct av_decode_params *dp)
{
	avcodec_free_context(&dp->codec_context);
	avformat_close_input(&dp->format_context);
	sws_freeContext(dp->sws_ctx);
	av_freep(&dp->dst_img[0]);
}

/**/
static Uint32 refresh_screen_callback(Uint32 interval, void *data)
{
	SDL_Event event;
	event.type = SDL_EVENT_REFRESH_SCREEN;
	event.user.data1 = data;
	SDL_PushEvent(&event);
	return (0);
}

/**/
static void schedule_refresh(struct av_decode_params *dp, int delay)
{
	SDL_AddTimer(delay, refresh_screen_callback, dp);
}

/**
 *
 */
static void refresh_screen(void *data)
{
	struct av_decode_params *dp;
	SDL_Texture *texture_frame;

	dp = (struct av_decode_params *)data;

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
	if (picture_queue_get(&picture_queue, &texture_frame) < 0)
		return;

	/* Update screen. */
	SDL_RenderClear(renderer);
	SDL_RenderCopy(renderer, texture_frame, NULL, NULL);
	SDL_RenderPresent(renderer);


	/* Adjust timers. */
	schedule_refresh(dp, 40);

	/* Release resources. */
	SDL_DestroyTexture(texture_frame);
}

/**
 *
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

#if DECODE_TO_FILE == 0
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
 *
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
			break;

		if (decode_packet(&packet, frame, dp) < 0)
			break;

		av_packet_unref(&packet);
	}


out0:
	av_frame_free(&frame);
	return (ret);
}

/**
 *
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


	while (1)
	{
		if (should_quit)
			break;

		/* Error/EOF. */
		if (av_read_frame(dp->format_context, packet) < 0)
			break;

		/* Check packet type and enqueue it. */
		if (packet->stream_index == dp->video_idx)
			packet_queue_put(&packet_queue, packet);
		else
			av_packet_unref(packet);
	}

out2:
	av_packet_free(&packet);
out3:
	av_frame_free(&frame);
	return (0);
}

/**
 *
 */
static int init_sdl(struct av_decode_params *dp)
{
	/* Initialize. */
	if (SDL_Init(SDL_INIT_VIDEO) < 0)
		LOG_GOTO("Unable to initialize SDL!\n", out0);

	/* Create window. */
	window = SDL_CreateWindow("video",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		dp->codec_context->width, dp->codec_context->height,
		SDL_WINDOW_SHOWN);

	if (!window)
		LOG_GOTO("Unable to create a new SDL Window!\n", out1);

	/* Create renderer. */
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
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
	SDL_EVENT_QUIT = SDL_RegisterEvents(2);
	if (SDL_EVENT_QUIT < 0)
		LOG_GOTO("Unable to register SDL events!\n", out3);
	SDL_EVENT_REFRESH_SCREEN = SDL_EVENT_QUIT + 1;

	return (0);
out3:
	SDL_DestroyRenderer(renderer);
out2:
	SDL_DestroyWindow(window);
out1:
	SDL_Quit();
out0:
	return (-1);
}

/**
 *
 */
static void finish_sdl(void)
{
	/* Join threads. */
	SDL_WaitThread(enqueue_thread, NULL);
	SDL_WaitThread(decode_thread, NULL);

	/* Release resources. */
	if (renderer)
		SDL_DestroyRenderer(renderer);
	if (window)
		SDL_DestroyWindow(window);
	SDL_Quit();
}

/**
 *
 */
static void usage(const char *prgname)
{
	fprintf(stderr, "Usage: %s <input-file>\n", prgname);
	exit(EXIT_FAILURE);
}

/**
 *
 */
int main(int argc, char **argv)
{
	int ret;
	SDL_Event event;
	struct av_decode_params dp = {0};

	ret = -1;

	if (argc < 2)
		usage(argv[0]);

	/* Initialize AV stuff. */
	if (init_av(&dp, argv[1]) < 0)
		LOG_GOTO("Unable to process input file, aborting!\n", out0);

	/* Initialize queues. */
	if (init_packet_queue(&packet_queue) < 0)
		LOG_GOTO("Unable to initialize packet queue!\n", out1);
	if (init_picture_queue(&picture_queue) < 0)
		LOG_GOTO("Unable to initialize picture queue!\n", out2);

	/* Initialize SDL and start enqueue & decode packet threads. */
	if (init_sdl(&dp) < 0)
		LOG_GOTO("Unable to initialize SDL, aborting!\n", out3);


	schedule_refresh(&dp, 40);

	/* SDL/Event loop. */
	while (1)
	{
		SDL_WaitEvent(&event);

		if (event.type == SDL_EVENT_QUIT || event.type == SDL_QUIT)
		{
			should_quit = 1;
			goto exit;
		}

		else if (event.type == SDL_EVENT_REFRESH_SCREEN)
			refresh_screen(event.user.data1);
	}

exit:
	/* Wake up (possible) sleeping threads. */
	SDL_CondSignal(packet_queue.cond);
	SDL_CondSignal(picture_queue.cond);

	ret = 0;
	finish_sdl();
out3:
	finish_picture_queue(&picture_queue);
out2:
	finish_packet_queue(&packet_queue);
out1:
	finish_av(&dp);
out0:
	return (ret);
}
